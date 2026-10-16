#include "qmainmodbusrtu.h"

QMainModbusRtu::~QMainModbusRtu()          //手动添加析构函数
{

}
QMainModbusRtu::QMainModbusRtu(QObject *parent) : QObject(parent)       //构造函数
{

}

unsigned short QMainModbusRtu::Crc16(unsigned char *puchMsg, unsigned short usDataLen)      //CRC计算
{
    unsigned char uchCRCHi = 0xFF ;                         // 高CRC字节初始化
    unsigned char uchCRCLo = 0xFF ;                         // 低CRC 字节初始化
    unsigned int  uIndex ;                                  // CRC循环中的索引
    while (usDataLen--)                                     // 传输消息缓冲区
    {
        uIndex = uchCRCHi ^ *puchMsg++ ;                    // 计算CRC
        uchCRCHi = uchCRCLo ^ AuchCRCHi[uIndex] ;
        uchCRCLo = AuchCRCLo[uIndex] ;
    }
    return (uchCRCHi << 8 | uchCRCLo) ;
}

void QMainModbusRtu::ReadRegisters(unsigned char addr)       //读取寄存器数据处理
{
    unsigned short reg;
    unsigned short i,allnumb=0;
    unsigned short date;

    allnumb = UartRxBuf[2]/2;                                //计算出操作的寄存器个数
    reg = NowOperRegAddr;                                    //读出寄存器地址
    for(i=0;i<allnumb;i++){
        date = UartRxBuf[3+i*2];                             //发送过来的数据
        date=(date<<8)+UartRxBuf[4+i*2];
        ModbusRegWordData[addr][reg] = date;
        addr++;
    }
}

unsigned short QMainModbusRtu::getUartBufData(unsigned short addr,unsigned short reg)             //获取缓冲区中的数据
{
    if(addr<MAINMODBUSWORDMAXBUF)
        return(ModbusRegWordData[addr][reg]);
    else return(0);
}
void QMainModbusRtu::setUartBufData(unsigned short addr,unsigned short reg,unsigned short date)   //设置数据到缓冲区
{
    if(addr<MAINMODBUSWORDMAXBUF)
        ModbusRegWordData[addr][reg] = date;
}

void QMainModbusRtu::UartSendNumb(unsigned char *buf,unsigned short numb)
{
    QByteArray retArray((char*)buf, numb);
//    commObjInstance->sendObject1Data(retArray);
}

//adrss 设备地址 regnumb 寄存器编号 numb数据个数
void QMainModbusRtu::SendReadDataCommand(unsigned char adrss,unsigned short regnumb,unsigned short numb)                 //发送读取单个或多个寄存器数据
{
    unsigned short crcData;

    UartTxBuf[0] = adrss;
    UartTxBuf[1] = 0x03;
    UartTxBuf[2] = regnumb/256;				      //读取的寄存器起始地址
    UartTxBuf[3] = regnumb%256;
    UartTxBuf[4] = numb/256;		              //读取的寄存器个数
    UartTxBuf[5] = numb%256;
    crcData = Crc16(UartTxBuf,6);
    UartTxBuf[6] = crcData >> 8;
    UartTxBuf[7] = crcData & 0xff;
    NowOperRegAddr = regnumb;
    UartSendNumb(UartTxBuf,8);
}

//adrss 设备地址 regnumb 寄存器编号 numb写入数据个数
void QMainModbusRtu::SendWriteDataCommand(unsigned char adrss,unsigned short regnumb,unsigned short numb)         //发送读写单个或多个寄存器命令
{
    unsigned short crcData,i;

    UartTxBuf[0] = adrss;
    if(numb<=1){
        UartTxBuf[1] = 0x06;
        UartTxBuf[2] = regnumb/256;				      //读取的寄存器起始地址
        UartTxBuf[3] = regnumb%256;
        UartTxBuf[4] = (ModbusRegWordData[adrss][regnumb]>>8);    //要写入的数据
        UartTxBuf[5] = ModbusRegWordData[adrss][regnumb];
        crcData = Crc16(UartTxBuf,6);	              //计算效验和
        UartTxBuf[6] = crcData >> 8;
        UartTxBuf[7] = crcData & 0xff;
        NowOperRegAddr = regnumb;
        UartSendNumb(UartTxBuf,8);
    }else{
        UartTxBuf[1] = 0x10;
        UartTxBuf[2] = regnumb/256;				      //读取的寄存器起始地址
        UartTxBuf[3] = regnumb%256;
        UartTxBuf[4] = numb/256;		              //读取的寄存器个数
        UartTxBuf[5] = numb%256;
        UartTxBuf[6] = numb*2;                        //字节个数
        for(i=0;i<numb;i++){                          //处理发送的数据
            UartTxBuf[7+i*2] = (ModbusRegWordData[adrss][regnumb]>>8);
            UartTxBuf[8+i*2] = ModbusRegWordData[adrss][regnumb];
            regnumb++;
        }
        crcData = Crc16(UartTxBuf,numb*2+7);
        UartTxBuf[numb*2+7] = crcData >> 8;
        UartTxBuf[numb*2+7+1] = crcData & 0xff;
        NowOperRegAddr = regnumb;
        UartSendNumb(UartTxBuf,numb*2+7+2);
    }
}

