#include "gb28181.h"
#include <QtEndian>
namespace {
QString id() {
    return QUuid::createUuid().toString(QUuid::Id128);
}
QByteArray md5(QByteArray b) {
    return QCryptographicHash::hash(b, QCryptographicHash::Md5).toHex();
}
QString esc(QString s) {
    return s.toHtmlEscaped();
}
struct Bits {
    QByteArray bytes;
    int count = 0;
    void put(quint64 n, int size) {
        for (int i = size - 1; i >= 0; --i) {
            if (count % 8 == 0)
                bytes.append(char(0));
            if ((n >> i) & 1)
                bytes[bytes.size() - 1] = char(quint8(bytes.back()) | (1 << (7 - count % 8)));
            ++count;
        }
    }
};
quint32 psCrc(QByteArray b) {
    quint32 c = 0xffffffff;
    for (quint8 v : b) {
        c ^= quint32(v) << 24;
        for (int i = 0; i < 8; ++i)
            c = (c & 0x80000000) ? (c << 1) ^ 0x04c11db7 : c << 1;
    }
    return c;
}
void u16(QByteArray &b, int n) {
    b.append(char(n >> 8));
    b.append(char(n));
}
void u32(QByteArray &b, quint32 n) {
    for (int i = 3; i >= 0; --i)
        b.append(char(n >> (i * 8)));
}
QByteArray ptsBytes(quint64 p) {
    Bits b;
    b.put(2, 4);
    b.put(p >> 30, 3);
    b.put(1, 1);
    b.put(p >> 15, 15);
    b.put(1, 1);
    b.put(p, 15);
    b.put(1, 1);
    return b.bytes;
}
} // namespace
SipMessage SipMessage::parse(const QByteArray &b) {
    SipMessage m;
    int end = b.indexOf("\r\n\r\n");
    if (end < 0)
        return m;
    auto lines = b.left(end).split('\n');
    if (lines.isEmpty())
        return m;
    m.first = QString::fromUtf8(lines.takeFirst()).trimmed();
    for (auto l : lines) {
        int n = l.indexOf(':');
        if (n > 0) {
            auto k = QString::fromLatin1(l.left(n)).trimmed().toLower();
            auto v = QString::fromUtf8(l.mid(n + 1)).trimmed();
            if (m.headers.contains(k) && k == "via")
                continue;
            m.headers[k] = v;
        }
    }
    bool ok = false;
    int n = m.headers.value("content-length").toInt(&ok);
    if (!ok || n < 0 || n != b.size() - end - 4) {
        m.first.clear();
        return m;
    }
    m.body = b.mid(end + 4, n);
    return m;
}
QByteArray makePs(const QByteArray &au, quint64 p, bool key) {
    p &= 0x1ffffffffULL;
    QByteArray out = QByteArray::fromHex("000001ba");
    Bits h;
    h.put(1, 2);
    h.put(p >> 30, 3);
    h.put(1, 1);
    h.put(p >> 15, 15);
    h.put(1, 1);
    h.put(p, 15);
    h.put(1, 1);
    h.put(0, 9);
    h.put(1, 1);
    h.put(50000, 22);
    h.put(3, 2);
    h.put(31, 5);
    h.put(0, 3);
    out += h.bytes;
    if (key) {
        Bits s;
        s.put(1, 1);
        s.put(50000, 22);
        s.put(1, 1);
        s.put(0, 6);
        s.put(0, 4);
        s.put(1, 1);
        s.put(1, 5);
        s.put(0, 1);
        s.put(127, 7);
        s.put(0xe0, 8);
        s.put(3, 2);
        s.put(1, 1);
        s.put(512, 13);
        out += QByteArray::fromHex("000001bb");
        u16(out, s.bytes.size());
        out += s.bytes;
        QByteArray map = QByteArray::fromHex("000001bc000ee0ff000000041be00000");
        u32(map, psCrc(map));
        out += map;
    }
    for (int offset = 0; offset < au.size(); offset += 60000) {
        auto d = au.mid(offset, 60000);
        out += QByteArray::fromHex("000001e0");
        u16(out, d.size() + 8);
        out.append(char(0x84));
        out.append(char(0x80));
        out.append(char(5));
        out += ptsBytes(p);
        out += d;
    }
    return out;
}
QByteArray makeRtp(const QByteArray &p, quint16 seq, quint32 ts, quint32 ssrc, bool mark, int pt) {
    QByteArray b;
    b.append(char(0x80));
    b.append(char(pt | (mark ? 128 : 0)));
    u16(b, seq);
    u32(b, ts);
    u32(b, ssrc);
    return b + p;
}
Gb28181::Gb28181(const Config &c, Cameras *cam, QObject *p) : QObject(p), cfg_(c), cameras_(cam) {
    tag_ = id();
    call_ = id();
    clock_.start();
    tick_.setInterval(100);
    connect(&tick_, &QTimer::timeout, this, &Gb28181::timer);
    connect(&sip_, &QUdpSocket::readyRead, this, &Gb28181::receive);
}
Gb28181::~Gb28181() {
    stop();
}
QString Gb28181::device() const {
    return cfg_.gb["deviceId"].toString();
}
QString Gb28181::domain() const {
    return cfg_.gb["domain"].toString();
}
void Gb28181::state(QString s) {
    status_ = s;
    emit statusChanged(s);
}
void Gb28181::start() {
    if (!cfg_.gb["enabled"].toBool())
        return;
    server_ = QHostAddress(cfg_.gb["serverAddress"].toString());
    QHostAddress local(cfg_.gb["localAddress"].toString());
    if (server_.protocol() != QAbstractSocket::IPv4Protocol ||
        local.protocol() != QAbstractSocket::IPv4Protocol ||
        secret(cfg_.gb["passwordEnv"].toString()).isEmpty()) {
        state("GB 需配置 IPv4 地址及认证密码环境变量");
        return;
    }
    if (!sip_.bind(local, quint16(cfg_.gb["localPort"].toInt(5060)))) {
        state("GB SIP 本地端口绑定失败");
        return;
    }
    running_ = true;
    tick_.start();
    registerDevice();
}
void Gb28181::stop() {
    running_ = false;
    tick_.stop();
    for (auto key : sessions_.keys())
        closeSession(key);
    sip_.close();
    transactions_.clear();
    registered_ = false;
}
void Gb28181::send(QByteArray b) {
    sip_.writeDatagram(b, server_, quint16(cfg_.gb["serverPort"].toInt(5060)));
}
QString Gb28181::digest(QString method, QString uri) {
    if (nonce_.isEmpty())
        return {};
    auto password = secret(cfg_.gb["passwordEnv"].toString());
    QString cnonce = id();
    QString nc = QString("%1").arg(++nc_, 8, 16, QChar('0'));
    auto ha1 = md5((device() + ":" + realm_ + ":" + password).toUtf8()),
         ha2 = md5((method + ":" + uri).toUtf8());
    auto r = md5(ha1 + ":" + nonce_.toUtf8() + ":" +
                 (qop_.isEmpty() ? QByteArray() : nc.toUtf8() + ":" + cnonce.toUtf8() + ":auth:") + ha2);
    QString h = "Digest username=\"" + device() + "\", realm=\"" + realm_ + "\", nonce=\"" + nonce_ +
                "\", uri=\"" + uri + "\", response=\"" + QString::fromLatin1(r) + "\", algorithm=MD5";
    if (!qop_.isEmpty())
        h += ", qop=auth, nc=" + nc + ", cnonce=\"" + cnonce + "\"";
    if (!opaque_.isEmpty())
        h += ", opaque=\"" + opaque_ + "\"";
    return h;
}
void Gb28181::request(QString method, QString target, QByteArray body, QString auth) {
    QString seq = QString::number(++cseq_) + " " + method;
    QString local = cfg_.gb["localAddress"].toString() + ":" + QString::number(sip_.localPort());
    QString h = method + " " + target + " SIP/2.0\r\nVia: SIP/2.0/UDP " + local + ";rport;branch=z9hG4bK" +
                id() + "\r\nFrom: <sip:" + device() + "@" + domain() + ">;tag=" + tag_ +
                "\r\nTo: <sip:" + (method == "REGISTER" ? device() : cfg_.gb["serverId"].toString()) + "@" +
                domain() + ">\r\nCall-ID: " + call_ + "\r\nCSeq: " + seq +
                "\r\nMax-Forwards: 70\r\nContact: <sip:" + device() + "@" + local +
                ">\r\nUser-Agent: MyOverFlow/1.0\r\n";
    if (method == "REGISTER")
        h += "Expires: " + QString::number(qMax(120, cfg_.gb["expires"].toInt(3600))) + "\r\n";
    if (!auth.isEmpty())
        h += "Authorization: " + auth + "\r\n";
    if (!body.isEmpty())
        h += "Content-Type: Application/MANSCDP+xml\r\n";
    auto wire = h.toUtf8() + "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
    transactions_[seq] = {wire, 0, clock_.elapsed() + 500, method};
    send(wire);
}
void Gb28181::registerDevice() {
    QString target = "sip:" + cfg_.gb["serverId"].toString() + "@" + domain();
    request("REGISTER", target, {}, digest("REGISTER", target));
    nextRegister_ = clock_.elapsed() + 60000;
    state("GB 注册中");
}
QByteArray Gb28181::buildResponse(const SipMessage &m, int code, QString reason, QByteArray body,
                                  QString ct) {
    QString to = m.headers.value("to");
    if (!to.contains(";tag="))
        to += ";tag=" + tag_;
    QString h = "SIP/2.0 " + QString::number(code) + " " + reason + "\r\n";
    for (auto k : {"via", "from"})
        h += QString(k) + ": " + m.headers.value(k) + "\r\n";
    h += "To: " + to + "\r\nCall-ID: " + m.headers.value("call-id") + "\r\nCSeq: " + m.headers.value("cseq") +
         "\r\nContact: <sip:" + device() + "@" + cfg_.gb["localAddress"].toString() + ":" +
         QString::number(sip_.localPort()) + ">\r\n";
    if (!ct.isEmpty())
        h += "Content-Type: " + ct + "\r\n";
    return h.toUtf8() + "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
}
void Gb28181::response(const SipMessage &m, int code, QString reason, QByteArray b, QString ct) {
    auto wire = buildResponse(m, code, reason, b, ct);
    send(wire);
    auto key = m.headers.value("call-id") + "/" + m.headers.value("cseq");
    if (responses_.size() > 256)
        responses_.clear();
    responses_[key] = wire;
}
void Gb28181::receive() {
    while (sip_.hasPendingDatagrams()) {
        auto d = sip_.receiveDatagram();
        if (d.senderAddress() != server_ || d.senderPort() != cfg_.gb["serverPort"].toInt(5060))
            continue;
        auto m = SipMessage::parse(d.data());
        if (!m.first.isEmpty())
            handle(m);
    }
}
void Gb28181::handle(const SipMessage &m) {
    if (m.first.startsWith("SIP/2.0")) {
        if (m.headers.value("call-id") != call_)
            return;
        QString seq = m.headers.value("cseq");
        if (!transactions_.contains(seq))
            return;
        auto tx = transactions_.value(seq);
        int code = m.first.section(' ', 1, 1).toInt();
        if (code < 200)
            return;
        transactions_.remove(seq);
        if (tx.method == "REGISTER") {
            if (code == 401) {
                QString h = m.headers.value("www-authenticate");
                QMap<QString, QString> fields;
                QRegularExpression re("(\\w+)=(?:\"([^\"]*)\"|([^, ]+))");
                auto it = re.globalMatch(h);
                while (it.hasNext()) {
                    auto x = it.next();
                    fields[x.captured(1).toLower()] = x.captured(2).isEmpty() ? x.captured(3) : x.captured(2);
                }
                auto newNonce = fields["nonce"];
                if (newNonce == nonce_ && !nonce_.isEmpty() && fields["stale"] != "true") {
                    state("GB 密码被拒绝");
                    registered_ = false;
                    nextRegister_ = clock_.elapsed() + 60000;
                    return;
                }
                if (fields.value("algorithm", "MD5").toUpper() != "MD5" ||
                    (!fields["qop"].isEmpty() && !fields["qop"].split(',').contains("auth"))) {
                    state("GB 不支持的 Digest 算法");
                    return;
                }
                nonce_ = newNonce;
                realm_ = fields["realm"];
                qop_ = fields["qop"];
                opaque_ = fields["opaque"];
                nc_ = 0;
                if (nonce_.isEmpty())
                    return;
                registerDevice();
            } else if (code == 200) {
                registered_ = true;
                nextRegister_ = clock_.elapsed() + qMax(60, cfg_.gb["expires"].toInt(3600) * 4 / 5) * 1000LL;
                nextHeartbeat_ = 0;
                state("GB 已注册");
            } else {
                registered_ = false;
                state("GB 注册失败: " + QString::number(code));
            }
        }
        return;
    }
    QString method = m.first.section(' ', 0, 0), call = m.headers.value("call-id");
    if (call.isEmpty() || m.headers.value("via").isEmpty() || m.headers.value("from").isEmpty() ||
        m.headers.value("to").isEmpty())
        return;
    if (method != "ACK") {
        auto key = call + "/" + m.headers.value("cseq");
        if (responses_.contains(key)) {
            send(responses_[key]);
            return;
        }
    }
    if (method == "MESSAGE") {
        response(m, 200, "OK");
        xml(m.body);
    } else if (method == "INVITE")
        invite(m);
    else if (method == "ACK") {
        auto s = sessions_.value(call);
        if (s && !s->ack) {
            s->ack = true;
            launch(s);
        }
    } else if (method == "BYE") {
        response(m, 200, "OK");
        closeSession(call);
    } else if (method == "OPTIONS")
        response(m, 200, "OK");
    else
        response(m, 405, "Method Not Allowed");
}
void Gb28181::message(QByteArray body) {
    request("MESSAGE", "sip:" + cfg_.gb["serverId"].toString() + "@" + domain(), body);
}
void Gb28181::xml(const QByteArray &b) {
    QXmlStreamReader x(b);
    QMap<QString, QString> v;
    while (!x.atEnd()) {
        x.readNext();
        if (x.isStartElement() &&
            QStringList{"CmdType", "SN", "DeviceID", "PTZCmd", "PresetID"}.contains(x.name().toString())) {
            auto name = x.name().toString();
            v[name] = x.readElementText();
        }
    }
    if (x.hasError())
        return;
    QString cmd = v["CmdType"], base = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<Response><CmdType>" +
                                       esc(cmd) + "</CmdType><SN>" + esc(v["SN"]) + "</SN><DeviceID>" +
                                       device() + "</DeviceID>";
    if (cmd == "Catalog") {
        int count = 0;
        QString items;
        for (auto c : cfg_.cameras)
            if (c.enabled) {
                ++count;
                items += "<Item><DeviceID>" + c.channelId + "</DeviceID><Name>" + esc(c.name) +
                         "</Name><Manufacturer>Hikvision</Manufacturer><Model>IPCamera</"
                         "Model><Owner>MyOverFlow</Owner><CivilCode>" +
                         c.channelId.left(6) +
                         "</CivilCode><Address>监测现场</Address><Parental>0</Parental><ParentID>" +
                         device() +
                         "</ParentID><SafetyWay>0</SafetyWay><RegisterWay>1</RegisterWay><Secrecy>0</"
                         "Secrecy><Status>ON</Status></Item>";
            }
        message((base + "<SumNum>" + QString::number(count) + "</SumNum><DeviceList Num=\"" +
                 QString::number(count) + "\">" + items + "</DeviceList></Response>")
                    .toUtf8());
    } else if (cmd == "DeviceInfo")
        message((base +
                 "<DeviceName>MyOverFlow</DeviceName><Result>OK</Result><Manufacturer>MyOverFlow</"
                 "Manufacturer><Model>RK3399</Model><Firmware>1.0</Firmware><Channel>" +
                 QString::number(cfg_.cameras.size()) + "</Channel></Response>")
                    .toUtf8());
    else if (cmd == "DeviceStatus")
        message((base + "<Result>OK</Result><Online>ONLINE</Online><Status>OK</Status><DeviceTime>" +
                 QDateTime::currentDateTime().toString(Qt::ISODate) +
                 "</DeviceTime><Alarmstatus Num=\"0\"/></Response>")
                    .toUtf8());
    else if (cmd == "DeviceControl") {
        int i = -1;
        for (int n = 0; n < cfg_.cameras.size(); ++n)
            if (cfg_.cameras[n].channelId == v["DeviceID"])
                i = n;
        if (i < 0)
            return;
        auto ptz = QByteArray::fromHex(v["PTZCmd"].toLatin1());
        if (ptz.size() != 8 || quint8(ptz[0]) != 0xa5)
            return;
        int sum = 0;
        for (int n = 0; n < 7; ++n)
            sum += quint8(ptz[n]);
        if ((sum & 255) != quint8(ptz[7]))
            return;
        int c = quint8(ptz[3]), pan = quint8(ptz[4]) * 100 / 255, tilt = quint8(ptz[5]) * 100 / 255,
            zoom = (quint8(ptz[6]) >> 4) * 100 / 15;
        if (c == 0x81)
            cameras_->preset(i, quint8(ptz[5]), true);
        else if (c == 0x82)
            cameras_->preset(i, quint8(ptz[5]));
        else
            cameras_->ptz(i,
                          (c & 1)   ? pan
                          : (c & 2) ? -pan
                                    : 0,
                          (c & 8)   ? tilt
                          : (c & 4) ? -tilt
                                    : 0,
                          (c & 16)   ? zoom
                          : (c & 32) ? -zoom
                                     : 0);
    }
}
void Gb28181::invite(const SipMessage &m) {
    QString target = m.first.section(' ', 1, 1).section(':', 1).section('@', 0, 0);
    int camera = -1;
    for (int i = 0; i < cfg_.cameras.size(); ++i)
        if (cfg_.cameras[i].enabled && cfg_.cameras[i].channelId == target)
            camera = i;
    if (camera < 0) {
        response(m, 404, "Not Found");
        return;
    }
    if (sessions_.size() >= 8 || sessions_.contains(m.headers.value("call-id"))) {
        response(m, 486, "Busy Here");
        return;
    }
    QString ip;
    int port = 0, pt = -1;
    quint32 ssrc = 0;
    bool media = false, udp = false, play = true;
    QStringList offered;
    for (auto line : m.body.split('\n')) {
        auto l = QString::fromLatin1(line).trimmed();
        if (l.startsWith("c=IN IP4 "))
            ip = l.mid(9);
        if (l.startsWith("s=") && l != "s=Play")
            play = false;
        if (l.startsWith("m=video ")) {
            auto f = l.split(' ');
            if (f.size() >= 4) {
                port = f[1].toInt();
                udp = f[2] == "RTP/AVP";
                offered = f.mid(3);
                media = true;
            }
        }
        if (l.startsWith("a=rtpmap:")) {
            auto payload = l.mid(9).section(' ', 0, 0);
            if (l.section(' ', 1).compare("PS/90000", Qt::CaseInsensitive) == 0 && offered.contains(payload))
                pt = payload.toInt();
        }
        if (l.startsWith("y="))
            ssrc = l.mid(2).toUInt();
    }
    if (!play || !media || !udp || pt < 0 || pt > 127 || port < 1 || port > 65535 ||
        QHostAddress(ip).protocol() != QAbstractSocket::IPv4Protocol || ssrc == 0) {
        response(m, 488, "Not Acceptable Here");
        return;
    }
    auto s = new Session;
    s->call = m.headers.value("call-id");
    s->camera = camera;
    s->remote = QHostAddress(ip);
    s->remotePort = quint16(port);
    s->pt = pt;
    s->ssrc = ssrc;
    s->created = clock_.elapsed();
    s->nextResponse = s->created + 500;
    s->fps = qBound(1, cfg_.gb["fps"].toInt(25), 60);
    s->rtp = new QUdpSocket(this);
    if (!s->rtp->bind(QHostAddress(cfg_.gb["localAddress"].toString()), quint16(0))) {
        delete s->rtp;
        delete s;
        response(m, 500, "Server Internal Error");
        return;
    }
    QString local = cfg_.gb["localAddress"].toString();
    QByteArray body = ("v=0\r\no=" + target + " 0 0 IN IP4 " + local + "\r\ns=Play\r\nc=IN IP4 " + local +
                       "\r\nt=0 0\r\nm=video " + QString::number(s->rtp->localPort()) + " RTP/AVP " +
                       QString::number(pt) + "\r\na=sendonly\r\na=rtpmap:" + QString::number(pt) +
                       " PS/90000\r\ny=" + QString("%1").arg(ssrc, 10, 10, QChar('0')) + "\r\n")
                          .toLatin1();
    s->response = buildResponse(m, 200, "OK", body, "application/sdp");
    sessions_[s->call] = s;
    send(s->response);
    responses_[s->call + "/" + m.headers.value("cseq")] = s->response;
}
void Gb28181::launch(Session *s) {
    auto p = new QProcess(this);
    s->process = p;
    p->setProgram(cfg_.recording["ffmpeg"].toString("ffmpeg"));
    p->setArguments({"-hide_banner", "-loglevel", "error", "-rtsp_transport", "tcp", "-timeout", "10000000",
                     "-i", cameraUrl(cfg_.cameras[s->camera]), "-map", "0:v:0", "-c:v", "copy", "-an",
                     "-bsf:v", "h264_mp4toannexb,dump_extra=freq=keyframe,h264_metadata=aud=insert", "-f",
                     "h264", "pipe:1"});
    connect(p, &QProcess::readyReadStandardOutput, this, [this, s] { media(s); });
    connect(p, &QProcess::readyReadStandardError, this, [this, p] {
        p->readAllStandardError();
        state("GB 媒体输入异常，请检查 H.264 码流");
    });
    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, s](int, QProcess::ExitStatus) {
                auto call = s->call;
                state("GB 媒体进程结束");
                closeSession(call);
            });
    connect(p, &QProcess::errorOccurred, this, [this, s](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            state("GB 无法启动 FFmpeg");
            closeSession(s->call);
        }
    });
    p->start();
}
void Gb28181::media(Session *s) {
    s->buffer += s->process->readAllStandardOutput();
    if (s->buffer.size() > 8 * 1024 * 1024) {
        state("GB 码流缓冲溢出");
        s->buffer.clear();
        return;
    }
    // FFmpeg inserts AUD (NAL type 9), allowing complete access-unit RTP markers.
    auto aud = [](const QByteArray &b, int start) {
        for (int i = start; i + 5 < b.size(); ++i) {
            int n = b.mid(i, 4) == QByteArray::fromHex("00000001") ? 4
                    : b.mid(i, 3) == QByteArray::fromHex("000001") ? 3
                                                                   : 0;
            if (n && (quint8(b[i + n]) & 31) == 9)
                return i;
        }
        return -1;
    };
    while (true) {
        int a = aud(s->buffer, 0);
        if (a < 0)
            return;
        int b = aud(s->buffer, a + 4);
        if (b < 0)
            return;
        auto au = s->buffer.mid(a, b - a);
        s->buffer.remove(0, b);
        bool key = false;
        for (int i = 0; i + 4 < au.size(); ++i)
            if (au.mid(i, 3) == QByteArray::fromHex("000001") && (quint8(au[i + 3]) & 31) == 5)
                key = true;
        auto ps = makePs(au, s->timestamp, key);
        for (int off = 0; off < ps.size(); off += 1300) {
            auto payload = ps.mid(off, 1300);
            auto r = makeRtp(payload, s->sequence++, s->timestamp, s->ssrc, off + payload.size() == ps.size(),
                             s->pt);
            if (s->rtp->writeDatagram(r, s->remote, s->remotePort) != r.size())
                state("GB RTP 发送失败");
        }
        s->timestamp += 90000 / s->fps;
    }
}
void Gb28181::closeSession(QString call) {
    auto s = sessions_.take(call);
    if (!s)
        return;
    if (s->process) {
        s->process->disconnect(this);
        s->process->kill();
        s->process->waitForFinished(1000);
        s->process->deleteLater();
    }
    delete s->rtp;
    delete s;
}
void Gb28181::timer() {
    auto now = clock_.elapsed();
    for (auto key : transactions_.keys()) {
        if (!transactions_.contains(key))
            continue;
        auto &t = transactions_[key];
        if (now < t.next)
            continue;
        if (t.count >= 6) {
            if (t.method == "REGISTER") {
                registered_ = false;
                state("GB 注册无响应");
            }
            transactions_.remove(key);
            continue;
        }
        send(t.wire);
        t.next = now + qMin(4000, 500 * (1 << ++t.count));
    }
    if (now >= nextRegister_)
        registerDevice();
    if (registered_ && now >= nextHeartbeat_) {
        message(("<?xml version=\"1.0\"?><Notify><CmdType>Keepalive</CmdType><SN>" + QString::number(++sn_) +
                 "</SN><DeviceID>" + device() + "</DeviceID><Status>OK</Status></Notify>")
                    .toUtf8());
        nextHeartbeat_ = now + qMax(5, cfg_.gb["heartbeatSeconds"].toInt(60)) * 1000LL;
    }
    for (auto key : sessions_.keys()) {
        auto s = sessions_[key];
        if (!s->ack) {
            if (now - s->created > 32000)
                closeSession(key);
            else if (now >= s->nextResponse) {
                send(s->response);
                s->nextResponse = now + 2000;
            }
        }
    }
}
