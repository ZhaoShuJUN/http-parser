#ifndef QCOMMOBJ_H
#define QCOMMOBJ_H
#include "Object1/object1.h"
#include "qmainmodbusrtu.h"
#include <QObject>
class QPortThread;
class QGatewayMqtt;
class QCameraComm;
class QGb28181;
#define commObjInstance QCommObj::instance()
class QCommObj : public QObject {
    Q_OBJECT
  public:
    explicit QCommObj(QObject *parent = nullptr);
    ~QCommObj();
    static QCommObj *instance();
    void initNetCommObj();
    void initSerialCommObj();
    void closeNetCommObj();
    void closeSerialCommObj();
    uint8_t getObject1CommStatus();
    void sendObject1Data(QByteArray data);
    void requestPoll();
    QCameraComm *cameraComm() const {
        return m_camera;
    }
    QGb28181 *gbComm() const {
        return m_gb;
    }
    QGatewayMqtt *mqttComm() const {
        return m_mqtt;
    }
    QMainModbusRtu *m_QMainModbusRtu[255]{}; // Original compatibility field; protocol instances now owned by
                                             // port objects.
  private slots:
    void onObject1Info(QString info);

  private:
    QObject1 *m_pObject1 = nullptr;
    QVector<QPortThread *> m_workers;
    QGatewayMqtt *m_mqtt = nullptr;
    QCameraComm *m_camera = nullptr;
    QGb28181 *m_gb = nullptr;
};
#endif
