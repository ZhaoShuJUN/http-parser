QT += core gui widgets sql serialport network multimedia multimediawidgets testlib
CONFIG += c++17 testcase
TEMPLATE = app
TARGET = monitor_tests
INCLUDEPATH += .. ../CommObj
SOURCES += test_system.cpp
include(../AppFunc/AppFunc.pri)
include(../CommObj/CommObj.pri)
include(../UserIntf/UserIntf.pri)
include(../ComnUtils/ComnUtils.pri)
include(../ConfigInfo/ConfigInfo.pri)
