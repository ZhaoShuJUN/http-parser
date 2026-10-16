#pragma once
#include "gb28181.h"
#include "modbus.h"
#include "mqtt.h"
#include <QMediaPlayer>
#include <QVideoWidget>
#include <QtWidgets>
class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    MainWindow(const Config &cfg, bool demo, QWidget *parent = nullptr);
    ~MainWindow();

  private:
    Config cfg_;
    bool demo_;
    Store store_;
    Cameras cameras_;
    Mqtt mqtt_;
    Gb28181 gb_;
    QVector<ModbusBus *> buses_;
    QTableWidget *values_ = nullptr, *alarms_ = nullptr;
    QComboBox *selected_ = nullptr, *sensorFilter_ = nullptr;
    QLabel *health_ = nullptr, *mqttStatus_ = nullptr, *gbStatus_ = nullptr, *cameraStatus_ = nullptr;
    QMediaPlayer *player_ = nullptr;
    QVideoWidget *video_ = nullptr;
    QDateTimeEdit *from_ = nullptr, *to_ = nullptr;
    QPlainTextEdit *editor_ = nullptr;
    QTimer refresh_, demoTimer_;
    QElapsedTimer uptime_;
    void refresh();
    void query();
    void saveConfig();
    void preview();
    void log(QString text);
};
