#include "corelogic.h"
#include "ConfigInfo/appcfginfo.h"
#include "alarmrecord.h"
#include "commobj.h"
#include "object1commintf.h"
#include <chrono>
CCoreLogic::CCoreLogic() {}
CCoreLogic::~CCoreLogic() {
    stopCore();
    delete m_alarm;
}
void CCoreLogic::initCore() {
    const auto cfg = appCfgInfoInstance->settings();
    Object1CommInstance->configure(cfg);
    qRegisterMetaType<AlarmEvent>("AlarmEvent");
    m_alarm = new QAlarmRecord(this);
    if (!m_alarm->open(cfg.dataDir + (cfg.demo ? "/demo-alarms.sqlite" : "/alarms.sqlite"), &m_error))
        return;
    connect(Object1CommInstance, &QObject1CommIntf::sglAlarmEvent, m_alarm, &QAlarmRecord::append,
            Qt::QueuedConnection);
    connect(m_alarm, &QAlarmRecord::statusChanged, this,
            [](QString s) { Object1CommInstance->setServiceStatus("database", s); });
    Object1CommInstance->setServiceStatus("database", "正常");
    commObjInstance->initNetCommObj();
    m_ready = true;
}
void CCoreLogic::runCore() {
    if (!m_ready || isRunning())
        return;
    m_bIsRunning = true;
    commObjInstance->initSerialCommObj();
    start();
}
void CCoreLogic::stopCore() {
    m_bIsRunning = false;
    requestInterruption();
    commObjInstance->closeSerialCommObj();
    wait();
    Object1CommInstance->processPending();
    if (m_alarm) {
        QCoreApplication::sendPostedEvents(m_alarm, QEvent::MetaCall);
        m_alarm->flush();
    }
    commObjInstance->closeNetCommObj();
}
qint64 CCoreLogic::getCurTimeCounter() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
bool CCoreLogic::isTimeOut(qint64 n, qint64 ref) {
    return getCurTimeCounter() - ref >= n;
}
void CCoreLogic::run() {
    auto cfg = appCfgInfoInstance->settings();
    qint64 start = getCurTimeCounter(), lastDemo = start - 1000;
    while (m_bIsRunning && !isInterruptionRequested()) {
        Object1CommInstance->processPending();
        if (cfg.demo && isTimeOut(1000, lastDemo)) {
            Object1CommInstance->simulateTick((getCurTimeCounter() - start) / 1000.0);
            lastDemo = getCurTimeCounter();
        }
        appCfgInfoInstance->LetterMeasValReal();
        ++Object1CommInstance->timerRef;
        msleep(50);
    }
}
