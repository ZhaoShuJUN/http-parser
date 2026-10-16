
#ifndef QOBJECT1_H
#define QOBJECT1_H


#include <QObject>
#include "ComnUtils/SerialComm/extserialport.h"
#include "../modbusrtu.h"

class QObject1 : public QObject
{
    Q_OBJECT
public:
    ~QObject1();
    explicit QObject1(QObject *parent = nullptr);         //QString strCommPort,

    uint8_t getCommStatus();
    void sendCommData(QByteArray baCommData);
    void OpenCommPort(QString strCommPort,int baud,int effic,int stop);

signals:
    void sglObject1Info(QString strInfo);

private slots:
    void onRecvCommData(uint16_t us_DataLen, uint8_t *p_ucCommData);

private:
    uint8_t m_ucCommStatus;/*通信状态*/
    QextSerialComm *m_pExtSerialComm;
    ModbusRtu *m_uartdtu;
};

#endif // QOBJECT1_H
