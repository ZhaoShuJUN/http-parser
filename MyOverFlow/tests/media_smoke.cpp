#include <QDebug>
#include <QGuiApplication>
#include <QMediaPlayer>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoProbe>
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    if (argc != 2) {
        qCritical() << "Pass a local video fixture";
        return 2;
    }
    QMediaPlayer player;
    QVideoProbe probe;
    if (!probe.setSource(&player)) {
        qCritical() << "No video probe backend";
        return 3;
    }
    int frames = 0;
    QObject::connect(&probe, &QVideoProbe::videoFrameProbed, &app, [&](const QVideoFrame &frame) {
        if (frame.isValid() && frame.width() == 160 && frame.height() == 120 && ++frames >= 5) {
            qInfo() << "PASS: Qt Multimedia decoded" << frames << "H.264 frames (160x120)";
            app.exit(0);
        }
    });
    QTimer::singleShot(10000, &app, [&] {
        qCritical() << player.errorString();
        app.exit(1);
    });
    player.setMedia(QString::fromLocal8Bit(argv[1]).startsWith("rtsp://")
                        ? QUrl(QString::fromLocal8Bit(argv[1]))
                        : QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1])));
    player.play();
    return app.exec();
}
