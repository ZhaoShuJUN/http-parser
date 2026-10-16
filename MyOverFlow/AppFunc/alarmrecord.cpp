#include "alarmrecord.h"
QAlarmRecord::QAlarmRecord(QObject *p) : QObject(p), m_connection(QUuid::createUuid().toString()) {
    m_retry.setInterval(2000);
    connect(&m_retry, &QTimer::timeout, this, &QAlarmRecord::flush);
}
QAlarmRecord::~QAlarmRecord() {
    m_retry.stop();
    while (!m_pending.isEmpty()) {
        int count = m_pending.size();
        flush();
        if (m_pending.size() == count)
            break;
    }
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connection);
}
bool QAlarmRecord::open(const QString &path, QString *e) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connection);
    m_db.setDatabaseName(path);
    m_db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=100");
    if (!m_db.open()) {
        *e = m_db.lastError().text();
        return false;
    }
    QSqlQuery q(m_db);
    for (auto sql :
         {"PRAGMA journal_mode=WAL", "PRAGMA synchronous=FULL",
          "CREATE TABLE IF NOT EXISTS alarms(id INTEGER PRIMARY KEY AUTOINCREMENT,time TEXT NOT NULL,sensor "
          "TEXT NOT NULL,name TEXT NOT NULL,kind TEXT NOT NULL,value REAL,state TEXT NOT NULL,detail TEXT)",
          "CREATE INDEX IF NOT EXISTS alarm_lookup ON alarms(time,sensor)"})
        if (!q.exec(sql)) {
            *e = q.lastError().text();
            return false;
        }
    if (!q.exec("PRAGMA table_info(alarms)")) {
        *e = q.lastError().text();
        return false;
    }
    QSet<QString> columns;
    while (q.next())
        columns.insert(q.value(1).toString());
    for (const auto &required : {"id", "time", "sensor", "kind", "value", "state", "detail"}) {
        if (!columns.contains(required)) {
            *e = "已有报警表结构不兼容，请先备份并迁移数据库";
            return false;
        }
    }
    // Preserve records from the preceding seven-column alarm table.
    if (!columns.contains("name") && !q.exec("ALTER TABLE alarms ADD COLUMN name TEXT NOT NULL DEFAULT ''")) {
        *e = q.lastError().text();
        return false;
    }
    m_retry.start();
    emit statusChanged("正常");
    return true;
}
void QAlarmRecord::append(AlarmEvent e) {
    if (m_pending.size() >= 4096) {
        emit statusChanged("报警待写队列已满，新事件未保存");
        return;
    }
    m_pending.enqueue(e);
    flush();
}
bool QAlarmRecord::insert(const AlarmEvent &e) {
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO alarms(time,sensor,name,kind,value,state,detail)VALUES(?,?,?,?,?,?,?)");
    q.addBindValue(e.time);
    q.addBindValue(e.sensor);
    q.addBindValue(e.name);
    q.addBindValue(e.kind);
    q.addBindValue(e.valid ? QVariant(e.value) : QVariant(QVariant::Double));
    q.addBindValue(e.state);
    q.addBindValue(e.detail);
    if (!q.exec()) {
        emit statusChanged("待重试: " + q.lastError().text());
        return false;
    }
    return true;
}
void QAlarmRecord::flush() {
    for (int i = 0; i < 16 && !m_pending.isEmpty(); ++i) {
        if (!insert(m_pending.head()))
            return;
        m_pending.dequeue();
    }
    emit statusChanged(m_pending.isEmpty() ? "正常" : QString("待写 %1 条").arg(m_pending.size()));
}
QList<QStringList> QAlarmRecord::query(const QString &id, const QString &from, const QString &to, int limit) {
    QSqlQuery q(m_db);
    q.prepare("SELECT id,time,sensor,name,kind,value,state,detail FROM alarms WHERE (?='' OR sensor=?) AND "
              "time>=? AND time<=? ORDER BY id DESC LIMIT ?");
    q.addBindValue(id);
    q.addBindValue(id);
    q.addBindValue(from);
    q.addBindValue(to);
    q.addBindValue(qBound(1, limit, 5000));
    QList<QStringList> rows;
    if (!q.exec()) {
        emit statusChanged(q.lastError().text());
        return rows;
    }
    while (q.next()) {
        QStringList row;
        for (int i = 0; i < 8; ++i)
            row << q.value(i).toString();
        rows << row;
    }
    return rows;
}
