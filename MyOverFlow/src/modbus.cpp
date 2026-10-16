#include "modbus.h"
#include <cmath>
#include <cstring>
quint16 modbusCrc(const QByteArray &d) {
    quint16 c = 0xffff;
    for (unsigned char b : d) {
        c ^= b;
        for (int i = 0; i < 8; ++i)
            c = (c & 1) ? (c >> 1) ^ 0xa001 : c >> 1;
    }
    return c;
}
QByteArray readRequest(const Sensor &s) {
    QByteArray d;
    d.append(char(s.slave));
    d.append(char(s.function));
    d.append(char(s.reg >> 8));
    d.append(char(s.reg));
    d.append(char(s.count >> 8));
    d.append(char(s.count));
    auto c = modbusCrc(d);
    d.append(char(c));
    d.append(char(c >> 8));
    return d;
}
bool decodeValue(QByteArray b, const Sensor &s, double *v) {
    if (b.size() != s.count * 2)
        return false;
    if (b.size() == 4) {
        if (s.order == "CDAB")
            b = b.mid(2) + b.left(2);
        else if (s.order == "BADC") {
            std::swap(b.data()[0], b.data()[1]);
            std::swap(b.data()[2], b.data()[3]);
        } else if (s.order == "DCBA")
            std::reverse(b.begin(), b.end());
    }
    quint32 n = 0;
    for (unsigned char c : b)
        n = (n << 8) | c;
    if (s.format == "f32") {
        float f;
        std::memcpy(&f, &n, 4);
        *v = f;
    } else if (s.format == "s16")
        *v = qint16(n);
    else if (s.format == "s32")
        *v = qint32(n);
    else
        *v = n;
    *v = *v * s.scale + s.offset;
    return std::isfinite(*v);
}
ModbusBus::ModbusBus(int i, const Port &p, const QVector<Sensor> &s, QObject *parent)
    : QObject(parent), index_(i), cfg_(p) {
    for (auto x : s)
        if (x.port == i)
            sensors_ << x;
    due_.fill(0, sensors_.size());
    clock_.start();
    tick_.setInterval(10);
    timeout_.setSingleShot(true);
    connect(&tick_, &QTimer::timeout, this, &ModbusBus::poll);
    connect(&port_, &QSerialPort::readyRead, this, &ModbusBus::receive);
    connect(&timeout_, &QTimer::timeout, this, [this] {
        if (active_ < 0)
            return;
        if (attempt_++ < cfg_.retries) {
            port_.clear();
            rx_.clear();
            QTimer::singleShot(qMax(5, 40000 / cfg_.baud), this, [this] {
                if (active_ >= 0)
                    send();
            });
        } else
            finish("响应超时");
    });
    connect(&port_, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError e) {
        if (e == QSerialPort::ResourceError) {
            if (active_ >= 0)
                finish("串口断开");
            port_.close();
            quietUntil_ = clock_.elapsed() + 1000;
        }
    });
}
void ModbusBus::start() {
    tick_.start();
    poll();
}
void ModbusBus::stop() {
    tick_.stop();
    timeout_.stop();
    active_ = -1;
    port_.close();
}
void ModbusBus::poll() {
    if (active_ >= 0 || clock_.elapsed() < quietUntil_ || sensors_.isEmpty())
        return;
    if (!port_.isOpen()) {
        port_.setPortName(cfg_.device);
        port_.setBaudRate(cfg_.baud);
        port_.setDataBits(QSerialPort::Data8);
        port_.setParity(cfg_.parity == "E"   ? QSerialPort::EvenParity
                        : cfg_.parity == "O" ? QSerialPort::OddParity
                                             : QSerialPort::NoParity);
        port_.setStopBits(cfg_.stopBits == 2 ? QSerialPort::TwoStop : QSerialPort::OneStop);
        if (!port_.open(QIODevice::ReadWrite)) {
            for (auto s : sensors_)
                emit fault(s.id, "串口无法打开: " + port_.errorString());
            quietUntil_ = clock_.elapsed() + 3000;
            return;
        }
    }
    for (int n = 0; n < sensors_.size(); ++n) {
        int i = (cursor_ + n) % sensors_.size();
        if (due_[i] <= clock_.elapsed()) {
            active_ = i;
            cursor_ = (i + 1) % sensors_.size();
            attempt_ = 0;
            port_.clear();
            rx_.clear();
            send();
            return;
        }
    }
}
void ModbusBus::send() {
    if (port_.write(readRequest(sensors_[active_])) != 8) {
        finish("串口写失败");
        return;
    }
    timeout_.start(cfg_.timeout);
}
void ModbusBus::finish(const QString &e) {
    timeout_.stop();
    if (active_ < 0)
        return;
    auto s = sensors_[active_];
    due_[active_] = clock_.elapsed() + s.interval;
    active_ = -1;
    rx_.clear();
    // After timeout, quarantine late responses for another response timeout plus RTU gap.
    quietUntil_ = clock_.elapsed() +
                  (e.isEmpty() ? qMax(5, 40000 / cfg_.baud) : cfg_.timeout + qMax(5, 40000 / cfg_.baud));
    if (!e.isEmpty())
        emit fault(s.id, e);
}
void ModbusBus::receive() {
    auto d = port_.readAll();
    if (active_ < 0)
        return;
    rx_ += d;
    if (rx_.size() > 512) {
        finish("响应过长");
        return;
    }
    auto s = sensors_[active_];
    while (rx_.size() >= 5) {
        if (quint8(rx_[0]) != s.slave ||
            (quint8(rx_[1]) != s.function && quint8(rx_[1]) != (s.function | 0x80))) {
            rx_.remove(0, 1);
            continue;
        }
        bool ex = quint8(rx_[1]) & 0x80;
        int len = ex ? 5 : 5 + quint8(rx_[2]);
        if (len > 255) {
            rx_.remove(0, 1);
            continue;
        }
        if (rx_.size() < len)
            return;
        auto frame = rx_.left(len);
        auto crc = modbusCrc(frame.left(len - 2));
        if (quint8(frame[len - 2]) != (crc & 255) || quint8(frame[len - 1]) != (crc >> 8)) {
            rx_.remove(0, 1);
            continue;
        }
        if (ex) {
            finish(QString("Modbus 异常码 %1").arg(quint8(frame[2])));
            return;
        }
        double value;
        if (quint8(frame[2]) != s.count * 2 || !decodeValue(frame.mid(3, s.count * 2), s, &value)) {
            finish("寄存器长度或数值无效");
            return;
        }
        emit sample(s.id, value);
        finish();
        return;
    }
}
