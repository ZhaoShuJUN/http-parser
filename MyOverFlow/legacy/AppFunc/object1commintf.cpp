
#include "object1commintf.h"
#include "CommObj/commobj.h"

Q_GLOBAL_STATIC(QObject1CommIntf, staticObject1CommIntf)
QObject1CommIntf::~QObject1CommIntf()
{
}

QObject1CommIntf::QObject1CommIntf(QObject *parent)
    : QObject{parent}
{
    m_strCommData = "Object1通信数据";
}

QObject1CommIntf *QObject1CommIntf::instance()
{
    return staticObject1CommIntf();
}

void QObject1CommIntf::setCommData(QString str)     //设置数据
{
    m_strCommData = str;
}

QString QObject1CommIntf::getCommData()
{
    return m_strCommData;
}

void QObject1CommIntf::proSendObject1Data()
{
    QByteArray baCommData;
    /*处理给object1对象 发送数据*/
    commObjInstance->sendObject1Data(baCommData);
}

