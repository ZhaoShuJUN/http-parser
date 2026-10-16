
#ifndef QAPPCFGINFO_H
#define QAPPCFGINFO_H

#include <QString>
#include <QGlobalStatic>

#define  MAINDEVICEMAX       16
#define  UARTPORTMAX         10                  //串口支持的最多端口数量
#define  LETTERMAX           200                 //最大遥信数量
#define  MEASMAX             200                 //最大要测数量

#define  LETTERALLMAX        200                 //最大总遥信数量 所有遥信放在此数组
#define  MEASALLMAX          200                 //最大总遥测数量 所有遥信放在此数组

#define appCfgInfoInstance QAppCfgInfo::instance()

class QAppCfgInfo
{
public:
    QAppCfgInfo();
    ~QAppCfgInfo();

    static QAppCfgInfo *instance();

    void writeAppCfgInfo();     //写入配置文件
    void readAppCfgInfo();      /*读取配置文件*/
    void LetterMeasValReal();   //处理实时数据到遥信和要测寄存器中

public:
    int m_uartNumb = 0;         //串口数量
    QString m_uartPort[UARTPORTMAX];
    int m_uartBaud[UARTPORTMAX];       //设备端口波特率
    int m_uartEffe[UARTPORTMAX];      //串口效验位 无效验
    int m_uartStop[UARTPORTMAX];      //串口停止位 1位

    int m_deviceModbusRtuNumb = 0;                 //设备数量
    int m_deviceConfig[MAINDEVICEMAX][4];          //配置设备信息[][0]端口号 [][1]设备地址 [][2]起始寄存器 [][3]寄存器数量

    int m_ReportLetterNumb = 0;                    //上报遥信数量
    QString m_ReportLetterName[LETTERMAX];         //遥信名称
    int m_ReportLetterReg[LETTERMAX];              //遥信寄存器地址
    uint8_t m_ReportLetterVal[LETTERMAX];          //上报的信数据

    int m_ReportMeasNumb = 0;                      //上报要测数量
    QString m_ReportMeasName[MEASMAX];             //遥测名称
    int m_ReportMeasReg[MEASMAX];                  //遥测寄存器地址
    int m_ReportMeasVal[MEASMAX];                  //上报的要测数据

    uint8_t m_AllLetterVal[LETTERALLMAX];          //所有遥信数据
    int     m_AllMeasVal[MEASALLMAX];              //所有要测数据

private:

    QString m_strCfgFile;//配置文件名称

    /*界面样式相关*/
    uint32_t m_uiStyleType;//样式类型

    /*记录数据相关*/
    bool m_bRecordRunLog;/*日志记录开关*/
    QString m_strDbName;/*数据库文件名称*/
    QString m_strCashFileDir;/*缓存数据存放目录*/
    QString m_strRecordFileDir;/*记录数据存放目录*/
    QString m_strPaperFileDir;/*报表导出目录*/


};

#endif // QAPPCFGINFO_H
