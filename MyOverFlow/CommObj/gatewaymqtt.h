#pragma once
#include "ConfigInfo/monitorconfig.h"
#include <QtNetwork>
#include <mosquitto.h>
class QGatewayMqtt : public QObject {
    Q_OBJECT
  public:
    QGatewayMqtt(const MonitorSettings &settings, QObject *parent = nullptr);
    ~QGatewayMqtt();
    void start();
    void stop();
    QString status() const {
        return m_status;
    }
  signals:
    void statusChanged(QString text);

  private:
    struct CallbackContext {
        QGatewayMqtt *owner;
        int generation;
    };
    static void onConnect(mosquitto *, void *, int);
    static void onDisconnect(mosquitto *, void *, int);
    static void onPublish(mosquitto *, void *, int);
    void connected(int code);
    void published(int mid);
    void auth();
    void openBroker(QString host, int port, bool tls);
    void reset(QString reason);
    void tick();
    void sendNext();
    void setStatus(QString text);
    void dispose();
    void topology();
    MonitorSettings m_cfg;
    QNetworkAccessManager m_http;
    QTimer m_timer;
    QElapsedTimer m_clock;
    mosquitto *m_mosq = nullptr;
    CallbackContext *m_context = nullptr;
    QQueue<QByteArray> m_topology, m_data;
    QByteArray m_pendingPayload;
    int m_generation = 0, m_pendingMid = 0;
    bool m_pendingTopology = false, m_running = false, m_auth = false, m_connected = false, m_loop = false;
    QString m_gateway, m_status = "禁用";
    qint64 m_retryAt = 0, m_connectAt = 0, m_pendingAt = 0, m_nextReport = 0;
    quint64 m_dropped = 0;
};
