/****************************************************************************
 * qtsingleapplication.cpp
 * --------------------------------------------------------------------------
 * 基于 QLocalServer / QLocalSocket 实现单实例应用，
 * 替代原来依赖的外部预编译库 libQtSingleApplication.a。
 * 跨平台：Windows 用命名管道，Linux 用本地 socket（含 RK3399 ARM）。
 ***************************************************************************/

#include "qtsingleapplication.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QFile>
#include <QTimer>
#include <QWidget>
#include <QDebug>

class QtLocalPeer : public QObject
{
    Q_OBJECT
public:
    explicit QtLocalPeer(QObject *parent = nullptr)
        : QObject(parent), m_server(nullptr)
    {
    }

    bool isClient(const QString &id)
    {
        m_id = id;
        QLocalSocket sock;
        sock.connectToServer(m_id, QIODevice::ReadWrite);
        bool ok = sock.waitForConnected(200);
        if (ok) sock.disconnectFromServer();
        return ok;
    }

    bool initializeServer()
    {
        if (m_server) return true;
        m_server = new QLocalServer(this);
        // 若上次异常退出导致残留 server 文件，先移除
        m_server->removeServer(m_id);
        if (!m_server->listen(m_id)) {
            qWarning() << "QtSingleApplication: listen failed"
                       << m_server->errorString();
            return false;
        }
        connect(m_server, &QLocalServer::newConnection,
                this, &QtLocalPeer::onNewConnection);
        return true;
    }

    bool sendMessage(const QString &message, int timeout)
    {
        QLocalSocket sock;
        sock.connectToServer(m_id, QIODevice::ReadWrite);
        if (!sock.waitForConnected(timeout)) return false;
        sock.write(message.toUtf8());
        sock.flush();
        return sock.waitForBytesWritten(timeout);
    }

signals:
    void messageReceived(const QString &message);

private slots:
    void onNewConnection()
    {
        QLocalSocket *sock = m_server->nextPendingConnection();
        if (!sock) return;
        connect(sock, &QLocalSocket::disconnected, sock, &QObject::deleteLater);
        connect(sock, &QLocalSocket::readyRead, this, [this, sock]() {
            QByteArray data = sock->readAll();
            emit messageReceived(QString::fromUtf8(data));
            sock->disconnectFromServer();
            sock->deleteLater();
        });
        // 兜底：超时未数据也关闭
        QTimer::singleShot(2000, sock, &QLocalSocket::disconnectFromServer);
    }

private:
    QString m_id;
    QLocalServer *m_server;
};

/* ========================================================================
 *  QtSingleApplication
 * ======================================================================== */

QtSingleApplication::QtSingleApplication(int &argc, char **argv, bool GUIenabled)
    : QApplication(argc, argv, GUIenabled),
      peer(new QtLocalPeer(this)),
      actWin(nullptr)
{
    sysInit();
}

QtSingleApplication::QtSingleApplication(const QString &id, int &argc, char **argv)
    : QApplication(argc, argv),
      peer(new QtLocalPeer(this)),
      actWin(nullptr)
{
    sysInit(id);
}

void QtSingleApplication::sysInit(const QString &appId)
{
    QString id = appId;
    if (id.isEmpty()) {
        id = QCoreApplication::applicationName();
        if (id.isEmpty()) id = "MyOverFlow";
    }
    // 先判断是否已有实例在跑
    m_isRunning = peer->isClient(id);
    if (!m_isRunning) {
        peer->initializeServer();
    }
    connect(peer, &QtLocalPeer::messageReceived,
            this, &QtSingleApplication::messageReceived);
}

bool QtSingleApplication::isRunning()
{
    return m_isRunning;
}

QString QtSingleApplication::id() const
{
    return QString();
}

void QtSingleApplication::setActivationWindow(QWidget *aw, bool activateOnMessage)
{
    actWin = aw;
    Q_UNUSED(activateOnMessage);
}

QWidget *QtSingleApplication::activationWindow() const
{
    return actWin;
}

bool QtSingleApplication::sendMessage(const QString &message, int timeout)
{
    if (!m_isRunning) return false;
    return peer->sendMessage(message, timeout);
}

void QtSingleApplication::activateWindow()
{
    if (actWin) {
        actWin->showNormal();
        actWin->raise();
        actWin->activateWindow();
    }
}

#include "qtsingleapplication.moc"
