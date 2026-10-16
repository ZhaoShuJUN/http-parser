#include "gatewaymqtt.h"
#include "AppFunc/object1commintf.h"
namespace {
struct MosquittoLibrary {
    MosquittoLibrary() {
        mosquitto_lib_init();
    }
    ~MosquittoLibrary() {
        mosquitto_lib_cleanup();
    }
};
} // namespace
QGatewayMqtt::QGatewayMqtt(const MonitorSettings &c, QObject *p) : QObject(p), m_cfg(c) {
    static MosquittoLibrary library;
    Q_UNUSED(library);
    m_clock.start();
    m_timer.setInterval(100);
    connect(&m_timer, &QTimer::timeout, this, &QGatewayMqtt::tick);
}
QGatewayMqtt::~QGatewayMqtt() {
    stop();
}
void QGatewayMqtt::setStatus(QString text) {
    m_status = text;
    emit statusChanged(text);
}
void QGatewayMqtt::start() {
    if (!m_cfg.mqtt["enabled"].toBool() || m_cfg.demo)
        return;
    m_running = true;
    m_timer.start();
    auth();
}
void QGatewayMqtt::dispose() {
    ++m_generation;
    if (m_mosq) {
        mosquitto_disconnect(m_mosq);
        if (m_loop)
            mosquitto_loop_stop(m_mosq, true);
        mosquitto_destroy(m_mosq);
        m_mosq = nullptr;
    }
    m_loop = false;
    delete m_context;
    m_context = nullptr;
    m_connected = false;
    m_pendingMid = 0;
}
void QGatewayMqtt::stop() {
    m_running = false;
    m_timer.stop();
    dispose();
}
void QGatewayMqtt::reset(QString reason) {
    dispose();
    m_topology.clear();
    m_retryAt = m_clock.elapsed() + 5000;
    setStatus(reason);
}
void QGatewayMqtt::onConnect(mosquitto *, void *ctx, int code) {
    auto c = static_cast<CallbackContext *>(ctx);
    auto owner = c->owner;
    int generation = c->generation;
    QMetaObject::invokeMethod(
        owner,
        [owner, generation, code] {
            if (generation == owner->m_generation)
                owner->connected(code);
        },
        Qt::QueuedConnection);
}
void QGatewayMqtt::onDisconnect(mosquitto *, void *ctx, int code) {
    auto c = static_cast<CallbackContext *>(ctx);
    auto o = c->owner;
    int g = c->generation;
    QMetaObject::invokeMethod(
        o,
        [o, g, code] {
            if (o->m_running && g == o->m_generation)
                o->reset(QString("MQTT 断开(%1)，等待重连").arg(code));
        },
        Qt::QueuedConnection);
}
void QGatewayMqtt::onPublish(mosquitto *, void *ctx, int mid) {
    auto c = static_cast<CallbackContext *>(ctx);
    auto o = c->owner;
    int g = c->generation;
    QMetaObject::invokeMethod(
        o,
        [o, g, mid] {
            if (g == o->m_generation)
                o->published(mid);
        },
        Qt::QueuedConnection);
}
void QGatewayMqtt::auth() {
    if (!m_running || m_auth)
        return;
    auto check = monitorSecret(m_cfg.mqtt["checkIdEnv"].toString());
    if (check.isEmpty()) {
        m_retryAt = m_clock.elapsed() + 5000;
        setStatus("缺少网关 checkID 环境变量");
        return;
    }
    m_auth = true;
    setStatus("网关 HTTP 鉴权中");
    auto m = m_cfg.mqtt;
    QJsonObject b{{"gatewaySN", m["gatewaySN"]},
                  {"algId", 0},
                  {"checkID", check},
                  {"devInfo", QJsonObject{{"name", "monitoringHost"},
                                          {"description", "RK3399 environmental monitoring"},
                                          {"manufacturerId", m["manufacturerId"]},
                                          {"model", m["model"]}}}};
    QNetworkRequest req(QUrl(m["authUrl"].toString()));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto r = m_http.post(req, QJsonDocument(b).toJson(QJsonDocument::Compact));
    QTimer::singleShot(10000, r, [r] {
        if (r->isRunning())
            r->abort();
    });
    connect(r, &QNetworkReply::finished, this, [this, r] {
        m_auth = false;
        r->deleteLater();
        if (!m_running)
            return;
        if (r->error() != QNetworkReply::NoError ||
            r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
            reset("网关鉴权失败");
            return;
        }
        auto o = QJsonDocument::fromJson(r->readAll()).object();
        m_gateway = o["gatewayId"].toString();
        QString addr;
        bool discoveredTls = false;
        std::function<void(QJsonValue)> search = [&](QJsonValue v) {
            if (v.isObject()) {
                auto obj = v.toObject();
                if (obj["protocol"] == "mqtt" || obj["protocol"] == "mqtts") {
                    addr = obj["addr"].toString();
                    discoveredTls = obj["protocol"] == "mqtts";
                }
                for (auto i = obj.begin(); i != obj.end(); ++i)
                    search(i.value());
            } else if (v.isArray())
                for (auto a : v.toArray())
                    search(a);
        };
        search(o);
        if (m_gateway.isEmpty() || m_gateway.size() > 128 || m_gateway.contains('/')) {
            reset("鉴权响应 gatewayId 无效");
            return;
        }
        QString host;
        int port = 1906;
        bool tls = m_cfg.mqtt["tls"].toBool() || discoveredTls;
        if (addr.isEmpty())
            host = QUrl(m_cfg.mqtt["authUrl"].toString()).host();
        else {
            QUrl u(addr.contains("://") ? addr : "mqtt://" + addr);
            if (!QStringList{"mqtt", "mqtts"}.contains(u.scheme()) || !u.userInfo().isEmpty()) {
                reset("MQTT 发现地址无效");
                return;
            }
            tls = tls || u.scheme() == "mqtts";
            host = u.host();
            port = u.port(tls ? 8883 : 1906);
        }
        if (host.isEmpty() || port < 1 || port > 65535) {
            reset("MQTT 主机或端口无效");
            return;
        }
        openBroker(host, port, tls);
    });
}
void QGatewayMqtt::openBroker(QString host, int port, bool tls) {
    dispose();
    m_context = new CallbackContext{this, m_generation};
    auto client = m_gateway.toUtf8();
    m_mosq = mosquitto_new(client.constData(), true, m_context);
    if (!m_mosq) {
        reset("创建 MQTT 客户端失败");
        return;
    }
    mosquitto_int_option(m_mosq, MOSQ_OPT_PROTOCOL_VERSION, MQTT_PROTOCOL_V311);
    mosquitto_connect_callback_set(m_mosq, onConnect);
    mosquitto_disconnect_callback_set(m_mosq, onDisconnect);
    mosquitto_publish_callback_set(m_mosq, onPublish);
    mosquitto_reconnect_delay_set(m_mosq, 2, 30, true);
    auto user = m_cfg.mqtt["username"].toString().toUtf8(),
         pass = monitorSecret(m_cfg.mqtt["passwordEnv"].toString()).toUtf8();
    if (!pass.isEmpty() && user.isEmpty()) {
        reset("MQTT 密码已配置，但用户名为空");
        return;
    }
    if (!user.isEmpty() &&
        mosquitto_username_pw_set(m_mosq, user.constData(), pass.isEmpty() ? nullptr : pass.constData()) !=
            MOSQ_ERR_SUCCESS) {
        reset("MQTT 认证配置失败");
        return;
    }
    if (tls) {
        auto ca = m_cfg.mqtt["caFile"].toString().toUtf8();
        const char *dir = QDir("/etc/ssl/certs").exists() ? "/etc/ssl/certs" : nullptr;
        if (mosquitto_tls_set(m_mosq, ca.isEmpty() ? nullptr : ca.constData(), dir, nullptr, nullptr,
                              nullptr) != MOSQ_ERR_SUCCESS) {
            reset("MQTT TLS 信任根配置失败");
            return;
        }
    }
    auto h = host.toUtf8();
    int rc = mosquitto_connect_async(m_mosq, h.constData(), port, 30);
    if (rc != MOSQ_ERR_SUCCESS) {
        reset("MQTT 建连请求失败");
        return;
    }
    rc = mosquitto_loop_start(m_mosq);
    if (rc != MOSQ_ERR_SUCCESS) {
        reset("MQTT 网络线程启动失败");
        return;
    }
    m_loop = true;
    m_connectAt = m_clock.elapsed();
    setStatus("MQTT 连接中");
}
void QGatewayMqtt::connected(int code) {
    if (code != 0) {
        reset(QString("MQTT 拒绝连接(%1)").arg(code));
        return;
    }
    m_connected = true;
    m_pendingMid = 0;
    m_nextReport = 0;
    topology();
    setStatus("MQTT 在线，注册子设备");
    sendNext();
}
void QGatewayMqtt::topology() {
    m_topology.clear();
    QJsonArray chunk;
    QSet<QString> seen;
    auto enqueue = [&] {
        if (chunk.isEmpty())
            return;
        m_topology.enqueue(QJsonDocument(QJsonObject{{"deviceInfo", chunk}}).toJson(QJsonDocument::Compact));
        chunk = QJsonArray();
    };
    for (auto s : m_cfg.sensors) {
        auto ids = cloudIdentity(s, m_cfg.mqtt["gatewaySN"].toString());
        if (seen.contains(ids.second))
            continue;
        seen.insert(ids.second);
        QJsonObject item{{"nodeId", ids.second},
                         {"name", ids.first},
                         {"description", s.localId},
                         {"manufacturerId", m_cfg.mqtt["manufacturerId"]},
                         {"model", m_cfg.mqtt["sensorModel"]}};
        auto candidate = chunk;
        candidate.append(item);
        if (!chunk.isEmpty() &&
            QJsonDocument(QJsonObject{{"deviceInfo", candidate}}).toJson(QJsonDocument::Compact).size() >
                3800)
            enqueue();
        chunk.append(item);
    }
    enqueue();
}
void QGatewayMqtt::published(int mid) {
    if (mid != m_pendingMid)
        return;
    if (m_pendingTopology) {
        if (!m_topology.isEmpty())
            m_topology.dequeue();
    } else if (!m_data.isEmpty())
        m_data.dequeue();
    m_pendingMid = 0;
    if (m_topology.isEmpty())
        setStatus(m_cfg.mqtt["qos"].toInt(1) == 1 ? "MQTT 在线（QoS1 Broker 确认）"
                                                  : "MQTT 在线（QoS0 本地写出）");
    sendNext();
}
void QGatewayMqtt::sendNext() {
    if (!m_connected || m_pendingMid)
        return;
    bool topo = !m_topology.isEmpty();
    if (!topo && m_data.isEmpty())
        return;
    auto body = topo ? m_topology.head() : m_data.head();
    auto topic = ("/v1/devices/" + m_gateway + (topo ? "/topo/add" : "/datas")).toUtf8();
    int mid = 0;
    int rc = mosquitto_publish(m_mosq, &mid, topic.constData(), body.size(), body.constData(),
                               m_cfg.mqtt["qos"].toInt(1), false);
    if (rc != MOSQ_ERR_SUCCESS) {
        reset("MQTT 发布失败");
        return;
    }
    m_pendingMid = mid;
    m_pendingTopology = topo;
    m_pendingAt = m_clock.elapsed();
}
void QGatewayMqtt::tick() {
    if (!m_running)
        return;
    auto now = m_clock.elapsed();
    if (!m_mosq) {
        if (now >= m_retryAt)
            auth();
        return;
    }
    if (!m_connected) {
        if (now - m_connectAt > 15000)
            reset("MQTT 建连超时");
        return;
    }
    if (m_pendingMid && now - m_pendingAt > 15000) {
        reset("MQTT 发布确认超时");
        return;
    }
    if (now >= m_nextReport) {
        auto all = Object1CommInstance->reportDevices();
        QJsonArray chunk;
        auto enqueue = [&] {
            if (chunk.isEmpty())
                return;
            auto bytes = QJsonDocument(QJsonObject{{"devices", chunk}}).toJson(QJsonDocument::Compact);
            if (m_data.size() < 48)
                m_data.enqueue(bytes);
            else {
                ++m_dropped;
                setStatus(QString("MQTT 内存队列已满，丢弃 %1 个新批次").arg(m_dropped));
            }
            chunk = QJsonArray();
        };
        for (auto item : all) {
            auto candidate = chunk;
            candidate.append(item);
            if (!chunk.isEmpty() &&
                QJsonDocument(QJsonObject{{"devices", candidate}}).toJson(QJsonDocument::Compact).size() >
                    3800)
                enqueue();
            chunk.append(item);
        }
        enqueue();
        m_nextReport = now + qMax(1000, m_cfg.mqtt["reportMs"].toInt(10000));
    }
    sendNext();
}
