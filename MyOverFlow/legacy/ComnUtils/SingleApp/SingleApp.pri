INCLUDEPATH += $$PWD

HEADERS += \
    $$PWD/qtsingleapplication.h

win32
{
    CONFIG(debug, debug|release) {
        contains(TARGET_ARCH, x86_64){
            LIBS += -L$$PWD/../../lib/$$COMPILE_TYPE/$$QT_VERSION/Win64  -lQSingleAppD
        } else {
            LIBS += -L$$PWD/../../lib/$$COMPILE_TYPE/$$QT_VERSION/Win32  -lQSingleAppD
        }
    }

    CONFIG(release,debug|release){
        contains(TARGET_ARCH, x86_64){
            LIBS += -L$$PWD/../../lib/$$COMPILE_TYPE/$$QT_VERSION/Win64  -lQSingleApp
        } else {
            LIBS += -L$$PWD/../../lib/$$COMPILE_TYPE/$$QT_VERSION/Win32  -lQSingleApp
        }
    }
}

