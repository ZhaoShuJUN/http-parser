
#include "appcfginfo.h"
#include <QDir>
#include <QSettings>
#include <QTextCodec>

Q_GLOBAL_STATIC(QAppCfgInfo, staticAppCfgInfo)
QAppCfgInfo::QAppCfgInfo()
{
    m_strCfgFile = QDir::currentPath() + "/configInfo.ini";
}

QAppCfgInfo::~QAppCfgInfo()
{
}

QAppCfgInfo *QAppCfgInfo::instance()
{
    return staticAppCfgInfo();
}

void QAppCfgInfo::writeAppCfgInfo()          //写入配置文件
{
    int i;
    QString numbstr;
    QString strTemp;
    QSettings set(m_strCfgFile, QSettings::IniFormat);
    set.setIniCodec(QTextCodec::codecForName("UTF-8"));

    set.beginGroup("UserItfCfg");
    set.setValue("StyleType", QString::number(m_uiStyleType));
    set.endGroup();
    /*记录文件配置信息*/
    set.beginGroup("RecordFileInfo");
    strTemp = "yes";
    set.setValue("recordRunLog", strTemp);
    m_strDbName = "database";
    set.setValue("datebaseName", m_strDbName);
    m_strCashFileDir = "D:/cashFile";
    set.setValue("cashFileDir", m_strCashFileDir);
    m_strRecordFileDir = "D:/RecordFile";
    set.setValue("recordFileDir", m_strRecordFileDir);
    set.endGroup();
    //串口配置
    set.beginGroup("UartCfg"); //设置组
    set.setValue("UartNumb", m_uartNumb);                                                   //启用串口数量
    for(i=0;i<UARTPORTMAX;i++){                                                                      //写入出串口端口号
        numbstr="UartPort";
        set.setValue(numbstr+QString::number(i+1), m_uartPort[i]);
    }
    for(i=0;i<UARTPORTMAX;i++){                                                                      //写入出串口波特率
        numbstr="UartBaud";
        set.setValue(numbstr+QString::number(i+1), QString::number(m_uartBaud[i]));
    }
    for(i=0;i<UARTPORTMAX;i++){                                                                      //写入串口的效验位
        numbstr="UartEffe";
        set.setValue(numbstr+QString::number(i+1),QString::number(m_uartEffe[i]));
    }
    for(i=0;i<UARTPORTMAX;i++){                                                                      //写入串口的停止位
        numbstr="UartStop";
        set.setValue(numbstr+QString::number(i+1),QString::number(m_uartStop[i]));
    }
    set.endGroup();

    //配置设备数量
    set.beginGroup("DeviceCfg");
    if(m_deviceModbusRtuNumb>MAINDEVICEMAX)
        m_deviceModbusRtuNumb = MAINDEVICEMAX;
    set.setValue("DeviceHaveNumb", m_deviceModbusRtuNumb);      //设备数量
    for(i=0;i<m_deviceModbusRtuNumb;i++){                                                    //读取设备配置信息
        numbstr="DevicePort";
        set.setValue(numbstr+QString::number(i+1),QString::number(m_deviceConfig[i][0]));
        numbstr="DeviceAddr";
        set.setValue(numbstr+QString::number(i+1),QString::number(m_deviceConfig[i][1]));
        numbstr="DeviceRegStart";
        set.setValue(numbstr+QString::number(i+1),QString::number(m_deviceConfig[i][2]));
        numbstr="DeviceRegEnd";
        set.setValue(numbstr+QString::number(i+1),QString::number(m_deviceConfig[i][3]));
    }
    set.endGroup();

    //G101上报数据配置
    set.beginGroup("ReportCfg");
    if(m_ReportLetterNumb>LETTERMAX)
        m_ReportLetterNumb = LETTERMAX;
    set.setValue("ReportLetterNumb", m_ReportLetterNumb);      //遥信数量
    if(m_ReportMeasNumb>MEASMAX)
        m_ReportMeasNumb = MEASMAX;
    set.setValue("ReportMeasNumb", m_ReportMeasNumb);          //遥测数量
    for(i=0;i<m_ReportLetterNumb;i++){                         //写入全部遥信配置信息
        numbstr="ReportLetterName";
        set.setValue(numbstr+QString::number(i+1),m_ReportLetterName[i]);
        numbstr="ReportLetterReg";
        set.setValue(numbstr+QString::number(i+1),QString::number(m_ReportLetterReg[i]));
    }
    for(i=0;i<m_ReportLetterNumb;i++){                         //写入全部遥测配置信息
        numbstr="ReportMeasName";
        set.setValue(numbstr+QString::number(i+1),m_ReportMeasName[i]);
        numbstr="ReportMeasReg";
        set.setValue(numbstr+QString::number(i+1),QString::number(m_ReportMeasReg[i]));
    }
    set.endGroup();

}

