#include "qmainmodbusrtu.h"
#include <algorithm>
#include <cmath>
#include <cstring>
Q_GLOBAL_STATIC(QMainModbusRtu, mainModbusInstance)
QMainModbusRtu::QMainModbusRtu(QObject *p) : QObject(p) {
    std::memset(ModbusRegWordData, 0, sizeof(ModbusRegWordData));
    std::memset(ModbusRegCommErrTime, 0, sizeof(ModbusRegCommErrTime));
    std::memset(ModbusRegCommErrBit, 1, sizeof(ModbusRegCommErrBit));
}
QMainModbusRtu::~QMainModbusRtu() {}
QMainModbusRtu *QMainModbusRtu::instance() {
    return mainModbusInstance();
}
quint16 QMainModbusRtu::crc(const QByteArray &data) {
    quint16 c = 0xffff;
    for (quint8 b : data) {
        c ^= b;
        for (int n = 0; n < 8; ++n)
            c = (c & 1) ? (c >> 1) ^ 0xa001 : c >> 1;
    }
    return c;
}
unsigned short QMainModbusRtu::Crc16(unsigned char *p, unsigned short n) {
    return crc(QByteArray(reinterpret_cast<char *>(p), n));
}
QByteArray QMainModbusRtu::readRequest(int slave, int reg, int count) {
    if (slave < 1 || slave > 247 || reg < 0 || count < 1 || count > 125 || reg + count > 65536)
        return {};
    QByteArray b;
    b.append(char(slave));
    b.append(char(3));
    b.append(char(reg >> 8));
    b.append(char(reg));
    b.append(char(count >> 8));
    b.append(char(count));
    auto c = crc(b);
    b.append(char(c));
    b.append(char(c >> 8));
    return b;
}
bool QMainModbusRtu::decode(const QVector<quint16> &words, const SensorSetting &s, double *v) {
    if (words.size() != s.count)
        return false;
    QByteArray b;
    for (auto w : words) {
        b.append(char(w >> 8));
        b.append(char(w));
    }
    if (b.size() == 4) {
        if (s.order == "CDAB")
            b = b.mid(2) + b.left(2);
        else if (s.order == "BADC") {
            std::swap(b.data()[0], b.data()[1]);
            std::swap(b.data()[2], b.data()[3]);
        } else if (s.order == "DCBA")
            std::reverse(b.begin(), b.end());
    }
    quint32 raw = 0;
    for (quint8 c : b)
        raw = (raw << 8) | c;
    if (s.format == "f32") {
        float f;
        std::memcpy(&f, &raw, 4);
        *v = f;
    } else if (s.format == "s16")
        *v = qint16(raw);
    else if (s.format == "s32")
        *v = qint32(raw);
    else
        *v = raw;
    *v = *v * s.scale + s.offset;
    return std::isfinite(*v);
}
void QMainModbusRtu::cacheResponse(int addr, int reg, const QVector<quint16> &words) {
    if (addr < 1 || addr >= MODBUS_SLAVE_COUNT)
        return;
    for (auto v : words) {
        if (reg >= 0 && reg < MAINMODBUSWORDMAXBUF)
            ModbusRegWordData[addr][reg] = v;
        ++reg;
    }
    ModbusRegCommErrTime[addr] = 0;
    ModbusRegCommErrBit[addr] = 0;
}
unsigned short QMainModbusRtu::getUartBufData(unsigned short a, unsigned short r) {
    return a < MODBUS_SLAVE_COUNT && r < MAINMODBUSWORDMAXBUF ? ModbusRegWordData[a][r] : 0;
}
void QMainModbusRtu::setUartBufData(unsigned short a, unsigned short r, unsigned short v) {
    if (a < MODBUS_SLAVE_COUNT && r < MAINMODBUSWORDMAXBUF)
        ModbusRegWordData[a][r] = v;
}
void QMainModbusRtu::UartSendNumb(unsigned char *b, unsigned short n) {
    if (n <= MAINUARTTXMAX)
        emit requestReady(QByteArray(reinterpret_cast<char *>(b), n));
}
void QMainModbusRtu::SendReadDataCommand(unsigned char a, unsigned short r, unsigned short n) {
    NowOperRegAddr = r;
    emit requestReady(readRequest(a, r, n));
}
void QMainModbusRtu::SendWriteDataCommand(unsigned char a, unsigned short r, unsigned short n) {
    if (a < 1 || a > 247 || r >= MAINMODBUSWORDMAXBUF || n < 1 || n > 123 || r + n > MAINMODBUSWORDMAXBUF)
        return;
    QByteArray b;
    b.append(char(a));
    b.append(char(n == 1 ? 6 : 16));
    b.append(char(r >> 8));
    b.append(char(r));
    if (n > 1) {
        b.append(char(n >> 8));
        b.append(char(n));
        b.append(char(n * 2));
    }
    for (int i = 0; i < n; ++i) {
        auto v = getUartBufData(a, r + i);
        b.append(char(v >> 8));
        b.append(char(v));
    }
    auto c = crc(b);
    b.append(char(c));
    b.append(char(c >> 8));
    NowOperRegAddr = r;
    emit requestReady(b);
}
void QMainModbusRtu::ReadRegisters(unsigned char addr) {
    if (UartRxNumb < 7)
        return;
    QVector<quint16> words;
    int n = UartRxBuf[2];
    if (n % 2 || unsigned(n + 5) != UartRxNumb)
        return;
    for (int i = 0; i < n; i += 2)
        words << (quint16(UartRxBuf[3 + i]) << 8 | UartRxBuf[4 + i]);
    cacheResponse(addr, NowOperRegAddr, words);
}
void QMainModbusRtu::HostModbusReadReg() {
    ReadRegisters(UartRxBuf[0]);
}
void QMainModbusRtu::HostModbusWriteOneReg() {
    if (UartRxBuf[0] < MODBUS_SLAVE_COUNT) {
        ModbusRegCommErrTime[UartRxBuf[0]] = 0;
        ModbusRegCommErrBit[UartRxBuf[0]] = 0;
    }
}
void QMainModbusRtu::HostModbusWriteMoreReg() {
    HostModbusWriteOneReg();
}
void QMainModbusRtu::ModbusRealRunTime() {
    for (int i = 1; i < MODBUS_SLAVE_COUNT; ++i)
        if (++ModbusRegCommErrTime[i] >= MODBUSREGCOMMERRTIME) {
            ModbusRegCommErrTime[i] = MODBUSREGCOMMERRTIME;
            ModbusRegCommErrBit[i] = 1;
        }
}
void QMainModbusRtu::ModbusCheckCommModbus() {
    if (!UartRxOverBit)
        return;
    UartRxOverBit = 0;
    if (UartRxNumb < 5 || UartRxNumb > MAINUARTRXMAX)
        return;
    auto n = UartRxNumb;
    quint16 c = Crc16(UartRxBuf, n - 2);
    if (UartRxBuf[n - 2] != (c & 255) || UartRxBuf[n - 1] != (c >> 8))
        return;
    switch (UartRxBuf[1]) {
    case 3:
        HostModbusReadReg();
        break;
    case 6:
        HostModbusWriteOneReg();
        break;
    case 16:
        HostModbusWriteMoreReg();
        break;
    default:
        break;
    }
}
