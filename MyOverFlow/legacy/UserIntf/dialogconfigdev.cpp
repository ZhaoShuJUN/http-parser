#include "dialogconfigdev.h"
#include "ui_dialogconfigdev.h"
#include <QTableView>
#include <QStandardItemModel>
#include <QMenu>
#include <QWidget>
#include <QMessageBox>
#include <QComboBox>
#include "ConfigInfo/appcfginfo.h"

DialogConfigDev::DialogConfigDev(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogConfigDev)
{
    ui->setupUi(this);

    SetRefreshUartConfig();               //配置文件信息写入到列表框
    SetRefreshDevConfig();                //配置文件数据显示到UI中
    SetReportLetterConfig();              //设置遥信配置
    SetReportMeasConfig();                //设置遥测配置
}

DialogConfigDev::~DialogConfigDev()
{
    delete ui;
}

void DialogConfigDev::SetRefreshUartConfig()          //配置文件信息写入到列表框
{
    QStandardItemModel *model;
    int i;
    model = new QStandardItemModel(this);
    ui->lineEditUart->setText(QString::number(appCfgInfoInstance->m_uartNumb));
    model->setHorizontalHeaderLabels(QStringList()<<"端口号"<<"波特率"<<"效验位"<<"停止位");
    for(i=0;i<appCfgInfoInstance->m_uartNumb;i++){
        QStandardItem *item00 = new QStandardItem(appCfgInfoInstance->m_uartPort[i]);
        QStandardItem *item01 = new QStandardItem(QString::number(appCfgInfoInstance->m_uartBaud[i]));
        QStandardItem *item02 = new QStandardItem(QString::number(appCfgInfoInstance->m_uartEffe[i]));
        QStandardItem *item03 = new QStandardItem(QString::number(appCfgInfoInstance->m_uartStop[i]));
//        QFont font = item00->font();
//        //设置为粗体
//        font.setBold(true);
//        //字体大小
//        font.setPointSize(12);
//        //设置字体
//        item01->setFont(font);
//        //设置字体颜色
//        item01->setForeground(QBrush(QColor(255, 0, 0)));
//        //文本对齐格式
//        item01->setTextAlignment(Qt::AlignHCenter | Qt::AlignVCenter);

         model->setItem(i,0,item00);
         model->setItem(i,1,item01);
         model->setItem(i,2,item02);
         model->setItem(i,3,item03);
    }
    ui->tableViewUart->setModel(model);
}

void DialogConfigDev::GetRefreshUartConfig()            //从列表框中获取配置文件信息
{
    int i;
    QVariant viewstr;
    for(i=0;i<appCfgInfoInstance->m_uartNumb;i++){
         viewstr = ui->tableViewUart->model()->index(i,0).data();
         appCfgInfoInstance->m_uartPort[i]= viewstr.toString();              //获取索引对应位置的数据转为字符串
         viewstr = ui->tableViewUart->model()->index(i,1).data();
         appCfgInfoInstance->m_uartBaud[i] = viewstr.toInt();
         viewstr = ui->tableViewUart->model()->index(i,2).data();
         appCfgInfoInstance->m_uartEffe[i] = viewstr.toInt();
         viewstr = ui->tableViewUart->model()->index(i,3).data();
         appCfgInfoInstance->m_uartStop[i] = viewstr.toInt();
    }
    appCfgInfoInstance->writeAppCfgInfo();              //写入配置文件
}
void DialogConfigDev::SetRefreshDevConfig()             //配置文件数据显示到UI中
{
    QStandardItemModel *model;
    int i;
    model = new QStandardItemModel(this);
    ui->lineEditDev->setText(QString::number(appCfgInfoInstance->m_deviceModbusRtuNumb));
    model->setHorizontalHeaderLabels(QStringList()<<"端口号"<<"设备地址"<<"起始寄存器"<<"寄存器数量");
    for(i=0;i<appCfgInfoInstance->m_deviceModbusRtuNumb;i++){
        QStandardItem *item00 = new QStandardItem(QString::number(appCfgInfoInstance->m_deviceConfig[i][0]));
        QStandardItem *item01 = new QStandardItem(QString::number(appCfgInfoInstance->m_deviceConfig[i][1]));
        QStandardItem *item02 = new QStandardItem(QString::number(appCfgInfoInstance->m_deviceConfig[i][2]));
        QStandardItem *item03 = new QStandardItem(QString::number(appCfgInfoInstance->m_deviceConfig[i][3]));
        model->setItem(i,0,item00);
        model->setItem(i,1,item01);
        model->setItem(i,2,item02);
        model->setItem(i,3,item03);
    }
    ui->tableViewDev->setModel(model);
}

void DialogConfigDev::GetRefreshDevConfig()               //读取出UI中的配置数据并写入配置文件
{
    int i;
    QVariant viewstr;
    for(i=0;i<appCfgInfoInstance->m_deviceModbusRtuNumb;i++){
         viewstr = ui->tableViewDev->model()->index(i,0).data();
         appCfgInfoInstance->m_deviceConfig[i][0]= viewstr.toInt();              //获取索引对应位置的数据转为字符串
         viewstr = ui->tableViewDev->model()->index(i,1).data();
         appCfgInfoInstance->m_deviceConfig[i][1] = viewstr.toInt();
         viewstr = ui->tableViewDev->model()->index(i,2).data();
         appCfgInfoInstance->m_deviceConfig[i][2] = viewstr.toInt();
         viewstr = ui->tableViewDev->model()->index(i,3).data();
         appCfgInfoInstance->m_deviceConfig[i][3] = viewstr.toInt();
    }
    appCfgInfoInstance->writeAppCfgInfo();              //写入配置文件
}

