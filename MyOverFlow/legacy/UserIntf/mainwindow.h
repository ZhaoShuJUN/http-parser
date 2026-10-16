void on_actionOpenFile_triggered();

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "dialogconfigdev.h"



QT_BEGIN_NAMESPACE
namespace Ui
{
    class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow

{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    Ui::MainWindow *ui;

    // QObject interface
protected:
    void timerEvent(QTimerEvent *event);
    void closeEvent(QCloseEvent *event);
private slots:
    void on_actionOpenFile_triggered();

    void on_actionSaveFile_triggered();

    void on_actionSaveAsFile_triggered();

    void on_actionConfigFile_triggered();

private:
    qint32 m_siTimerID;/*定时器编号*/
    DialogConfigDev *FromConfigDev;
};

#endif // MAINWINDOW_H
