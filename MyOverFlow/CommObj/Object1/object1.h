#ifndef QOBJECT1_H
#define QOBJECT1_H
#include "CommObj/qmainmodbusrtu.h"
#include "ComnUtils/SerialComm/extserialport.h"
#include <QObject>
// Original per-port communication object. Construct and use inside its port worker thread.
class QObject1 : public QObject {
    Q_OBJECT
  public:
    explicit QObject1(QObject *parent = nullptr);
    ~QObject1();
    uint8_t getCommStatus();
    void sendCommData(QByteArray data);
    void OpenCommPort(QString port, int baud, int parity, int stop);
    void CloseCommPort();
    AcquisitionResult pollSensor(const SensorSetting &sensor, const PortSetting &port);
    QMainModbusRtu *master() const {
        return m_master;
    }
  signals:
    void sglObject1Info(QString text);
  private slots:
    void onRecvCommData(uint16_t length, uint8_t *data);

  private:
    uint8_t m_ucCommStatus = 0x55;
    QextSerialComm *m_pExtSerialComm = nullptr;
    QMainModbusRtu *m_master = nullptr;
};
#endif
