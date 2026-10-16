FORMS += \
    $$PWD/dialogconfigdev.ui \
    $$PWD/mainwindow.ui

HEADERS += \
    $$PWD/dialogconfigdev.h \
    $$PWD/mainwindow.h

SOURCES += \
    $$PWD/dialogconfigdev.cpp \
    $$PWD/mainwindow.cpp

contains(CONFIG, gst_mpp) {
    CONFIG += link_pkgconfig
    PKGCONFIG += gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0
    DEFINES += HMI_HAS_GST_MPP
    HEADERS += $$PWD/Video/gstmpprtspplayer.h
    SOURCES += $$PWD/Video/gstmpprtspplayer.cpp
}
RESOURCES += $$PWD/hmi.qrc
