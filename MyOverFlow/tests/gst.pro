QT += core gui
CONFIG += c++17 console link_pkgconfig
CONFIG -= app_bundle
PKGCONFIG += gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0
TARGET = gst_smoke
SOURCES += gst_smoke.cpp ../UserIntf/Video/gstmpprtspplayer.cpp
HEADERS += ../UserIntf/Video/gstmpprtspplayer.h
