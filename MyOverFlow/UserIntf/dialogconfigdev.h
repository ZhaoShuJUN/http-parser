#ifndef DIALOGCONFIGDEV_H
#define DIALOGCONFIGDEV_H
#include "ConfigInfo/monitorconfig.h"
#include <QtWidgets>
namespace Ui {
class DialogConfigDev;
}
class DialogConfigDev : public QDialog {
    Q_OBJECT
  public:
    explicit DialogConfigDev(QWidget *parent = nullptr);
    ~DialogConfigDev();
    void SetRefreshUartConfig();
    void GetRefreshUartConfig();
    void SetRefreshDevConfig();
    void GetRefreshDevConfig();
    void SetReportLetterConfig();
    void GetReportLetterConfig();
    void SetReportMeasConfig();
    void GetReportMeasConfig();
  private slots:
    void on_lineEditUart_textChanged(const QString &text);
    void on_lineEditDev_textChanged(const QString &text);
    void on_buttonBox_accepted();
    void on_lineEditLetter_textChanged(const QString &text);
    void on_lineEditMeas_textChanged(const QString &text);

  private:
    Ui::DialogConfigDev *ui;
    MonitorSettings m_editing;
    QStandardItemModel *m_uart, *m_dev, *m_letter, *m_meas, *m_cameras;
    QMap<QString, QWidget *> m_fields;
    QSpinBox *m_cameraCount = nullptr;
    void cameraRows();
    void setCount();
    QJsonObject readForm(const QString &prefix, const QJsonObject &base) const;
    void buildForm(QWidget *page, const QString &prefix, const QJsonObject &values,
                   const QMap<QString, QString> &labels);
};
#endif
