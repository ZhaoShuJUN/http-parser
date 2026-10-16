#include "../UserIntf/Video/gstmpprtspplayer.h"
#include <QDebug>
#include <QGuiApplication>
#include <QTimer>
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    if (argc != 2)
        return 2;
    GstMppRtspPlayer player;
    int frames = 0;
    QObject::connect(&player, &GstMppRtspPlayer::frameReady, &app, [&](const QImage &image) {
        if (!image.isNull() && image.width() == 960 && image.height() == 540 && ++frames >= 5) {
            qInfo() << "PASS: HMI GStreamer preview decoded five RGB frames (960x540)";
            app.quit();
        }
    });
    QObject::connect(&player, &GstMppRtspPlayer::errorText, &app, [&](QString) {
        qCritical() << "GStreamer preview failed";
        app.exit(1);
    });
    QTimer::singleShot(10000, &app, [&] {
        qCritical() << "GStreamer frames timeout";
        app.exit(1);
    });
    player.play(QString::fromLocal8Bit(argv[1]));
    return app.exec();
}
