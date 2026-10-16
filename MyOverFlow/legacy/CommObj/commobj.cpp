
#include "commobj.h"
#include "../ConfigInfo/appcfginfo.h"

Q_GLOBAL_STATIC(QCommObj, staticCommObj)

QCommObj::~QCommObj()
{
    closeNetCommObj();
    closeSerialCommObj();
}

QCommObj::QCommObj(QObject *parent)
    : QObject{parent}
{
    m_pObject1 = nullptr;

    for(int i=0;i<appCfgInfoInstance->m_deviceModbusRtuNumb;i++){          //根据设置的设备数量新建
         m_QMainModbusRtu[i] = new QMainModbusRtu();
    }
}

QCommObj *QCommObj::instance()
{
    return staticCommObj();
}

void QCommObj::initNetCommObj()
{
}

void QCommObj::initSerialCommObj()
{
    /*根据配置文件，初始化串口通信对象*/
    m_pObject1 = new QObject1();
    m_pObject1->OpenCommPort(appCfgInfoInstance->m_uartPort[0],appCfgInfoInstance->m_uartBaud[0],appCfgInfoInstance->m_uartEffe[0],appCfgInfoInstance->m_uartStop[0]);
    connect(m_pObject1, &QObject1::sglObject1Info, this, &QCommObj::onObject1Info);
}

void QCommObj::closeNetCommObj()
{
}

void QCommObj::closeSerialCommObj()
{
}

uint8_t QCommObj::getObject1CommStatus()
{
    uint8_t ucCommStatus = 0x55;
    if(nullptr != m_pObject1)
    {
        ucCommStatus = m_pObject1->getCommStatus();
    }
    return ucCommStatus;
}

void QCommObj::sendObject1Data(QByteArray baCommData)
{
    if(!baCommData.isEmpty() && nullptr != m_pObject1)
    {
        m_pObject1->sendCommData(baCommData);
    }
}

void QCommObj::onObject1Info(QString strInfo)
{
    Q_UNUSED(strInfo);
}

