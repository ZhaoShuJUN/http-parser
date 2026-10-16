QT       += core gui sql serialport network multimedia multimediawidgets

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17
INCLUDEPATH += $$PWD $$PWD/CommObj
DEFINES += QT_DEPRECATED_WARNINGS

#QT_ARCH针对qt5及其以上版本有效，对之前的版本是无效的
greaterThan(QT_MAJOR_VERSION, 4) {
    TARGET_ARCH=$${QT_ARCH}
} else {
    TARGET_ARCH=$${QMAKE_HOST.arch}
}
#use TARGET_ARCH to check.

#获取编译器类型
win32-msvc*{
    COMPILE_TYPE = MSVC
}
else{
    win32-g++{
        COMPILE_TYPE = MinGW
    }
    else{
        COMPILE_TYPE = LINUX
        if(contains(QMAKE_CXX,g++)){
            COMPILE_TYPE = UBUNTU
        }
        if(contains(QMAKE_CXX,aarch64-linux-gnu-g++)){
            COMPILE_TYPE = KYLIN
        }
    }
}

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \

include(./AppFunc/AppFunc.pri)
include(./CommObj/CommObj.pri)
include(./UserIntf/UserIntf.pri)
include(./ComnUtils/ComnUtils.pri)
include(./ConfigInfo/ConfigInfo.pri)



# Default rules for deployment.
#qnx: target.path = /tmp/$${TARGET}/bin
#else: unix:!android: target.path = /opt/$${TARGET}/bin
#!isEmpty(target.path): INSTALLS += target
