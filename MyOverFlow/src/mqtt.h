#pragma once
#include "store.h"
#include <QtNetwork>
// MQTT 3.1.1, QoS1, one in-flight message. No third-party binary ABI dependency.
class Mqtt : public QObject {
    Q_OBJECT
  public:
    Mqtt(const Config &cfg, Store *store, QObject *parent = nullptr);
    void start();
    void stop();
    QString status() const {
        return status_;
    }
    static QByteArray packet(quint8 type, const QByteArray &body);
    static QByteArray string(const QString &s);
  signals:
    void statusChanged(QString text);

  private:
    Config cfg_;
    Store *store_;
    QNetworkAccessManager http_;
    QTcpSocket *socket_ = nullptr;
    QTimer timer_;
    QElapsedTimer clock_;
    QByteArray rx_, pendingPacket_;
    QQueue<QPair<QString, QByteArray>> queue_;
    QString gateway_, host_, status_ = "disabled";
    int port_ = 1883;
    quint16 sequence_ = 0, pending_ = 0;
    qint64 lastRx_ = 0, lastTx_ = 0, pendingAt_ = 0, nextReport_ = 0, retryAt_ = 0;
    bool running_ = false, authenticating_ = false, connected_ = false, ping_ = false, topologyAck_ = false;
    void auth();
    void connectBroker();
    void receive();
    void tick();
    void sendNext();
    void reset(const QString &reason);
    void state(QString s);
    void write(quint8 type, const QByteArray &data);
    void enqueueSnapshot();
};
