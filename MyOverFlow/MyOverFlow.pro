QT += core gui widgets network sql serialport multimedia multimediawidgets
CONFIG += c++17 warn_on
TARGET = MyOverFlow
TEMPLATE = app
INCLUDEPATH += src
SOURCES += src/main.cpp src/config.cpp src/modbus.cpp src/store.cpp src/mqtt.cpp src/camera.cpp src/gb28181.cpp src/mainwindow.cpp
HEADERS += src/config.h src/modbus.h src/store.h src/mqtt.h src/camera.h src/gb28181.h src/mainwindow.h
unix: LIBS += -lpthread
