#pragma once
#include "ConfigInfo/monitorconfig.h"
#include <QtSql>
class QAlarmRecord : public QObject {
    Q_OBJECT
  public:
    explicit QAlarmRecord(QObject *parent = nullptr);
    ~QAlarmRecord();
    bool open(const QString &file, QString *error);
    void flush();
    QList<QStringList> query(const QString &sensor, const QString &from, const QString &to, int limit = 500);
  public slots:
    void append(AlarmEvent event);
  signals:
    void statusChanged(QString status);

  private:
    QSqlDatabase m_db;
    QString m_connection;
    QQueue<AlarmEvent> m_pending;
    QTimer m_retry;
    bool insert(const AlarmEvent &e);
};
