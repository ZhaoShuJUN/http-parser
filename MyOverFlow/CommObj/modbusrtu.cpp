#include "modbusrtu.h"
#include "commobj.h"

ModbusRtu::~ModbusRtu()
{
}

ModbusRtu::ModbusRtu(QObject *parent) : QObject(parent)
{

}

void ModbusRtu::initModbusRtu()
{

}

unsigned short ModbusRtu::Crc16(unsigned char *puchMsg, unsigned short usDataLen)      //CRC计算
{
    unsigned char uchCRCHi = 0xFF ;                         // 高CRC字节初始化
    unsigned char uchCRCLo = 0xFF ;                         // 低CRC 字节初始化
    unsigned int  uIndex ;                                 // CRC循环中的索引
    while (usDataLen--)                             // 传输消息缓冲区
    {
        uIndex = uchCRCHi ^ *puchMsg++ ;            // 计算CRC
        uchCRCHi = uchCRCLo ^ AuchCRCHi[uIndex] ;
        uchCRCLo = AuchCRCLo[uIndex] ;
    }
    return (uchCRCHi << 8 | uchCRCLo) ;
}

unsigned short ModbusRtu::getUartBufData(unsigned short addr)             //获取缓冲区中的数据
{
    if(addr<MODBUSWORDMAXBUF)
        return(ModbusRegWordData[addr]);
    else return(0);
}
void ModbusRtu::setUartBufData(unsigned short addr,unsigned short date)   //设置数据到缓冲区
{
    if(addr<MODBUSWORDMAXBUF)
        ModbusRegWordData[addr] = date;
}

void ModbusRtu::UartSendNumb(unsigned char *buf,unsigned short numb)
{
    QByteArray retArray((char*)buf, numb);
    commObjInstance->sendObject1Data(retArray);
}

void ModbusRtu::ModbusReadInputStatus(void)                    //读取线圈 功能码 02
{
    unsigned short readnumb=0;                       //读取线圈的个数
    unsigned short byteCount=0;
    unsigned short crcData = 0;
    unsigned char  addr=0;                           //读取线圈的起始地址
    unsigned short i=0;
    unsigned char  datenumb=0;                       //字节个数
    unsigned char  date=0,numb=0;

    addr = UartRxBuf[2];                                //读寄存器地址
    addr = (addr<<8)+UartRxBuf[3];
    readnumb = UartRxBuf[4];
    readnumb = (readnumb<<8)+UartRxBuf[5];              //读字个数

    UartTxBuf[0] = UartRxBuf[0];                       //设备地址 原接收数据包返回
    UartTxBuf[1] = 2;	                                 //功能码
    for(i=0;i<readnumb;i++){                           //处理位变量
        if((addr/8) < MODBUFINPUTMAXBUF){	             //防止超界
            if(ModbusInputDateBuf[addr/8]&(1<<(addr%8))){     //若对应位为1
                date |= (1<<(numb));                         //处理线圈状态若不是8的倍数则最后一个字节的多余的高位无效
            }
            addr++;                           //地址自动增加
            numb++;                           //处理的位数增加
            if(numb>=8){                      //够1个字节数据
                numb=0;
                UartTxBuf[3+datenumb] = date;
                datenumb++;                   //字节个数增加
                date=0;                       //清当前的数据
            }
        }
    }
    if(readnumb%8){                           //不是8的倍数
        UartTxBuf[3+datenumb] = date;
        datenumb++;
    }else if(readnumb<=7){
        UartTxBuf[3+datenumb] = date;
        datenumb++;                   //字节个数增加
    }
    UartTxBuf[2] = datenumb;
    byteCount = datenumb+3;
    crcData = Crc16(UartTxBuf,byteCount);
    UartTxBuf[byteCount] = crcData >> 8;
    byteCount++;
    UartTxBuf[byteCount] = crcData & 0xff;
    byteCount++;

    UartSendNumb (UartTxBuf,byteCount);	               //发送数据
}


void ModbusRtu::ModbusDtuReadRegisters(void)                        //读寄存器  03
{
    unsigned short addr;
    unsigned short crcData;
    unsigned short readCount;
    unsigned short i;
    unsigned short byteCount = 0;

    addr = UartRxBuf[2];                                  //读寄存器地址
    addr = (addr<<8)+UartRxBuf[3];
    readCount = UartRxBuf[4];
    readCount = (readCount<<8)+UartRxBuf[5];              //读字个数
    byteCount = readCount*2;

    for(i=0;i<readCount;i++){	                                  //处理所有需要处理的数据
        if(addr+i<MODBUSWORDMAXBUF){	                          //防止数组超界
            ShortChar.ShortDate = ModbusRegWordData[addr+i];   //读取出数据
            UartTxBuf[i*2+3] = (ShortChar.UShortDate) >> 8;
            UartTxBuf[i*2+4] = (ShortChar.UShortDate) & 0xff;
        }
    }
    UartTxBuf[0] = UartRxBuf[0];                         //设备地址 原接收数据包返回
    UartTxBuf[1] = 3;	                                   //功能码
    UartTxBuf[2] = byteCount;
    byteCount += 3;
    crcData = Crc16(UartTxBuf,byteCount);
    UartTxBuf[byteCount] = crcData >> 8;
    byteCount++;
    UartTxBuf[byteCount] = crcData & 0xff;
    byteCount++;

    UartSendNumb (UartTxBuf,byteCount);	               //发送数据
}

