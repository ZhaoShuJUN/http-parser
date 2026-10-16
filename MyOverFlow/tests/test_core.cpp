#include "gb28181.h"
#include "modbus.h"
#include "mqtt.h"
#include "store.h"
#include <QtTest>
class CoreTests : public QObject {
    Q_OBJECT
  private slots:
    void crcAndRequest() {
        Sensor s;
        s.slave = 1;
        s.reg = 0;
        s.count = 10;
        QCOMPARE(readRequest(s).toHex(), QByteArray("01030000000ac5cd"));
        QCOMPARE(modbusCrc(QByteArray("123456789")), quint16(0x4b37));
    }
    void decode() {
        Sensor s;
        double v;
        s.format = "s16";
        s.scale = .1;
        QVERIFY(decodeValue(QByteArray::fromHex("ff9c"), s, &v));
        QCOMPARE(v, -10.0);
        s.format = "f32";
        s.scale = 1;
        s.count = 2;
        QVERIFY(decodeValue(QByteArray::fromHex("41480000"), s, &v));
        QCOMPARE(v, 12.5);
        s.order = "CDAB";
        QVERIFY(decodeValue(QByteArray::fromHex("00004148"), s, &v));
        QCOMPARE(v, 12.5);
        s.order = "BADC";
        QVERIFY(decodeValue(QByteArray::fromHex("48410000"), s, &v));
        QCOMPARE(v, 12.5);
        s.order = "DCBA";
        QVERIFY(decodeValue(QByteArray::fromHex("00004841"), s, &v));
        QCOMPARE(v, 12.5);
        s.order = "ABCD";
        QVERIFY(!decodeValue(QByteArray::fromHex("7fc00000"), s, &v));
        QVERIFY(!decodeValue(QByteArray(2, 0), s, &v));
    }
    void alarmLifecycle() {
        QTemporaryDir dir;
        Store store;
        QString e;
        QVERIFY2(store.open(dir.filePath("alarms.db"), &e), qPrintable(e));
        Sensor s;
        s.id = "t1";
        s.low = 0;
        s.high = 40;
        s.hysteresis = 2;
        s.camera = 0;
        store.configure({s});
        QSignalSpy alarm(&store, &Store::alarmRaised);
        store.sample("t1", 41);
        QVERIFY(store.snapshot()["t1"].alarm);
        store.sample("t1", 39);
        QVERIFY(store.snapshot()["t1"].alarm);
        QCOMPARE(alarm.count(), 1);
        store.sample("t1", 38);
        QVERIFY(!store.snapshot()["t1"].alarm);
        store.fault("t1", "timeout");
        store.fault("t1", "timeout");
        QCOMPARE(alarm.count(), 2);
        QVERIFY(!store.snapshot()["t1"].valid);
        store.sample("t1", 20);
        QVERIFY(!store.snapshot()["t1"].alarm);
        auto rows = store.history("t1", "2000", "2100");
        QCOMPARE(rows.size(), 4);
        QVERIFY(store.healthy());
        auto report = store.report({s})[0].toObject();
        QCOMPARE(report["serviceId"].toString(), QString("analog"));
        QCOMPARE(report["data"].toArray()[0].toObject()["value"].toDouble(), 20.0);
    }
    void parser() {
        auto m = SipMessage::parse("MESSAGE sip:a SIP/2.0\r\nCall-ID: abc\r\nContent-Length: 3\r\n\r\nabc");
        QCOMPARE(m.body, QByteArray("abc"));
        QCOMPARE(m.headers["call-id"], QString("abc"));
        QVERIFY(SipMessage::parse("MESSAGE x SIP/2.0\r\nContent-Length: 99\r\n\r\na").first.isEmpty());
    }
    void databaseBusyRecovery() {
        QTemporaryDir dir;
        Store store;
        QString error;
        const auto path = dir.filePath("locked.db");
        QVERIFY(store.open(path, &error));
        Sensor s;
        s.id = "locked-sensor";
        store.configure({s});
        {
            auto lock = QSqlDatabase::addDatabase("QSQLITE", "test-lock");
            lock.setDatabaseName(path);
            QVERIFY(lock.open());
            QSqlQuery q(lock);
            QVERIFY(q.exec("BEGIN IMMEDIATE"));
            store.sample(s.id, 101);
            QVERIFY(!store.healthy());
            QVERIFY(store.snapshot()[s.id].alarm);
            QVERIFY(q.exec("COMMIT"));
            lock.close();
        }
        QSqlDatabase::removeDatabase("test-lock");
        QTRY_VERIFY_WITH_TIMEOUT(store.healthy(), 4000);
        QCOMPARE(store.history(s.id, "2000", "2100").size(), 1);
    }
    void packets() {
        auto r = makeRtp("payload", 0x1234, 0x11223344, 0x55667788, true);
        QCOMPARE(r.toHex(), QByteArray("80e0123411223344556677887061796c6f6164"));
        auto ps = makePs(QByteArray::fromHex("000000016588"), 90000, true);
        QVERIFY(ps.startsWith(QByteArray::fromHex("000001ba")));
        QVERIFY(ps.contains(QByteArray::fromHex("000001bc000ee0ff000000041be00000")));
        QVERIFY(ps.endsWith(QByteArray::fromHex("000000016588")));
        QCOMPARE(Mqtt::packet(0xc0, {}).toHex(), QByteArray("c000"));
        QCOMPARE(Mqtt::packet(0x30, QByteArray(128, 'a')).left(3).toHex(), QByteArray("308001"));
    }
    void retention() {
        QTemporaryDir t;
        auto dir = t.filePath("MyOverFlow/camera1");
        QDir().mkpath(dir);
        for (int i = 0; i < 5; ++i) {
            QFile f(dir + QString("/mo_000%1.mkv").arg(i));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("record");
            f.flush();
            f.setFileTime(QDateTime::currentDateTime().addSecs(-60 + i), QFileDevice::FileModificationTime);
        }
        QFile other(dir + "/unrelated.mkv");
        other.open(QIODevice::WriteOnly);
        other.write("keep");
        other.close();
        auto list = Cameras::pruneCandidates(t.path());
        QCOMPARE(list.size(), 3);
        QVERIFY(!list.join('\n').contains("unrelated"));
        QVERIFY(!list.join('\n').contains("0004"));
    }
};
QTEST_GUILESS_MAIN(CoreTests)
#include "test_core.moc"
