#ifndef CCORELOGIC_H
#define CCORELOGIC_H
#include <QThread>
#include <atomic>
class QAlarmRecord;
class CCoreLogic : public QThread {
    Q_OBJECT
  public:
    CCoreLogic();
    ~CCoreLogic();
    void runCore();
    void initCore();
    void stopCore();
    bool ready() const {
        return m_ready;
    }
    QString error() const {
        return m_error;
    }
    QAlarmRecord *alarmRecord() const {
        return m_alarm;
    }

  protected:
    void run() override;

  private:
    qint64 getCurTimeCounter();
    bool isTimeOut(qint64 length, qint64 reference);
    std::atomic<bool> m_bIsRunning{false};
    bool m_ready = false;
    QString m_error;
    QAlarmRecord *m_alarm = nullptr;
};
#endif
