/****************************************************************************
 * extserialport.cpp
 * --------------------------------------------------------------------------
 * 基于 Qt5 QSerialPort 重新实现 QextSerialComm / QextSerialMonitor，
 * 替代原来依赖的外部预编译库 libextSerialPort.a。
 * 跨平台支持 Windows (MinGW/MSVC) 与 Linux (含 RK3399 ARM)。
 * --------------------------------------------------------------------------
 * 作者：项目适配改造
 * 日期：2026-10-16
 ***************************************************************************/

#include "extserialport.h"
#include "qextserialport_global.h"

#include <QSerialPort>
#include <QSerialPortInfo>
#include <QDebug>

using namespace extSerialPort;

/* ========================================================================
 *  QextSerialMonitor —— 串口设备枚举（极简实现）
 * ======================================================================== */
QextSerialMonitor::QextSerialMonitor(QObject *parent)
    : QObject(parent), m_pExtSerialEnumerator(nullptr)
{
}

QextSerialMonitor::~QextSerialMonitor()
{
}

QList<QextPortInfo> QextSerialMonitor::getPorts()
{
    QList<QextPortInfo> list;
    const auto infos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : infos) {
        QextPortInfo p;
        p.portName   = info.portName();
        p.physName  = info.systemLocation();
        p.friendName = info.description();
        p.enumName   = info.manufacturer();
        p.vendorID   = info.hasVendorIdentifier() ? info.vendorIdentifier() : 0;
        p.productID  = info.hasProductIdentifier() ? info.productIdentifier() : 0;
        list.append(p);
    }
    return list;
}

/* ========================================================================
 *  QextSerialComm —— 串口通信封装（基于 QSerialPort）
 * ======================================================================== */

QextSerialComm::QextSerialComm(QObject *parent)
    : QObject(parent),
      m_siBaudRate(9600),
      m_pSerialPort(new QSerialPort(this)),
      m_bNeedPacket(false),
      m_usRecvDataSize(0),
      m_usRecvBuffSize(0),
      m_ucFrameLen(0)
{
    // 串口收到数据时转入 onRecvSerialData
    connect(m_pSerialPort, &QSerialPort::readyRead,
            this, &QextSerialComm::onRecvSerialData);
}

QextSerialComm::~QextSerialComm()
{
    if (m_pSerialPort && m_pSerialPort->isOpen()) {
        m_pSerialPort->close();
    }
}

bool QextSerialComm::isOpen()
{
    return m_pSerialPort && m_pSerialPort->isOpen();
}

void QextSerialComm::closeSerial()
{
    if (m_pSerialPort && m_pSerialPort->isOpen()) {
        m_pSerialPort->close();
    }
}

void QextSerialComm::clearSendBuffer()
{
    if (m_pSerialPort) m_pSerialPort->clear(QSerialPort::Output);
}

void QextSerialComm::clearRecvBuffer()
{
    if (m_pSerialPort) m_pSerialPort->clear(QSerialPort::Input);
    m_byteCacheData.clear();
}

void QextSerialComm::clearCommDataBuffer()
{
    m_queRecvData.clear();
    m_queSendData.clear();
    m_queSerialCommData.clear();
    m_byteCacheData.clear();
}

QByteArray QextSerialComm::readAll()
{
    if (!m_pSerialPort) return QByteArray();
    return m_pSerialPort->readAll();
}

int32_t QextSerialComm::bytesAvailable()
{
    if (!m_pSerialPort) return 0;
    return static_cast<int32_t>(m_pSerialPort->bytesAvailable());
}

int32_t QextSerialComm::getSerialCommTime(int32_t si_CommDataLength)
{
    // 估算单字节传输时间（ms）：10 bit / 波特率 * 1000
    if (m_siBaudRate <= 0) return 0;
    return (int32_t)(10000.0 / m_siBaudRate * si_CommDataLength);
}

void QextSerialComm::sendData(QByteArray baCommData)
{
    writeComData(baCommData);
}

void QextSerialComm::sendData(const char *p_ucSendData, const uint16_t us_sendBytes)
{
    if (!p_ucSendData || us_sendBytes == 0) return;
    writeComData(QByteArray::fromRawData(p_ucSendData, us_sendBytes));
}

void QextSerialComm::setRecvDataSize(uint16_t us_RecvDataSize)
{
    m_usRecvDataSize = us_RecvDataSize;
}

void QextSerialComm::setRecvBuffSize(uint16_t us_RecvBuffSize)
{
    m_usRecvBuffSize = us_RecvBuffSize;
}

void QextSerialComm::setRecvFrameFlag(uint8_t uc_HeadLen, uint8_t *p_ucFrameHead,
                                      uint8_t uc_TailLen, uint8_t *p_ucFrameTail)
{
    Q_UNUSED(uc_HeadLen);
    Q_UNUSED(p_ucFrameHead);
    Q_UNUSED(uc_TailLen);
    Q_UNUSED(p_ucFrameTail);
    // 组包功能由上层 ModbusRtu 实现，这里保留接口
    m_bNeedPacket = true;
}

void QextSerialComm::setRecvFrameFlag(uint8_t uc_FlagLen, uint8_t *p_ucFrameFlag,
                                       uint8_t uc_FrameLen)
{
    Q_UNUSED(uc_FlagLen);
    Q_UNUSED(p_ucFrameFlag);
    Q_UNUSED(uc_FrameLen);
    m_bNeedPacket = true;
}

void QextSerialComm::setEscapeChar(uint8_t uc_Escape, uint8_t uc_Special)
{
    Q_UNUSED(uc_Escape);
    Q_UNUSED(uc_Special);
}

