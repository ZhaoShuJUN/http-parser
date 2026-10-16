#include "mainwindow.h"
#include "AppFunc/alarmrecord.h"
#include "AppFunc/corelogic.h"
#include "AppFunc/object1commintf.h"
#include "CommObj/Video/cameracomm.h"
#include "CommObj/commobj.h"
#include "ConfigInfo/appcfginfo.h"
#include "dialogconfigdev.h"
#include "ui_mainwindow.h"
#ifdef HMI_HAS_GST_MPP
#include "Video/gstmpprtspplayer.h"
#endif
namespace {
class SensorItemDelegate : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        painter->save();
        auto rect = option.rect.adjusted(8, 6, -8, -6);
        const bool alarm = index.data(Qt::UserRole + 1).toBool();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(alarm ? QColor("#d26767") : QColor("#2d6a4f"), 1));
        painter->setBrush(alarm ? QColor("#482d2d") : QColor("#1b4332"));
        painter->drawRoundedRect(rect, 10, 10);
        auto lines = index.data(Qt::DisplayRole).toString().split('\n');
        auto textRect = rect.adjusted(18, 12, -18, -10);
        QFont font = option.font;
        font.setPixelSize(18);
        font.setBold(true);
        painter->setFont(font);
        painter->setPen(alarm ? QColor("#ffc0c0") : QColor("#e8f5e9"));
        painter->drawText(textRect, Qt::AlignTop | Qt::AlignLeft,
                          QFontMetrics(font).elidedText(lines.value(0), Qt::ElideRight, textRect.width()));
        font.setPixelSize(14);
        font.setBold(false);
        painter->setFont(font);
        painter->setPen(QColor("#acc7b5"));
        painter->drawText(textRect.adjusted(0, 32, 0, 0), Qt::AlignTop | Qt::AlignLeft,
                          QFontMetrics(font).elidedText(lines.value(1), Qt::ElideRight, textRect.width()));
        painter->restore();
    }
};
QString stateName(const SensorReading &r) {
    if (r.state == "unknown")
        return "待采样";
    if (r.state == "offline")
        return "通信异常";
    if (r.state == "high")
        return "高限报警";
    if (r.state == "low")
        return "低限报警";
    return "正常";
}
QString timeLabel(qint64 ms) {
    qint64 s = ms / 1000;
    return QString("%1:%2:%3")
        .arg(s / 3600, 2, 10, QChar('0'))
        .arg(s / 60 % 60, 2, 10, QChar('0'))
        .arg(s % 60, 2, 10, QChar('0'));
}
} // namespace
MainWindow::MainWindow(QWidget *p)
    : QMainWindow(p), ui(new Ui::MainWindow), m_cfg(appCfgInfoInstance->settings()) {
    ui->setupUi(this);
    QFile qss(":/hmi/hmi.qss");
    if (qss.open(QIODevice::ReadOnly))
        setStyleSheet(QString::fromUtf8(qss.readAll()));
    menuBar()->hide();
    statusBar()->hide();
    setMinimumSize(960, 600);
    setWindowTitle("环境监控 — MyOverFlow" + (m_cfg.demo ? QString(" [模拟演示]") : QString()));
    buildUi();
    stopVideo();
    m_siTimerID = startTimer(500);
    refresh();
}
MainWindow::~MainWindow() {
    if (m_siTimerID > 0)
        killTimer(m_siTimerID);
    stopVideo();
    delete ui;
}
void MainWindow::setCoreLogic(CCoreLogic *c) {
    m_core = c;
    queryAlarms();
}
void MainWindow::buildUi() {
    auto root = new QVBoxLayout(ui->centralwidget);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(10);
    auto header = new QHBoxLayout;
    auto title = new QLabel("环境监控");
    title->setObjectName("titleLabel");
    header->addWidget(title);
    m_time = new QLabel;
    header->addStretch();
    header->addWidget(m_time);
    auto poll = new QPushButton("立即采集");
    poll->setObjectName("pollButton");
    header->addWidget(poll);
    connect(poll, &QPushButton::clicked, this, [] { Object1CommInstance->proSendObject1Data(); });
    auto configure = new QPushButton("设备 / 系统设置");
    configure->setObjectName("configButton");
    header->addWidget(configure);
    connect(configure, &QPushButton::clicked, this, &MainWindow::on_actionConfigFile_triggered);
    root->addLayout(header);
    m_summary = new QLabel;
    m_summary->setObjectName("summaryLabel");
    root->addWidget(m_summary);
    auto navScroll = new QScrollArea;
    navScroll->setWidgetResizable(true);
    navScroll->setFixedHeight(72);
    navScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto nav = new QWidget;
    auto navLayout = new QHBoxLayout(nav);
    navLayout->setContentsMargins(0, 0, 0, 0);
    auto group = new QButtonGroup(this);
    group->setExclusive(true);
    for (auto name : QStringList{"全部测点", "温度", "湿度", "液位", "六氟化硫", "烟雾浓度", "摄像头",
                                 "录像回放", "报警记录"}) {
        auto b = new QPushButton(name);
        b->setCheckable(true);
        b->setMinimumHeight(40);
        group->addButton(b);
        navLayout->addWidget(b);
        if (name == "全部测点")
            b->setChecked(true);
        connect(b, &QPushButton::clicked, this, [this, name] {
            selectPage(name, name == "摄像头" ? 1 : name == "录像回放" ? 2 : name == "报警记录" ? 3 : 0);
        });
    }
    navLayout->addStretch();
    navScroll->setWidget(nav);
    root->addWidget(navScroll);
    m_pages = new QStackedWidget;
    root->addWidget(m_pages, 1);
    auto sensors = new QWidget;
    auto sl = new QVBoxLayout(sensors);
    sl->setContentsMargins(0, 0, 0, 0);
    m_sensorList = new QListWidget;
    m_sensorList->setItemDelegate(new SensorItemDelegate(m_sensorList));
    m_sensorList->setObjectName("sensorList");
    sl->addWidget(m_sensorList);
    auto tip = new QLabel("显示和上报共用内存中的最新采样。未标定测点仅供调试，请先确认倍率、单位及阈值。");
    tip->setWordWrap(true);
    sl->addWidget(tip);
    m_pages->addWidget(sensors);
    m_player = new QMediaPlayer(this);
    connect(m_player, QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error), this,
            [this](QMediaPlayer::Error e) {
                if (e != QMediaPlayer::NoError)
                    m_cameraState->setText("播放失败，请检查 RTSP/认证、文件完整性及 Qt/GStreamer 解码插件");
            });
    m_livePage = new QWidget;
    auto vl = new QVBoxLayout(m_livePage);
    auto vt = new QHBoxLayout;
    m_cameraCombo = new QComboBox;
    for (int i = 0; i < m_cfg.cameras.size(); ++i) {
        auto c = m_cfg.cameras[i];
        m_cameraCombo->addItem(QString("%1  %2%3").arg(i + 1).arg(c.name).arg(c.enabled ? "" : "（未启用）"),
                               i);
    }
    vt->addWidget(m_cameraCombo, 1);
    auto play = new QPushButton("查看选中摄像头"), stop = new QPushButton("停止预览");
    vt->addWidget(play);
    vt->addWidget(stop);
    vl->addLayout(vt);
    connect(play, &QPushButton::clicked, this, &MainWindow::startPreview);
    connect(stop, &QPushButton::clicked, this, &MainWindow::stopVideo);
    connect(m_cameraCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { stopVideo(); });
    m_liveVideo = new QVideoWidget;
    m_liveVideo->setMinimumHeight(260);
    m_liveVideo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    vl->addWidget(m_liveVideo, 1);
    m_cameraImage = new QLabel("选择摄像头后点击查看");
    m_cameraImage->setMinimumHeight(200);
    m_cameraImage->setAlignment(Qt::AlignCenter);
    m_cameraImage->setStyleSheet("background:#07120d;color:#91a89b;");
    vl->addWidget(m_cameraImage, 1);
