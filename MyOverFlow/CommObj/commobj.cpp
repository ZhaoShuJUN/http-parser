#include "commobj.h"
#include "AppFunc/object1commintf.h"
#include "ConfigInfo/appcfginfo.h"
#include "Video/cameracomm.h"
#include "Video/gb28181.h"
#include "gatewaymqtt.h"
#include <QMutex>
#include <QThread>
#include <atomic>
class QPortThread : public QThread {
  public:
    QPortThread(PortSetting port, QVector<SensorSetting> sensors, QObject *parent)
        : QThread(parent), m_port(port), m_sensors(sensors) {}
    void force() {
        m_force = true;
    }
    void write(QByteArray b) {
        QMutexLocker lock(&m_mutex);
        if (m_writes.size() < 32)
            m_writes.enqueue(b);
    }

  protected:
    void run() override {
        if (m_sensors.isEmpty())
            return;
        QObject1 device;
        QVector<qint64> due(m_sensors.size(), 0);
        QElapsedTimer clock;
        clock.start();
        int cursor = 0;
        while (!isInterruptionRequested()) {
            if (m_force.exchange(false))
                due.fill(0);
            QQueue<QByteArray> writes;
            {
                QMutexLocker lock(&m_mutex);
                writes.swap(m_writes);
            }
            while (!writes.isEmpty())
                device.sendCommData(writes.dequeue());
            bool worked = false;
            for (int n = 0; n < m_sensors.size() && !isInterruptionRequested(); ++n) {
                int i = (cursor + n) % m_sensors.size();
                if (due[i] > clock.elapsed())
                    continue;
                auto result = device.pollSensor(m_sensors[i], m_port);
                if (isInterruptionRequested())
                    break;
                Object1CommInstance->pushAcquisition(result);
                due[i] = clock.elapsed() + m_sensors[i].intervalMs;
                cursor = (i + 1) % m_sensors.size();
                worked = true;
                break;
            }
            if (!worked)
                msleep(10);
        }
        device.CloseCommPort();
    }

  private:
    PortSetting m_port;
    QVector<SensorSetting> m_sensors;
    std::atomic<bool> m_force{false};
    QMutex m_mutex;
    QQueue<QByteArray> m_writes;
};
Q_GLOBAL_STATIC(QCommObj, staticCommObj)
QCommObj::QCommObj(QObject *p) : QObject(p) {}
QCommObj::~QCommObj() {
    closeSerialCommObj();
    closeNetCommObj();
}
QCommObj *QCommObj::instance() {
    return staticCommObj();
}
void QCommObj::initSerialCommObj() {
    if (!m_workers.isEmpty())
        return;
    const auto cfg = appCfgInfoInstance->settings();
    if (cfg.demo)
        return;
    for (int i = 0; i < cfg.ports.size(); ++i) {
        QVector<SensorSetting> sensors;
        for (auto s : cfg.sensors)
            if (s.port == i)
                sensors << s;
        auto worker = new QPortThread(cfg.ports[i], sensors, this);
        worker->setObjectName("ModbusPort" + QString::number(i));
        m_workers << worker;
        worker->start();
    }
}
void QCommObj::closeSerialCommObj() {
    for (auto w : m_workers)
        w->requestInterruption();
    for (auto w : m_workers) {
        w->wait();
        delete w;
    }
    m_workers.clear();
}
void QCommObj::initNetCommObj() {
    if (m_camera)
        return;
    auto cfg = appCfgInfoInstance->settings();
    m_camera = new QCameraComm(cfg, this);
    m_gb = new QGb28181(cfg, m_camera, this);
    m_mqtt = new QGatewayMqtt(cfg, this);
    connect(m_mqtt, &QGatewayMqtt::statusChanged, this,
            [](QString s) { Object1CommInstance->setServiceStatus("mqtt", s); });
    connect(m_gb, &QGb28181::statusChanged, this,
            [](QString s) { Object1CommInstance->setServiceStatus("gb28181", s); });
    connect(m_camera, &QCameraComm::error, this, [](QString s) {
        Object1CommInstance->setServiceStatus("recording", s);
        qWarning().noquote() << s;
    });
    connect(
        Object1CommInstance, &QObject1CommIntf::sglAlarmEvent, m_camera,
        [this, cfg](AlarmEvent e) {
            if (!cfg.demo && e.kind != "recovery" && e.camera >= 0)
                m_camera->preset(e.camera, e.preset);
        },
        Qt::QueuedConnection);
    if (!cfg.demo) {
        m_camera->start();
        m_gb->start();
        m_mqtt->start();
    } else {
        Object1CommInstance->setServiceStatus("mqtt", "模拟模式未连接");
        Object1CommInstance->setServiceStatus("gb28181", "模拟模式未连接");
    }
}
void QCommObj::closeNetCommObj() {
    delete m_gb;
    m_gb = nullptr;
    delete m_camera;
    m_camera = nullptr;
    delete m_mqtt;
    m_mqtt = nullptr;
}
uint8_t QCommObj::getObject1CommStatus() {
    for (auto r : Object1CommInstance->snapshot())
        if (r.valid)
            return 0xaa;
    return 0x55;
}
void QCommObj::sendObject1Data(QByteArray b) {
    if (!b.isEmpty() && !m_workers.isEmpty())
        m_workers[0]->write(b);
}
void QCommObj::requestPoll() {
    for (auto w : m_workers)
        w->force();
}
void QCommObj::onObject1Info(QString text) {
    Object1CommInstance->setCommData(text);
}