int32_t QextSerialComm::getRecvData(uint8_t *p_ucRecvData, uint16_t us_maxGetBytes)
{
    if (!p_ucRecvData || us_maxGetBytes == 0) return 0;
    int32_t n = qMin<int32_t>(m_queRecvData.size(), us_maxGetBytes);
    for (int32_t i = 0; i < n; ++i) {
        p_ucRecvData[i] = m_queRecvData.dequeue();
    }
    return n;
}

bool QextSerialComm::openSerial(QString str_portName, PortSettings &stSerialCommPara,
                                QueryMode eQueryMode)
{
    m_polling = (eQueryMode == Polling);
    if (!m_pSerialPort) return false;

    m_pSerialPort->setPortName(str_portName);

    // 波特率
    m_siBaudRate = (int32_t)stSerialCommPara.BaudRate;
    m_pSerialPort->setBaudRate((qint32)stSerialCommPara.BaudRate);

    // 数据位
    switch (stSerialCommPara.DataBits) {
    case DATA_5: m_pSerialPort->setDataBits(QSerialPort::Data5); break;
    case DATA_6: m_pSerialPort->setDataBits(QSerialPort::Data6); break;
    case DATA_7: m_pSerialPort->setDataBits(QSerialPort::Data7); break;
    case DATA_8:
    default:     m_pSerialPort->setDataBits(QSerialPort::Data8); break;
    }

    // 校验位
    switch (stSerialCommPara.Parity) {
    case PAR_ODD:  m_pSerialPort->setParity(QSerialPort::OddParity);  break;
    case PAR_EVEN: m_pSerialPort->setParity(QSerialPort::EvenParity); break;
    case PAR_NONE:
    default:       m_pSerialPort->setParity(QSerialPort::NoParity);   break;
    }

    // 停止位
    switch (stSerialCommPara.StopBits) {
    case STOP_2: m_pSerialPort->setStopBits(QSerialPort::TwoStop); break;
    case STOP_1:
    default:     m_pSerialPort->setStopBits(QSerialPort::OneStop); break;
    }

    // 流控
    switch (stSerialCommPara.FlowControl) {
    case FLOW_HARDWARE: m_pSerialPort->setFlowControl(QSerialPort::HardwareControl); break;
    case FLOW_XONXOFF:  m_pSerialPort->setFlowControl(QSerialPort::SoftwareControl); break;
    case FLOW_OFF:
    default:            m_pSerialPort->setFlowControl(QSerialPort::NoFlowControl);   break;
    }

    if (!m_pSerialPort->open(QIODevice::ReadWrite)) {
        qWarning() << "open serial" << str_portName << "failed:"
                   << m_pSerialPort->errorString();
        return false;
    }
    return true;
}

void QextSerialComm::onRecvSerialData()
{
    if (!m_pSerialPort || m_polling) return;
    QByteArray ba = m_pSerialPort->readAll();
    if (ba.isEmpty()) return;

    // 缓存入字节队列
    for (uint8_t ch : ba) {
        if (m_queRecvData.size() >= MAX_SERIAL_BUFF_SIZE) m_queRecvData.dequeue();
        m_queRecvData.enqueue(ch);
    }

    emit sglReceiveData();
    // 直接把本帧数据整体抛给上层（object1/object2 的 onRecvCommData）
    emit sglRecvCommData(static_cast<uint16_t>(ba.size()),
                         reinterpret_cast<uint8_t *>(ba.data()));
}

void QextSerialComm::writeComData(QByteArray baCommData)
{
    if (!m_pSerialPort || !m_pSerialPort->isOpen()) return;
    qint64 n = m_pSerialPort->write(baCommData);
    if (n < 0) {
        qWarning() << "serial write failed:" << m_pSerialPort->errorString();
    }
}

/* 转义/还原（静态方法，未在业务代码中实际使用，保留实现满足链接） */
uint8_t QextSerialComm::dataRestore(uint8_t auc_Dest[], uint16_t *us_DestLen,
                                    uint8_t auc_Srt[], uint16_t us_SrtLen)
{
    if (!auc_Dest || !us_DestLen || !auc_Srt || us_SrtLen == 0) return 0x55;
    uint16_t j = 0;
    for (uint16_t i = 0; i < us_SrtLen; ++i) {
        auc_Dest[j++] = auc_Srt[i];
    }
    *us_DestLen = j;
    return 0xaa;
}

uint8_t QextSerialComm::dataTranslate(uint8_t auc_Dest[], uint16_t *us_DestLen,
                                      uint8_t auc_Srt[], uint16_t us_SrtLen)
{
    if (!auc_Dest || !us_DestLen || !auc_Srt || us_SrtLen == 0) return 0x55;
    for (uint16_t i = 0; i < us_SrtLen; ++i) {
        auc_Dest[i] = auc_Srt[i];
    }
    *us_DestLen = us_SrtLen;
    return 0xaa;
}

bool QextSerialComm::waitForReadyRead(int ms){return m_pSerialPort->waitForReadyRead(ms);}
bool QextSerialComm::waitForBytesWritten(int ms){return !m_pSerialPort->bytesToWrite() || m_pSerialPort->waitForBytesWritten(ms);}
qint64 QextSerialComm::writeChecked(const QByteArray &b){return m_pSerialPort->write(b);}
QString QextSerialComm::lastError() const{return m_pSerialPort->errorString();}
bool QextSerialComm::hasFatalError() const {
    const auto e = m_pSerialPort->error();
    return e == QSerialPort::ResourceError || e == QSerialPort::DeviceNotFoundError ||
           e == QSerialPort::PermissionError || e == QSerialPort::NotOpenError;
}
