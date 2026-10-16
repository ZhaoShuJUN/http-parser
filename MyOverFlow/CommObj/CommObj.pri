HEADERS += \
    $$PWD/commobj.h \
    $$PWD/modbusrtu.h \
    $$PWD/qmainmodbusrtu.h

SOURCES += \
    $$PWD/commobj.cpp \
    $$PWD/modbusrtu.cpp \
    $$PWD/qmainmodbusrtu.cpp

include(./Object1/Object1.pri)

HEADERS += $$PWD/Video/cameracomm.h $$PWD/Video/gb28181.h $$PWD/gatewaymqtt.h
SOURCES += $$PWD/Video/cameracomm.cpp $$PWD/Video/gb28181.cpp $$PWD/gatewaymqtt.cpp
LIBS += -lmosquitto
