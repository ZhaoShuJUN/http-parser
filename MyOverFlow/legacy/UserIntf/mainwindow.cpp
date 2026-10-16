
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "CommObj/commobj.h"
#include "AppFunc/object1commintf.h"
#include "dialogconfigdev.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    /*系统定时器*/
    m_siTimerID = startTimer(500);
}

MainWindow::~MainWindow()
{
    delete ui;
    if(m_siTimerID > 0)
    {
        killTimer(m_siTimerID);
    }
}

void MainWindow::timerEvent(QTimerEvent *event)
{
    QString strCommData;
    uint8_t ucCommStatus;
    if(m_siTimerID == event->timerId())
    {
        /*获取并更新显示数据-周期更新方式获取*/
        ucCommStatus = commObjInstance->getObject1CommStatus();
        if(0xAA == ucCommStatus)
        {
            ui->label_CommStatus->setText("通信正常");
        }
        else
        {
            ui->label_CommStatus->setText("通信异常");
        }
        strCommData = Object1CommInstance->getCommData();
        if(!strCommData.isEmpty())
        {
            ui->label_CommData->setText(strCommData);
        }
        ui->label_TimerRun->setText(QString::number(Object1CommInstance->timerRef));
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    Q_UNUSED(event);
}

//菜单项文件打开
void MainWindow::on_actionOpenFile_triggered()
{

}

//菜单项文件保存
void MainWindow::on_actionSaveFile_triggered()
{

}
//菜单项文件另存为
void MainWindow::on_actionSaveAsFile_triggered()
{

}
//菜单项设备配置
void MainWindow::on_actionConfigFile_triggered()
{
    FromConfigDev = new  DialogConfigDev();
    FromConfigDev->show();
}
