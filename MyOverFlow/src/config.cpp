#include "config.h"
#include <cmath>
QString secret(const QString &name) {
    return name.isEmpty() ? QString() : qEnvironmentVariable(name.toUtf8().constData());
}
QString utcNow() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}
bool Config::load(const QString &file, QString *error) {
    QFile f(file);
    auto fail = [&](QString e) {
        *error = e;
        return false;
    };
    if (!f.open(QIODevice::ReadOnly))
        return fail(f.errorString());
    QJsonParseError pe;
    auto doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error || !doc.isObject())
        return fail("配置 JSON 无效: " + pe.errorString());
    path = QFileInfo(file).absoluteFilePath();
    raw = doc.object();
    dataDir = raw.value("dataDir").toString(QDir::homePath() + "/.local/share/MyOverFlow");
    if (!QDir::isAbsolutePath(dataDir))
        dataDir = QFileInfo(file).dir().absoluteFilePath(dataDir);
    ports.clear();
    sensors.clear();
    cameras.clear();
    QSet<QString> ids, channels, devices;
    for (auto v : raw.value("ports").toArray()) {
        auto o = v.toObject();
        Port p;
        p.device = o["device"].toString();
        p.baud = o["baud"].toInt(9600);
        p.parity = o["parity"].toString("N");
        p.stopBits = o["stopBits"].toInt(1);
        p.dataBits = o["dataBits"].toInt(8);
        p.timeout = o["timeoutMs"].toInt(500);
        p.retries = o["retries"].toInt(2);
        if (p.device.isEmpty() || devices.contains(p.device) || p.baud < 1200 || p.timeout < 50 ||
            p.retries < 0 || p.retries > 5 || !QStringList{"N", "E", "O"}.contains(p.parity) ||
            p.dataBits != 8 || (p.stopBits != 1 && p.stopBits != 2))
            return fail("串口参数无效或设备路径重复");
        devices.insert(p.device);
        ports << p;
    }
    if (ports.isEmpty() || ports.size() > 8)
        return fail("串口数量必须为 1..8");
    for (auto v : raw.value("cameras").toArray()) {
        auto o = v.toObject();
        Camera c;
        c.enabled = o["enabled"].toBool();
        c.name = o["name"].toString();
        c.rtsp = o["rtsp"].toString();
        c.isapi = o["isapi"].toString();
        c.user = o["user"].toString();
        c.passwordEnv = o["passwordEnv"].toString();
        c.channelId = o["channelId"].toString();
        c.channel = o["channel"].toInt(1);
        if (c.enabled && (QUrl(c.rtsp).scheme() != "rtsp" || !QUrl(c.rtsp).userInfo().isEmpty() ||
                          !QStringList{"http", "https"}.contains(QUrl(c.isapi).scheme()) ||
                          !QUrl(c.isapi).userInfo().isEmpty() || c.channel < 1))
            return fail("摄像头地址无效；密码必须通过环境变量提供");
        if (!c.channelId.isEmpty() && (!QRegularExpression("^[0-9]{20}$").match(c.channelId).hasMatch() ||
                                       channels.contains(c.channelId)))
            return fail("GB 通道编号必须是唯一的 20 位数字");
        if (c.enabled)
            channels.insert(c.channelId);
        cameras << c;
    }
    if (cameras.size() > 8)
        return fail("摄像头最多 8 个");
    for (auto v : raw.value("sensors").toArray()) {
        auto o = v.toObject();
        Sensor s;
        s.id = o["id"].toString();
        s.name = o["name"].toString();
        s.unit = o["unit"].toString();
        s.deviceId = o["deviceId"].toString();
        s.nodeId = o["nodeId"].toString();
        s.valueKey = o["valueKey"].toString("value");
        s.alarmKey = o["alarmKey"].toString("alarm");
        s.serviceId = o["serviceId"].toString("analog");
        if (s.valueKey.isEmpty() || s.alarmKey.isEmpty() || s.valueKey == s.alarmKey || s.serviceId.isEmpty())
            return fail("上报属性名称或服务名称无效: " + s.id);
        s.port = o["port"].toInt();
        s.slave = o["slave"].toInt(1);
        s.function = o["function"].toInt(3);
        s.reg = o["register"].toInt();
        s.format = o["format"].toString("u16");
        s.order = o["wordOrder"].toString("ABCD");
        s.count = (s.format.endsWith("32") ? 2 : 1);
        s.scale = o["scale"].toDouble(1);
        s.offset = o["offset"].toDouble();
        s.low = o["low"].toDouble();
        s.high = o["high"].toDouble(100);
        s.hysteresis = o["hysteresis"].toDouble(1);
        s.interval = o["intervalMs"].toInt(1000);
        s.camera = o["camera"].toInt(-1);
        s.preset = o["preset"].toInt(1);
        if (s.id.isEmpty() || ids.contains(s.id) || s.port < 0 || s.port >= ports.size() || s.slave < 1 ||
            s.slave > 247 || (s.function != 3 && s.function != 4) || s.reg < 0 || s.reg + s.count > 65536 ||
            s.interval < 100 || s.low >= s.high || s.hysteresis < 0 || s.hysteresis * 2 >= s.high - s.low ||
            s.camera < -1 || s.camera >= cameras.size() || s.preset < 1 || s.preset > 300 ||
            !QStringList{"u16", "s16", "u32", "s32", "f32"}.contains(s.format) ||
            !QStringList{"ABCD", "CDAB", "BADC", "DCBA"}.contains(s.order) || !std::isfinite(s.scale))
            return fail("传感器配置无效: " + s.id);
        ids.insert(s.id);
        sensors << s;
    }
    if (sensors.isEmpty() || sensors.size() > 512)
        return fail("传感器数量必须为 1..512");
    mqtt = raw["mqtt"].toObject();
    recording = raw["recording"].toObject();
    gb = raw["gb28181"].toObject();
    if (mqtt["enabled"].toBool()) {
        if (mqtt["qos"].toInt(1) != 0 && mqtt["qos"].toInt(1) != 1)
            return fail("MQTT QoS 仅支持 0 或 1");
        QUrl u(mqtt["authUrl"].toString());
        if (!u.isValid() || u.host().isEmpty() || !u.userInfo().isEmpty() ||
            !QStringList{"http", "https"}.contains(u.scheme()) || mqtt["gatewaySN"].toString().isEmpty() ||
            mqtt["checkIdEnv"].toString().isEmpty())
            return fail("MQTT 网关鉴权参数缺失");
        for (const auto &s : sensors)
            if (s.deviceId.isEmpty() || s.nodeId.isEmpty())
                return fail("启用 MQTT 时须配置各传感器 deviceId 和 nodeId");
    }
    if (recording["enabled"].toBool() &&
        (!QDir::isAbsolutePath(recording["directory"].toString()) || recording["maxGiB"].toDouble(100) <= 0 ||
         recording["reserveMiB"].toDouble(1024) < 0 || recording["segmentSeconds"].toInt(300) < 5))
        return fail("录像目录必须为绝对路径，容量及分段时间必须有效");
    if (gb["enabled"].toBool()) {
        if (gb["version"].toString("2016") != "2016")
            return fail("当前 GB28181 实现支持 2016 UDP 实时点播，尚不支持 2022 安全扩展");
        for (QString key : {"localPort", "serverPort"})
            if (gb[key].toInt(5060) < 1 || gb[key].toInt(5060) > 65535)
                return fail("GB SIP 端口必须为 1..65535");
        if (!QRegularExpression("^[0-9]{10,20}$").match(gb["domain"].toString()).hasMatch())
            return fail("GB 域编码必须是 10..20 位数字");
        for (QString k : {"deviceId", "serverId", "domain"})
            if (!QRegularExpression("^[0-9]{20}$").match(gb[k].toString()).hasMatch() && k != "domain")
                return fail("GB 设备及服务器 ID 须为 20 位数字");
        if (gb["localAddress"].toString().isEmpty() || gb["serverAddress"].toString().isEmpty() ||
            gb["passwordEnv"].toString().isEmpty())
            return fail("GB 地址或密码环境变量名缺失");
        for (auto &c : cameras)
            if (c.enabled && c.channelId.isEmpty())
                return fail("启用 GB 时摄像头必须配置 channelId");
    }
    return true;
}