#ifdef HMI_HAS_GST_MPP
    m_liveVideo->hide();
    m_gst = new GstMppRtspPlayer(this);
    connect(m_gst, &GstMppRtspPlayer::frameReady, this, [this](const QImage &img) {
        m_cameraImage->setPixmap(QPixmap::fromImage(img).scaled(m_cameraImage->size(), Qt::KeepAspectRatio,
                                                                Qt::SmoothTransformation));
    });
    connect(m_gst, &GstMppRtspPlayer::errorText, this, [this](QString e) { m_cameraState->setText(e); });
#else
    m_cameraImage->hide();
#endif
    auto controls = new QHBoxLayout;
    auto ptz = new QGridLayout;
    int index = 0;
    for (auto v : QVector<QPair<QString, QPoint>>{
             {"上", {0, 40}}, {"左", {-40, 0}}, {"停止", {0, 0}}, {"右", {40, 0}}, {"下", {0, -40}}}) {
        auto b = new QPushButton(v.first);
        int rows[] = {0, 1, 1, 1, 2}, cols[] = {1, 0, 1, 2, 1};
        ptz->addWidget(b, rows[index], cols[index]);
        ++index;
        connect(b, &QPushButton::pressed, this, [this, v] {
            if (!m_cfg.demo && commObjInstance->cameraComm())
                commObjInstance->cameraComm()->ptz(m_cameraCombo->currentIndex(), v.second.x(), v.second.y(),
                                                   0);
        });
        connect(b, &QPushButton::released, this, [this] {
            if (!m_cfg.demo && commObjInstance->cameraComm())
                commObjInstance->cameraComm()->ptz(m_cameraCombo->currentIndex(), 0, 0, 0);
        });
    }
    controls->addLayout(ptz);
    auto zoom = new QVBoxLayout;
    for (auto z : QVector<QPair<QString, int>>{{"放大", 40}, {"缩小", -40}}) {
        auto b = new QPushButton(z.first);
        zoom->addWidget(b);
        connect(b, &QPushButton::pressed, this, [this, z] {
            if (!m_cfg.demo && commObjInstance->cameraComm())
                commObjInstance->cameraComm()->ptz(m_cameraCombo->currentIndex(), 0, 0, z.second);
        });
        connect(b, &QPushButton::released, this, [this] {
            if (!m_cfg.demo && commObjInstance->cameraComm())
                commObjInstance->cameraComm()->ptz(m_cameraCombo->currentIndex(), 0, 0, 0);
        });
    }
    controls->addLayout(zoom);
    controls->addStretch();
    auto preset = new QSpinBox;
    preset->setRange(1, 300);
    controls->addWidget(new QLabel("预置位"));
    controls->addWidget(preset);
    auto go = new QPushButton("调用"), save = new QPushButton("保存当前位置");
    controls->addWidget(go);
    controls->addWidget(save);
    connect(go, &QPushButton::clicked, this, [this, preset] {
        if (!m_cfg.demo && commObjInstance->cameraComm())
            commObjInstance->cameraComm()->preset(m_cameraCombo->currentIndex(), preset->value());
    });
    connect(save, &QPushButton::clicked, this, [this, preset] {
        if (!m_cfg.demo && commObjInstance->cameraComm())
            commObjInstance->cameraComm()->preset(m_cameraCombo->currentIndex(), preset->value(), true);
    });
    vl->addLayout(controls);
    m_cameraState = new QLabel("预览一路；全部已启用摄像头可独立录像。");
    m_cameraState->setWordWrap(true);
    vl->addWidget(m_cameraState);
    m_pages->addWidget(m_livePage);
    m_recordPage = new QWidget;
    auto rl = new QVBoxLayout(m_recordPage);
    auto rt = new QHBoxLayout;
    m_recordCamera = new QComboBox;
    for (auto c : m_cfg.cameras)
        m_recordCamera->addItem(c.name);
    rt->addWidget(m_recordCamera);
    auto recRefresh = new QPushButton("刷新录像列表");
    rt->addWidget(recRefresh);
    rt->addStretch();
    rl->addLayout(rt);
    connect(recRefresh, &QPushButton::clicked, this, &MainWindow::refreshRecordings);
    connect(m_recordCamera, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        stopVideo();
        refreshRecordings();
    });
    auto split = new QSplitter;
    m_recordList = new QListWidget;
    m_recordList->setMinimumWidth(360);
    m_recordVideo = new QVideoWidget;
    split->addWidget(m_recordList);
    split->addWidget(m_recordVideo);
    split->setStretchFactor(1, 1);
    rl->addWidget(split, 1);
    connect(m_recordList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        auto path = item->data(Qt::UserRole).toString();
        if (path.isEmpty())
            return;
        stopVideo();
        m_player->setVideoOutput(m_recordVideo);
        m_player->setMedia(QUrl::fromLocalFile(path));
        m_player->play();
    });
    auto seekLayout = new QHBoxLayout;
    auto recStop = new QPushButton("停止播放");
    seekLayout->addWidget(recStop);
    connect(recStop, &QPushButton::clicked, this, &MainWindow::stopVideo);
    m_seek = new QSlider(Qt::Horizontal);
    seekLayout->addWidget(m_seek, 1);
    m_seekTime = new QLabel("00:00:00 / 00:00:00");
    seekLayout->addWidget(m_seekTime);
    rl->addLayout(seekLayout);
    rl->addWidget(new QLabel("双击已完成分段回放；最新分段可能仍在写入。循环录像会按配额覆盖旧文件。"));
    connect(m_player, &QMediaPlayer::durationChanged, this,
            [this](qint64 n) { m_seek->setRange(0, int(qMin(n, qint64(INT_MAX)))); });
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 n) {
        if (!m_seek->isSliderDown())
            m_seek->setValue(int(n));
        m_seekTime->setText(timeLabel(n) + " / " + timeLabel(m_player->duration()));
    });
    connect(m_seek, &QSlider::sliderReleased, this, [this] { m_player->setPosition(m_seek->value()); });
    m_pages->addWidget(m_recordPage);
    auto alarmPage = new QWidget;
    auto al = new QVBoxLayout(alarmPage);
    auto filters = new QHBoxLayout;
    m_alarmSensor = new QComboBox;
    m_alarmSensor->addItem("全部传感器", "");
    for (auto s : m_cfg.sensors)
        m_alarmSensor->addItem(s.name, s.id);
    filters->addWidget(m_alarmSensor);
    m_from = new QDateTimeEdit(QDateTime::currentDateTime().addDays(-7));
    m_to = new QDateTimeEdit(QDateTime::currentDateTime().addDays(1));
    for (auto d : {m_from, m_to}) {
        d->setCalendarPopup(true);
        d->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
        filters->addWidget(d);
    }
    auto search = new QPushButton("查询"), exportCsv = new QPushButton("导出 CSV");
    filters->addWidget(search);
    filters->addWidget(exportCsv);
    al->addLayout(filters);
    connect(search, &QPushButton::clicked, this, &MainWindow::queryAlarms);
    connect(exportCsv, &QPushButton::clicked, this, &MainWindow::exportAlarms);
    m_alarms = new QTableWidget(0, 8);
    m_alarms->setHorizontalHeaderLabels(
        {"序号", "UTC 时间", "测点ID", "名称", "事件", "数值", "状态", "详情"});
    m_alarms->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_alarms->horizontalHeader()->setStretchLastSection(true);
    m_alarms->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_alarms->setAlternatingRowColors(true);
    al->addWidget(m_alarms, 1);
    al->addWidget(new QLabel("显示最近 500 条。筛选时间为本机时区，存储与导出时间为 UTC。"));
    m_pages->addWidget(alarmPage);
    m_services = new QLabel;
    m_services->setWordWrap(true);
    root->addWidget(m_services);
    sensorRows();
}
void MainWindow::sensorRows() {
    m_sensorList->clear();
    m_items.clear();
    for (auto s : m_cfg.sensors) {
        if (!m_category.isEmpty() && m_category != "全部测点" && s.category != m_category)
            continue;
        auto item = new QListWidgetItem;
        item->setSizeHint(QSize(500, 100));
        item->setData(Qt::UserRole, s.id);
        m_sensorList->addItem(item);
        m_items[s.id] = item;
    }
    if (m_items.isEmpty())
        m_sensorList->addItem("此分类暂无测点，请在设备配置中设置类别。");
}
void MainWindow::selectPage(QString category, int index) {
    stopVideo();
    m_category = category;
    m_pages->setCurrentIndex(index);
    if (index == 0)
        sensorRows();
    else if (index == 2)
        refreshRecordings();
    else if (index == 3)
        queryAlarms();
    refresh();
}
void MainWindow::refresh() {
    auto readings = Object1CommInstance->snapshot();
    int valid = 0, alarm = 0, unconfirmed = 0;
    for (auto s : m_cfg.sensors) {
        auto r = readings.value(s.id);
        valid += r.valid;
        alarm += r.alarm;
        unconfirmed += !s.calibrated;
        if (auto item = m_items.value(s.id)) {
            auto value = r.valid ? QString::number(r.value, 'f', 3) + " " + s.unit : QString("--");
            QString text = QString("%1  |  %2  |  %3  |  %4%5\n串口 %6 · 从站 %7 · 寄存器 %8    采样 %9%10")
                               .arg(s.name, s.category, value, stateName(r), s.calibrated ? "" : "  [未标定]")
                               .arg(s.port)
                               .arg(s.slave)
                               .arg(s.reg)
                               .arg(r.time, r.error.isEmpty() ? QString() : "  " + r.error);
            if (item->text() != text)
                item->setText(text);
            item->setData(Qt::UserRole + 1, r.alarm);
            item->setForeground(r.alarm   ? QColor("#ff9c9c")
                                : r.valid ? QColor("#e8f5e9")
                                          : QColor("#92a99b"));
        }
    }
    m_time->setText(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
    m_summary->setText(QString("测点 %1    通信正常 %2    报警 %3    串口总线 %4    未标定 %5%6")
                           .arg(m_cfg.sensors.size())
                           .arg(valid)
                           .arg(alarm)
                           .arg(m_cfg.ports.size())
                           .arg(unconfirmed)
                           .arg(m_cfg.demo ? "    【模拟数据，不连接硬件及网络】" : ""));
    auto statuses = Object1CommInstance->serviceStatuses();
    m_services->setText(
        "MQTT: " + statuses.value("mqtt", "禁用") + "    GB28181: " + statuses.value("gb28181", "禁用") +
        "    报警数据库: " + statuses.value("database", "初始化中") + "    录像: " +
        statuses.value("recording", m_cfg.recording["enabled"].toBool() ? "已启用，详见摄像头页" : "禁用"));
    if (m_pages->currentIndex() == 1 && commObjInstance->cameraComm()) {
        auto i = m_cameraCombo->currentIndex();
        auto c = i >= 0 && i < m_cfg.cameras.size() ? m_cfg.cameras[i] : CameraSetting();
        m_cameraCombo->setToolTip("GB 通道: " + c.channelId);
        if (m_player->error() == QMediaPlayer::NoError)
            m_cameraState->setText("录像: " + commObjInstance->cameraComm()->status(i) +
                                   "    GB 通道: " + c.channelId);
    }
}
void MainWindow::timerEvent(QTimerEvent *e) {
    if (e->timerId() == m_siTimerID)
        refresh();
    else
        QMainWindow::timerEvent(e);
}
void MainWindow::closeEvent(QCloseEvent *e) {
    stopVideo();
    QMainWindow::closeEvent(e);
}
void MainWindow::startPreview() {
    int i = m_cameraCombo->currentIndex();
    if (m_cfg.demo) {
        m_cameraState->setText("模拟模式不连接摄像头");
        return;
    }
    if (i < 0 || i >= m_cfg.cameras.size() || !m_cfg.cameras[i].enabled) {
        m_cameraState->setText("摄像头未启用");
        return;
    }
    stopVideo();
    auto url = cameraRtspUrl(m_cfg.cameras[i]);
#ifdef HMI_HAS_GST_MPP
    m_gst->play(url);
#else
    m_cameraImage->hide();
    m_liveVideo->show();
    m_player->setVideoOutput(m_liveVideo);
    m_player->setMedia(QUrl(url));
    m_player->play();
#endif
}
void MainWindow::stopVideo() {
    if (m_liveVideo)
        m_liveVideo->hide();
    if (m_cameraImage) {
        m_cameraImage->clear();
        m_cameraImage->setText("预览已停止；选择摄像头后点击查看");
        m_cameraImage->show();
    }
    if (m_player)
        m_player->stop();
#ifdef HMI_HAS_GST_MPP
    if (m_gst)
        m_gst->stop();
#endif
}
void MainWindow::queryAlarms() {
    if (!m_core || !m_core->alarmRecord())
        return;
    auto rows = m_core->alarmRecord()->query(m_alarmSensor->currentData().toString(),
                                             m_from->dateTime().toUTC().toString(Qt::ISODateWithMs),
                                             m_to->dateTime().toUTC().toString(Qt::ISODateWithMs));
    m_alarms->setRowCount(rows.size());
    for (int r = 0; r < rows.size(); ++r)
        for (int c = 0; c < rows[r].size(); ++c)
            m_alarms->setItem(r, c, new QTableWidgetItem(rows[r][c]));
}
void MainWindow::exportAlarms() {
    auto path = QFileDialog::getSaveFileName(this, "导出报警记录", {}, "CSV (*.csv)");
    if (path.isEmpty())
        return;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "导出失败", f.errorString());
        return;
    }
    QByteArray bytes = "\xef\xbb\xbf";
    for (int r = -1; r < m_alarms->rowCount(); ++r) {
        QStringList row;
        for (int c = 0; c < m_alarms->columnCount(); ++c) {
            QString s = r < 0 ? m_alarms->horizontalHeaderItem(c)->text() : m_alarms->item(r, c)->text();
            if (!s.isEmpty() && QString("=+-@").contains(s.front()))
                s.prepend('\'');
            s.replace('"', "\"\"");
            row << '"' + s + '"';
        }
        bytes += row.join(',').toUtf8() + "\r\n";
    }
    if (f.write(bytes) != bytes.size() || !f.commit())
        QMessageBox::warning(this, "导出失败", f.errorString());
}
void MainWindow::refreshRecordings() {
    m_recordList->clear();
    int i = m_recordCamera->currentIndex();
    if (i < 0)
        return;
    QString root = m_cfg.recording["directory"].toString();
    QDir dir(root + "/MyOverFlow/camera" + QString::number(i + 1));
    if (!dir.exists() || !dir.canonicalPath().startsWith(QFileInfo(root).canonicalFilePath() + "/")) {
        m_recordList->addItem("录像目录尚无文件");
        return;
    }
    for (auto file :
         dir.entryInfoList({"mo_*.mkv"}, QDir::Files | QDir::NoSymLinks, QDir::Name | QDir::Reversed)) {
        auto item = new QListWidgetItem(file.fileName() +
                                        QString("\n%1 MiB").arg(file.size() / 1024.0 / 1024.0, 0, 'f', 1));
        item->setData(Qt::UserRole, file.absoluteFilePath());
        m_recordList->addItem(item);
    }
    if (m_recordList->count() == 0)
        m_recordList->addItem("暂无录像分段");
}
void MainWindow::on_actionConfigFile_triggered() {
    DialogConfigDev dialog(this);
    dialog.exec();
}
void MainWindow::on_actionOpenFile_triggered() {
    on_actionConfigFile_triggered();
}
void MainWindow::on_actionSaveFile_triggered() {
    on_actionConfigFile_triggered();
}
void MainWindow::on_actionSaveAsFile_triggered() {
    auto path = QFileDialog::getSaveFileName(this, "导出配置", {}, "INI (*.ini)");
    if (path.isEmpty())
        return;
    QString error;
    if (!writeMonitorIni(path, m_cfg, &error))
        QMessageBox::warning(this, "导出失败", error);
}
