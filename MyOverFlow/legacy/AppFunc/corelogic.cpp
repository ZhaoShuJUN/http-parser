#include "corelogic.h"
#include "CommObj/commobj.h"
#include "object1commintf.h"

#include <chrono>
using namespace std::chrono;

CCoreLogic::CCoreLogic()
{
    m_bIsRunning = false;
}

CCoreLogic::~CCoreLogic()
{
    /*结束逻辑控制线程*/
    m_bIsRunning = false;
    wait();/*延时结束*/
}

void CCoreLogic::runCore()
{
    m_bIsRunning = true;
    this->start();
}

void CCoreLogic::initCore()
{
    commObjInstance->initSerialCommObj();
}

qint64 CCoreLogic::getCurTimeCounter()
{
    qint64 ll_CurTime = 0;
    ll_CurTime = steady_clock::now().time_since_epoch().count();
    ll_CurTime = ll_CurTime / 1000000ll;
    return ll_CurTime;
}

bool CCoreLogic::isTimeOut(qint64 ll_TimeLength, qint64 ll_RefTime)
{
    int64_t llCurTime = 0;
    bool bIsTimeOut = false;
    llCurTime = getCurTimeCounter();
    if ((llCurTime - ll_RefTime) >=  ll_TimeLength)
    {
        bIsTimeOut = true;
    }
    return bIsTimeOut;
}


void CCoreLogic::run()
{
    //主控逻辑计数器
    int numb = 0;
    int i_CtrlLogicCycle = 50;//核心逻辑运行周期为50ms
    qint64 ll_CtrlLogicTimer = getCurTimeCounter();
    while (m_bIsRunning)
    {
        if (isTimeOut(i_CtrlLogicCycle, ll_CtrlLogicTimer))
        {
            /*接收数据*/
            /*逻辑处理*/
            /*发送数据*/
            Object1CommInstance->timerRef = numb++;
            Object1CommInstance->vidaoDataBuf[0] = numb;
            Object1CommInstance->setCommData(QString::number(ll_CtrlLogicTimer));
            Object1CommInstance->proSendObject1Data();
            /*更新逻辑运算时间*/
            ll_CtrlLogicTimer = getCurTimeCounter();
        }
        msleep(5);
    }
    deleteLater();
}



