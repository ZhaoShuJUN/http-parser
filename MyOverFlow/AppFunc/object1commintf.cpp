#include "object1commintf.h"
#include "CommObj/commobj.h"
#include "CommObj/qmainmodbusrtu.h"
#include "ConfigInfo/appcfginfo.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
Q_GLOBAL_STATIC(QObject1CommIntf, staticObject1CommIntf)
QObject1CommIntf::QObject1CommIntf(QObject *p) : QObject(p) {}
QObject1CommIntf::~QObject1CommIntf() {}
QObject1CommIntf *QObject1CommIntf::instance() {
    return staticObject1CommIntf();
}
void QObject1CommIntf::configure(const MonitorSettings &cfg) {
    QMutexLocker lock(&m_mutex);
    m_settings = cfg;
    m_readings.clear();
    m_specs.clear();
    m_pending.clear();
    for (auto s : cfg.sensors) {
        m_specs[s.id] = s;
        SensorReading r;
        r.id = s.id;
        m_readings[s.id] = r;
    }
}
QString QObject1CommIntf::getCommData() {
    QMutexLocker lock(&m_mutex);
    return m_strCommData;
}
void QObject1CommIntf::setCommData(QString text) {
    QMutexLocker lock(&m_mutex);
    m_strCommData = text;
}
void QObject1CommIntf::proSendObject1Data() {
    commObjInstance->requestPoll();
}
void QObject1CommIntf::pushAcquisition(const AcquisitionResult &r) {
    QMutexLocker lock(&m_mutex);
    if (m_pending.size() >= 4096) {
        m_pending.dequeue();
        ++m_dropped;
        m_status["acquisition"] = QString("待处理队列超限，丢弃 %1 条旧采样").arg(m_dropped);
    }
    m_pending.enqueue(r);
}
void QObject1CommIntf::processPending() {
    QQueue<AcquisitionResult> pending;
    {
        QMutexLocker lock(&m_mutex);
        pending.swap(m_pending);
    }
    while (!pending.isEmpty())
        applyResult(pending.dequeue());
}
void QObject1CommIntf::applyResult(const AcquisitionResult &a) {
    AlarmEvent event;
    bool changed = false;
    int index = -1;
    SensorReading r;
    SensorSetting s;
    {
        QMutexLocker lock(&m_mutex);
        if (!m_specs.contains(a.id))
            return;
        s = m_specs[a.id];
        auto old = m_readings[a.id];
        r = old;
        r.id = a.id;
        r.time = a.time.isEmpty() ? monitorUtcNow() : a.time;
        r.error = a.error;
        double value = 0;
        if (r.error.isEmpty() && !QMainModbusRtu::decode(a.registers, s, &value))
            r.error = "寄存器解码无效";
        if (!r.error.isEmpty()) {
            r.valid = false;
            r.alarm = true;
            r.state = "offline";
        } else {
            r.registers = a.registers;
            r.value = value;
            r.valid = true;
            if (value < s.low)
                r.state = "low";
            else if (value > s.high)
                r.state = "high";
            else if (old.state == "low" && value < s.low + s.hysteresis)
                r.state = "low";
            else if (old.state == "high" && value > s.high - s.hysteresis)
                r.state = "high";
            else
                r.state = "normal";
            r.alarm = r.state != "normal";
        }
        changed = old.state != r.state && !(old.state == "unknown" && r.state == "normal");
        m_readings[a.id] = r;
        for (int i = 0; i < m_settings.sensors.size(); ++i)
            if (m_settings.sensors[i].id == a.id) {
                index = i;
                break;
            }
        if (changed) {
            event.sensor = a.id;
            event.name = s.name;
            event.time = r.time;
            event.kind = r.state == "offline" ? "communication" : r.alarm ? "alarm" : "recovery";
            event.state = r.state;
            event.detail = r.error;
            event.value = r.value;
            event.valid = r.valid;
            event.camera = s.camera;
            event.preset = s.preset;
        }
    }
    // Maintain original telemetry/discrete arrays while UI and MQTT use locked snapshots.
    if (index >= 0 && index < MEASALLMAX) {
        QWriteLocker lock(&appCfgInfoInstance->m_dataLock);
        if (r.valid)
            appCfgInfoInstance->m_AllMeasVal[index] = int(qBound(double(INT_MIN), r.value, double(INT_MAX)));
        appCfgInfoInstance->m_AllLetterVal[index] = r.alarm ? 1 : 0;
    }
    if (changed)
        emit sglAlarmEvent(event);
    emit sglDataChanged();
}
QMap<QString, SensorReading> QObject1CommIntf::snapshot() const {
    QMutexLocker lock(&m_mutex);
    return m_readings;
}
void QObject1CommIntf::setServiceStatus(const QString &key, const QString &value) {
    QMutexLocker lock(&m_mutex);
    m_status[key] = value;
}
QMap<QString, QString> QObject1CommIntf::serviceStatuses() const {
    QMutexLocker lock(&m_mutex);
    return m_status;
}
QPair<QString, QString> cloudIdentity(const SensorSetting &s, const QString &sn) {
    static const QMap<QString, QString> types{
        {"wl_", "0010"},   {"th_", "0001"},   {"cable_", "0018"}, {"probe_", "0012"}, {"leak_", "0005"},
        {"door_", "0003"}, {"gas_", "0002"},  {"smoke_", "0008"}, {"pd_", "0009"},    {"ir_", "0004"},
        {"bat_", "0011"},  {"pump_", "0006"}, {"fan_", "0007"}};
    QString device, node;
    bool known = false;
    for (auto it = types.begin(); it != types.end(); ++it)
        if (s.localId.startsWith(it.key())) {
            bool ok = false;
            int n = s.localId.mid(it.key().size()).toInt(&ok);
            if (ok && n >= 1 && n <= 99999) {
                device = "INH" + it.value() + QString("%1").arg(n, 5, 10, QChar('0'));
                known = true;
            }
            break;
        }
    if (!known) {
        device = "L" + s.localId;
        node = s.localId;
    }
    if (!s.deviceId.isEmpty())
        device = s.deviceId;
    if (known || !s.deviceId.isEmpty())
        node = "1511" + device + sn;
    if (!s.nodeId.isEmpty())
        node = s.nodeId;
    return {device, node};
}
QJsonArray QObject1CommIntf::reportDevices() const {
    QMutexLocker lock(&m_mutex);
    QJsonArray out;
    for (auto s : m_settings.sensors) {
        auto r = m_readings.value(s.id);
        auto ids = cloudIdentity(s, m_settings.mqtt["gatewaySN"].toString());
        QJsonObject data{{s.valueKey, r.valid ? QJsonValue(r.value) : QJsonValue(QJsonValue::Null)},
                         {s.alarmKey, r.alarm},
                         {"valid", r.valid},
                         {"state", r.state},
                         {"sampleTime", r.time}};
        out.append(
            QJsonObject{{"deviceId", ids.first},
                        {"serviceId", s.serviceId},
                        {"data", QJsonArray{data}},
                        {"eventTime", QDateTime::currentDateTimeUtc().toString("yyyyMMdd'T'HHmmss'Z'")}});
    }
    return out;
}
void QObject1CommIntf::simulateTick(double t) {
    MonitorSettings cfg;
    {
        QMutexLocker lock(&m_mutex);
        cfg = m_settings;
    }
    for (int i = 0; i < cfg.sensors.size(); ++i) {
        auto s = cfg.sensors[i];
        double value = (s.low + s.high) / 2 + (s.high - s.low) * .65 * std::sin(t / 3 + i);
        AcquisitionResult a;
        a.id = s.id;
        a.time =
            monitorUtcNow(); // Simulation encodes real register formats and uses the same decode/alarm path.
        double raw = (value - s.offset) / (s.scale == 0 ? 1 : s.scale);
        if (s.format == "u16")
            raw = qBound(0.0, raw, 65535.0);
        else if (s.format == "u32")
            raw = qBound(0.0, raw, 4294967295.0);
        quint32 n = 0;
        if (s.format == "f32") {
            float f = float(raw);
            std::memcpy(&n, &f, 4);
        } else
            n = quint32(qint64(raw));
        QByteArray b;
        if (s.count == 2) {
            b.append(char(n >> 24));
            b.append(char(n >> 16));
        }
        b.append(char(n >> 8));
        b.append(char(n));
        if (s.count == 2) {
            if (s.order == "CDAB")
                b = b.mid(2) + b.left(2);
            else if (s.order == "BADC") {
                std::swap(b.data()[0], b.data()[1]);
                std::swap(b.data()[2], b.data()[3]);
            } else if (s.order == "DCBA")
                std::reverse(b.begin(), b.end());
        }
        for (int j = 0; j < b.size(); j += 2)
            a.registers << (quint16(quint8(b[j])) << 8 | quint8(b[j + 1]));
        applyResult(a);
    }
}
