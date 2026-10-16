#pragma once
#include <QtCore>
struct Sensor {
    QString id, name, unit, deviceId, nodeId;
    QString valueKey = "value", alarmKey = "alarm", serviceId = "analog";
    int port = 0, slave = 1, function = 3, reg = 0, count = 1, interval = 1000, camera = -1, preset = 1;
    QString format = "u16", order = "ABCD";
    double scale = 1, offset = 0, low = 0, high = 100, hysteresis = 1;
};
struct Port {
    QString device;
    int baud = 9600, dataBits = 8, stopBits = 1;
    QString parity = "N";
    int timeout = 500, retries = 2;
};
struct Camera {
    bool enabled = false;
    QString name, rtsp, isapi, user, passwordEnv, channelId;
    int channel = 1;
};
struct Config {
    QJsonObject raw, mqtt, recording, gb;
    QVector<Port> ports;
    QVector<Sensor> sensors;
    QVector<Camera> cameras;
    QString path, dataDir;
    bool load(const QString &file, QString *error);
};
QString secret(const QString &name);
QString utcNow();
