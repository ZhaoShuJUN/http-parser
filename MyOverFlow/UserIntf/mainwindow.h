#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include "ConfigInfo/monitorconfig.h"
#include <QMediaPlayer>
#include <QVideoWidget>
#include <QtWidgets>
class CCoreLogic;
class GstMppRtspPlayer;
namespace Ui {
class MainWindow;
}
class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    void setCoreLogic(CCoreLogic *core);

  protected:
    void timerEvent(QTimerEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
  private slots:
    void on_actionOpenFile_triggered();
    void on_actionSaveFile_triggered();
    void on_actionSaveAsFile_triggered();
    void on_actionConfigFile_triggered();

  private:
    Ui::MainWindow *ui;
    CCoreLogic *m_core = nullptr;
    MonitorSettings m_cfg;
    qint32 m_siTimerID = 0;
    QStackedWidget *m_pages = nullptr;
    QListWidget *m_sensorList = nullptr, *m_recordList = nullptr;
    QLabel *m_time = nullptr, *m_summary = nullptr, *m_services = nullptr, *m_cameraState = nullptr,
           *m_cameraImage = nullptr;
    QComboBox *m_cameraCombo = nullptr, *m_recordCamera = nullptr, *m_alarmSensor = nullptr;
    QDateTimeEdit *m_from = nullptr, *m_to = nullptr;
    QTableWidget *m_alarms = nullptr;
    QMediaPlayer *m_player = nullptr;
    QVideoWidget *m_liveVideo = nullptr, *m_recordVideo = nullptr;
    QSlider *m_seek = nullptr;
    QLabel *m_seekTime = nullptr;
    QString m_category;
    QMap<QString, QListWidgetItem *> m_items;
    QWidget *m_livePage = nullptr, *m_recordPage = nullptr;
#ifdef HMI_HAS_GST_MPP
    GstMppRtspPlayer *m_gst = nullptr;
#endif
    void buildUi();
    void refresh();
    void sensorRows();
    void selectPage(QString category, int index);
    void startPreview();
    void stopVideo();
    void queryAlarms();
    void exportAlarms();
    void refreshRecordings();
};
#endif
