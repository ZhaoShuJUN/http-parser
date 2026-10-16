#include "dialogconfigdev.h"
#include "ConfigInfo/appcfginfo.h"
#include "ui_dialogconfigdev.h"
namespace {
void fill(QStandardItemModel *m, int row, const QStringList &v) {
    for (int c = 0; c < v.size(); ++c)
        m->setItem(row, c, new QStandardItem(v[c]));
}
QString text(QStandardItemModel *m, int r, int c) {
    return m->item(r, c) ? m->item(r, c)->text().trimmed() : QString();
}
void view(QTableView *v, QStandardItemModel *m) {
    v->setModel(m);
    v->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    v->setAlternatingRowColors(true);
    v->setSelectionBehavior(QAbstractItemView::SelectRows);
}
} // namespace
DialogConfigDev::DialogConfigDev(QWidget *p)
    : QDialog(p), ui(new Ui::DialogConfigDev), m_editing(appCfgInfoInstance->settings()) {
    ui->setupUi(this);
    resize(1180, 740);
    QString error;
    MonitorSettings disk;
    if (readMonitorIni(m_editing.path, &disk, &error))
        m_editing = disk;
    m_uart = new QStandardItemModel(this);
    m_dev = new QStandardItemModel(this);
    m_meas = new QStandardItemModel(this);
    m_letter = new QStandardItemModel(this);
    m_cameras = new QStandardItemModel(this);
    view(ui->tableViewUart, m_uart);
    view(ui->tableViewDev, m_dev);
    view(ui->tableViewMeas, m_meas);
    view(ui->tableViewLetter, m_letter);
    ui->lineEditUart->setValidator(new QIntValidator(1, 8, this));
    ui->lineEditDev->setValidator(new QIntValidator(1, 200, this));
    ui->lineEditMeas->setReadOnly(true);
    ui->lineEditLetter->setReadOnly(true);
    ui->tableViewMeas->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableViewLetter->setEditTriggers(QAbstractItemView::NoEditTriggers);
    auto cl = new QVBoxLayout(ui->cameraPage);
    cl->addWidget(new QLabel("最多 8 路。密码仅填写环境变量名，PTZ 通道与 GB 20 位编号是不同配置。"));
    m_cameraCount = new QSpinBox;
    m_cameraCount->setRange(0, 8);
    m_cameraCount->setValue(m_editing.cameras.size());
    cl->addWidget(m_cameraCount);
    auto cv = new QTableView;
    view(cv, m_cameras);
    cl->addWidget(cv);
    connect(m_cameraCount, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { cameraRows(); });
    cameraRows();
    buildForm(ui->mqttPage, "mqtt", m_editing.mqtt,
              {{"enabled", "启用 MQTT"},
               {"authUrl", "HTTP 鉴权地址"},
               {"gatewaySN", "网关序列号"},
               {"checkIdEnv", "checkID 环境变量名"},
               {"manufacturerId", "厂商编码"},
               {"model", "网关型号"},
               {"sensorModel", "子设备型号"},
               {"qos", "QoS（0 或 1）"},
               {"reportMs", "上报周期 ms"},
               {"tls", "启用 TLS"},
               {"caFile", "TLS CA 文件"},
               {"username", "MQTT 用户名"},
               {"passwordEnv", "MQTT 密码环境变量名"}});
    buildForm(ui->recordingPage, "recording", m_editing.recording,
              {{"enabled", "启用全部摄像头录像"},
               {"directory", "外接硬盘录像目录"},
               {"requireMount", "要求独立挂载硬盘"},
               {"maxGiB", "本程序录像总配额 GiB"},
               {"reserveMiB", "磁盘预留空间 MiB"},
               {"segmentSeconds", "分段长度 秒"},
               {"ffmpeg", "FFmpeg 可执行文件"}});
    buildForm(ui->gbPage, "gb", m_editing.gb,
              {{"enabled", "启用 GB28181"},
               {"charset", "XML 编码（GB2312 / UTF-8）"},
               {"version", "版本（当前仅 2016 UDP）"},
               {"deviceId", "本机设备编码（20 位）"},
               {"serverId", "平台编码（20 位）"},
               {"domain", "SIP 域"},
               {"serverAddress", "平台 IPv4 地址"},
               {"serverPort", "平台 SIP 端口"},
               {"localAddress", "本机可达 IPv4 地址"},
               {"localPort", "本机 SIP 端口"},
               {"passwordEnv", "认证密码环境变量名"},
               {"expires", "注册有效期 秒"},
               {"heartbeatSeconds", "心跳周期 秒"},
               {"fps", "GB H.264 固定帧率"}});
    SetRefreshUartConfig();
    SetRefreshDevConfig();
    SetReportLetterConfig();
    SetReportMeasConfig();
    connect(ui->lineEditUart, &QLineEdit::editingFinished, this, &DialogConfigDev::setCount);
    connect(ui->lineEditDev, &QLineEdit::editingFinished, this, &DialogConfigDev::setCount);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
DialogConfigDev::~DialogConfigDev() {
    delete ui;
}
void DialogConfigDev::buildForm(QWidget *page, const QString &prefix, const QJsonObject &values,
                                const QMap<QString, QString> &labels) {
    auto outer = new QVBoxLayout(page);
    auto scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto host = new QWidget;
    auto form = new QFormLayout(host);
    for (auto it = values.begin(); it != values.end(); ++it) {
        QWidget *field = nullptr;
        if (it.value().isBool()) {
            auto x = new QCheckBox;
            x->setChecked(it.value().toBool());
            field = x;
        } else {
            auto x = new QLineEdit;
            x->setText(it.value().isString() ? it.value().toString()
                                             : QString::number(it.value().toDouble(), 'g', 12));
            field = x;
        }
        m_fields[prefix + "/" + it.key()] = field;
        form->addRow(labels.value(it.key(), it.key()), field);
    }
    scroll->setWidget(host);
    outer->addWidget(scroll);
}
QJsonObject DialogConfigDev::readForm(const QString &prefix, const QJsonObject &base) const {
    auto o = base;
    for (auto it = base.begin(); it != base.end(); ++it) {
        auto w = m_fields.value(prefix + "/" + it.key());
        if (auto b = qobject_cast<QCheckBox *>(w))
            o[it.key()] = b->isChecked();
        else if (auto l = qobject_cast<QLineEdit *>(w)) {
            if (it.value().isDouble()) {
                bool ok = false;
                double n = l->text().toDouble(&ok);
                if (!ok || !qIsFinite(n))
                    throw QString("参数不是有效数字: " + it.key());
                o[it.key()] = n;
            } else
                o[it.key()] = l->text().trimmed();
        }
    }
    return o;
}
void DialogConfigDev::SetRefreshUartConfig() {
    m_uart->setHorizontalHeaderLabels(
        {"TTY 路径", "波特率", "校验 0无/1奇/2偶", "停止位 0=1/2=2", "超时 ms", "重试次数"});
    m_uart->setRowCount(m_editing.ports.size());
    for (int i = 0; i < m_editing.ports.size(); ++i) {
        auto p = m_editing.ports[i];
        fill(m_uart, i,
             {p.device, QString::number(p.baud), QString::number(p.parity), QString::number(p.stopBits),
              QString::number(p.timeoutMs), QString::number(p.retries)});
    }
    ui->lineEditUart->setText(QString::number(m_editing.ports.size()));
}
void DialogConfigDev::GetRefreshUartConfig() {
    QVector<PortSetting> ports;
    for (int i = 0; i < m_uart->rowCount(); ++i) {
        PortSetting p;
        p.device = text(m_uart, i, 0);
        int *fields[] = {&p.baud, &p.parity, &p.stopBits, &p.timeoutMs, &p.retries};
        for (int c = 1; c <= 5; ++c) {
            bool ok = false;
            *fields[c - 1] = text(m_uart, i, c).toInt(&ok);
            if (!ok)
                throw QString("串口第 %1 行存在无效整数").arg(i + 1);
        }
        ports << p;
    }
    m_editing.ports = ports;
}
void DialogConfigDev::SetRefreshDevConfig() {
    m_dev->setHorizontalHeaderLabels({"ID",           "名称",       "类别",         "单位",
                                      "串口索引0..7", "从站1..247", "寄存器零基",   "类型",
                                      "32位序",       "倍率",       "偏移",         "下限",
                                      "上限",         "回差",       "周期ms",       "联动摄像头-1/0..7",
                                      "预置位",       "LocalId",    "DeviceId可选", "NodeId可选",
                                      "数值属性",     "报警属性",   "服务标识",     "已标定0/1"});
    m_dev->setRowCount(m_editing.sensors.size());
    for (int i = 0; i < m_editing.sensors.size(); ++i) {
        auto s = m_editing.sensors[i];
        fill(m_dev, i,
             {s.id,
              s.name,
              s.category,
              s.unit,
              QString::number(s.port),
              QString::number(s.slave),
              QString::number(s.reg),
              s.format,
              s.order,
              QString::number(s.scale, 'g', 12),
              QString::number(s.offset, 'g', 12),
              QString::number(s.low, 'g', 12),
              QString::number(s.high, 'g', 12),
              QString::number(s.hysteresis, 'g', 12),
              QString::number(s.intervalMs),
              QString::number(s.camera),
              QString::number(s.preset),
              s.localId,
              s.deviceId,
              s.nodeId,
              s.valueKey,
              s.alarmKey,
              s.serviceId,
              s.calibrated ? "1" : "0"});
    }
    ui->lineEditDev->setText(QString::number(m_editing.sensors.size()));
}
void DialogConfigDev::GetRefreshDevConfig() {
    QVector<SensorSetting> sensors;
    for (int i = 0; i < m_dev->rowCount(); ++i) {
        SensorSetting s;
        s.id = text(m_dev, i, 0);
        s.name = text(m_dev, i, 1);
        s.category = text(m_dev, i, 2);
        s.unit = text(m_dev, i, 3);
        s.format = text(m_dev, i, 7);
        s.order = text(m_dev, i, 8);
        s.localId = text(m_dev, i, 17);
        s.deviceId = text(m_dev, i, 18);
        s.nodeId = text(m_dev, i, 19);
        s.valueKey = text(m_dev, i, 20);
        s.alarmKey = text(m_dev, i, 21);
        s.serviceId = text(m_dev, i, 22);
        s.calibrated = text(m_dev, i, 23) == "1";
        int cols[] = {4, 5, 6, 14, 15, 16};
        int *ints[] = {&s.port, &s.slave, &s.reg, &s.intervalMs, &s.camera, &s.preset};
        for (int j = 0; j < 6; ++j) {
            bool ok = false;
            *ints[j] = text(m_dev, i, cols[j]).toInt(&ok);
            if (!ok)
                throw QString("传感器第 %1 行整数参数无效").arg(i + 1);
        }
        double *numbers[] = {&s.scale, &s.offset, &s.low, &s.high, &s.hysteresis};
        for (int j = 0; j < 5; ++j) {
            bool ok = false;
            *numbers[j] = text(m_dev, i, 9 + j).toDouble(&ok);
            if (!ok || !qIsFinite(*numbers[j]))
                throw QString("传感器第 %1 行数值参数无效").arg(i + 1);
        }
        s.count = s.format.endsWith("32") ? 2 : 1;
        sensors << s;
    }
    m_editing.sensors = sensors;
}
void DialogConfigDev::SetReportLetterConfig() {
    m_letter->setHorizontalHeaderLabels({"缓存索引", "传感器", "报警属性"});
    m_letter->setRowCount(m_editing.sensors.size());
    for (int i = 0; i < m_editing.sensors.size(); ++i)
        fill(m_letter, i, {QString::number(i), m_editing.sensors[i].name, m_editing.sensors[i].alarmKey});
    ui->lineEditLetter->setText(QString::number(m_editing.sensors.size()));
}
void DialogConfigDev::SetReportMeasConfig() {
    m_meas->setHorizontalHeaderLabels({"缓存索引", "传感器", "数值属性"});
    m_meas->setRowCount(m_editing.sensors.size());
    for (int i = 0; i < m_editing.sensors.size(); ++i)
        fill(m_meas, i, {QString::number(i), m_editing.sensors[i].name, m_editing.sensors[i].valueKey});
    ui->lineEditMeas->setText(QString::number(m_editing.sensors.size()));
}
void DialogConfigDev::GetReportLetterConfig() {}
void DialogConfigDev::GetReportMeasConfig() {}
void DialogConfigDev::cameraRows() {
    m_cameras->setHorizontalHeaderLabels({"启用0/1", "名称", "RTSP 地址", "ISAPI 地址", "用户名",
                                          "密码环境变量名", "PTZ 通道", "GB 20 位通道 ID"});
    int old = m_cameras->rowCount();
    m_cameras->setRowCount(m_cameraCount->value());
    for (int i = old; i < m_cameras->rowCount(); ++i) {
        CameraSetting c;
        if (i < m_editing.cameras.size())
            c = m_editing.cameras[i];
        else {
            c.name = "摄像头" + QString::number(i + 1);
            c.rtsp = "rtsp://192.168.1." + QString::number(101 + i) + "/Streaming/Channels/101";
            c.isapi = "http://192.168.1." + QString::number(101 + i);
            c.user = "admin";
            c.passwordEnv = "CAMERA_" + QString::number(i + 1) + "_PASSWORD";
            c.channelId = QString("340200000013200000%1").arg(i + 1, 2, 10, QChar('0'));
        }
        fill(m_cameras, i,
             {c.enabled ? "1" : "0", c.name, c.rtsp, c.isapi, c.user, c.passwordEnv,
              QString::number(c.channel), c.channelId});
    }
}
void DialogConfigDev::setCount() {
    try {
        GetRefreshUartConfig();
        GetRefreshDevConfig();
        int p = ui->lineEditUart->text().toInt(), n = ui->lineEditDev->text().toInt();
        if (p < 1 || p > 8 || n < 1 || n > 200)
            throw QString("数量超出范围");
        while (m_editing.ports.size() < p) {
            PortSetting port;
            port.device = "/dev/ttyUSB" + QString::number(m_editing.ports.size());
            m_editing.ports << port;
        }
        m_editing.ports.resize(p);
        while (m_editing.sensors.size() < n) {
            SensorSetting s;
            s.id = "sensor" + QString::number(m_editing.sensors.size() + 1);
            s.name = s.id;
            s.category = "其他";
            s.localId = s.id;
            s.slave = qMin(247, m_editing.sensors.size() + 1);
            m_editing.sensors << s;
        }
        m_editing.sensors.resize(n);
        SetRefreshUartConfig();
        SetRefreshDevConfig();
        SetReportLetterConfig();
        SetReportMeasConfig();
    } catch (const QString &e) {
        QMessageBox::warning(this, "参数错误", e);
    }
}
void DialogConfigDev::on_buttonBox_accepted() {
    try {
        GetRefreshUartConfig();
        GetRefreshDevConfig();
        m_editing.cameras.clear();
        for (int i = 0; i < m_cameras->rowCount(); ++i) {
            CameraSetting c;
            c.enabled = text(m_cameras, i, 0) == "1";
            c.name = text(m_cameras, i, 1);
            c.rtsp = text(m_cameras, i, 2);
            c.isapi = text(m_cameras, i, 3);
            c.user = text(m_cameras, i, 4);
            c.passwordEnv = text(m_cameras, i, 5);
            bool ok = false;
            c.channel = text(m_cameras, i, 6).toInt(&ok);
            if (!ok)
                throw QString("摄像头 PTZ 通道不是整数");
            c.channelId = text(m_cameras, i, 7);
            m_editing.cameras << c;
        }
        m_editing.mqtt = readForm("mqtt", m_editing.mqtt);
        m_editing.recording = readForm("recording", m_editing.recording);
        m_editing.gb = readForm("gb", m_editing.gb);
        QString error;
        if (!writeMonitorIni(m_editing.path, m_editing, &error))
            throw error;
        QMessageBox::information(this, "已保存", "INI 配置已校验保存，重新启动程序后生效。");
        accept();
    } catch (const QString &e) {
        QMessageBox::warning(this, "保存失败", e);
    }
}
void DialogConfigDev::on_lineEditUart_textChanged(const QString &s) {
    Q_UNUSED(s);
}
void DialogConfigDev::on_lineEditDev_textChanged(const QString &s) {
    Q_UNUSED(s);
}
void DialogConfigDev::on_lineEditLetter_textChanged(const QString &s) {
    Q_UNUSED(s);
}
void DialogConfigDev::on_lineEditMeas_textChanged(const QString &s) {
    Q_UNUSED(s);
}