void ModbusRtu::ModbusDtuWriteSingleOne(void)            //设置单个寄存器  06
{
    unsigned short addr;
    unsigned short date;
    unsigned short i;

    addr = UartRxBuf[2];                       //读出寄存器地址
    addr = (addr<<8)+UartRxBuf[3];
    date = UartRxBuf[4];                       //发送过来的数据
    date=(date<<8)+UartRxBuf[5];
    ShortChar.UShortDate = date;

    if(addr<MODBUSWORDMAXBUF)
        ModbusRegWordData[addr] = ShortChar.ShortDate;

    for(i=0;i<UartRxNumb;i++){
        UartTxBuf[i] = UartRxBuf[i];
    }
    UartSendNumb (UartTxBuf,UartRxNumb);	                    //发送数据
}

void ModbusRtu::ModbusDtuPresetMultipleRegisters(void)           //设置多个寄存器  16命令 0x10
{
    unsigned short addr;
    unsigned short setCount;
    unsigned short crcData;
    unsigned short tempData;
    unsigned short i;

    addr = UartRxBuf[2];                         //读出寄存器地址
    addr = (addr<<8)+UartRxBuf[3];
    setCount = UartRxBuf[4];
    setCount = (setCount<<8)+UartRxBuf[5];

    for(i=0;i<setCount;i++){
        tempData = (UartRxBuf[i*2+7]<<8) + UartRxBuf[i*2+8];
        ShortChar.UShortDate = tempData;
        if(addr+i<MODBUSWORDMAXBUF)
            ModbusRegWordData[addr+i] = ShortChar.ShortDate;
    }

    UartTxBuf[0] = UartRxBuf[0];
    UartTxBuf[1] = 16;
    UartTxBuf[2] = addr >> 8;
    UartTxBuf[3] = addr & 0xff;
    UartTxBuf[4] = setCount >> 8;
    UartTxBuf[5] = setCount & 0xff;
    crcData = Crc16(UartTxBuf,6);
    UartTxBuf[6] = crcData >> 8;
    UartTxBuf[7] = crcData & 0xff;

    UartSendNumb (UartTxBuf,8);	                    //发送数据
}

void ModbusRtu::ModbusDtuCheckCommModbus(void)                         //检查uart0数据
{
    unsigned short crcData;
    unsigned short tempData;
    unsigned char  addr;
    unsigned char  shdaddr;
    if(UartRxOverBit){                                      //接收到完整数据包
        UartRxOverBit = 0;
        if(UartRxNumb >= 8){                             //数据是有效数据
            shdaddr = 1;
            addr = UartRxBuf[0];                         //读取出设备地址
            if(addr==shdaddr){
                switch(UartRxBuf[1]){
                    case 2:
                        crcData = Crc16(UartRxBuf,6);
                        if(crcData == UartRxBuf[7]+(UartRxBuf[6]<<8)){    //校验正确
                            ModbusReadInputStatus();                    //读取线圈 功能码 02
                        }
                    break;
                    case 3:                                   //读取保持寄存器(一个或多个)
                        crcData = Crc16(UartRxBuf,6);
                        if(crcData == UartRxBuf[7]+(UartRxBuf[6]<<8)){    //校验正确
                            ModbusDtuReadRegisters();                     //读寄存器  03
                        }
                    break;
                    case 6:                                   //设置单个寄存器
                        crcData = Crc16(UartRxBuf,6);
                        if(crcData == UartRxBuf[7]+(UartRxBuf[6]<<8)){    //校验正确
                            ModbusDtuWriteSingleOne();            //设置单个寄存器  06
                        }
                    break;
                    case 16:                                               //设置多个寄存器
                        tempData = (UartRxBuf[4]<<8) + UartRxBuf[5];
                        tempData = tempData * 2;	                   //数据个数
                        tempData += 9;
                        crcData = Crc16(UartRxBuf,tempData-2);
                        if(crcData == (UartRxBuf[tempData-2]<<8)+ UartRxBuf[tempData-1]){
                            ModbusDtuPresetMultipleRegisters();           //设置多个寄存器  16命令 0x10
                        }
                    break;
                    default:
                    break;
                }
            }
        }
    }
}


