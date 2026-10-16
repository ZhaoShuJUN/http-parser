
#include "object1.h"
#include "../../ConfigInfo/appcfginfo.h"

QObject1::~QObject1()
{
    m_pExtSerialComm->closeSerial();
    delete m_pExtSerialComm;
}

QObject1::QObject1(QObject *parent)                    //QString strCommPort,
    : QObject{parent}
{
    m_uartdtu = new ModbusRtu();                     //新建DTU设备类
}

void QObject1::OpenCommPort(QString strCommPort,int baud,int effic,int stop)       //打开串口
{
    PortSettings stSerialCommPara;
    m_pExtSerialComm = new QextSerialComm();
    switch(effic){                                   //根据设定值处理效验位
    case 0:
        stSerialCommPara.Parity = PAR_NONE;
        break;
    case 1:
        stSerialCommPara.Parity = PAR_ODD;
        break;
    case 2:
        stSerialCommPara.Parity = PAR_EVEN;
        break;
    default:
        stSerialCommPara.Parity = PAR_NONE;
        break;
    }
    stSerialCommPara.DataBits = DATA_8;
    switch(stop){
    case 0:
        stSerialCommPara.StopBits = STOP_1;
        break;
    case 1:
        stSerialCommPara.StopBits = STOP_1_5;
        break;
    case 2:
        stSerialCommPara.StopBits = STOP_2;
        break;
    default:
        break;
    }
    switch(baud){
    case 110:
        stSerialCommPara.BaudRate = BAUD110;
        break;
    case 300:
        stSerialCommPara.BaudRate = BAUD300;
        break;
    case 600:
        stSerialCommPara.BaudRate = BAUD600;
        break;
    case 1200:
        stSerialCommPara.BaudRate = BAUD1200;
        break;
    case 2400:
        stSerialCommPara.BaudRate = BAUD2400;
        break;
    case 4800:
        stSerialCommPara.BaudRate = BAUD4800;
        break;
    case 9600:
        stSerialCommPara.BaudRate = BAUD9600;
        break;
    case 19200:
        stSerialCommPara.BaudRate = BAUD19200;
        break;
    case 38400:
        stSerialCommPara.BaudRate = BAUD38400;
        break;
    case 57600:
        stSerialCommPara.BaudRate = BAUD57600;
        break;
    case 115200:
        stSerialCommPara.BaudRate = BAUD115200;
        break;
    default:
        stSerialCommPara.BaudRate = BAUD9600;
        break;
    }
    stSerialCommPara.FlowControl = FLOW_OFF;
    stSerialCommPara.Timeout_Millisec = 10;
    m_pExtSerialComm->openSerial(strCommPort, stSerialCommPara);
    m_ucCommStatus = 0xAA;
    /*绑定信号槽-串口接收到数据后，发送接收数据信号*/
    connect(m_pExtSerialComm, &QextSerialComm::sglRecvCommData, this, &QObject1::onRecvCommData);
}

uint8_t QObject1::getCommStatus()
{
    return m_ucCommStatus;
}

void QObject1::sendCommData(QByteArray baCommData)
{
    /*通过串口发送通信数据*/
    if(nullptr != m_pExtSerialComm)
    {
        m_pExtSerialComm->sendData(baCommData);
    }
}

void QObject1::onRecvCommData(uint16_t us_DataLen, uint8_t *p_ucCommData)
{
    /*通过信号和槽的方式获取串口通信数据
     * 处理接收数据*/
    for(int i=0;i<us_DataLen;i++){
        if(m_uartdtu->UartRxStartBit==0){             //还没接收到数据头
            if(p_ucCommData[i]==m_uartdtu->ShdDeviceAddr){
                m_uartdtu->UartRxNumb = 0;
                m_uartdtu->UartRxBuf[m_uartdtu->UartRxNumb] = p_ucCommData[i];
                if(m_uartdtu->UartRxNumb<UARTRXMAX)
                    m_uartdtu->UartRxNumb++;
                m_uartdtu->UartRxStartBit = 1;
            }
        }else{
            m_uartdtu->UartRxBuf[m_uartdtu->UartRxNumb] = p_ucCommData[i];
            if(m_uartdtu->UartRxNumb<UARTRXMAX)
                m_uartdtu->UartRxNumb++;
            if(m_uartdtu->UartRxNumb==2){             //接收到控制命令
                if(m_uartdtu->UartRxBuf[1]==0x02){
                    m_uartdtu->UartRxAllNumb = 8;
                }else if(m_uartdtu->UartRxBuf[1]==0x03){
                    m_uartdtu->UartRxAllNumb = 8;
                }else if(m_uartdtu->UartRxBuf[1]==0x06){
                    m_uartdtu->UartRxAllNumb = 8;
                }else if(m_uartdtu->UartRxBuf[1]==0x10){

                }else {                                //错误的命令退出本包接收
                    m_uartdtu->UartRxStartBit = 0;
                    m_uartdtu->UartRxNumb = 0;
                }
            }
            if(m_uartdtu->UartRxNumb>=4){
                if(m_uartdtu->UartRxBuf[1]==0x10){
                    if(m_uartdtu->UartRxNumb==4){
                        m_uartdtu->UartRxAllNumb = ((m_uartdtu->UartRxBuf[4]<<8)+m_uartdtu->UartRxBuf[5])*2+7;             //计算出数据包长度
                        if(m_uartdtu->UartRxAllNumb>=256){         //数据包太大则认为是错的包
                            m_uartdtu->UartRxStartBit = 0;
                            m_uartdtu->UartRxNumb = 0;
                        }
                    }
                }
                if(m_uartdtu->UartRxNumb>=m_uartdtu->UartRxAllNumb){              //接收到完整数据包
                    m_uartdtu->UartRxStartBit = 0;                                //清零等待重新接收下一包数据
                    m_uartdtu->UartRxOverBit = 1;                                 //标志接收到完整数据包
                    m_uartdtu->ModbusDtuCheckCommModbus();                        //处理接收到的数据包
                    m_uartdtu->UartRxNumb = 0;
                }
            }
        }
    }
}


