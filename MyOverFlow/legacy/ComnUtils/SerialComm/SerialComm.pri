INCLUDEPATH += $$PWD

#DEFINES += QEXTSERIALPORT_USING_SHARED

HEADERS += \
    $$PWD/extserialport.h \
#    $$PWD/qextserialenumerator.h \
#    $$PWD/qextserialport.h \
    $$PWD/qextserialport_global.h

win32
{
    CONFIG(debug, debug|release) {
        contains(TARGET_ARCH, x86_64){
            LIBS += -L$$PWD/../../lib/$$COMPILE_TYPE/$$QT_VERSION/Win64  -lextSerialPortD
        } else {
            LIBS += -L$$PWD/../../lib/$$COMPILE_TYPE/$$QT_VERSION/Win32  -lextSerialPortD
        }
    }

    CONFIG(release,debug|release){
        contains(TARGET_ARCH, x86_64){
            LIBS += -L$$PWD/../../lib/$$COMPILE_TYPE/$$QT_VERSION/Win64  -lextSerialPort
        } else {
            LIBS += -L$$PWD/../../lib/$$COMPILE_TYPE/$$QT_VERSION/Win32  -lextSerialPort
        }
    }
}

