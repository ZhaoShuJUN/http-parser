
#ifndef QOBJECT1COMMINTF_H
#define QOBJECT1COMMINTF_H

#include <QObject>

#define   VIDAODATABUFMAX           512

#define Object1CommInstance QObject1CommIntf::instance()

class QObject1CommIntf : public QObject
{
    Q_OBJECT
public:
    ~QObject1CommIntf();
    explicit QObject1CommIntf(QObject *parent = nullptr);

    static QObject1CommIntf *instance();

    QString getCommData();
    void  setCommData(QString str);

    void proSendObject1Data();

public:
    unsigned short vidaoDataBuf[VIDAODATABUFMAX];
    int timerRef = 0;

signals:

private:
    QString m_strCommData;
};

#endif // QOBJECT1COMMINTF_H
