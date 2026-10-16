#include "object1.h"
#include <QThread>
QObject1::QObject1(QObject *p)
    : QObject(p), m_pExtSerialComm(new QextSerialComm(this)), m_master(new QMainModbusRtu(this)) {
    connect(m_master, &QMainModbusRtu::requestReady, this, &QObject1::sendCommData);
    connect(m_pExtSerialComm, &QextSerialComm::sglRecvCommData, this, &QObject1::onRecvCommData);
}
QObject1::~QObject1() {
    CloseCommPort();
}
void QObject1::OpenCommPort(QString name, int baud, int parity, int stop) {
    if (m_pExtSerialComm->isOpen())
        return;
    PortSettings p{};
    p.BaudRate = static_cast<BaudRateType>(baud);
    p.DataBits = DATA_8;
    p.Parity = parity == 1 ? PAR_ODD : parity == 2 ? PAR_EVEN : PAR_NONE;
    p.StopBits = stop == 2 ? STOP_2 : STOP_1;
    p.FlowControl = FLOW_OFF;
    p.Timeout_Millisec = 10;
    if (!m_pExtSerialComm->openSerial(name, p, Polling))
        m_ucCommStatus = 0x55;
}
void QObject1::CloseCommPort() {
    m_pExtSerialComm->closeSerial();
    m_ucCommStatus = 0x55;
}
uint8_t QObject1::getCommStatus() {
    return m_ucCommStatus;
}
void QObject1::sendCommData(QByteArray b) {
    if (!b.isEmpty())
        m_pExtSerialComm->sendData(b);
}
void QObject1::onRecvCommData(uint16_t n, uint8_t *b) {
    emit sglObject1Info(QString::fromLatin1(QByteArray(reinterpret_cast<char *>(b), n).toHex()));
}
AcquisitionResult QObject1::pollSensor(const SensorSetting &s, const PortSetting &p) {
    AcquisitionResult result;
    result.id = s.id;
    result.time = monitorUtcNow();
    OpenCommPort(p.device, p.baud, p.parity, p.stopBits);
    if (!m_pExtSerialComm->isOpen()) {
        result.error = "串口打开失败: " + m_pExtSerialComm->lastError();
        return result;
    }
    auto request = QMainModbusRtu::readRequest(s.slave, s.reg, s.count);
    if (request.isEmpty()) {
        result.error = "读寄存器参数无效";
        return result;
    }
    for (int attempt = 0; attempt <= p.retries; ++attempt) {
        if (QThread::currentThread()->isInterruptionRequested()) {
            result.error = "采集停止";
            return result;
        }
        m_pExtSerialComm->clearRecvBuffer();
        QThread::msleep(qMax(4, 40000 / p.baud));
        if (m_pExtSerialComm->writeChecked(request) != request.size() ||
            !m_pExtSerialComm->waitForBytesWritten(p.timeoutMs)) {
            result.error = "串口写入失败";
            CloseCommPort();
            return result;
        }
        QByteArray bytes;
        QElapsedTimer deadline;
        deadline.start();
        while (deadline.elapsed() < p.timeoutMs && !QThread::currentThread()->isInterruptionRequested()) {
            if (m_pExtSerialComm->bytesAvailable() ||
                m_pExtSerialComm->waitForReadyRead(qBound(1, p.timeoutMs - int(deadline.elapsed()), 50)))
                bytes += m_pExtSerialComm->readAll();
            if (m_pExtSerialComm->hasFatalError()) {
                result.error = "串口断开: " + m_pExtSerialComm->lastError();
                CloseCommPort();
                return result;
            }
            if (bytes.size() > 512) {
                bytes.clear();
                result.error = "响应帧过长";
                break;
            }
            while (bytes.size() >= 5) {
                if (quint8(bytes[0]) != s.slave || (quint8(bytes[1]) != 3 && quint8(bytes[1]) != 0x83)) {
                    bytes.remove(0, 1);
                    continue;
                }
                bool exception = quint8(bytes[1]) == 0x83;
                int length = exception ? 5 : 5 + quint8(bytes[2]);
                if (length > 255) {
                    bytes.remove(0, 1);
                    continue;
                }
                if (bytes.size() < length)
                    break;
                auto frame = bytes.left(length);
                auto crc = QMainModbusRtu::crc(frame.left(length - 2));
                if (quint8(frame[length - 2]) != (crc & 255) || quint8(frame[length - 1]) != (crc >> 8)) {
                    bytes.remove(0, 1);
                    continue;
                }
                if (exception) {
                    result.error = QString("Modbus 异常码 %1").arg(quint8(frame[2]));
                    m_ucCommStatus = 0x55;
                    return result;
                }
                if (quint8(frame[2]) != s.count * 2) {
                    result.error = "响应寄存器长度不符";
                    m_ucCommStatus = 0x55;
                    return result;
                }
                for (int i = 0; i < s.count; ++i)
                    result.registers << (quint16(quint8(frame[3 + i * 2])) << 8 | quint8(frame[4 + i * 2]));
                m_master->cacheResponse(s.slave, s.reg, result.registers);
                result.time = monitorUtcNow();
                m_ucCommStatus = 0xaa;
                return result;
            }
        }
        // RTU has no transaction ID. Quarantine late replies before retry/next request.
        for (int waited = 0; waited < p.timeoutMs && !QThread::currentThread()->isInterruptionRequested();
             waited += 20)
            QThread::msleep(20);
    }
    m_ucCommStatus = 0x55;
    result.error = "响应超时或 CRC 错误";
    result.time = monitorUtcNow();
    return result;
}
