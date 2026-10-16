#include "AppFunc/corelogic.h"
#include "ComnUtils/SingleApp/qtsingleapplication.h"
#include "ConfigInfo/appcfginfo.h"
#include "UserIntf/mainwindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QLockFile>
#include <csignal>
namespace {
volatile std::sig_atomic_t stopping = 0;
void stopSignal(int) {
    stopping = 1;
}
} // namespace
int main(int argc, char *argv[]) {
    QString configPath = QDir::current().absoluteFilePath("configInfo.ini");
    for (int i = 1; i + 1 < argc; ++i)
        if (QString::fromLocal8Bit(argv[i]) == "--config" || QString::fromLocal8Bit(argv[i]) == "-c")
            configPath = QFileInfo(QString::fromLocal8Bit(argv[i + 1])).absoluteFilePath();
    QString appId =
        "MyOverFlow-" +
        QString::fromLatin1(
            QCryptographicHash::hash(configPath.toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
    QtSingleApplication app(appId, argc, argv);
    QCoreApplication::setApplicationName("MyOverFlow");
    QCoreApplication::setApplicationVersion("5.0.0");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({{"c", "config"}, "Original INI configuration file", "file", configPath});
    parser.addOption({"demo", "Simulated acquisition; no serial/MQTT/camera/GB networking"});
    parser.addOption({"check-config", "Validate INI and exit"});
    parser.addOption({"quit-after", "Exit after milliseconds for validation", "ms"});
    parser.addOption({"screenshot", "Save a screenshot after 1500ms", "file"});
    parser.process(app);
    appCfgInfoInstance->setConfigFile(parser.value("config"));
    appCfgInfoInstance->readAppCfgInfo();
    if (!appCfgInfoInstance->configError().isEmpty()) {
        qCritical().noquote() << appCfgInfoInstance->configError();
        return 2;
    }
    appCfgInfoInstance->setDemo(parser.isSet("demo"));
    if (parser.isSet("check-config")) {
        qInfo() << "INI configuration valid";
        return 0;
    }
    if (app.isRunning()) {
        qCritical() << "MyOverFlow is already running for this configuration";
        app.sendMessage("activate", 500);
        return 4;
    }
    auto cfg = appCfgInfoInstance->settings();
    if (!QDir().mkpath(cfg.dataDir)) {
        qCritical() << "Cannot create data directory";
        return 3;
    }
    QLockFile lock(cfg.dataDir + (cfg.demo ? "/demo.lock" : "/monitor.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) {
        qCritical() << "Data directory already in use or not writable";
        return 4;
    }
    std::signal(SIGINT, stopSignal);
    std::signal(SIGTERM, stopSignal);
    QTimer signalsTimer;
    QObject::connect(&signalsTimer, &QTimer::timeout, &app, [&app] {
        if (stopping)
            app.quit();
    });
    signalsTimer.start(200);
    CCoreLogic coreLogic;
    coreLogic.initCore();
    if (!coreLogic.ready()) {
        qCritical().noquote() << "Core initialization failed:" << coreLogic.error();
        return 3;
    }
    coreLogic.runCore();
    MainWindow w;
    w.setCoreLogic(&coreLogic);
    app.setActivationWindow(&w);
    QObject::connect(&app, &QtSingleApplication::messageReceived, &app,
                     [&app](QString) { app.activateWindow(); });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &coreLogic, &CCoreLogic::stopCore);
    w.show();
    if (parser.isSet("quit-after"))
        QTimer::singleShot(qMax(1, parser.value("quit-after").toInt()), &app, &QCoreApplication::quit);
    if (parser.isSet("screenshot")) {
        QString path = parser.value("screenshot");
        QTimer::singleShot(1500, &w, [&w, path] {
            if (!w.grab().save(path))
                qWarning() << "Screenshot could not be saved";
        });
    }
    return app.exec();
}
