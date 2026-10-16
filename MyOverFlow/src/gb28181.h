#pragma once
#include "camera.h"
struct SipMessage {
    QString first;
    QMap<QString, QString> headers;
    QByteArray body;
    static SipMessage parse(const QByteArray &wire);
};
QByteArray makePs(const QByteArray &annexB, quint64 pts, bool keyframe);
QByteArray makeRtp(const QByteArray &payload, quint16 sequence, quint32 timestamp, quint32 ssrc, bool marker,
                   int pt = 96);
class Gb28181 : public QObject {
    Q_OBJECT
  public:
    Gb28181(const Config &cfg, Cameras *cameras, QObject *parent = nullptr);
    ~Gb28181();
    void start();
    void stop();
    QString status() const {
        return status_;
    }
  signals:
    void statusChanged(QString text);

  private:
    struct Transaction {
        QByteArray wire;
        int count = 0;
        qint64 next = 0;
        QString method;
    };
    struct Session {
        QString call, from, to, via, cseq;
        int camera = 0, pt = 96;
        quint32 ssrc = 0, timestamp = 0;
        quint16 sequence = 0, remotePort = 0;
        QHostAddress remote;
        QUdpSocket *rtp = nullptr;
        QProcess *process = nullptr;
        QByteArray buffer, response;
        bool ack = false;
        int fps = 25;
        qint64 created = 0, nextResponse = 0;
    };
    Config cfg_;
    Cameras *cameras_;
    QUdpSocket sip_;
    QTimer tick_;
    QElapsedTimer clock_;
    QHostAddress server_;
    QString tag_, call_, nonce_, realm_, qop_, opaque_, status_ = "disabled";
    int nc_ = 0, cseq_ = 0, sn_ = 0;
    bool registered_ = false, running_ = false;
    qint64 nextRegister_ = 0, nextHeartbeat_ = 0;
    QMap<QString, Transaction> transactions_;
    QMap<QString, Session *> sessions_;
    QMap<QString, QByteArray> responses_;
    QString device() const;
    QString domain() const;
    void state(QString text);
    void send(QByteArray b);
    void registerDevice();
    void request(QString method, QString target, QByteArray body = {}, QString auth = {});
    void response(const SipMessage &m, int code, QString reason, QByteArray body = {},
                  QString contentType = {});
    void receive();
    void handle(const SipMessage &m);
    void timer();
    void xml(const QByteArray &body);
    void invite(const SipMessage &m);
    void launch(Session *s);
    void media(Session *s);
    void closeSession(QString id);
    void message(QByteArray body);
    QString digest(QString method, QString uri);
    QByteArray buildResponse(const SipMessage &m, int code, QString reason, QByteArray body,
                             QString contentType);
};
