
#ifndef QCOMMOBJ_H
#define QCOMMOBJ_H

#include <QObject>
#include "Object1/object1.h"
#include "qmainmodbusrtu.h"

#define commObjInstance QCommObj::instance()

class QCommObj : public QObject
{
    Q_OBJECT
public:
    ~QCommObj();
    explicit QCommObj(QObject *parent = nullptr);

    static QCommObj *instance();

    /*初始化通信对象*/
    void initNetCommObj();
    void initSerialCommObj();

    /*关闭通信对象*/
    void closeNetCommObj();
    void closeSerialCommObj();

    /*object1通信对象数据处理接口函数*/
    uint8_t getObject1CommStatus();
    void sendObject1Data(QByteArray baCommData);

public:
    QMainModbusRtu *m_QMainModbusRtu[255];


signals:


private slots:
    void onObject1Info(QString strInfo);

private:
    QObject1 *m_pObject1;
};

#endif // QCOMMOBJ_H
