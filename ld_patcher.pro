QT       += core gui concurrent

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17
CONFIG += console
CONFIG += force_debug_info

QMAKE_CFLAGS_RELEASE -= -O2
QMAKE_CXXFLAGS_RELEASE -= -O2
QMAKE_CFLAGS_RELEASE += -O0
QMAKE_CXXFLAGS_RELEASE += -O0
QMAKE_LFLAGS_RELEASE -= -Wl,-s

SOURCES += \
    analysisservice.cpp \
    catalogloader.cpp \
    detector.cpp \
    main.cpp \
    mainwindow.cpp \
    profilesdialog.cpp \
    sourcepackage.cpp \
    workflowservice.cpp \
    ziparchive.cpp

HEADERS += \
    analysisservice.h \
    catalogloader.h \
    catalogtypes.h \
    detector.h \
    mainwindow.h \
    profilesdialog.h \
    sourcepackage.h \
    workflowservice.h \
    ziparchive.h

win32 {
    POWERSHELL_EXE = powershell
    ENSURE_LIBZIP_SCRIPT = $$shell_path($$PWD/ensure_libzip.ps1)
    CONFIG(debug, debug|release) {
        BUILD_VARIANT = Debug
        TARGET_RUNTIME_DIR_RAW = $$OUT_PWD/debug
    } else {
        BUILD_VARIANT = Release
        TARGET_RUNTIME_DIR_RAW = $$OUT_PWD/release
    }
    TARGET_RUNTIME_DIR = $$shell_path($$TARGET_RUNTIME_DIR_RAW)
    QMAKE_PATH_WIN = $$shell_path($$[QT_HOST_BINS]/qmake.exe)
    BUILD_DIR_WIN = $$shell_path($$OUT_PWD)
    PREPARE_LIBZIP_CMD = $$POWERSHELL_EXE -ExecutionPolicy Bypass -File $$ENSURE_LIBZIP_SCRIPT -Configuration $$BUILD_VARIANT -RuntimeDir $$TARGET_RUNTIME_DIR -BuildDir $$BUILD_DIR_WIN -QMakePath $$QMAKE_PATH_WIN

    prepare_libzip.target = prepare_libzip
    prepare_libzip.commands = $$PREPARE_LIBZIP_CMD
    QMAKE_EXTRA_TARGETS += prepare_libzip
    PRE_TARGETDEPS += prepare_libzip
}

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
