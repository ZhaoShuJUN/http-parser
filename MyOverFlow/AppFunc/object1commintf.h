#ifndef QOBJECT1COMMINTF_H
#define QOBJECT1COMMINTF_H
#include "ConfigInfo/monitorconfig.h"
#include <QMutex>
#include <QObject>
#include <atomic>
#define VIDAODATABUFMAX 512
#define Object1CommInstance QObject1CommIntf::instance()
class QObject1CommIntf : public QObject {
    Q_OBJECT
  public:
    explicit QObject1CommIntf(QObject *parent = nullptr);
    ~QObject1CommIntf();
    static QObject1CommIntf *instance();
    QString getCommData();
    void setCommData(QString text);
    void proSendObject1Data();
    void configure(const MonitorSettings &settings);
    void pushAcquisition(const AcquisitionResult &result);
    void processPending();
    void simulateTick(double seconds);
    QMap<QString, SensorReading> snapshot() const;
    QJsonArray reportDevices() const;
    void setServiceStatus(const QString &service, const QString &status);
    QMap<QString, QString> serviceStatuses() const;
    unsigned short vidaoDataBuf[VIDAODATABUFMAX]{};
    std::atomic<int> timerRef{0};
  signals:
    void sglDataChanged();
    void sglAlarmEvent(AlarmEvent event);

  private:
    void applyResult(const AcquisitionResult &result);
    mutable QMutex m_mutex;
    QString m_strCommData;
    MonitorSettings m_settings;
    QMap<QString, SensorReading> m_readings;
    QMap<QString, SensorSetting> m_specs;
    QQueue<AcquisitionResult> m_pending;
    QMap<QString, QString> m_status;
    quint64 m_dropped = 0;
};
QPair<QString, QString> cloudIdentity(const SensorSetting &sensor, const QString &gatewaySN);
#endif
