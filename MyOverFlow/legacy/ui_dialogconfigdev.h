/********************************************************************************
** Form generated from reading UI file 'dialogconfigdev.ui'
**
** Created by: Qt User Interface Compiler version 5.15.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_DIALOGCONFIGDEV_H
#define UI_DIALOGCONFIGDEV_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QTableView>

QT_BEGIN_NAMESPACE

class Ui_DialogConfigDev
{
public:
    QDialogButtonBox *buttonBox;
    QLabel *label;
    QTableView *tableViewUart;
    QLineEdit *lineEditUart;
    QTableView *tableViewDev;
    QLineEdit *lineEditDev;
    QLabel *label_2;
    QLabel *label_3;
    QLabel *label_4;
    QLabel *label_5;
    QLineEdit *lineEditLetter;
    QLabel *label_6;
    QTableView *tableViewLetter;
    QLabel *label_7;
    QLineEdit *lineEditMeas;
    QTableView *tableViewMeas;

    void setupUi(QDialog *DialogConfigDev)
    {
        if (DialogConfigDev->objectName().isEmpty())
            DialogConfigDev->setObjectName(QString::fromUtf8("DialogConfigDev"));
        DialogConfigDev->resize(1092, 691);
        buttonBox = new QDialogButtonBox(DialogConfigDev);
        buttonBox->setObjectName(QString::fromUtf8("buttonBox"));
        buttonBox->setGeometry(QRect(880, 650, 201, 32));
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);
        label = new QLabel(DialogConfigDev);
        label->setObjectName(QString::fromUtf8("label"));
        label->setGeometry(QRect(10, 10, 101, 21));
        QFont font;
        font.setPointSize(12);
        label->setFont(font);
        tableViewUart = new QTableView(DialogConfigDev);
        tableViewUart->setObjectName(QString::fromUtf8("tableViewUart"));
        tableViewUart->setGeometry(QRect(10, 50, 521, 231));
        lineEditUart = new QLineEdit(DialogConfigDev);
        lineEditUart->setObjectName(QString::fromUtf8("lineEditUart"));
        lineEditUart->setGeometry(QRect(120, 9, 113, 31));
        lineEditUart->setFont(font);
        tableViewDev = new QTableView(DialogConfigDev);
        tableViewDev->setObjectName(QString::fromUtf8("tableViewDev"));
        tableViewDev->setGeometry(QRect(550, 50, 531, 231));
        lineEditDev = new QLineEdit(DialogConfigDev);
        lineEditDev->setObjectName(QString::fromUtf8("lineEditDev"));
        lineEditDev->setGeometry(QRect(660, 10, 111, 31));
        lineEditDev->setFont(font);
        label_2 = new QLabel(DialogConfigDev);
        label_2->setObjectName(QString::fromUtf8("label_2"));
        label_2->setGeometry(QRect(560, 20, 91, 16));
        label_2->setFont(font);
        label_3 = new QLabel(DialogConfigDev);
        label_3->setObjectName(QString::fromUtf8("label_3"));
        label_3->setGeometry(QRect(250, 0, 271, 16));
        label_4 = new QLabel(DialogConfigDev);
        label_4->setObjectName(QString::fromUtf8("label_4"));
        label_4->setGeometry(QRect(250, 20, 261, 16));
        label_5 = new QLabel(DialogConfigDev);
        label_5->setObjectName(QString::fromUtf8("label_5"));
        label_5->setGeometry(QRect(780, 20, 191, 16));
        lineEditLetter = new QLineEdit(DialogConfigDev);
        lineEditLetter->setObjectName(QString::fromUtf8("lineEditLetter"));
        lineEditLetter->setGeometry(QRect(110, 310, 111, 31));
        lineEditLetter->setFont(font);
        label_6 = new QLabel(DialogConfigDev);
        label_6->setObjectName(QString::fromUtf8("label_6"));
        label_6->setGeometry(QRect(10, 315, 91, 21));
        label_6->setFont(font);
        tableViewLetter = new QTableView(DialogConfigDev);
        tableViewLetter->setObjectName(QString::fromUtf8("tableViewLetter"));
        tableViewLetter->setGeometry(QRect(10, 350, 521, 281));
        label_7 = new QLabel(DialogConfigDev);
        label_7->setObjectName(QString::fromUtf8("label_7"));
        label_7->setGeometry(QRect(550, 315, 91, 21));
        label_7->setFont(font);
        lineEditMeas = new QLineEdit(DialogConfigDev);
        lineEditMeas->setObjectName(QString::fromUtf8("lineEditMeas"));
        lineEditMeas->setGeometry(QRect(650, 310, 113, 31));
        lineEditMeas->setFont(font);
        tableViewMeas = new QTableView(DialogConfigDev);
        tableViewMeas->setObjectName(QString::fromUtf8("tableViewMeas"));
        tableViewMeas->setGeometry(QRect(550, 350, 531, 281));

        retranslateUi(DialogConfigDev);
        QObject::connect(buttonBox, SIGNAL(accepted()), DialogConfigDev, SLOT(accept()));
        QObject::connect(buttonBox, SIGNAL(rejected()), DialogConfigDev, SLOT(reject()));

        QMetaObject::connectSlotsByName(DialogConfigDev);
    } // setupUi

    void retranslateUi(QDialog *DialogConfigDev)
    {
        DialogConfigDev->setWindowTitle(QCoreApplication::translate("DialogConfigDev", "Dialog", nullptr));
        label->setText(QCoreApplication::translate("DialogConfigDev", "\344\270\262\345\217\243\346\225\260\351\207\217\357\274\232", nullptr));
        label_2->setText(QCoreApplication::translate("DialogConfigDev", "\350\256\276\345\244\207\346\225\260\351\207\217\357\274\232", nullptr));
        label_3->setText(QCoreApplication::translate("DialogConfigDev", "\346\225\210\351\252\214\344\275\215\357\274\2320-\346\227\240\346\225\210\351\252\214 1-\345\245\207\346\225\210\351\252\214 2-\345\201\266\346\225\210\351\252\214", nullptr));
        label_4->setText(QCoreApplication::translate("DialogConfigDev", "\345\201\234\346\255\242\344\275\215\357\274\2320-1\344\275\215    1-1.5\344\275\215  2-2\344\275\215", nullptr));
        label_5->setText(QCoreApplication::translate("DialogConfigDev", "\347\253\257\345\217\243\345\217\267\357\274\2321\344\270\272COM1\344\273\245\346\255\244\347\261\273\346\216\250", nullptr));
        label_6->setText(QCoreApplication::translate("DialogConfigDev", "\351\201\245\344\277\241\346\225\260\351\207\217:", nullptr));
        label_7->setText(QCoreApplication::translate("DialogConfigDev", "\351\201\245\346\265\213\346\225\260\351\207\217:", nullptr));
    } // retranslateUi

};

namespace Ui {
    class DialogConfigDev: public Ui_DialogConfigDev {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_DIALOGCONFIGDEV_H
