#pragma once
#include <QtCore>
// Configuration DTOs belong to the original ConfigInfo layer. No service owns a second configuration.
struct PortSetting {
    QString device;
    int baud = 9600, parity = 0, stopBits = 0, timeoutMs = 500, retries = 2;
};
struct SensorSetting {
    QString id, name, category, unit, localId, deviceId, nodeId;
    QString format = "u16", order = "ABCD", valueKey = "value", alarmKey = "alarm", serviceId = "analog";
    int port = 0, slave = 1, reg = 0, count = 1, intervalMs = 1000, camera = -1, preset = 1;
    double scale = 1, offset = 0, low = 0, high = 100, hysteresis = 1;
    bool calibrated = false;
};
struct CameraSetting {
    bool enabled = false;
    QString name, rtsp, isapi, user, passwordEnv, channelId;
    int channel = 1;
};
struct MonitorSettings {
    QString path, dataDir;
    QVector<PortSetting> ports;
    QVector<SensorSetting> sensors;
    QVector<CameraSetting> cameras;
    QJsonObject mqtt, recording, gb;
    bool demo = false;
};
struct SensorReading {
    QString id, time, error, state = "unknown";
    double value = 0;
    bool valid = false, alarm = false;
    QVector<quint16> registers;
};
struct AcquisitionResult {
    QString id, error, time;
    QVector<quint16> registers;
};
struct AlarmEvent {
    QString sensor, name, time, kind, state, detail;
    double value = 0;
    bool valid = false;
    int camera = -1, preset = 1;
};
Q_DECLARE_METATYPE(AlarmEvent)
QString monitorUtcNow();
QString monitorSecret(const QString &name);
bool readMonitorIni(const QString &path, MonitorSettings *settings, QString *error);
bool writeMonitorIni(const QString &path, const MonitorSettings &settings, QString *error);
bool validateMonitorSettings(const MonitorSettings &settings, QString *error);
