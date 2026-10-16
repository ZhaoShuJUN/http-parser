#ifndef CCORELOGIC_H
#define CCORELOGIC_H

#include <QThread>


class CCoreLogic : public QThread
{
    Q_OBJECT
public:
    CCoreLogic();
    ~CCoreLogic();

    void runCore(void);
    void initCore(void);

protected:
    void run();

private:
    qint64 getCurTimeCounter();
    bool isTimeOut(qint64 ll_TimeLength, qint64 ll_RefTime);

private:
    bool m_bIsRunning;
};

#endif // CCORELOGIC_H
