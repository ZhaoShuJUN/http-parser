#include "AppFunc/alarmrecord.h"
#include "AppFunc/corelogic.h"
#include "AppFunc/object1commintf.h"
#include "CommObj/Video/cameracomm.h"
#include "CommObj/Video/gb28181.h"
#include "CommObj/qmainmodbusrtu.h"
#include "ConfigInfo/appcfginfo.h"
#include "UserIntf/dialogconfigdev.h"
#include "UserIntf/mainwindow.h"
#include <QtTest>
#include <thread>
#include <vector>
class MonitorTests : public QObject {
    Q_OBJECT
    MonitorSettings base;
  private slots:
    void initTestCase() {
        QString error;
        QString file = QFINDTESTDATA("../configInfo.ini");
        QVERIFY(readMonitorIni(file, &base, &error));
        qRegisterMetaType<AlarmEvent>("AlarmEvent");
        appCfgInfoInstance->setConfigFile(file);
        appCfgInfoInstance->readAppCfgInfo();
        QVERIFY(appCfgInfoInstance->configError().isEmpty());
    }
    void modbusWireAndDecode() {
        QCOMPARE(QMainModbusRtu::readRequest(1, 0, 10).toHex(), QByteArray("01030000000ac5cd"));
        QCOMPARE(QMainModbusRtu::crc("123456789"), quint16(0x4b37));
        QVERIFY(QMainModbusRtu::readRequest(248, 0, 1).isEmpty());
        SensorSetting s;
        s.format = "s16";
        s.scale = .1;
        double v;
        QVERIFY(QMainModbusRtu::decode({0xff9c}, s, &v));
        QCOMPARE(v, -10.0);
        s.count = 2;
        s.scale = 1;
        s.format = "f32";
        QVERIFY(QMainModbusRtu::decode({0x4148, 0}, s, &v));
        QCOMPARE(v, 12.5);
        s.order = "CDAB";
        QVERIFY(QMainModbusRtu::decode({0, 0x4148}, s, &v));
        QCOMPARE(v, 12.5);
        s.order = "BADC";
        QVERIFY(QMainModbusRtu::decode({0x4841, 0}, s, &v));
        QCOMPARE(v, 12.5);
        s.order = "DCBA";
        QVERIFY(QMainModbusRtu::decode({0, 0x4841}, s, &v));
        QCOMPARE(v, 12.5);
        s.order = "ABCD";
        QVERIFY(!QMainModbusRtu::decode({0x7fc0, 0}, s, &v));
    }
    void originalMasterInterfaces() {
        QMainModbusRtu master;
        master.cacheResponse(247, 511, {123, 456});
        QCOMPARE(master.getUartBufData(247, 511), quint16(123));
        QCOMPARE(master.getUartBufData(247, 512), quint16(0));
        QSignalSpy writes(&master, &QMainModbusRtu::requestReady);
        master.SendReadDataCommand(200, 0, 1);
        QCOMPARE(writes.count(), 1);
        QCOMPARE(writes.takeFirst()[0].toByteArray(), QMainModbusRtu::readRequest(200, 0, 1));
        master.setUartBufData(1, 0, 0x1234);
        master.SendWriteDataCommand(1, 0, 1);
        auto frame = writes.takeFirst()[0].toByteArray();
        QCOMPARE(frame.left(6).toHex(), QByteArray("010600001234"));
        QCOMPARE(QMainModbusRtu::crc(frame.left(6)), quint16(quint8(frame[6]) | (quint8(frame[7]) << 8)));
    }
    void iniRoundTripAndValidation() {
        QTemporaryDir dir;
        QString path = dir.filePath("configInfo.ini");
        QFile seed(path);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("[Custom]\nKeep=original\n[ReportCfg]\nUserMapping=keep\n");
        seed.close();
        auto cfg = base;
        cfg.sensors[0].slave = 247;
        cfg.sensors[0].format = "f32";
        cfg.sensors[0].count = 2;
        cfg.sensors[0].order = "CDAB";
        cfg.sensors[0].scale = .25;
        QString error;
        QVERIFY2(writeMonitorIni(path, cfg, &error), qPrintable(error));
        MonitorSettings restored;
        QVERIFY2(readMonitorIni(path, &restored, &error), qPrintable(error));
        QCOMPARE(restored.sensors[0].slave, 247);
        QCOMPARE(restored.sensors[0].count, 2);
        QCOMPARE(restored.sensors[0].scale, .25);
        QCOMPARE(restored.ports.size(), 8);
        QSettings ini(path, QSettings::IniFormat);
        QCOMPARE(ini.value("Custom/Keep").toString(), QString("original"));
        QCOMPARE(ini.value("ReportCfg/UserMapping").toString(), QString("keep"));
        auto bytes = [&] {
            QFile f(path);
            f.open(QIODevice::ReadOnly);
            return f.readAll();
        };
        auto before = bytes();
        cfg.sensors[0].slave = 248;
        QVERIFY(!writeMonitorIni(path, cfg, &error));
        QCOMPARE(bytes(), before);
    }
    void cacheAlarmAndLegacyArrays() {
        auto cfg = base;
        SensorSetting s = cfg.sensors[0];
        s.id = "alarm-test";
        s.low = 10;
        s.high = 40;
        s.hysteresis = 2;
        s.camera = 0;
        cfg.sensors = {s};
        auto cache = Object1CommInstance;
        cache->configure(cfg);
        QSignalSpy events(cache, &QObject1CommIntf::sglAlarmEvent);
        auto sample = [&](quint16 raw) {
            AcquisitionResult a;
            a.id = s.id;
            a.time = monitorUtcNow();
            a.registers = {raw};
            cache->pushAcquisition(a);
            cache->processPending();
        };
        sample(41);
        sample(39);
        QVERIFY(cache->snapshot()[s.id].alarm);
        QCOMPARE(events.count(), 1);
        sample(38);
        QVERIFY(!cache->snapshot()[s.id].alarm);
        sample(9);
        QCOMPARE(cache->snapshot()[s.id].state, QString("low"));
        sample(11);
        QVERIFY(cache->snapshot()[s.id].alarm);
        sample(12);
        QVERIFY(!cache->snapshot()[s.id].alarm);
        AcquisitionResult a;
        a.id = s.id;
        a.error = "timeout";
        cache->pushAcquisition(a);
        cache->processPending();
        QVERIFY(!cache->snapshot()[s.id].valid);
        QVERIFY(cache->snapshot()[s.id].alarm);
        QCOMPARE(appCfgInfoInstance->m_AllLetterVal[0], quint8(1));
        auto payload = cache->reportDevices()[0].toObject()["data"].toArray()[0].toObject();
        QVERIFY(payload["value"].isNull());
        QVERIFY(payload["alarm"].toBool());
    }
    void cacheThreadSafety() {
        auto cfg = base;
        cfg.sensors.resize(1);
        auto cache = Object1CommInstance;
        cache->configure(cfg);
        auto id = cfg.sensors[0].id;
        std::vector<std::thread> producers;
        for (int i = 0; i < 8; ++i)
            producers.emplace_back([=] {
                for (int n = 0; n < 80; ++n) {
                    AcquisitionResult a;
                    a.id = id;
                    a.time = monitorUtcNow();
                    a.registers = {quint16(n)};
                    cache->pushAcquisition(a);
                }
            });
        for (auto &t : producers)
            t.join();
        cache->processPending();
        QVERIFY(cache->snapshot()[id].valid);
        QCOMPARE(cache->snapshot()[id].value, 79.0);
    }
    void gatewayMappingCompatibility() {
        SensorSetting s;
        s.localId = "th_1";
        auto ids = cloudIdentity(s, "SN");
        QCOMPARE(ids.first, QString("INH000100001"));
        QCOMPARE(ids.second, QString("1511INH000100001SN"));
        s.localId = "smoke_2";
        QCOMPARE(cloudIdentity(s, "SN").first, QString("INH000800002"));
        s.localId = "pm_1";
        QCOMPARE(cloudIdentity(s, "SN"), qMakePair(QString("Lpm_1"), QString("pm_1")));
        s.deviceId = "custom";
        s.nodeId = "node";
        QCOMPARE(cloudIdentity(s, "SN"), qMakePair(QString("custom"), QString("node")));
    }
    void databaseRetryAndQuery() {
        QTemporaryDir dir;
        QString path = dir.filePath("alarm.db"), error;
        QAlarmRecord records;
        QVERIFY(records.open(path, &error));
        QSignalSpy status(&records, &QAlarmRecord::statusChanged);
        {
            auto lock = QSqlDatabase::addDatabase("QSQLITE", "test-lock");
            lock.setDatabaseName(path);
            QVERIFY(lock.open());
            QSqlQuery q(lock);
            QVERIFY(q.exec("BEGIN IMMEDIATE"));
            AlarmEvent e;
            e.sensor = "sensor";
            e.name = "温度";
            e.kind = "alarm";
            e.state = "high";
            e.time = monitorUtcNow();
            e.value = 51;
            e.valid = true;
            records.append(e);
            QVERIFY(status.last()[0].toString().startsWith("待重试"));
            QVERIFY(q.exec("COMMIT"));
            lock.close();
        }
        QSqlDatabase::removeDatabase("test-lock");
        QTRY_COMPARE_WITH_TIMEOUT(records.query("sensor", "2000", "2100").size(), 1, 4500);
        QCOMPARE(records.query("different", "2000", "2100").size(), 0);
    }
    void mediaPacketsAndRetention() {
        auto r = makeRtp("payload", 0x1234, 0x11223344, 0x55667788, true);
        QCOMPARE(r.toHex(), QByteArray("80e0123411223344556677887061796c6f6164"));
        auto ps = makePs(QByteArray::fromHex("000000016588"), 90000, true);
        QVERIFY(ps.startsWith(QByteArray::fromHex("000001ba")));
        QVERIFY(ps.contains(QByteArray::fromHex("000001bc000ee0ff000000041be00000")));
        QTemporaryDir dir;
        QString path = dir.filePath("MyOverFlow/camera1");
        QDir().mkpath(path);
        for (int i = 0; i < 5; ++i) {
            QFile f(path + QString("/mo_000%1.mkv").arg(i));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("record");
            f.flush();
            f.setFileTime(QDateTime::currentDateTime().addSecs(-60 + i), QFileDevice::FileModificationTime);
        }
        QFile unrelated(path + "/unrelated.mkv");
        QVERIFY(unrelated.open(QIODevice::WriteOnly));
        unrelated.close();
        auto files = QCameraComm::pruneCandidates(dir.path());
        QCOMPARE(files.size(), 3);
        QVERIFY(!files.join('\n').contains("unrelated"));
        auto cfg = base;
        cfg.recording["enabled"] = true;
        cfg.recording["directory"] = dir.path();
        cfg.recording["requireMount"] = false;
        cfg.recording["reserveMiB"] = 0;
        cfg.recording["maxGiB"] = 15.0 / (1024 * 1024 * 1024);
        QCameraComm recorder(cfg);
        recorder.start();
        QCOMPARE(QDir(path).entryList({"mo_*.mkv"}, QDir::Files).size(), 2);
        QVERIFY(QFileInfo::exists(path + "/unrelated.mkv"));
        recorder.stop();
    }
    void originalCoreLifecycleAndHmi() {
        QTemporaryDir dir;
        auto cfg = base;
        cfg.dataDir = dir.filePath("runtime");
        cfg.path = dir.filePath("configInfo.ini");
        QString error;
        QVERIFY(writeMonitorIni(cfg.path, cfg, &error));
        appCfgInfoInstance->setConfigFile(cfg.path);
        appCfgInfoInstance->readAppCfgInfo();
        appCfgInfoInstance->setDemo(true);
        {
            CCoreLogic core;
            core.initCore();
            QVERIFY2(core.ready(), qPrintable(core.error()));
            core.runCore();
            MainWindow window;
            window.setCoreLogic(&core);
            window.show();
            QTest::qWait(200);
            QVERIFY(core.isRunning());
            auto list = window.findChild<QListWidget *>("sensorList");
            QVERIFY(list);
            QCOMPARE(list->count(), 5);
            QVERIFY(list->item(0)->text().contains("温度"));
            QPushButton *humidity = nullptr;
            for (auto b : window.findChildren<QPushButton *>())
                if (b->text() == "湿度")
                    humidity = b;
            QVERIFY(humidity);
            QTest::mouseClick(humidity, Qt::LeftButton);
            QCOMPARE(list->count(), 1);
            QVERIFY(list->item(0)->text().contains("湿度"));
            core.stopCore();
            QVERIFY(!core.isRunning());
        }
        appCfgInfoInstance->setConfigFile(base.path);
        appCfgInfoInstance->readAppCfgInfo();
    }
    void legacyDatabaseMigration() {
        QTemporaryDir dir;
        const auto file = dir.filePath("old.db");
        {
            auto old = QSqlDatabase::addDatabase("QSQLITE", "legacy-db");
            old.setDatabaseName(file);
            QVERIFY(old.open());
            QSqlQuery q(old);
            QVERIFY(q.exec("CREATE TABLE alarms(id INTEGER PRIMARY KEY, time TEXT, sensor TEXT, kind TEXT, "
                           "value REAL, state TEXT, detail TEXT)"));
            QVERIFY(q.exec("INSERT INTO alarms VALUES(1,'2020','old-sensor','alarm',51,'high','retained')"));
            old.close();
        }
        QSqlDatabase::removeDatabase("legacy-db");
        QAlarmRecord records;
        QString error;
        QVERIFY2(records.open(file, &error), qPrintable(error));
        auto retained = records.query("old-sensor", "2000", "2100");
        QCOMPARE(retained.size(), 1);
        QCOMPARE(retained[0][5], QString("51"));
        AlarmEvent event;
        event.sensor = "new-sensor";
        event.name = "温度";
        event.time = "2021";
        event.kind = "alarm";
        event.state = "high";
        records.append(event);
        QCOMPARE(records.query("new-sensor", "2000", "2100").size(), 1);
    }
    void originalSettingsDialog() {
        QTemporaryDir dir;
        auto cfg = base;
        cfg.path = dir.filePath("configInfo.ini");
        QString error;
        QVERIFY(writeMonitorIni(cfg.path, cfg, &error));
        appCfgInfoInstance->setConfigFile(cfg.path);
        appCfgInfoInstance->readAppCfgInfo();
        {
            DialogConfigDev dialog;
            auto uart = dialog.findChild<QTableView *>("tableViewUart"),
                 dev = dialog.findChild<QTableView *>("tableViewDev");
            QVERIFY(uart && dev);
            QCOMPARE(uart->model()->rowCount(), 8);
            QCOMPARE(dev->model()->rowCount(), 5);
            QVERIFY(dev->model()->setData(dev->model()->index(0, 5), 247));
            QTimer closeMessages;
            closeMessages.setInterval(30);
            connect(&closeMessages, &QTimer::timeout, [] {
                for (auto w : QApplication::topLevelWidgets())
                    if (auto box = qobject_cast<QMessageBox *>(w))
                        box->accept();
            });
            closeMessages.start();
            QSignalSpy accepted(&dialog, &QDialog::accepted);
            QVERIFY(QMetaObject::invokeMethod(&dialog, "on_buttonBox_accepted", Qt::DirectConnection));
            QCOMPARE(accepted.count(), 1);
            MonitorSettings restored;
            QVERIFY(readMonitorIni(cfg.path, &restored, &error));
            QCOMPARE(restored.sensors[0].slave, 247);
        }
        appCfgInfoInstance->setConfigFile(base.path);
        appCfgInfoInstance->readAppCfgInfo();
    }
};
QTEST_MAIN(MonitorTests)
#include "test_system.moc"
