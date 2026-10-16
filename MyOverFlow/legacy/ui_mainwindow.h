/********************************************************************************
** Form generated from reading UI file 'mainwindow.ui'
**
** Created by: Qt User Interface Compiler version 5.15.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_MainWindow
{
public:
    QAction *actionOpenFile;
    QAction *actionSaveFile;
    QAction *actionSaveAsFile;
    QAction *actionConfigFile;
    QWidget *centralwidget;
    QLabel *label_CommStatus;
    QLabel *label_CommData;
    QLabel *label_TimerRun;
    QMenuBar *menubar;
    QMenu *menu;
    QStatusBar *statusbar;

    void setupUi(QMainWindow *MainWindow)
    {
        if (MainWindow->objectName().isEmpty())
            MainWindow->setObjectName(QString::fromUtf8("MainWindow"));
        MainWindow->resize(1920, 1080);
        actionOpenFile = new QAction(MainWindow);
        actionOpenFile->setObjectName(QString::fromUtf8("actionOpenFile"));
        actionSaveFile = new QAction(MainWindow);
        actionSaveFile->setObjectName(QString::fromUtf8("actionSaveFile"));
        actionSaveAsFile = new QAction(MainWindow);
        actionSaveAsFile->setObjectName(QString::fromUtf8("actionSaveAsFile"));
        actionConfigFile = new QAction(MainWindow);
        actionConfigFile->setObjectName(QString::fromUtf8("actionConfigFile"));
        centralwidget = new QWidget(MainWindow);
        centralwidget->setObjectName(QString::fromUtf8("centralwidget"));
        label_CommStatus = new QLabel(centralwidget);
        label_CommStatus->setObjectName(QString::fromUtf8("label_CommStatus"));
        label_CommStatus->setGeometry(QRect(90, 100, 69, 20));
        label_CommData = new QLabel(centralwidget);
        label_CommData->setObjectName(QString::fromUtf8("label_CommData"));
        label_CommData->setGeometry(QRect(90, 140, 201, 20));
        label_TimerRun = new QLabel(centralwidget);
        label_TimerRun->setObjectName(QString::fromUtf8("label_TimerRun"));
        label_TimerRun->setGeometry(QRect(90, 180, 201, 20));
        MainWindow->setCentralWidget(centralwidget);
        menubar = new QMenuBar(MainWindow);
        menubar->setObjectName(QString::fromUtf8("menubar"));
        menubar->setGeometry(QRect(0, 0, 1920, 26));
        menu = new QMenu(menubar);
        menu->setObjectName(QString::fromUtf8("menu"));
        MainWindow->setMenuBar(menubar);
        statusbar = new QStatusBar(MainWindow);
        statusbar->setObjectName(QString::fromUtf8("statusbar"));
        MainWindow->setStatusBar(statusbar);

        menubar->addAction(menu->menuAction());
        menu->addAction(actionOpenFile);
        menu->addAction(actionSaveFile);
        menu->addAction(actionSaveAsFile);
        menu->addAction(actionConfigFile);

        retranslateUi(MainWindow);

        QMetaObject::connectSlotsByName(MainWindow);
    } // setupUi

    void retranslateUi(QMainWindow *MainWindow)
    {
        MainWindow->setWindowTitle(QCoreApplication::translate("MainWindow", "MainWindow", nullptr));
        actionOpenFile->setText(QCoreApplication::translate("MainWindow", "\346\211\223\345\274\200\346\226\207\344\273\266", nullptr));
        actionSaveFile->setText(QCoreApplication::translate("MainWindow", "\344\277\235\345\255\230\346\226\207\344\273\266", nullptr));
        actionSaveAsFile->setText(QCoreApplication::translate("MainWindow", "\345\217\246\345\255\230\346\226\207\344\273\266", nullptr));
        actionConfigFile->setText(QCoreApplication::translate("MainWindow", "\351\205\215\347\275\256\346\226\207\344\273\266", nullptr));
        label_CommStatus->setText(QCoreApplication::translate("MainWindow", "\351\200\232\344\277\241\347\212\266\346\200\201", nullptr));
        label_CommData->setText(QCoreApplication::translate("MainWindow", "\351\200\232\344\277\241\346\225\260\346\215\256", nullptr));
        label_TimerRun->setText(QCoreApplication::translate("MainWindow", "\351\200\232\344\277\241\346\225\260\346\215\256", nullptr));
        menu->setTitle(QCoreApplication::translate("MainWindow", "\346\226\207\344\273\266", nullptr));
    } // retranslateUi

};

namespace Ui {
    class MainWindow: public Ui_MainWindow {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_MAINWINDOW_H