void QAppCfgInfo::readAppCfgInfo()           //读取配置文件
{
    int i;
    QString numbstr;
    QString strTemp;
    QSettings set(m_strCfgFile, QSettings::IniFormat);
    set.setIniCodec(QTextCodec::codecForName("UTF-8"));
    /*人机接口配置信息*/
    set.beginGroup("UserItfCfg");
    m_uiStyleType = set.value("StyleType", m_uiStyleType).toInt();
    set.endGroup();
    /*记录文件配置信息*/
    set.beginGroup("RecordFileInfo");
    m_bRecordRunLog = false;
    strTemp = set.value("recordRunLog", strTemp).toString();
    if(strTemp.startsWith("yes", Qt::CaseInsensitive))
    {
        m_bRecordRunLog = true;
    }
    m_strDbName = set.value("datebaseName", m_strDbName).toString();
    m_strCashFileDir = set.value("cashFileDir", m_strCashFileDir).toString();
    m_strRecordFileDir = set.value("recordFileDir", m_strRecordFileDir).toString();
    set.endGroup();
    //串口配置信息
    set.beginGroup("UartCfg");
    m_uartNumb = set.value("UartNumb", m_uartNumb).toInt();                                //启用串口数量
    for(i=0;i<UARTPORTMAX;i++){                                                                     //读取出串口端口号
        numbstr="UartPort";
        m_uartPort[i] = set.value(numbstr+QString::number(i+1),m_uartPort[i]).toString();
    }
    for(i=0;i<UARTPORTMAX;i++){                                                                     //读取出串口波特率
        numbstr="UartBaud";
        m_uartBaud[i] = set.value(numbstr+QString::number(i+1),m_uartBaud[i]).toInt();
    }
    for(i=0;i<UARTPORTMAX;i++){                                                                      //读取串口的效验位
        numbstr="UartEffe";
        m_uartEffe[i]= set.value(numbstr+QString::number(i+1),m_uartEffe[i]).toInt();
    }
    for(i=0;i<UARTPORTMAX;i++){                                                                      //读取串口的停止位
        numbstr="UartStop";
        m_uartStop[i]= set.value(numbstr+QString::number(i+1),m_uartStop[i]).toInt();
    }
    set.endGroup();
    //配置设备数量
    set.beginGroup("DeviceCfg");
    m_deviceModbusRtuNumb = set.value("DeviceHaveNumb", m_deviceModbusRtuNumb).toInt();      //设备数量
    if(m_deviceModbusRtuNumb>MAINDEVICEMAX)
        m_deviceModbusRtuNumb = MAINDEVICEMAX;
    for(i=0;i<m_deviceModbusRtuNumb;i++){                                                    //读取设备配置信息
        numbstr="DevicePort";
        m_deviceConfig[i][0]= set.value(numbstr+QString::number(i+1),m_deviceConfig[i][0]).toInt();
        numbstr="DeviceAddr";
        m_deviceConfig[i][1]= set.value(numbstr+QString::number(i+1),m_deviceConfig[i][1]).toInt();
        numbstr="DeviceRegStart";
        m_deviceConfig[i][2]= set.value(numbstr+QString::number(i+1),m_deviceConfig[i][2]).toInt();
        numbstr="DeviceRegEnd";
        m_deviceConfig[i][3]= set.value(numbstr+QString::number(i+1),m_deviceConfig[i][3]).toInt();
    }
    set.endGroup();

    //G101上报数据配置
    set.beginGroup("ReportCfg");
    m_ReportLetterNumb = set.value("ReportLetterNumb", m_ReportLetterNumb).toInt();         //遥信数量
    if(m_ReportLetterNumb>LETTERMAX)
        m_ReportLetterNumb = LETTERMAX;
    m_ReportMeasNumb = set.value("ReportMeasNumb", m_ReportMeasNumb).toInt();             //遥测数量
    if(m_ReportMeasNumb>MEASMAX)
        m_ReportMeasNumb = MEASMAX;
    for(i=0;i<m_ReportLetterNumb;i++){                         //读取全部遥信配置信息
        numbstr="ReportLetterName";
        m_ReportLetterName[i] = set.value(numbstr+QString::number(i+1),m_ReportLetterName[i]).toString();
        numbstr="ReportLetterReg";
        m_ReportLetterReg[i]= set.value(numbstr+QString::number(i+1),m_ReportLetterReg[i]).toInt();
    }
    for(i=0;i<m_ReportMeasNumb;i++){                           //读取全部遥测配置信息
        numbstr="ReportMeasName";
        m_ReportMeasName[i] = set.value(numbstr+QString::number(i+1),m_ReportMeasName[i]).toString();
        numbstr="ReportMeasReg";
        m_ReportMeasReg[i]= set.value(numbstr+QString::number(i+1),m_ReportMeasReg[i]).toInt();
    }
    set.endGroup();
}

void QAppCfgInfo::LetterMeasValReal()                //处理实时数据到遥信和要测寄存器中
{
    int i;
    for(i=0;i<m_ReportLetterNumb;i++){
        if(m_ReportLetterReg[i]<LETTERALLMAX)        //拷贝遥信数据到上报的遥信数组
            m_ReportLetterVal[i] = m_AllLetterVal[m_ReportLetterReg[i]];
    }
    for(i=0;i<m_ReportMeasNumb;i++){                 //拷贝要测数据到上报的遥测数组
        if(m_ReportMeasReg[i]<MEASALLMAX)
            m_ReportMeasVal[i] = m_AllMeasVal[m_ReportMeasReg[i]];
    }
}


