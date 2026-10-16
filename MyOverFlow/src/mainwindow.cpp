#include "mainwindow.h"
#include <cmath>
MainWindow::MainWindow(const Config &c, bool demo, QWidget *p)
    : QMainWindow(p), cfg_(c), demo_(demo), store_(this), cameras_(c, this), mqtt_(c, &store_, this),
      gb_(c, &cameras_, this) {
    setWindowTitle("MyOverFlow — RK3399 环境监控" + (demo ? QString(" [模拟演示]") : QString()));
    resize(1150, 760);
    uptime_.start();
    QString error;
    if (!store_.open(c.dataDir + (demo ? "/demo-alarms.sqlite" : "/alarms.sqlite"), &error))
        throw std::runtime_error(("SQLite 初始化失败: " + error).toStdString());
    store_.configure(c.sensors);
    auto tabs = new QTabWidget;
    setCentralWidget(tabs);
    auto live = new QWidget;
    auto lv = new QVBoxLayout(live);
    health_ = new QLabel(demo ? "模拟数据；串口、MQTT、录像、GB 网络功能均未启动" : "实时采集");
    lv->addWidget(health_);
    values_ = new QTableWidget(0, 8);
    values_->setHorizontalHeaderLabels(
        {"ID", "传感器", "数值", "单位", "状态", "报警", "采样 UTC 时间", "通信错误"});
    values_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    values_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lv->addWidget(values_);
    mqttStatus_ = new QLabel("MQTT: 禁用");
    gbStatus_ = new QLabel("GB28181: 禁用");
    lv->addWidget(mqttStatus_);
    lv->addWidget(gbStatus_);
    tabs->addTab(live, "实时数据");
    auto history = new QWidget;
    auto hv = new QVBoxLayout(history);
    auto filters = new QHBoxLayout;
    sensorFilter_ = new QComboBox;
    sensorFilter_->addItem("全部传感器", "");
    for (auto s : c.sensors)
        sensorFilter_->addItem(s.name, s.id);
    from_ = new QDateTimeEdit(QDateTime::currentDateTime().addDays(-7));
    to_ = new QDateTimeEdit(QDateTime::currentDateTime().addDays(1));
    for (auto d : {from_, to_}) {
        d->setCalendarPopup(true);
        d->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    }
    auto search = new QPushButton("查询");
    auto csv = new QPushButton("导出 CSV");
    filters->addWidget(sensorFilter_);
    filters->addWidget(from_);
    filters->addWidget(to_);
    filters->addWidget(search);
    filters->addWidget(csv);
    hv->addLayout(filters);
    hv->addWidget(
        new QLabel("显示最新 500 条；时间选择为本机时区，数据库按 UTC 保存。通信异常同样记录报警。"));
    alarms_ = new QTableWidget(0, 7);
    alarms_->setHorizontalHeaderLabels({"序号", "UTC 时间", "传感器 ID", "事件", "数值", "状态", "详情"});
    alarms_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    alarms_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    hv->addWidget(alarms_);
    connect(search, &QPushButton::clicked, this, &MainWindow::query);
    connect(csv, &QPushButton::clicked, this, [this] {
        QString file = QFileDialog::getSaveFileName(this, "导出报警", {}, "CSV (*.csv)");
        if (file.isEmpty())
            return;
        QSaveFile f(file);
        if (!f.open(QIODevice::WriteOnly)) {
            log(f.errorString());
            return;
        }
        QByteArray b = "\xef\xbb\xbf";
        for (int r = -1; r < alarms_->rowCount(); ++r) {
            QStringList row;
            for (int col = 0; col < alarms_->columnCount(); ++col) {
                QString s =
                    r < 0 ? alarms_->horizontalHeaderItem(col)->text() : alarms_->item(r, col)->text();
                if (!s.isEmpty() && QString("=+-@").contains(s.front()))
                    s.prepend('\'');
                s.replace('"', "\"\"");
                row << '"' + s + '"';
            }
            b += row.join(',').toUtf8() + "\r\n";
        }
        f.write(b);
        if (!f.commit())
            log(f.errorString());
    });
    tabs->addTab(history, "报警查询");
    auto camera = new QWidget;
    auto cv = new QVBoxLayout(camera);
    auto toolbar = new QHBoxLayout;
    selected_ = new QComboBox;
    for (int i = 0; i < c.cameras.size(); ++i)
        selected_->addItem(QString::number(i + 1) + " " + c.cameras[i].name, i);
    toolbar->addWidget(selected_);
    auto view = new QPushButton("查看选中摄像头"), stop = new QPushButton("停止预览");
    toolbar->addWidget(view);
    toolbar->addWidget(stop);
    cv->addLayout(toolbar);
    video_ = new QVideoWidget;
    video_->setMinimumHeight(300);
    cv->addWidget(video_);
    player_ = new QMediaPlayer(this);
    player_->setVideoOutput(video_);
    connect(view, &QPushButton::clicked, this, &MainWindow::preview);
    connect(stop, &QPushButton::clicked, player_, &QMediaPlayer::stop);
    connect(selected_, QOverload<int>::of(&QComboBox::currentIndexChanged), player_, &QMediaPlayer::stop);
    connect(player_, QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error), this,
            [this](QMediaPlayer::Error e) {
                if (e != QMediaPlayer::NoError)
                    log("预览失败：检查 RTSP 地址、摄像头认证与 Qt Multimedia/GStreamer 解码插件");
            });
    auto controls = new QHBoxLayout;
    for (auto v : QVector<QPair<QString, QPoint>>{
             {"左", {-40, 0}}, {"右", {40, 0}}, {"上", {0, 40}}, {"下", {0, -40}}}) {
        auto b = new QPushButton(v.first);
        controls->addWidget(b);
        connect(b, &QPushButton::pressed, this, [this, v] {
            if (!demo_)
                cameras_.ptz(selected_->currentIndex(), v.second.x(), v.second.y(), 0);
        });
        connect(b, &QPushButton::released, this, [this] {
            if (!demo_)
                cameras_.ptz(selected_->currentIndex(), 0, 0, 0);
        });
    }
    for (auto direction : QVector<QPair<QString, int>>{{"放大", 40}, {"缩小", -40}}) {
        auto b = new QPushButton(direction.first);
        controls->addWidget(b);
        connect(b, &QPushButton::pressed, this, [this, direction] {
            if (!demo_)
                cameras_.ptz(selected_->currentIndex(), 0, 0, direction.second);
        });
        connect(b, &QPushButton::released, this, [this] {
            if (!demo_)
                cameras_.ptz(selected_->currentIndex(), 0, 0, 0);
        });
    }
    auto preset = new QSpinBox;
    preset->setRange(1, 300);
    auto go = new QPushButton("调用预置位"), set = new QPushButton("保存预置位");
    controls->addWidget(preset);
    controls->addWidget(go);
    controls->addWidget(set);
    connect(go, &QPushButton::clicked, this, [this, preset] {
        if (!demo_)
            cameras_.preset(selected_->currentIndex(), preset->value());
    });
    connect(set, &QPushButton::clicked, this, [this, preset] {
        if (!demo_)
            cameras_.preset(selected_->currentIndex(), preset->value(), true);
    });
    cv->addLayout(controls);
    cameraStatus_ = new QLabel;
    cv->addWidget(cameraStatus_);
    tabs->addTab(camera, "摄像头与云台");
    auto settings = new QWidget;
    auto sv = new QVBoxLayout(settings);
    sv->addWidget(new QLabel("JSON 配置：串口 0..7、Modbus "
                             "从站/寄存器/格式、阈值、摄像头/"
                             "预置位、磁盘、MQTT、GB28181。\n修改后校验保存，重新启动程序生效。密码仅填写环境"
                             "变量名称；详见 docs/部署与协议.md。"));
    editor_ = new QPlainTextEdit;
    editor_->setPlainText(QJsonDocument(c.raw).toJson(QJsonDocument::Indented));
    sv->addWidget(editor_);
    auto save = new QPushButton("校验并保存配置（重启生效）");
    sv->addWidget(save);
    connect(save, &QPushButton::clicked, this, &MainWindow::saveConfig);
    tabs->addTab(settings, "系统配置");
    auto sensorPage = new QWidget;
    auto sensorLayout = new QVBoxLayout(sensorPage);
    sensorLayout->addWidget(new QLabel(
        "配置软件访问的从站地址，不会改写传感器硬件地址。串口/联动摄像头使用 0 起始编号，摄像头 -1 "
        "表示不联动。\n"
        "默认 0x03、寄存器 0。倍率、单位、阈值为示例，须按传感器手册和现场要求确认；保存后重启生效。"));
    auto sensorTable = new QTableWidget(c.sensors.size(), 13);
    sensorTable->setHorizontalHeaderLabels({"ID", "名称", "串口", "从站地址", "寄存器", "格式", "倍率",
                                            "下限", "上限", "回差", "摄像头", "预置位", "单位"});
    sensorTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    QStringList keys{"id",  "name", "port",       "slave",  "register", "format", "scale",
                     "low", "high", "hysteresis", "camera", "preset",   "unit"};
    auto initial = c.raw["sensors"].toArray();
    for (int row = 0; row < initial.size(); ++row)
        for (int col = 0; col < keys.size(); ++col) {
            auto value = initial[row].toObject()[keys[col]];
            auto item =
                new QTableWidgetItem(value.isString() ? value.toString() : QString::number(value.toDouble()));
            if (col == 0)
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            sensorTable->setItem(row, col, item);
        }
    sensorLayout->addWidget(sensorTable);
    auto sensorSave = new QPushButton("校验并保存传感器设置（重启生效）");
    sensorLayout->addWidget(sensorSave);
    connect(sensorSave, &QPushButton::clicked, this, [this, sensorTable, keys] {
        QJsonParseError pe;
        auto doc = QJsonDocument::fromJson(editor_->toPlainText().toUtf8(), &pe);
        if (pe.error || !doc.isObject()) {
            QMessageBox::warning(this, "配置错误", "请先修正系统配置 JSON");
            return;
        }
        auto root = doc.object();
        auto sensors = root["sensors"].toArray();
        if (sensors.size() != sensorTable->rowCount()) {
            QMessageBox::warning(this, "配置已变化",
                                 "系统配置的传感器数量已变化，请保存配置并重启后编辑表格");
            return;
        }
        for (int row = 0; row < sensors.size(); ++row) {
            auto o = sensors[row].toObject();
            if (o["id"].toString() != sensorTable->item(row, 0)->text()) {
                QMessageBox::warning(this, "配置已变化", "传感器顺序已变化，请重启后编辑");
                return;
            }
            for (int col = 1; col < keys.size(); ++col) {
                auto text = sensorTable->item(row, col)->text();
                if (col == 1 || col == 5 || col == 12)
                    o[keys[col]] = text;
                else {
                    bool ok = false;
                    double n = text.toDouble(&ok);
                    if (!ok || !qIsFinite(n) ||
                        ((col == 2 || col == 3 || col == 4 || col == 10 || col == 11) &&
                         std::floor(n) != n)) {
                        QMessageBox::warning(this, "配置错误",
                                             QString("第 %1 行：%2 数值无效").arg(row + 1).arg(keys[col]));
                        return;
                    }
                    o[keys[col]] = n;
                }
            }
            sensors[row] = o;
        }
        root["sensors"] = sensors;
        editor_->setPlainText(QJsonDocument(root).toJson(QJsonDocument::Indented));
        saveConfig();
    });
    tabs->addTab(sensorPage, "传感器设置");
    connect(&store_, &Store::databaseError, this, [this](QString e) { log("报警数据库写入失败: " + e); });
    connect(&cameras_, &Cameras::error, this, &MainWindow::log);
    connect(&mqtt_, &Mqtt::statusChanged, this, [this](QString s) { mqttStatus_->setText("MQTT: " + s); });
    connect(&gb_, &Gb28181::statusChanged, this, [this](QString s) { gbStatus_->setText("GB28181: " + s); });
    connect(&store_, &Store::alarmRaised, this, [this](QString id, int camera, int preset) {
        log(id + " 报警触发");
        if (!demo_ && camera >= 0)
            cameras_.preset(camera, preset);
    });
    refresh_.setInterval(500);
    connect(&refresh_, &QTimer::timeout, this, &MainWindow::refresh);
    refresh_.start();
    if (demo_) {
        demoTimer_.setInterval(1000);
        connect(&demoTimer_, &QTimer::timeout, this, [this] {
            for (int i = 0; i < cfg_.sensors.size(); ++i) {
                auto s = cfg_.sensors[i];
                double mid = (s.low + s.high) / 2, amp = (s.high - s.low) * 0.65;
                store_.sample(s.id, mid + amp * std::sin(uptime_.elapsed() / 3000.0 + i));
            }
        });
        demoTimer_.start();
    } else {
        for (int i = 0; i < c.ports.size(); ++i) {
            auto bus = new ModbusBus(i, c.ports[i], c.sensors, this);
            connect(bus, &ModbusBus::sample, &store_, &Store::sample);
            connect(bus, &ModbusBus::fault, &store_, &Store::fault);
            buses_ << bus;
            bus->start();
        }
        cameras_.start();
        mqtt_.start();
        gb_.start();
    }
    refresh();
    query();
}
MainWindow::~MainWindow() {
    for (auto b : buses_)
        b->stop();
    player_->stop();
    mqtt_.stop();
    gb_.stop();
    cameras_.stop();
}
void MainWindow::log(QString s) {
    statusBar()->showMessage(s, 15000);
    qWarning().noquote() << utcNow() << s;
}
void MainWindow::refresh() {
    auto data = store_.snapshot();
    values_->setRowCount(cfg_.sensors.size());
    for (int i = 0; i < cfg_.sensors.size(); ++i) {
        auto s = cfg_.sensors[i];
        auto r = data[s.id];
        QStringList cells{s.id,   s.name,  r.valid ? QString::number(r.value, 'f', 3) : "—",
                          s.unit, r.state, r.alarm ? "报警" : r.valid ? "正常" : "待采样",
                          r.time, r.error};
        for (int j = 0; j < cells.size(); ++j) {
            auto item = new QTableWidgetItem(cells[j]);
            item->setBackground(r.alarm   ? QColor("#ffcccc")
                                : r.valid ? QColor("#d8f0df")
                                          : QColor("#eeeeee"));
            values_->setItem(i, j, item);
        }
    }
    if (!store_.healthy())
        health_->setText("数据库异常，报警记录可能未保存：" + store_.error());
    else
        health_->setText(demo_ ? "模拟数据；串口、MQTT、录像、GB 网络功能均未启动"
                               : "实时采集；报警数据库可用");
    cameraStatus_->setText("选中摄像头: " + cameras_.status(selected_->currentIndex()));
}
void MainWindow::query() {
    auto rows = store_.history(sensorFilter_->currentData().toString(),
                               from_->dateTime().toUTC().toString(Qt::ISODateWithMs),
                               to_->dateTime().toUTC().toString(Qt::ISODateWithMs));
    alarms_->setRowCount(rows.size());
    for (int r = 0; r < rows.size(); ++r)
        for (int c = 0; c < rows[r].size(); ++c)
            alarms_->setItem(r, c, new QTableWidgetItem(rows[r][c]));
}
void MainWindow::preview() {
    int i = selected_->currentIndex();
    if (i < 0 || i >= cfg_.cameras.size() || !cfg_.cameras[i].enabled) {
        log("该摄像头未启用");
        return;
    }
    if (demo_) {
        log("模拟模式不连接真实摄像头");
        return;
    }
    player_->stop();
    player_->setMedia(QUrl(cameraUrl(cfg_.cameras[i])));
    player_->play();
}
void MainWindow::saveConfig() {
    QTemporaryFile tmp;
    tmp.open();
    tmp.write(editor_->toPlainText().toUtf8());
    tmp.flush();
    Config candidate;
    QString err;
    if (!candidate.load(tmp.fileName(), &err)) {
        QMessageBox::warning(this, "配置错误", err);
        return;
    }
    QSaveFile f(cfg_.path);
    if (!f.open(QIODevice::WriteOnly) || f.write(editor_->toPlainText().toUtf8()) < 0 || !f.commit()) {
        QMessageBox::warning(this, "保存失败", f.errorString());
        return;
    }
    QMessageBox::information(this, "已保存", "配置校验通过并已保存，重新启动程序后生效。");
}
