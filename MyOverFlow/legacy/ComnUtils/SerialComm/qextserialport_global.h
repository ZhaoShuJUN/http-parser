/****************************************************************************
** Copyright (c) 2000-2003 Wayne Roth
** Copyright (c) 2004-2007 Stefan Sander
** Copyright (c) 2007 Michal Policht
** Copyright (c) 2008 Brandon Fosdick
** Copyright (c) 2009-2010 Liam Staskawicz
** Copyright (c) 2011 Debao Zhang
** All right reserved.
** Web: http://code.google.com/p/qextserialport/
**
** Permission is hereby granted, free of charge, to any person obtaining
** a copy of this software and associated documentation files (the
** "Software"), to deal in the Software without restriction, including
** without limitation the rights to use, copy, modify, merge, publish,
** distribute, sublicense, and/or sell copies of the Software, and to
** permit persons to whom the Software is furnished to do so, subject to
** the following conditions:
**
** The above copyright notice and this permission notice shall be
** included in all copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
** EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
** MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
** NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
** LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
** OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
** WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
**
****************************************************************************/

#ifndef QEXTSERIALPORT_GLOBAL_H
#define QEXTSERIALPORT_GLOBAL_H


#include <QtCore/QObject>
#include <QtCore/QtGlobal>


#ifdef QEXTSERIALPORT_BUILD_SHARED
#  define QEXTSERIALPORT_EXPORT Q_DECL_EXPORT
#elif defined(QEXTSERIALPORT_USING_SHARED)
#  define QEXTSERIALPORT_EXPORT Q_DECL_IMPORT
#else
#  define QEXTSERIALPORT_EXPORT
#endif

// ### for compatible with old version. should be removed in QESP 2.0
#ifdef _TTY_NOWARN_
#  define QESP_NO_WARN
#endif
#ifdef _TTY_NOWARN_PORT_
#  define QESP_NO_PORTABILITY_WARN
#endif

/*if all warning messages are turned off, flag portability warnings to be turned off as well*/
#ifdef QESP_NO_WARN
#  define QESP_NO_PORTABILITY_WARN
#endif

/*macros for warning and debug messages*/
#ifdef QESP_NO_PORTABILITY_WARN
#  define QESP_PORTABILITY_WARNING  while (false)qWarning
#else
#  define QESP_PORTABILITY_WARNING qWarning
#endif /*QESP_NOWARN_PORT*/

#ifdef QESP_NO_WARN
#  define QESP_WARNING while (false)qWarning
#else
#  define QESP_WARNING qWarning
#endif /*QESP_NOWARN*/


