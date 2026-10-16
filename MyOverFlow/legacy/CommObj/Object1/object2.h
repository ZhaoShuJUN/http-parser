#ifndef OBJECT2_H
#define OBJECT2_H

#include <QObject>
#include "ComnUtils/SerialComm/extserialport.h"
#include "../qmainmodbusrtu.h"

#define   RXDATABUFMAX           1024

class Object2 : public QObject
{
    Q_OBJECT
public:
    ~Object2();
    explicit Object2(QObject *parent = nullptr);

    uint8_t getCommStatus();
    void sendCommData(QByteArray baCommData);
    void OpenCommPort(QString strCommPort,int baud,int effic,int stop);

signals:

private slots:
    void onRecvCommData(uint16_t us_DataLen, uint8_t *p_ucCommData);

private:
    uint8_t  m_UartRxBuf[RXDATABUFMAX];
    uint8_t  m_UartRxOverBit = 0;
    uint16_t m_UartRxStartBit = 0;
    uint16_t m_UartRxNumb = 0;
    uint16_t m_UartRxAllNumb = 0;
    uint8_t  m_UartNowAddr = 0;


    uint8_t m_ucCommStatus;/*通信状态*/
    QextSerialComm *m_pExtSerialComm;
    QMainModbusRtu *m_mainuartdtu[MAINDEVICEMAX];

};

#endif // OBJECT2_H
