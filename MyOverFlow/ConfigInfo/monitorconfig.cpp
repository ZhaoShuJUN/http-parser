#include "monitorconfig.h"
#include <climits>
#include <cmath>
QString monitorUtcNow() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}
QString monitorSecret(const QString &name) {
    return name.isEmpty() ? QString() : qEnvironmentVariable(name.toUtf8().constData());
}
namespace {
QJsonObject readObject(QSettings &s, const QString &group, const QJsonObject &defaults) {
    QJsonObject o = defaults;
    s.beginGroup(group);
    for (auto k : s.childKeys()) {
        auto v = s.value(k);
        auto d = defaults[k];
        if (d.isBool())
            o[k] = v.toBool();
        else if (d.isDouble())
            o[k] = v.toDouble();
        else
            o[k] = v.toString();
    }
    s.endGroup();
    return o;
}
void writeObject(QSettings &s, const QString &group, const QJsonObject &o) {
    s.beginGroup(group);
    for (auto i = o.begin(); i != o.end(); ++i)
        s.setValue(i.key(), i.value().toVariant());
    s.endGroup();
}
QJsonObject mqttDefaults() {
    return {{"enabled", false},
            {"authUrl", ""},
            {"gatewaySN", ""},
            {"checkIdEnv", "MONITOR_CHECK_ID"},
            {"manufacturerId", "YSHY"},
            {"model", "HSHY-HK68-510P"},
            {"sensorModel", "HSHY-HK68-510-001"},
            {"qos", 1},
            {"reportMs", 10000},
            {"username", ""},
            {"passwordEnv", "MQTT_PASSWORD"},
            {"tls", false},
            {"caFile", ""}};
}
QJsonObject recordingDefaults() {
    return {{"enabled", false},     {"directory", "/mnt/monitor-disk"},
            {"requireMount", true}, {"maxGiB", 200},
            {"reserveMiB", 2048},   {"segmentSeconds", 300},
            {"ffmpeg", "ffmpeg"}};
}
QJsonObject gbDefaults() {
    return {{"enabled", false},
            {"version", "2016"},
            {"charset", "GB2312"},
            {"deviceId", "34020000002000000001"},
            {"serverId", "34020000002000000000"},
            {"domain", "3402000000"},
            {"serverAddress", "192.168.1.10"},
            {"serverPort", 5060},
            {"localAddress", "192.168.1.20"},
            {"localPort", 5060},
            {"passwordEnv", "GB28181_PASSWORD"},
            {"expires", 3600},
            {"heartbeatSeconds", 60},
            {"fps", 25}};
}
} // namespace
bool readMonitorIni(const QString &path, MonitorSettings *out, QString *error) {
    if (!QFileInfo::exists(path)) {
        *error = "配置文件不存在: " + path;
        return false;
    }
    QSettings set(path, QSettings::IniFormat);
    set.setIniCodec("UTF-8");
    const QRegularExpression integerKey(
        "^(UartCfg/(UartNumb|UartBaud[0-9]+|UartEffe[0-9]+|UartStop[0-9]+|TimeoutMs[0-9]+|Retries[0-9]+)|"
        "DeviceCfg/"
        "(DeviceHaveNumb|DevicePort[0-9]+|DeviceAddr[0-9]+|DeviceRegStart[0-9]+|DeviceRegEnd[0-9]+)|"
        "CameraCfg/Count|Camera[0-9]+/PtzChannel|Sensor[0-9]+/(IntervalMs|Camera|Preset)|Mqtt/(qos|reportMs)|"
        "Recording/segmentSeconds|GB28181/(serverPort|localPort|expires|heartbeatSeconds|fps))$");
    const QRegularExpression numberKey(
        "^(Sensor[0-9]+/(Scale|Offset|Low|High|Hysteresis)|Recording/(maxGiB|reserveMiB))$");
    const QRegularExpression booleanKey("^(Sensor[0-9]+/Calibrated|Camera[0-9]+/Enabled|Mqtt/"
                                        "(enabled|tls)|Recording/(enabled|requireMount)|GB28181/enabled)$");
    for (const auto &key : set.allKeys()) {
        bool ok = false;
        const auto value = set.value(key).toString().trimmed();
        if (integerKey.match(key).hasMatch()) {
            const auto n = value.toLongLong(&ok);
            if (!ok || n < INT_MIN || n > INT_MAX) {
                *error = "配置项须为有效整数: " + key;
                return false;
            }
        } else if (numberKey.match(key).hasMatch()) {
            const double n = value.toDouble(&ok);
            if (!ok || !std::isfinite(n)) {
                *error = "配置项须为有限数值: " + key;
                return false;
            }
        } else if (booleanKey.match(key).hasMatch() &&
                   !QStringList{"true", "false", "0", "1"}.contains(value.toLower())) {
            *error = "布尔配置项须为 true/false 或 0/1: " + key;
            return false;
        }
    }
    MonitorSettings c;
    c.path = QFileInfo(path).absoluteFilePath();
    c.dataDir = set.value("Monitor/DataDir", "runtime").toString();
    if (!QDir::isAbsolutePath(c.dataDir))
        c.dataDir = QFileInfo(path).dir().absoluteFilePath(c.dataDir);
    int ports = set.value("UartCfg/UartNumb", 8).toInt();
    if (ports < 1 || ports > 8) {
        *error = "串口数量必须为 1..8";
        return false;
    }
    for (int i = 1; i <= ports; ++i) {
        QString n = QString::number(i);
        PortSetting p;
        p.device = set.value("UartCfg/UartPort" + n, "/dev/ttyUSB" + QString::number(i - 1)).toString();
        p.baud = set.value("UartCfg/UartBaud" + n, 9600).toInt();
        p.parity = set.value("UartCfg/UartEffe" + n, 0).toInt();
        p.stopBits = set.value("UartCfg/UartStop" + n, 0).toInt();
        p.timeoutMs = set.value("UartCfg/TimeoutMs" + n, 500).toInt();
        p.retries = set.value("UartCfg/Retries" + n, 2).toInt();
        c.ports << p;
    }
    int count = set.value("DeviceCfg/DeviceHaveNumb", 5).toInt();
    if (count < 1 || count > 200) {
        *error = "设备测点数量必须为 1..200";
        return false;
    }
    const QStringList names{"温度", "湿度", "液位", "六氟化硫", "烟雾浓度"},
        ids{"temperature1", "humidity1", "level1", "sf6_1", "smoke1"},
        locals{"th_1", "th_2", "wl_1", "gas_1", "smoke_1"};
    for (int i = 1; i <= count; ++i) {
        QString n = QString::number(i), g = "Sensor" + n + "/";
        SensorSetting s;
        s.id = set.value(g + "Id", i <= 5 ? ids[i - 1] : "sensor" + n).toString();
        s.name = set.value(g + "Name", i <= 5 ? names[i - 1] : "传感器" + n).toString();
        s.category = set.value(g + "Category", i <= 5 ? names[i - 1] : "其他").toString();
        s.unit = set.value(g + "Unit", "原始值").toString();
        s.port = set.value("DeviceCfg/DevicePort" + n, 0).toInt();
        s.slave = set.value("DeviceCfg/DeviceAddr" + n, i).toInt();
        s.reg = set.value("DeviceCfg/DeviceRegStart" + n, 0).toInt();
        s.format = set.value(g + "Format", "u16").toString();
        s.count = s.format.endsWith("32") ? 2 : 1;
        s.order = set.value(g + "WordOrder", "ABCD").toString();
        s.scale = set.value(g + "Scale", 1).toDouble();
        s.offset = set.value(g + "Offset", 0).toDouble();
        s.low = set.value(g + "Low", 0).toDouble();
        s.high = set.value(g + "High", 100).toDouble();
        s.hysteresis = set.value(g + "Hysteresis", 1).toDouble();
        s.intervalMs = set.value(g + "IntervalMs", 1000).toInt();
        s.camera = set.value(g + "Camera", -1).toInt();
        s.preset = set.value(g + "Preset", 1).toInt();
        s.localId = set.value(g + "LocalId", i <= 5 ? locals[i - 1] : "sensor_" + n).toString();
        s.deviceId = set.value(g + "DeviceId").toString();
        s.nodeId = set.value(g + "NodeId").toString();
        s.valueKey = set.value(g + "ValueKey", "value").toString();
        s.alarmKey = set.value(g + "AlarmKey", "alarm").toString();
        s.serviceId = set.value(g + "ServiceId", "analog").toString();
        s.calibrated = set.value(g + "Calibrated", false).toBool();
        c.sensors << s;
    }
    int cams = set.value("CameraCfg/Count", 8).toInt();
    if (cams < 0 || cams > 8) {
        *error = "摄像头数量必须为 0..8";
        return false;
    }
    for (int i = 1; i <= cams; ++i) {
        QString g = "Camera" + QString::number(i) + "/";
        CameraSetting x;
        x.enabled = set.value(g + "Enabled", false).toBool();
        x.name = set.value(g + "Name", "摄像头" + QString::number(i)).toString();
        x.rtsp = set.value(g + "Rtsp",
                           "rtsp://192.168.1." + QString::number(100 + i) + ":554/Streaming/Channels/101")
                     .toString();
        x.isapi = set.value(g + "Isapi", "http://192.168.1." + QString::number(100 + i)).toString();
        x.user = set.value(g + "User", "admin").toString();
        x.passwordEnv = set.value(g + "PasswordEnv", "CAMERA_" + QString::number(i) + "_PASSWORD").toString();
        x.channel = set.value(g + "PtzChannel", 1).toInt();
        x.channelId =
            set.value(g + "ChannelId", QString("340200000013200000%1").arg(i, 2, 10, QChar('0'))).toString();
        c.cameras << x;
    }
    c.mqtt = readObject(set, "Mqtt", mqttDefaults());
    c.recording = readObject(set, "Recording", recordingDefaults());
    c.gb = readObject(set, "GB28181", gbDefaults());
    if (set.status() != QSettings::NoError) {
        *error = "INI 文件读取失败";
        return false;
    }
    if (!validateMonitorSettings(c, error))
        return false;
    *out = c;
    return true;
}
bool validateMonitorSettings(const MonitorSettings &c, QString *error) {
    auto fail = [&](QString e) {
        *error = e;
        return false;
    };
    QSet<QString> ports, ids, channels;
    if (c.ports.isEmpty() || c.ports.size() > 8)
        return fail("串口数量必须为 1..8");
    for (auto p : c.ports) {
        if (p.device.isEmpty() || ports.contains(p.device) || p.baud < 1200 || p.baud > 4000000 ||
            p.parity < 0 || p.parity > 2 || (p.stopBits != 0 && p.stopBits != 2) || p.timeoutMs < 50 ||
            p.timeoutMs > 10000 || p.retries < 0 || p.retries > 5)
            return fail("串口路径重复或参数无效；停止位 0=1 位，2=2 位");
        ports.insert(p.device);
    }
    if (c.cameras.size() > 8)
        return fail("摄像头最多 8 路");
    for (auto x : c.cameras) {
        if (!x.channelId.isEmpty() && (!QRegularExpression("^[0-9]{20}$").match(x.channelId).hasMatch() ||
                                       channels.contains(x.channelId)))
            return fail("摄像头通道编号须为唯一的 20 位数字");
        channels.insert(x.channelId);
        if (x.enabled &&
            (QUrl(x.rtsp).scheme() != "rtsp" || QUrl(x.rtsp).host().isEmpty() ||
             !QUrl(x.rtsp).userInfo().isEmpty() ||
             !QStringList{"http", "https"}.contains(QUrl(x.isapi).scheme()) ||
             QUrl(x.isapi).host().isEmpty() || !QUrl(x.isapi).userInfo().isEmpty() || x.channel < 1))
            return fail("摄像头地址无效，认证密码请使用环境变量");
    }
    if (c.sensors.isEmpty() || c.sensors.size() > 200)
        return fail("传感器测点数量必须为 1..200");
    for (auto s : c.sensors) {
        if (s.id.size() > 64 || s.name.size() > 128 || s.unit.size() > 32 || s.deviceId.size() > 31 ||
            s.nodeId.size() > 79 || s.valueKey.size() > 32 || s.alarmKey.size() > 32 ||
            s.serviceId.isEmpty() || s.serviceId.size() > 31)
            return fail("传感器标识、名称或上报字段长度超限: " + s.id);
        if (s.id.isEmpty() || ids.contains(s.id) || s.port < 0 || s.port >= c.ports.size() || s.slave < 1 ||
            s.slave > 247 || s.reg < 0 || s.reg + s.count > 65536 || s.intervalMs < 100 ||
            !QStringList{"u16", "s16", "u32", "s32", "f32"}.contains(s.format) ||
            !QStringList{"ABCD", "CDAB", "BADC", "DCBA"}.contains(s.order) || !std::isfinite(s.scale) ||
            !std::isfinite(s.offset) || !std::isfinite(s.low) || !std::isfinite(s.high) ||
            !std::isfinite(s.hysteresis) || s.low >= s.high || s.hysteresis < 0 ||
            s.hysteresis * 2 >= s.high - s.low || s.camera < -1 || s.camera >= c.cameras.size() ||
            s.preset < 1 || s.preset > 300 || s.valueKey.isEmpty() || s.alarmKey.isEmpty() ||
            s.valueKey == s.alarmKey)
            return fail("传感器参数无效: " + s.id);
        ids.insert(s.id);
    }
    if (c.mqtt["enabled"].toBool()) {
        QUrl u(c.mqtt["authUrl"].toString());
        if (!QStringList{"http", "https"}.contains(u.scheme()) || u.host().isEmpty() ||
            !u.userInfo().isEmpty() || c.mqtt["gatewaySN"].toString().isEmpty() ||
            c.mqtt["checkIdEnv"].toString().isEmpty() ||
            (c.mqtt["qos"].toInt(1) != 0 && c.mqtt["qos"].toInt(1) != 1))
            return fail("MQTT 鉴权参数或 QoS 无效");
        if (c.mqtt["gatewaySN"].toString().size() > 32 || c.mqtt["reportMs"].toInt() < 1000)
            return fail("MQTT 网关序列号长度或上报周期无效");
        for (auto s : c.sensors)
            if (!QRegularExpression("^[A-Za-z0-9_-]{1,31}$").match(s.localId).hasMatch())
                return fail("MQTT LocalId 无效: " + s.id);
    }
    if (c.recording["enabled"].toBool() &&
        (!QDir::isAbsolutePath(c.recording["directory"].toString()) ||
         c.recording["maxGiB"].toDouble() <= 0 || c.recording["reserveMiB"].toDouble() < 0 ||
         c.recording["segmentSeconds"].toInt() < 5))
        return fail("录像路径、配额或分段时间无效");
    if (c.gb["enabled"].toBool()) {
        if (!QStringList{"GB2312", "UTF-8"}.contains(c.gb["charset"].toString("GB2312")))
            return fail("GB XML 编码仅支持 GB2312 或 UTF-8");
        if (c.gb["version"].toString() != "2016")
            return fail("目前支持 GB28181-2016 UDP 实时接入，不可声明为 2022 安全扩展");
        for (auto k : {"deviceId", "serverId"})
            if (!QRegularExpression("^[0-9]{20}$").match(c.gb[k].toString()).hasMatch())
                return fail("GB 设备及平台 ID 需要 20 位数字");
        if (!QRegularExpression("^[0-9]{10,20}$").match(c.gb["domain"].toString()).hasMatch())
            return fail("GB 域编码无效");
        for (auto k : {"localPort", "serverPort"})
            if (c.gb[k].toInt() < 1 || c.gb[k].toInt() > 65535)
                return fail("GB SIP 端口无效");
        if (c.gb["localAddress"].toString().isEmpty() || c.gb["serverAddress"].toString().isEmpty() ||
            c.gb["passwordEnv"].toString().isEmpty())
            return fail("GB 地址或密码变量名缺失");
        if (c.gb["expires"].toInt() < 120 || c.gb["expires"].toInt() > 86400 ||
            c.gb["heartbeatSeconds"].toInt() < 5 || c.gb["heartbeatSeconds"].toInt() > 3600 ||
            c.gb["fps"].toInt() < 1 || c.gb["fps"].toInt() > 60)
            return fail("GB 注册周期、心跳或帧率无效");
    }
    return true;
}
bool writeMonitorIni(const QString &path, const MonitorSettings &c, QString *error) {
    if (!validateMonitorSettings(c, error))
        return false;
    QTemporaryFile tmp(QFileInfo(path).dir().absoluteFilePath(".monitor-XXXXXX.ini"));
    if (!tmp.open()) {
        *error = tmp.errorString();
        return false;
    }
    QString name = tmp.fileName();
    if (QFileInfo::exists(path)) {
        QFile old(path);
        if (!old.open(QIODevice::ReadOnly)) {
            *error = old.errorString();
            return false;
        }
        tmp.write(old.readAll());
        tmp.flush();
    }
    tmp.close();
    {
        QSettings set(name, QSettings::IniFormat);
        set.setIniCodec("UTF-8");
        set.setValue("Monitor/DataDir", c.dataDir);
        set.setValue("UartCfg/UartNumb", c.ports.size());
        for (int i = 0; i < c.ports.size(); ++i) {
            auto p = c.ports[i];
            auto n = QString::number(i + 1);
            set.setValue("UartCfg/UartPort" + n, p.device);
            set.setValue("UartCfg/UartBaud" + n, p.baud);
            set.setValue("UartCfg/UartEffe" + n, p.parity);
            set.setValue("UartCfg/UartStop" + n, p.stopBits);
            set.setValue("UartCfg/TimeoutMs" + n, p.timeoutMs);
            set.setValue("UartCfg/Retries" + n, p.retries);
        }
        set.setValue("DeviceCfg/DeviceHaveNumb", c.sensors.size());
        for (int i = 0; i < c.sensors.size(); ++i) {
            auto s = c.sensors[i];
            auto n = QString::number(i + 1), g = "Sensor" + n + "/";
            set.setValue("DeviceCfg/DevicePort" + n, s.port);
            set.setValue("DeviceCfg/DeviceAddr" + n, s.slave);
            set.setValue("DeviceCfg/DeviceRegStart" + n, s.reg);
            set.setValue("DeviceCfg/DeviceRegEnd" + n, s.count);
            QVariantMap v{{"Id", s.id},
                          {"Name", s.name},
                          {"Category", s.category},
                          {"Unit", s.unit},
                          {"Format", s.format},
                          {"WordOrder", s.order},
                          {"Scale", s.scale},
                          {"Offset", s.offset},
                          {"Low", s.low},
                          {"High", s.high},
                          {"Hysteresis", s.hysteresis},
                          {"IntervalMs", s.intervalMs},
                          {"Camera", s.camera},
                          {"Preset", s.preset},
                          {"LocalId", s.localId},
                          {"DeviceId", s.deviceId},
                          {"NodeId", s.nodeId},
                          {"ValueKey", s.valueKey},
                          {"AlarmKey", s.alarmKey},
                          {"ServiceId", s.serviceId},
                          {"Calibrated", s.calibrated}};
            for (auto it = v.begin(); it != v.end(); ++it)
                set.setValue(g + it.key(), it.value());
        }
        set.setValue("CameraCfg/Count", c.cameras.size());
        for (int i = 0; i < c.cameras.size(); ++i) {
            auto x = c.cameras[i];
            auto g = "Camera" + QString::number(i + 1) + "/";
            QVariantMap v{{"Enabled", x.enabled},     {"Name", x.name},
                          {"Rtsp", x.rtsp},           {"Isapi", x.isapi},
                          {"User", x.user},           {"PasswordEnv", x.passwordEnv},
                          {"ChannelId", x.channelId}, {"PtzChannel", x.channel}};
            for (auto it = v.begin(); it != v.end(); ++it)
                set.setValue(g + it.key(), it.value());
        }
        writeObject(set, "Mqtt", c.mqtt);
        writeObject(set, "Recording", c.recording);
        writeObject(set, "GB28181", c.gb);
        set.sync();
        if (set.status() != QSettings::NoError) {
            *error = "写入临时 INI 失败";
            return false;
        }
    }
    QFile input(name);
    if (!input.open(QIODevice::ReadOnly)) {
        *error = input.errorString();
        return false;
    }
    QSaveFile output(path);
    auto bytes = input.readAll();
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
        *error = output.errorString();
        return false;
    }
    return true;
}
