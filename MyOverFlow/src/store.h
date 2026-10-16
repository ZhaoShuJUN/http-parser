#pragma once
#include "config.h"
#include <QtSql>
struct Reading {
    double value = 0;
    bool valid = false, alarm = false;
    QString state = "unknown", time, error;
};
class Store : public QObject {
    Q_OBJECT
  public:
    explicit Store(QObject *parent = nullptr);
    ~Store();
    bool open(const QString &path, QString *error);
    void configure(const QVector<Sensor> &sensors);
    QMap<QString, Reading> snapshot() const {
        return data_;
    }
    QJsonArray report(const QVector<Sensor> &sensors) const;
    QList<QStringList> history(const QString &sensor, const QString &from, const QString &to,
                               int limit = 500);
    bool healthy() const {
        return dbError_.isEmpty();
    }
    QString error() const {
        return dbError_;
    }
  public slots:
    void sample(QString id, double value);
    void fault(QString id, QString error);
  signals:
    void changed();
    void alarmRaised(QString sensor, int camera, int preset);
    void databaseError(QString error);

  private:
    QSqlDatabase db_;
    QString connection_, dbError_;
    struct PendingEvent {
        QString id, kind;
        Reading reading;
    };
    QQueue<PendingEvent> pendingEvents_;
    QTimer retry_;
    QMap<QString, Sensor> specs_;
    QMap<QString, Reading> data_;
    bool recordEvent(const QString &id, const QString &kind, const Reading &r);
    bool insertEvent(const PendingEvent &event);
    void flushEvents();
};
