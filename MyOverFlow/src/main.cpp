#include "mainwindow.h"
#include <csignal>
namespace {
volatile std::sig_atomic_t stopped = 0;
void signalHandler(int) {
    stopped = 1;
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("MyOverFlow");
    QCoreApplication::setApplicationVersion("1.0.0");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({{"c", "config"}, "JSON configuration path", "file", "config/monitor.json"});
    parser.addOption({"demo", "Simulated sensors; no serial, MQTT, cameras, or GB network traffic"});
    parser.addOption({"check-config", "Validate configuration and exit"});
    parser.addOption({"quit-after", "Exit after milliseconds (smoke test)", "ms"});
    parser.process(app);
    Config cfg;
    QString error;
    if (!cfg.load(parser.value("config"), &error)) {
        qCritical().noquote() << error;
        return 2;
    }
    if (parser.isSet("check-config")) {
        qInfo() << "Configuration valid";
        return 0;
    }
    if (!QDir().mkpath(cfg.dataDir)) {
        qCritical() << "Cannot create data directory";
        return 3;
    }
    QLockFile instanceLock(cfg.dataDir + (parser.isSet("demo") ? "/demo.lock" : "/monitor.lock"));
    instanceLock.setStaleLockTime(0);
    if (!instanceLock.tryLock(0)) {
        qCritical() << "Monitor already running for this data directory, or lock cannot be acquired";
        return 4;
    }
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    QTimer signalTimer;
    QObject::connect(&signalTimer, &QTimer::timeout, &app, [&app] {
        if (stopped)
            app.quit();
    });
    signalTimer.start(200);
    try {
        MainWindow w(cfg, parser.isSet("demo"));
        w.show();
        if (parser.isSet("quit-after"))
            QTimer::singleShot(qMax(1, parser.value("quit-after").toInt()), &app, &QApplication::quit);
        return app.exec();
    } catch (const std::exception &e) {
        qCritical() << e.what();
        return 3;
    }
}
