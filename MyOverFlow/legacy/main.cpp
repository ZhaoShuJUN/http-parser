#include <QFile>
#include <QtSql>
#include <QMutex>
#include <QDebug>
#include <QDateTime>
#include <QMessageBox>
#include <QTextStream>

#include <QApplication>
#include "AppFunc/corelogic.h"
#include "UserIntf/mainwindow.h"
#include "ConfigInfo/appcfginfo.h"
#include "ComnUtils/SingleApp/qtsingleapplication.h"

void swRunMessage(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QString str_InfoType;
    switch(type)
    {
        case QtDebugMsg:
            str_InfoType = "Debug";
            break;
        case QtWarningMsg:
            str_InfoType = "Warning";
            break;
        case QtCriticalMsg:
            str_InfoType = "Critical";
            break;
        case QtFatalMsg:
            str_InfoType = "Fatal";
            break;
        default:
            break;
    }
    if(!str_InfoType.isEmpty())
    {
        QString str_PosInfo = QString("File:(%1) Line:(%2)").arg(QString(context.file)).arg(context.line);
        QString str_CurTime = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss:zzz");
        QString str_MsgInfo = QString("%1-%2:%3:%4").arg(str_InfoType).arg(str_CurTime).arg(str_PosInfo).arg(msg);
        static QMutex mutex;
        QFile file("./systemRunLog.txt");
        if(file.open(QIODevice::WriteOnly | QIODevice::Append))
        {
            mutex.lock();
            QTextStream text_stream(&file);
            text_stream << str_MsgInfo << "\r\n";
            file.flush();
            file.close();
            mutex.unlock();
        }
    }
}

int main(int argc, char *argv[])
{
    qputenv("QT_IM_MODULE", QByteArray("qtvirtualkeyboard")); // 虚拟键盘调用
    QtSingleApplication app("MyOverFlow", argc, argv);
    if(app.isRunning())   //判断实例是否已经运行
    {
        QMessageBox::warning(NULL, "提示！", "Demo程序正在运行中！");
        QString str = "MyOverFlow正在运行中！当前时间为：" + QDateTime::currentDateTime().toString("yyyy/MM/dd hh:mm:ss");
        app.sendMessage(str, 4000); //4s后激活前个实例
        return  EXIT_FAILURE;
    }
    //注册MessageHandler
    qInstallMessageHandler(swRunMessage);
//    //写入配置文件
//    appCfgInfoInstance->writeAppCfgInfo();               //写入配置文件
    /*读取配置文件*/
    appCfgInfoInstance->readAppCfgInfo();
    //    SQLLite数据库测试代码
    qDebug() << QSqlDatabase::drivers();
    Q_ASSERT(QSqlDatabase::isDriverAvailable("QSQLITE")); // from Qt
    /*逻辑处理*/
    CCoreLogic coreLogic;
    coreLogic.initCore();
    coreLogic.runCore();
    /*显示界面*/
    MainWindow w;
    w.show();
    return app.exec();
}
