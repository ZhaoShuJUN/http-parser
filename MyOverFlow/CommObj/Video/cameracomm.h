#pragma once
#include "ConfigInfo/monitorconfig.h"
#include <QtNetwork>
QString cameraRtspUrl(const CameraSetting &c);
class QCameraComm : public QObject {
    Q_OBJECT
  public:
    QCameraComm(const MonitorSettings &cfg, QObject *parent = nullptr);
    ~QCameraComm();
    void start();
    void stop();
    void ptz(int index, int pan, int tilt, int zoom);
    void preset(int index, int number, bool save = false);
    QString status(int index) const {
        return states_.value(index, "停止");
    }
    static QStringList pruneCandidates(const QString &directory);
  signals:
    void statusChanged();
    void error(QString text);

  private:
    MonitorSettings cfg_;
    QNetworkAccessManager net_;
    QVector<QProcess *> recorders_;
    QMap<int, QString> states_;
    QTimer monitor_;
    bool running_ = false, stopping_ = false;
    void request(int index, QString endpoint, QByteArray body);
    void record(int index);
    void supervise();
    bool diskReady(QString *error) const;
};
