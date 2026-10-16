#ifndef EXTSERIALPORT_H
#define EXTSERIALPORT_H

#include <QQueue>
#include <QObject>
#include <QByteArray>
#include <QGlobalStatic>
#include "qextserialport_global.h"
#include <QSerialPort>

QT_BEGIN_NAMESPACE
#define MAX_SERIAL_BUFF_SIZE 2048

//单字节对齐
#pragma pack(1)
//串口通信数据定义
typedef struct
{
   uint16_t us_DataLength;//数据长度
   uint8_t auc_DataArray[MAX_SERIAL_BUFF_SIZE];/*数据缓冲区大小*/
}st_SerialCommData,*pst_SerialCommData;
#pragma pack()

using namespace extSerialPort;

/*串口监测*/
class QextSerialEnumerator;
class QEXTSERIALPORT_EXPORT QextSerialMonitor : public QObject
{
    Q_OBJECT
    Q_DECLARE_PRIVATE(QObject);

public:
    ~QextSerialMonitor();
    QextSerialMonitor(QObject *parent = nullptr);

    QList<QextPortInfo> getPorts();

Q_SIGNALS:
    void sglRemoveDevice(const QextPortInfo &info);
    void sglDiscoverDevice(const QextPortInfo &info);

private:
    QextSerialEnumerator *m_pExtSerialEnumerator;
};

/*注：在子线程（run函数中定义）中使用该动态库时，不能通过信号槽的方式接收数据，
只能通过周期调用bytesAvailable/readAll获取*/
// QextSerialPort 已由 Qt5 QSerialPort 替代
class QEXTSERIALPORT_EXPORT QextSerialComm : public QObject
{
    Q_OBJECT
    Q_DECLARE_PRIVATE(QObject);

public:
    ~QextSerialComm();
    QextSerialComm(QObject *parent = nullptr);

    bool waitForReadyRead(int ms);
    bool waitForBytesWritten(int ms);
    qint64 writeChecked(const QByteArray &data);
    QString lastError() const;
    bool hasFatalError() const;
    bool isOpen();
    void closeSerial();
    void clearSendBuffer();
    void clearRecvBuffer();
    void clearCommDataBuffer();
    QByteArray readAll();
    int32_t bytesAvailable();
    int32_t getSerialCommTime(int32_t si_CommDataLength);

    void sendData(QByteArray baCommData);
    void setRecvDataSize(uint16_t us_RecvDataSize);
    void setRecvBuffSize(uint16_t us_RecvBuffSize);
    void setRecvFrameFlag(uint8_t uc_HeadLen,uint8_t *p_ucFrameHead,
                          uint8_t uc_TailLen,uint8_t *p_ucFrameTail);
    /*设置转义字符*/
    void setEscapeChar(uint8_t uc_Escape=0xff,uint8_t uc_Special=0xfd);

    /*获得接收数据。返回字节数*/
    int32_t getRecvData(uint8_t *p_ucRecvData, uint16_t us_maxGetBytes);
    /*打开串口*/
    bool openSerial(QString str_portName,PortSettings &stSerialCommPara,QueryMode eQueryMode=EventDriven);
    /*发送数据*/
    void sendData(const char *p_ucSendData, const uint16_t us_sendBytes);
    /*uc_FlagLen-数据帧标志长度 p_ucFrameFlag-数据帧标志 uc_FrameLen-数据帧长度（含帧标志）*/
    void setRecvFrameFlag(uint8_t uc_FlagLen,uint8_t *p_ucFrameFlag,uint8_t uc_FrameLen);

    /**
     * @brief 转译数据还原
    * @param  aus_Dest[] : 还原后的数据，空间应不小于待还原的字节流长度
    * @param  destLen:		还原后的数据长度
    * @param  auc_Srt:		待还原的数据
    * @param  srcLen：		待还原的数据长度
    * @return 0xaa-还原正确，0x55-数据还原错误
     */
    static uint8_t dataRestore(uint8_t auc_Dest[], uint16_t* us_DestLen, uint8_t auc_Srt[], uint16_t us_SrtLen);
    /**
     * @brief 将待处理的字节进行转义处理
    * @param  auc_Dest[]: 转义处理后的数据存储区，需保证大于待处理的数据长度至少1/200
    * @param  us_DestLen: 转义处理后的数据长度
    * @param  auc_Str[]:  待转义处理的数据存储区
    * @param  us_StrLen:  待转义处理的数据长度
    * @return  0xaa-成功，0x55-失败
     */
    static uint8_t dataTranslate(uint8_t auc_Dest[], uint16_t* us_DestLen, uint8_t auc_Srt[], uint16_t us_SrtLen);

Q_SIGNALS:
    void sglReceiveData();
    void sglRecvCommData(uint16_t us_DataLen,uint8_t *p_ucCommData);
    void sglRecvCommFrame(uint16_t us_FrameLen,uint8_t *p_ucDataFrame);

private Q_SLOTS:
    void onRecvSerialData();
    void writeComData(QByteArray baCommData);

private:
    bool m_polling = false;
    int32_t m_siBaudRate;
    QSerialPort *m_pSerialPort;

    /*comm data queue*/
    bool m_bNeedPacket;/*是否需要组包*/
    uint16_t m_usRecvDataSize;
    uint16_t m_usRecvBuffSize;

    QByteArray m_byteHeadArray;
    QByteArray m_byteTailArray;

    uint8_t m_ucFrameLen;
    QByteArray m_byteFrameFlag;

    QByteArray m_byteCacheData;
    QQueue<uint8_t> m_queRecvData;
    QQueue<uint8_t> m_queSendData;
    QQueue<st_SerialCommData> m_queSerialCommData;
};

QT_END_NAMESPACE

#endif // EXTSERIALPORT_H
