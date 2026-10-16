#include "cameracomm.h"
QString cameraRtspUrl(const CameraSetting &c) {
    QUrl u(c.rtsp);
    u.setUserName(c.user);
    u.setPassword(monitorSecret(c.passwordEnv));
    return u.toString(QUrl::FullyEncoded);
}
QCameraComm::QCameraComm(const MonitorSettings &c, QObject *p) : QObject(p), cfg_(c) {
    recorders_.fill(nullptr, c.cameras.size());
    monitor_.setInterval(3000);
    connect(&monitor_, &QTimer::timeout, this, &QCameraComm::supervise);
    connect(&net_, &QNetworkAccessManager::authenticationRequired, this,
            [this](QNetworkReply *r, QAuthenticator *a) {
                int i = r->property("camera").toInt();
                if (i >= 0 && i < cfg_.cameras.size()) {
                    a->setUser(cfg_.cameras[i].user);
                    a->setPassword(monitorSecret(cfg_.cameras[i].passwordEnv));
                }
            });
}
QCameraComm::~QCameraComm() {
    stop();
}
void QCameraComm::start() {
    if (!cfg_.recording["enabled"].toBool())
        return;
    running_ = true;
    monitor_.start();
    supervise();
}
void QCameraComm::stop() {
    stopping_ = true;
    running_ = false;
    monitor_.stop();
    for (auto p : recorders_)
        if (p) {
            p->disconnect(this);
            p->write("q\n");
            if (!p->waitForFinished(3000)) {
                p->terminate();
                if (!p->waitForFinished(1000)) {
                    p->kill();
                    p->waitForFinished(1000);
                }
            }
            delete p;
        }
    recorders_.fill(nullptr);
    stopping_ = false;
}
bool QCameraComm::diskReady(QString *err) const {
    auto path = cfg_.recording["directory"].toString();
    if (!QFileInfo(path).isDir()) {
        *err = "录像根目录不存在，请先挂载硬盘并创建目录";
        return false;
    }
    QStorageInfo disk(path);
    disk.refresh();
    if (!disk.isValid() || !disk.isReady() || disk.isReadOnly()) {
        *err = "录像磁盘未就绪或只读";
        return false;
    }
    if (cfg_.recording["requireMount"].toBool(true) &&
        disk.rootPath() == QStorageInfo(QDir::rootPath()).rootPath()) {
        *err = "录像目录没有独立挂载硬盘，已停止录像以保护系统盘";
        return false;
    }
    return true;
}
void QCameraComm::request(int i, QString ep, QByteArray body) {
    if (i < 0 || i >= cfg_.cameras.size() || !cfg_.cameras[i].enabled)
        return;
    const auto c = cfg_.cameras[i];
    if (c.user.isEmpty() || monitorSecret(c.passwordEnv).isEmpty()) {
        emit error("摄像头认证信息缺失");
        return;
    }
    QUrl u(c.isapi);
    u.setPath("/ISAPI/PTZCtrl/channels/" + QString::number(c.channel) + ep);
    QNetworkRequest req(u);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/xml");
    auto r = net_.put(req, body);
    r->setProperty("camera", i);
    QTimer::singleShot(5000, r, [r] {
        if (r->isRunning())
            r->abort();
    });
    connect(r, &QNetworkReply::finished, this, [this, r, i] {
        auto b = r->readAll();
        if (r->error() != QNetworkReply::NoError ||
            (b.contains("<statusCode>") && !b.contains("<statusCode>1</statusCode>")))
            emit error(QString("摄像头 %1 云台控制失败").arg(i + 1));
        r->deleteLater();
    });
}
void QCameraComm::ptz(int i, int pan, int tilt, int zoom) {
    QByteArray b = QString("<PTZData><pan>%1</pan><tilt>%2</tilt><zoom>%3</zoom></PTZData>")
                       .arg(qBound(-100, pan, 100))
                       .arg(qBound(-100, tilt, 100))
                       .arg(qBound(-100, zoom, 100))
                       .toUtf8();
    request(i, "/continuous", b);
    // Bound manual and remote movement even if a button-release/stop packet is lost.
    if (pan || tilt || zoom)
        QTimer::singleShot(700, this, [this, i] { ptz(i, 0, 0, 0); });
}
void QCameraComm::preset(int i, int n, bool save) {
    if (n < 1 || n > 300)
        return;
    request(i, "/presets/" + QString::number(n) + (save ? "" : "/goto"),
            save ? QString("<PTZPreset><id>%1</id><presetName>环境监测预置位%1</presetName></PTZPreset>")
                       .arg(n)
                       .toUtf8()
                 : QByteArray());
}
void QCameraComm::record(int i) {
    if (recorders_[i] || !cfg_.cameras[i].enabled)
        return;
    auto root = cfg_.recording["directory"].toString();
    auto dir = root + "/MyOverFlow/camera" + QString::number(i + 1);
    if (!QDir().mkpath(dir)) {
        emit error("无法创建录像目录");
        return;
    }
    if (!QFileInfo(dir).canonicalFilePath().startsWith(QFileInfo(root).canonicalFilePath() + "/")) {
        emit error("录像子目录不能通过符号链接指向录像根目录之外");
        return;
    }
    auto p = new QProcess(this);
    recorders_[i] = p;
    p->setProcessChannelMode(QProcess::SeparateChannels);
    QString pattern =
        dir + "/mo_" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz") + "_%06d.mkv";
    p->setProgram(cfg_.recording["ffmpeg"].toString("ffmpeg"));
    p->setArguments({"-hide_banner",
                     "-loglevel",
                     "error",
                     "-rtsp_transport",
                     "tcp",
                     "-timeout",
                     "10000000",
                     "-i",
                     cameraRtspUrl(cfg_.cameras[i]),
                     "-map",
                     "0:v:0",
                     "-map",
                     "0:a?",
                     "-c",
                     "copy",
                     "-f",
                     "segment",
                     "-segment_time",
                     QString::number(cfg_.recording["segmentSeconds"].toInt(300)),
                     "-reset_timestamps",
                     "1",
                     pattern});
    connect(p, &QProcess::started, this, [this, i] {
        states_[i] = "录像进程运行，等待分段写入";
        emit statusChanged();
    });
    connect(p, &QProcess::readyReadStandardError, this, [this, p, i] {
        p->readAllStandardError();
        states_[i] = "录像输入错误，请检查网络/RTSP/编码";
        emit statusChanged();
    });
    auto cleanup = [this, p, i] {
        if (recorders_.value(i) != p)
            return;
        recorders_[i] = nullptr;
        p->deleteLater();
        states_[i] = "录像已退出，将重连";
        emit statusChanged();
    };
    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [cleanup](int, QProcess::ExitStatus) { cleanup(); });
    connect(p, &QProcess::errorOccurred, this, [cleanup](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            cleanup();
    });
    p->start();
}
QStringList QCameraComm::pruneCandidates(const QString &root) {
    QFileInfoList all;
    for (int i = 1; i <= 8; ++i) {
        QDir d(root + "/MyOverFlow/camera" + QString::number(i));
        if (!d.exists() || !d.canonicalPath().startsWith(QFileInfo(root).canonicalFilePath() + "/"))
            continue;
        auto files =
            d.entryInfoList({"mo_*.mkv"}, QDir::Files | QDir::NoSymLinks,
                            QDir::Name); // Keep two newest segments per camera, including active writer.
        for (int j = 0; j < files.size() - 2; ++j)
            if (files[j].lastModified().secsTo(QDateTime::currentDateTime()) > 5)
                all << files[j];
    }
    std::sort(all.begin(), all.end(),
              [](const QFileInfo &a, const QFileInfo &b) { return a.lastModified() < b.lastModified(); });
    QStringList result;
    for (auto f : all)
        result << f.absoluteFilePath();
    return result;
}
void QCameraComm::supervise() {
    if (!running_)
        return;
    QString err;
    if (!diskReady(&err)) {
        for (auto p : recorders_)
            if (p)
                p->terminate();
        emit error(err);
        return;
    }
    auto root = cfg_.recording["directory"].toString();
    QStorageInfo disk(root);
    qint64 used = 0;
    for (int i = 1; i <= 8; ++i) {
        QDir d(root + "/MyOverFlow/camera" + QString::number(i));
        for (auto f : d.entryInfoList({"mo_*.mkv"}, QDir::Files | QDir::NoSymLinks))
            used += f.size();
    }
    qint64 quota = qint64(cfg_.recording["maxGiB"].toDouble(100) * 1024 * 1024 * 1024),
           reserve = qint64(cfg_.recording["reserveMiB"].toDouble(1024) * 1024 * 1024);
    qint64 available = disk.bytesAvailable();
    for (auto name : pruneCandidates(root)) {
        if (used <= quota && available >= reserve)
            break;
        QFileInfo f(name);
        auto size = f.size();
        if (QFile::remove(name)) {
            used -= size;
            available += size;
        }
    }
    if (used > quota || available < reserve) {
        for (auto p : recorders_)
            if (p)
                p->terminate();
        emit error("录像空间不足，无法安全回收旧分段；暂停录像");
        return;
    }
    for (int i = 0; i < cfg_.cameras.size(); ++i) {
        if (!cfg_.cameras[i].enabled)
            continue;
        record(i);
        QDir d(root + "/MyOverFlow/camera" + QString::number(i + 1));
        auto f = d.entryInfoList({"mo_*.mkv"}, QDir::Files, QDir::Time);
        if (!f.isEmpty() && f.first().size() > 0 &&
            f.first().lastModified().secsTo(QDateTime::currentDateTime()) < 15)
            states_[i] = "录像正在写入";
    }
    emit statusChanged();
}
