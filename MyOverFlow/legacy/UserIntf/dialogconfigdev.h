#ifndef DIALOGCONFIGDEV_H
#define DIALOGCONFIGDEV_H

#include <QDialog>
#include <QTableView>
#include <QStandardItemModel>
#include <QMenu>
#include <QWidget>
#include <QMessageBox>
#include <QComboBox>

namespace Ui {
class DialogConfigDev;
}

class DialogConfigDev : public QDialog
{
    Q_OBJECT

public:
    explicit DialogConfigDev(QWidget *parent = nullptr);
    ~DialogConfigDev();
    void SetRefreshUartConfig();               //设置串口配置参数到ui
    void GetRefreshUartConfig();               //从UI读取设置参数并写入配置文件
    void SetRefreshDevConfig();                //设置设备参数到UI
    void GetRefreshDevConfig();                //从UI读取设置参数并写入配置文件
    void SetReportLetterConfig();              //设置遥信配置
    void GetReportLetterConfig();              //获取遥信配置
    void SetReportMeasConfig();                //设置要测配置
    void GetReportMeasConfig();                //获取要测配置

private slots:
    void on_lineEditUart_textChanged(const QString &arg1);

    void on_lineEditDev_textChanged(const QString &arg1);

    void on_buttonBox_accepted();

    void on_lineEditLetter_textChanged(const QString &arg1);

    void on_lineEditMeas_textChanged(const QString &arg1);

private:
    Ui::DialogConfigDev *ui;
//    QStandardItemModel *model;
};

#endif // DIALOGCONFIGDEV_H
