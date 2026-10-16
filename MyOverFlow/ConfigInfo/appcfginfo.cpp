#include "appcfginfo.h"
#include <QDir>
#include <QSettings>
Q_GLOBAL_STATIC(QAppCfgInfo, staticAppCfgInfo)
QAppCfgInfo::QAppCfgInfo() : m_strCfgFile(QDir::current().absoluteFilePath("configInfo.ini")) {}
QAppCfgInfo::~QAppCfgInfo() {}
QAppCfgInfo *QAppCfgInfo::instance() {
    return staticAppCfgInfo();
}
void QAppCfgInfo::setConfigFile(const QString &path) {
    m_strCfgFile = QFileInfo(path).absoluteFilePath();
}
void QAppCfgInfo::readAppCfgInfo() {
    m_configError.clear();
    MonitorSettings candidate;
    if (!readMonitorIni(m_strCfgFile, &candidate, &m_configError))
        return;
    m_settings = candidate;
    m_uartNumb = candidate.ports.size();
    for (int i = 0; i < m_uartNumb; ++i) {
        auto p = candidate.ports[i];
        m_uartPort[i] = p.device;
        m_uartBaud[i] = p.baud;
        m_uartEffe[i] = p.parity;
        m_uartStop[i] = p.stopBits;
    }
    m_deviceModbusRtuNumb = candidate.sensors.size();
    m_ReportMeasNumb = m_ReportLetterNumb = m_deviceModbusRtuNumb;
    for (int i = 0; i < m_deviceModbusRtuNumb; ++i) {
        auto s = candidate.sensors[i];
        m_deviceConfig[i][0] = s.port;
        m_deviceConfig[i][1] = s.slave;
        m_deviceConfig[i][2] = s.reg;
        m_deviceConfig[i][3] = s.count;
        m_ReportMeasName[i] = s.name;
        m_ReportMeasReg[i] = i;
        m_ReportLetterName[i] = s.name + "报警";
        m_ReportLetterReg[i] = i;
    }
    QSettings set(m_strCfgFile, QSettings::IniFormat);
    set.setIniCodec("UTF-8");
    m_uiStyleType = set.value("UserItfCfg/StyleType", 8).toUInt();
    m_strDbName = "alarms.sqlite";
    m_strRecordFileDir = candidate.recording["directory"].toString();
}
void QAppCfgInfo::writeAppCfgInfo() {
    m_configError.clear();
    writeMonitorIni(m_strCfgFile, m_settings, &m_configError);
}
void QAppCfgInfo::LetterMeasValReal() {
    QWriteLocker guard(&m_dataLock);
    for (int i = 0; i < m_ReportLetterNumb && i < LETTERMAX; ++i) {
        auto index = m_ReportLetterReg[i];
        if (index >= 0 && index < LETTERALLMAX)
            m_ReportLetterVal[i] = m_AllLetterVal[index];
    }
    for (int i = 0; i < m_ReportMeasNumb && i < MEASMAX; ++i) {
        auto index = m_ReportMeasReg[i];
        if (index >= 0 && index < MEASALLMAX)
            m_ReportMeasVal[i] = m_AllMeasVal[index];
    }
}
