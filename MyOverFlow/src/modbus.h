#pragma once
#include "config.h"
#include <QSerialPort>
quint16 modbusCrc(const QByteArray &data);
QByteArray readRequest(const Sensor &s);
bool decodeValue(QByteArray bytes, const Sensor &s, double *value);
// One outstanding transaction per physical bus; independent event-driven buses.
class ModbusBus : public QObject {
    Q_OBJECT
  public:
    ModbusBus(int index, const Port &port, const QVector<Sensor> &sensors, QObject *parent = nullptr);
    void start();
    void stop();
  signals:
    void sample(QString id, double value);
    void fault(QString id, QString reason);

  private:
    int index_, cursor_ = 0, attempt_ = 0;
    Port cfg_;
    QVector<Sensor> sensors_;
    QVector<qint64> due_;
    QSerialPort port_;
    QTimer tick_, timeout_;
    QElapsedTimer clock_;
    QByteArray rx_;
    int active_ = -1;
    qint64 quietUntil_ = 0;
    void poll();
    void send();
    void receive();
    void finish(const QString &error = QString());
};