void DialogConfigDev::SetReportLetterConfig()           //设置遥信配置
{
    QStandardItemModel *model;
    int i;
    model = new QStandardItemModel(this);
    ui->lineEditLetter->setText(QString::number(appCfgInfoInstance->m_ReportLetterNumb));
    model->setHorizontalHeaderLabels(QStringList()<<"遥信名称"<<"寄存器地址");
    for(i=0;i<appCfgInfoInstance->m_ReportLetterNumb;i++){
        QStandardItem *item00 = new QStandardItem(appCfgInfoInstance->m_ReportLetterName[i]);
        QStandardItem *item01 = new QStandardItem(QString::number(appCfgInfoInstance->m_ReportLetterReg[i]));
        model->setItem(i,0,item00);
        model->setItem(i,1,item01);
    }
    ui->tableViewLetter->setModel(model);
}

void DialogConfigDev::GetReportLetterConfig()           //获取遥信配置
{
    int i;
    QVariant viewstr;
    for(i=0;i<appCfgInfoInstance->m_ReportLetterNumb;i++){
         viewstr = ui->tableViewLetter->model()->index(i,0).data();
         appCfgInfoInstance->m_ReportLetterName[i]= viewstr.toString();              //获取索引对应位置的数据转为字符串
         viewstr = ui->tableViewLetter->model()->index(i,1).data();
         appCfgInfoInstance->m_ReportLetterReg[i] = viewstr.toInt();
    }
    appCfgInfoInstance->writeAppCfgInfo();              //写入配置文件
}

void DialogConfigDev::SetReportMeasConfig()             //设置遥测配置
{
    QStandardItemModel *model;
    int i;
    model = new QStandardItemModel(this);
    ui->lineEditMeas->setText(QString::number(appCfgInfoInstance->m_ReportMeasNumb));
    model->setHorizontalHeaderLabels(QStringList()<<"遥测名称"<<"寄存器地址");
    for(i=0;i<appCfgInfoInstance->m_ReportMeasNumb;i++){
        QStandardItem *item00 = new QStandardItem(appCfgInfoInstance->m_ReportMeasName[i]);
        QStandardItem *item01 = new QStandardItem(QString::number(appCfgInfoInstance->m_ReportMeasReg[i]));
        model->setItem(i,0,item00);
        model->setItem(i,1,item01);
    }
    ui->tableViewMeas->setModel(model);
}

void DialogConfigDev::GetReportMeasConfig()             //获取遥测配置
{
    int i;
    QVariant viewstr;
    for(i=0;i<appCfgInfoInstance->m_ReportMeasNumb;i++){
         viewstr = ui->tableViewMeas->model()->index(i,0).data();
         appCfgInfoInstance->m_ReportMeasName[i]= viewstr.toString();              //获取索引对应位置的数据转为字符串
         viewstr = ui->tableViewMeas->model()->index(i,1).data();
         appCfgInfoInstance->m_ReportMeasReg[i] = viewstr.toInt();
    }
    appCfgInfoInstance->writeAppCfgInfo();              //写入配置文件
}


//串口数量改变事件
void DialogConfigDev::on_lineEditUart_textChanged(const QString &arg1)
{
    appCfgInfoInstance->m_uartNumb = arg1.toInt();      //ui->lineEditUart->text().toInt();
    if(appCfgInfoInstance->m_uartNumb>UARTPORTMAX){
        appCfgInfoInstance->m_uartNumb = UARTPORTMAX;
        ui->lineEditUart->setText(QString::number(appCfgInfoInstance->m_uartNumb));
    }
    SetRefreshUartConfig();
}

//设备数量改变事件
void DialogConfigDev::on_lineEditDev_textChanged(const QString &arg1)
{
    appCfgInfoInstance->m_deviceModbusRtuNumb = arg1.toInt();      //ui->lineEditUart->text().toInt();
    if(appCfgInfoInstance->m_deviceModbusRtuNumb>MAINDEVICEMAX){
        appCfgInfoInstance->m_deviceModbusRtuNumb = MAINDEVICEMAX;
        ui->lineEditDev->setText(QString::number(appCfgInfoInstance->m_deviceModbusRtuNumb));
    }
    SetRefreshDevConfig();
}

//遥信数量改变事件
void DialogConfigDev::on_lineEditLetter_textChanged(const QString &arg1)
{
    appCfgInfoInstance->m_ReportLetterNumb = arg1.toInt();      //ui->lineEditUart->text().toInt();
    if(appCfgInfoInstance->m_ReportLetterNumb>LETTERMAX){
        appCfgInfoInstance->m_ReportLetterNumb = LETTERMAX;
        ui->lineEditLetter->setText(QString::number(appCfgInfoInstance->m_ReportLetterNumb));
    }
    SetReportLetterConfig();           //设置遥信配置
}

//遥测数量改变事件
void DialogConfigDev::on_lineEditMeas_textChanged(const QString &arg1)
{
    appCfgInfoInstance->m_ReportMeasNumb = arg1.toInt();         //ui->lineEditUart->text().toInt();
    if(appCfgInfoInstance->m_ReportMeasNumb>MEASMAX){
        appCfgInfoInstance->m_ReportMeasNumb = MEASMAX;
        ui->lineEditMeas->setText(QString::number(appCfgInfoInstance->m_ReportMeasNumb));
    }
    SetReportMeasConfig();              //设置要测配置
}

void DialogConfigDev::on_buttonBox_accepted()        //点击OK参数后
{
    GetRefreshUartConfig();                          //从列表框中获取配置文件信息
    GetRefreshDevConfig();                           //读取出UI中的配置数据并写入配置文件
    GetReportLetterConfig();                         //获取遥信配置
    GetReportMeasConfig();                           //获取遥测配置
}


