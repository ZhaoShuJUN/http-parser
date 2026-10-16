INCLUDEPATH += $$PWD

# 从源码编译（替代外部预编译库 libextSerialPort.a）
# 跨平台：Windows MinGW/MSVC 与 Linux（含 RK3399 ARM）统一从源码构建
HEADERS += \
    $$PWD/extserialport.h \
    $$PWD/qextserialport_global.h

SOURCES += \
    $$PWD/extserialport.cpp