namespace  extSerialPort {
/*line status constants*/
// ### QESP2.0 move to enum
#define LS_CTS  0x01
#define LS_DSR  0x02
#define LS_DCD  0x04
#define LS_RI   0x08
#define LS_RTS  0x10
#define LS_DTR  0x20
#define LS_ST   0x40
#define LS_SR   0x80

/*error constants*/
// ### QESP2.0 move to enum
#define E_NO_ERROR                   0
#define E_INVALID_FD                 1
#define E_NO_MEMORY                  2
#define E_CAUGHT_NON_BLOCKED_SIGNAL  3
#define E_PORT_TIMEOUT               4
#define E_INVALID_DEVICE             5
#define E_BREAK_CONDITION            6
#define E_FRAMING_ERROR              7
#define E_IO_ERROR                   8
#define E_BUFFER_OVERRUN             9
#define E_RECEIVE_OVERFLOW          10
#define E_RECEIVE_PARITY_ERROR      11
#define E_TRANSMIT_OVERFLOW         12
#define E_READ_FAILED               13
#define E_WRITE_FAILED              14
#define E_FILE_NOT_FOUND            15
#define E_PERMISSION_DENIED         16
#define E_AGAIN                     17

enum BaudRateType
{
#if defined(Q_OS_UNIX) || defined(qdoc)
    BAUD50 = 50,                //POSIX ONLY
    BAUD75 = 75,                //POSIX ONLY
    BAUD134 = 134,              //POSIX ONLY
    BAUD150 = 150,              //POSIX ONLY
    BAUD200 = 200,              //POSIX ONLY
    BAUD1800 = 1800,            //POSIX ONLY
#  if defined(B76800) || defined(qdoc)
    BAUD76800 = 76800,          //POSIX ONLY
#  endif
#  if (defined(B230400) && defined(B4000000)) || defined(qdoc)
    BAUD230400 = 230400,        //POSIX ONLY
    BAUD460800 = 460800,        //POSIX ONLY
    BAUD500000 = 500000,        //POSIX ONLY
    BAUD576000 = 576000,        //POSIX ONLY
    BAUD921600 = 921600,        //POSIX ONLY
    BAUD1000000 = 1000000,      //POSIX ONLY
    BAUD1152000 = 1152000,      //POSIX ONLY
    BAUD1500000 = 1500000,      //POSIX ONLY
    BAUD2000000 = 2000000,      //POSIX ONLY
    BAUD2500000 = 2500000,      //POSIX ONLY
    BAUD3000000 = 3000000,      //POSIX ONLY
    BAUD3500000 = 3500000,      //POSIX ONLY
    BAUD4000000 = 4000000,      //POSIX ONLY
#  endif
#endif //Q_OS_UNIX
#if defined(Q_OS_WIN) || defined(qdoc)
    BAUD14400 = 14400,          //WINDOWS ONLY
    BAUD56000 = 56000,          //WINDOWS ONLY
    BAUD128000 = 128000,        //WINDOWS ONLY
    BAUD256000 = 256000,        //WINDOWS ONLY
    BAUD321500 = 321500,
    BAUD512000 = 512000,
    BAUD1000000 = 1000000,
    BAUD1500000 = 1500000,
    BAUD30000000 = 3000000,
    BAUD35000000 = 3500000,
    BAUD60000000 = 6000000,
#endif  //Q_OS_WIN
    BAUD110 = 110,
    BAUD300 = 300,
    BAUD600 = 600,
    BAUD1200 = 1200,
    BAUD2400 = 2400,
    BAUD4800 = 4800,
    BAUD9600 = 9600,
    BAUD19200 = 19200,
    BAUD38400 = 38400,
    BAUD57600 = 57600,
    BAUD115200 = 115200,
};

enum DataBitsType
{
    DATA_5 = 5,
    DATA_6 = 6,
    DATA_7 = 7,
    DATA_8 = 8
};

enum ParityType
{
    PAR_NONE,
    PAR_ODD,
    PAR_EVEN,
#if defined(Q_OS_WIN) || defined(qdoc)
    PAR_MARK,               //WINDOWS ONLY
#endif
    PAR_SPACE
};

enum StopBitsType
{
    STOP_1,
#if defined(Q_OS_WIN) || defined(qdoc)
    STOP_1_5,               //WINDOWS ONLY
#endif
    STOP_2
};

enum FlowType
{
    FLOW_OFF,
    FLOW_HARDWARE,
    FLOW_XONXOFF
};


enum QueryMode {
    Polling,
    EventDriven
};

/**
 * structure to comm port info
 */
struct QextPortInfo {
    QString portName;   ///< Port name.
    QString physName;   ///< Physical name.
    QString friendName; ///< Friendly name.
    QString enumName;   ///< Enumerator name.
    int vendorID;       ///< Vendor ID.
    int productID;      ///< Product ID
};

/**
 * structure to contain port settings
 */
struct PortSettings
{
    BaudRateType BaudRate;
    DataBitsType DataBits;
    ParityType Parity;
    StopBitsType StopBits;
    FlowType FlowControl;
    //当且仅当串口工作在轮询模式下时有效
    //两字符之间最大的延时，当读取串口数据时，一旦两个字符传输的时间差超过该时间，读取函数将返回现有的数据。
    //设置为0表示该参数不起作用,设置为-1系统默认为 0xffffffff。指定时间最大值（毫秒），允许接收的2个字节间有时间差。
    //也就是说，刚接收了一个字节后，等了Timeout_Millisec时间后还没有新的字节到达，就认为本次读串口操作结束
    //（后面的字节等下一次读取操作来处理）。即使你想读8个字节，但读第2个字节后，过了Timeout_Millisec时间后，第3个字节还没到。实际上就只读了2个字节
    long Timeout_Millisec;
};
}

#endif // QEXTSERIALPORT_GLOBAL_H
