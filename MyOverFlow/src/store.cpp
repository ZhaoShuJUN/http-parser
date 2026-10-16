#include "store.h"
Store::Store(QObject *p) : QObject(p), connection_(QUuid::createUuid().toString()) {
    retry_.setInterval(2000);
    connect(&retry_, &QTimer::timeout, this, &Store::flushEvents);
}
Store::~Store() {
    retry_.stop();
    while (!pendingEvents_.isEmpty()) {
        const int previous = pendingEvents_.size();
        flushEvents();
        if (pendingEvents_.size() == previous)
            break;
    }
    db_.close();
    db_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connection_);
}
bool Store::open(const QString &path, QString *error) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    db_ = QSqlDatabase::addDatabase("QSQLITE", connection_);
    db_.setDatabaseName(path);
    db_.setConnectOptions("QSQLITE_BUSY_TIMEOUT=100");
    if (!db_.open()) {
        *error = db_.lastError().text();
        return false;
    }
    QSqlQuery q(db_);
    for (auto sql :
         {"PRAGMA journal_mode=WAL", "PRAGMA synchronous=FULL",
          "CREATE TABLE IF NOT EXISTS alarms(id INTEGER PRIMARY KEY AUTOINCREMENT, time TEXT NOT NULL, "
          "sensor TEXT NOT NULL, kind TEXT NOT NULL, value REAL, state TEXT NOT NULL, detail TEXT)",
          "CREATE INDEX IF NOT EXISTS alarms_time_sensor ON alarms(time,sensor)"})
        if (!q.exec(sql)) {
            *error = q.lastError().text();
            return false;
        }
    dbError_.clear();
    retry_.start();
    return true;
}
void Store::configure(const QVector<Sensor> &s) {
    for (auto x : s) {
        specs_[x.id] = x;
        data_[x.id] = Reading();
    }
}
bool Store::recordEvent(const QString &id, const QString &kind, const Reading &r) {
    if (pendingEvents_.size() >= 4096) {
        dbError_ = "报警数据库持续不可写，4096 条内存待写队列已满；新事件未保存";
        emit databaseError(dbError_);
        return false;
    }
    pendingEvents_.enqueue({id, kind, r});
    flushEvents();
    return pendingEvents_.isEmpty();
}
void Store::flushEvents() {
    for (int count = 0; !pendingEvents_.isEmpty() && count < 16; ++count) {
        if (!insertEvent(pendingEvents_.head()))
            return;
        pendingEvents_.dequeue();
    }
    if (pendingEvents_.isEmpty())
        dbError_.clear();
}
bool Store::insertEvent(const PendingEvent &e) {
    const auto &r = e.reading;
    QSqlQuery q(db_);
    q.prepare("INSERT INTO alarms(time,sensor,kind,value,state,detail) VALUES(?,?,?,?,?,?)");
    q.addBindValue(r.time);
    q.addBindValue(e.id);
    q.addBindValue(e.kind);
    q.addBindValue(r.valid ? QVariant(r.value) : QVariant(QVariant::Double));
    q.addBindValue(r.state);
    q.addBindValue(r.error);
    if (!q.exec()) {
        dbError_ = q.lastError().text();
        emit databaseError(dbError_);
        return false;
    }
    return true;
}
void Store::sample(QString id, double value) {
    if (!specs_.contains(id) || !qIsFinite(value))
        return;
    const auto s = specs_[id];
    const auto old = data_[id];
    Reading r = old;
    r.value = value;
    r.valid = true;
    r.time = utcNow();
    r.error.clear();
    if (value < s.low)
        r.state = "low";
    else if (value > s.high)
        r.state = "high";
    else if (old.state == "low" && value < s.low + s.hysteresis)
        r.state = "low";
    else if (old.state == "high" && value > s.high - s.hysteresis)
        r.state = "high";
    else
        r.state = "normal";
    r.alarm = r.state != "normal";
    data_[id] = r;
    if (old.state != r.state) {
        recordEvent(id, r.alarm ? "alarm" : "recovery", r);
        if (r.alarm)
            emit alarmRaised(id, s.camera, s.preset);
    }
    emit changed();
}
void Store::fault(QString id, QString error) {
    if (!specs_.contains(id))
        return;
    auto old = data_[id], r = old;
    r.valid = false;
    r.alarm = true;
    r.state = "offline";
    r.time = utcNow();
    r.error = error;
    data_[id] = r;
    if (old.state != "offline") {
        recordEvent(id, "communication", r);
        const auto s = specs_[id];
        emit alarmRaised(id, s.camera, s.preset);
    }
    emit changed();
}
QJsonArray Store::report(const QVector<Sensor> &ss) const {
    QJsonArray a;
    for (auto s : ss) {
        auto r = data_.value(s.id);
        QJsonObject d{
            {"sensorId", s.id},    {s.valueKey, r.valid ? QJsonValue(r.value) : QJsonValue(QJsonValue::Null)},
            {"valid", r.valid},    {s.alarmKey, r.alarm},
            {"state", r.state},    {"unit", s.unit},
            {"sampleTime", r.time}};
        a.append(
            QJsonObject{{"deviceId", s.deviceId},
                        {"serviceId", s.serviceId},
                        {"data", QJsonArray{d}},
                        {"eventTime", QDateTime::currentDateTimeUtc().toString("yyyyMMdd'T'HHmmss'Z'")}});
    }
    return a;
}
QList<QStringList> Store::history(const QString &sensor, const QString &from, const QString &to, int limit) {
    QSqlQuery q(db_);
    q.prepare("SELECT id,time,sensor,kind,value,state,detail FROM alarms WHERE (?='' OR sensor=?) AND "
              "time>=? AND time<=? ORDER BY id DESC LIMIT ?");
    q.addBindValue(sensor);
    q.addBindValue(sensor);
    q.addBindValue(from);
    q.addBindValue(to);
    q.addBindValue(qBound(1, limit, 5000));
    QList<QStringList> rows;
    if (!q.exec()) {
        dbError_ = q.lastError().text();
        emit databaseError(dbError_);
        return rows;
    }
    while (q.next()) {
        QStringList row;
        for (int i = 0; i < 7; ++i)
            row << q.value(i).toString();
        rows << row;
    }
    return rows;
}