void QMainModbusRtu::HostModbusReadReg()           //读寄存器  03
{
    unsigned char  addr;
    unsigned short reg;
    unsigned short readCount;
    unsigned short i,date;

    reg = NowOperRegAddr;                          //读寄存器地址
    addr = UartRxBuf[0];                           //设备地址
    if(addr<MAINDEVICEMAX){                        //防止设备地址超界
        readCount = (UartRxNumb-4)/2;              //计算出寄存器个数
        for(i=0;i<readCount;i++){	                               //处理所有需要处理的数据
            date = (UartRxBuf[3+i*2]<<8)+UartRxBuf[4+i*2];   //转换出发送过来的数据
            if(reg<MAINMODBUSWORDMAXBUF){	                       //防止数组超界
                ModbusRegWordData[addr][reg] = date;         //读取出数据
                reg++;
                ModbusRegCommErrTime[addr] = 0;
                ModbusRegCommErrBit[addr] = 0;
            }
        }
    }
}

void QMainModbusRtu::HostModbusWriteOneReg()        //写单个寄存器数据
{
    unsigned char  addr;
//    unsigned short reg;

//    reg = NowOperRegAddr;                           //读寄存器地址
    addr = UartRxBuf[0];                            //设备地址

    ModbusRegCommErrTime[addr] = 0;
    ModbusRegCommErrBit[addr] = 0;
}

void QMainModbusRtu::HostModbusWriteMoreReg()       //写多个寄存器数据
{
    unsigned char  addr;
//    unsigned short reg;

//    reg = NowOperRegAddr;                           //读寄存器地址
    addr = UartRxBuf[0];                            //设备地址

    ModbusRegCommErrTime[addr] = 0;
    ModbusRegCommErrBit[addr] = 0;
}

void QMainModbusRtu::ModbusRealRunTime()                //处理超时时间
{
    int i;
    for(i=0;i<MAINDEVICEMAX;i++){
        ModbusRegCommErrTime[i]++;
        if(ModbusRegCommErrTime[i]>=MODBUSREGCOMMERRTIME){
            ModbusRegCommErrTime[i] = MODBUSREGCOMMERRTIME;
            ModbusRegCommErrBit[i] = 1;                  //标志通讯故障
        }
    }
}

void QMainModbusRtu::ModbusCheckCommModbus()             //检查uart数据
{
    unsigned short crcData;
    unsigned short tempData;
    if(UartRxOverBit){                                   //接收到完整数据包
        UartRxOverBit = 0;
        if(UartRxNumb>= 8){                              //数据是有效数据
            switch(UartRxBuf[1]){
                case 3:                                   //读取保持寄存器(一个或多个)
                    crcData = Crc16(UartRxBuf,UartRxNumb-2);
                    if(crcData == UartRxBuf[UartRxNumb-1]+(UartRxBuf[UartRxNumb-2]<<8)){    //校验正确
                        HostModbusReadReg();              //读寄存器  03
                    }
                break;
                case 6:                                   //设置单个寄存器
                    crcData = Crc16(UartRxBuf,6);
                    if(crcData == UartRxBuf[7]+(UartRxBuf[6]<<8)){    //校验正确
                        HostModbusWriteOneReg();          //写单个寄存器数据
                    }
                break;
                case 16:                                               //设置多个寄存器
                    tempData = (UartRxBuf[4]<<8) + UartRxBuf[5];
                    tempData = tempData * 2;	                   //数据个数
                    tempData += 9;
                    crcData = Crc16(UartRxBuf,tempData-2);
                    if(crcData == (UartRxBuf[tempData-2]<<8)+ UartRxBuf[tempData-1]){
                        HostModbusWriteMoreReg();         //写多个寄存器数据
                    }
                break;
                default:
                break;
            }
        }
    }
    ModbusRealRunTime();                                   //处理超时时间
}
