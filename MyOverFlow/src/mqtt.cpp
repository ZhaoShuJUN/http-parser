#include "mqtt.h"
QByteArray Mqtt::string(const QString &s) {
    auto b = s.toUtf8();
    QByteArray r;
    r.append(char(b.size() >> 8));
    r.append(char(b.size()));
    return r + b;
}
QByteArray Mqtt::packet(quint8 t, const QByteArray &b) {
    QByteArray r(1, char(t));
    int n = b.size();
    do {
        int c = n % 128;
        n /= 128;
        r.append(char(c | (n ? 128 : 0)));
    } while (n);
    return r + b;
}
Mqtt::Mqtt(const Config &c, Store *s, QObject *p) : QObject(p), cfg_(c), store_(s) {
    clock_.start();
    timer_.setInterval(200);
    connect(&timer_, &QTimer::timeout, this, &Mqtt::tick);
}
void Mqtt::state(QString s) {
    status_ = s;
    emit statusChanged(s);
}
void Mqtt::start() {
    if (!cfg_.mqtt["enabled"].toBool())
        return;
    running_ = true;
    timer_.start();
    auth();
}
void Mqtt::stop() {
    running_ = false;
    timer_.stop();
    if (socket_) {
        socket_->disconnect(this);
        socket_->abort();
        socket_->deleteLater();
        socket_ = nullptr;
    }
}
void Mqtt::reset(const QString &reason) {
    connected_ = false;
    ping_ = false;
    topologyAck_ = false;
    pending_ = 0;
    pendingPacket_.clear();
    queue_.clear();
    rx_.clear();
    if (socket_) {
        auto s = socket_;
        socket_ = nullptr;
        s->disconnect(this);
        s->abort();
        s->deleteLater();
    }
    retryAt_ = clock_.elapsed() + 5000;
    state(reason);
}
void Mqtt::auth() {
    if (!running_ || authenticating_)
        return;
    QString check = secret(cfg_.mqtt["checkIdEnv"].toString());
    if (check.isEmpty()) {
        reset("缺少网关 checkID 环境变量");
        return;
    }
    authenticating_ = true;
    state("HTTP 网关鉴权中");
    auto m = cfg_.mqtt;
    QJsonObject body{{"gatewaySN", m["gatewaySN"]},
                     {"algId", 0},
                     {"checkID", check},
                     {"devInfo", QJsonObject{{"name", "monitoringHost"},
                                             {"description", "RK3399 environmental monitoring"},
                                             {"manufacturerId", m["manufacturerId"]},
                                             {"model", m["model"]}}}};
    QNetworkRequest req(QUrl(m["authUrl"].toString()));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto r = http_.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QTimer::singleShot(10000, r, [r] {
        if (r->isRunning())
            r->abort();
    });
    connect(r, &QNetworkReply::finished, this, [this, r] {
        authenticating_ = false;
        r->deleteLater();
        if (!running_)
            return;
        if (r->error() != QNetworkReply::NoError ||
            r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
            reset("网关 HTTP 鉴权失败");
            return;
        }
        auto o = QJsonDocument::fromJson(r->readAll()).object();
        gateway_ = o["gatewayId"].toString();
        QString addr;
        std::function<void(QJsonValue)> search = [&](QJsonValue v) {
            if (v.isObject()) {
                auto x = v.toObject();
                if (x["protocol"] == "mqtt")
                    addr = x["addr"].toString();
                for (auto it = x.begin(); it != x.end(); ++it)
                    search(it.value());
            } else if (v.isArray())
                for (auto x : v.toArray())
                    search(x);
        };
        search(o);
        if (addr.isEmpty()) {
            host_ = QUrl(cfg_.mqtt["authUrl"].toString()).host();
            port_ = 1906;
        } else {
            QUrl u(addr.contains("://") ? addr : "mqtt://" + addr);
            if (u.scheme() != "mqtt" && u.scheme() != "mqtts") {
                reset("服务器返回不支持的 MQTT 协议");
                return;
            }
            host_ = u.host();
            port_ = u.port(u.scheme() == "mqtts" ? 8883 : 1906);
        }
        if (gateway_.isEmpty() || gateway_.contains('/') || host_.isEmpty() || port_ < 1 || port_ > 65535) {
            reset("网关鉴权应答格式错误");
            return;
        }
        // TLS is explicitly selected by configuration or mqtts discovery.
        cfg_.mqtt["discoveredTls"] = addr.startsWith("mqtts://");
        connectBroker();
    });
}
void Mqtt::connectBroker() {
    bool tls = cfg_.mqtt["tls"].toBool() || cfg_.mqtt["discoveredTls"].toBool();
    socket_ = tls ? new QSslSocket(this) : new QTcpSocket(this);
    auto sendConnect = [this] {
        QByteArray b = string("MQTT");
        b.append(char(4));
        quint8 flags = 2;
        auto user = cfg_.mqtt["username"].toString(), pass = secret(cfg_.mqtt["passwordEnv"].toString());
        if (!user.isEmpty())
            flags |= 0x80;
        if (!pass.isEmpty())
            flags |= 0x40;
        b.append(char(flags));
        b.append(char(0));
        b.append(char(30));
        b += string(gateway_);
        if (flags & 0x80)
            b += string(user);
        if (flags & 0x40)
            b += string(pass);
        write(0x10, b);
    };
    connect(socket_, &QTcpSocket::readyRead, this, &Mqtt::receive);
    connect(socket_, &QTcpSocket::disconnected, this, [this] {
        if (running_)
            reset("MQTT 连接断开，等待重试");
    });
    if (tls) {
        auto s = qobject_cast<QSslSocket *>(socket_);
        connect(s, &QSslSocket::encrypted, this, sendConnect);
        s->connectToHostEncrypted(host_, quint16(port_));
    } else {
        connect(socket_, &QTcpSocket::connected, this, sendConnect);
        socket_->connectToHost(host_, quint16(port_));
    }
    lastRx_ = lastTx_ = clock_.elapsed();
    state("连接 MQTT 中");
}
void Mqtt::write(quint8 t, const QByteArray &b) {
    if (socket_) {
        socket_->write(packet(t, b));
        lastTx_ = clock_.elapsed();
    }
}
void Mqtt::receive() {
    if (!socket_)
        return;
    rx_ += socket_->readAll();
    if (rx_.size() > 1024 * 1024) {
        reset("MQTT 报文过长");
        return;
    }
    while (rx_.size() > 1) {
        int n = 0, mul = 1, i = 1;
        quint8 byte;
        do {
            if (i >= rx_.size())
                return;
            if (i > 4) {
                reset("MQTT 长度字段无效");
                return;
            }
            byte = quint8(rx_[i++]);
            n += (byte & 127) * mul;
            mul *= 128;
        } while (byte & 128);
        if (n > 1024 * 1024) {
            reset("MQTT 报文过长");
            return;
        }
        if (rx_.size() < i + n)
            return;
        quint8 type = quint8(rx_[0]) >> 4;
        auto b = rx_.mid(i, n);
        rx_.remove(0, i + n);
        lastRx_ = clock_.elapsed();
        if (type == 2) {
            if (b.size() != 2 || b[1] != 0) {
                reset("MQTT CONNACK 拒绝连接");
                return;
            }
            connected_ = true;
            state("MQTT 在线，注册子设备");
            QJsonArray a;
            QSet<QString> seen;
            for (auto s : cfg_.sensors)
                if (!seen.contains(s.nodeId)) {
                    seen.insert(s.nodeId);
                    a.append(QJsonObject{{"nodeId", s.nodeId},
                                         {"name", s.name},
                                         {"description", s.id},
                                         {"manufacturerId", cfg_.mqtt["manufacturerId"]},
                                         {"model", cfg_.mqtt["sensorModel"]}});
                }
            for (int i = 0; i < a.size(); i += 8) {
                QJsonArray chunk;
                for (int j = i; j < qMin(i + 8, a.size()); ++j)
                    chunk.append(a[j]);
                queue_.enqueue(
                    {"topo/add",
                     QJsonDocument(QJsonObject{{"deviceInfo", chunk}}).toJson(QJsonDocument::Compact)});
            }
            nextReport_ = 0;
            sendNext();
        } else if (type == 4 && b.size() == 2) {
            quint16 id = (quint8(b[0]) << 8) | quint8(b[1]);
            if (id == pending_) {
                bool topo = !queue_.isEmpty() && queue_.head().first == "topo/add";
                if (!queue_.isEmpty())
                    queue_.dequeue();
                pending_ = 0;
                if (topo) {
                    topologyAck_ = true;
                    state("MQTT 在线（QoS1）");
                }
                sendNext();
            }
        } else if (type == 13) {
            ping_ = false;
        }
    }
}
void Mqtt::enqueueSnapshot() {
    // Latest-value reporting is bounded in RAM; alarms remain durable in SQLite.
    if (queue_.size() > 2)
        return;
    auto all = store_->report(cfg_.sensors);
    for (int i = 0; i < all.size(); i += 8) {
        QJsonArray batch;
        for (int j = i; j < qMin(i + 8, all.size()); ++j)
            batch.append(all[j]);
        queue_.enqueue(
            {"datas", QJsonDocument(QJsonObject{{"devices", batch}}).toJson(QJsonDocument::Compact)});
    }
}
void Mqtt::sendNext() {
    if (!connected_ || pending_ || queue_.isEmpty())
        return;
    if (cfg_.mqtt["qos"].toInt(1) == 0) {
        const auto item = queue_.head();
        auto wire = packet(0x30, string("/v1/devices/" + gateway_ + "/" + item.first) + item.second);
        if (socket_->write(wire) != wire.size()) {
            reset("MQTT 写入失败");
            return;
        }
        queue_.dequeue();
        lastTx_ = clock_.elapsed();
        if (item.first == "topo/add") {
            topologyAck_ = true;
            state("MQTT 在线（QoS0，本地写出不代表服务器确认）");
        }
        return;
    }
    if (++sequence_ == 0)
        ++sequence_;
    pending_ = sequence_;
    QByteArray b = string("/v1/devices/" + gateway_ + "/" + queue_.head().first);
    b.append(char(pending_ >> 8));
    b.append(char(pending_));
    b += queue_.head().second;
    pendingPacket_ = packet(0x32, b);
    socket_->write(pendingPacket_);
    pendingAt_ = lastTx_ = clock_.elapsed();
}
void Mqtt::tick() {
    if (!running_)
        return;
    auto now = clock_.elapsed();
    if (!socket_) {
        if (now >= retryAt_)
            auth();
        return;
    }
    if (!connected_) {
        if (now - lastRx_ > 10000)
            reset("MQTT 建连超时");
        return;
    }
    if (pending_ && now - pendingAt_ > 15000) {
        reset("MQTT PUBACK 超时");
        return;
    }
    if (ping_ && now - lastRx_ > 45000) {
        reset("MQTT 心跳超时");
        return;
    }
    if (now - lastTx_ > 15000) {
        write(0xc0, {});
        ping_ = true;
    }
    if (topologyAck_ && now >= nextReport_) {
        enqueueSnapshot();
        nextReport_ = now + qMax(1000, cfg_.mqtt["reportMs"].toInt(10000));
    }
    sendNext();
}
