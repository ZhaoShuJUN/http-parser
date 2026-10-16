QT += core network sql serialport testlib
CONFIG += c++17 console testcase
CONFIG -= app_bundle
TARGET = monitor_tests
INCLUDEPATH += ../src
SOURCES += test_core.cpp ../src/config.cpp ../src/modbus.cpp ../src/store.cpp ../src/camera.cpp ../src/gb28181.cpp ../src/mqtt.cpp
HEADERS += ../src/config.h ../src/modbus.h ../src/store.h ../src/camera.h ../src/gb28181.h ../src/mqtt.h
