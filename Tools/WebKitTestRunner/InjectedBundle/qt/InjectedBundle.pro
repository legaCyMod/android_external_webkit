TEMPLATE = lib

isEmpty(OUTPUT_DIR): OUTPUT_DIR = ../../../..

CONFIG(standalone_package) {
    isEmpty(WC_GENERATED_SOURCES_DIR):WC_GENERATED_SOURCES_DIR = $$PWD/../../../../Source/WebCore/generated
} else {
    isEmpty(WC_GENERATED_SOURCES_DIR):WC_GENERATED_SOURCES_DIR = ../../../../Source/WebCore/generated
}

GENERATED_SOURCES_DIR = ../../generated

SOURCES += \
    ../InjectedBundle.cpp \
    ../InjectedBundle.h \
    ../InjectedBundleMain.cpp \
    ../InjectedBundlePage.cpp \
    ../InjectedBundlePage.h \
    ../EventSendingController.cpp \
    ../EventSendingController.h \
    ../GCController.cpp \
    ../GCController.h \
    ../LayoutTestController.cpp \
    ../LayoutTestController.h \
    ../Bindings/JSWrapper.cpp \
    ActivateFontsQt.cpp \
    LayoutTestControllerQt.cpp \
    $$GENERATED_SOURCES_DIR/JSEventSendingController.cpp \
    $$GENERATED_SOURCES_DIR/JSGCController.cpp \
    $$GENERATED_SOURCES_DIR/JSLayoutTestController.cpp \

HEADERS += \
    ../ActivateFonts.h \
    ../EventSendingController.h \
    ../GCController.h \
    ../InjectedBundle.h \
    ../InjectedBundlePage.h \
    ../LayoutTestController.h \

!CONFIG(release, debug|release) {
    OBJECTS_DIR = obj/debug
} else { # Release
    OBJECTS_DIR = obj/release
}

include(../../../../Source/WebKit.pri)
include(../../../../Source/JavaScriptCore/JavaScriptCore.pri)
addJavaScriptCoreLib(../../../JavaScriptCore)
include(../../../../Source/WebKit2/WebKit2.pri)
addWebKit2Lib(../../../WebKit2)

INCLUDEPATH += \
    $$PWD \
    $$PWD/.. \
    $$PWD/../.. \
    $$PWD/../Bindings \
    $$PWD/../../../../Source/JavaScriptCore \
    $$PWD/../../../../Source/JavaScriptCore/wtf \
    $$PWD/../../../../Source/WebCore \
    $$PWD/../../../../Source/WebCore/platform/text \
    $$PWD/../../../../Source/WebKit2 \
    $$PWD/../../../../Source/WebKit2/Shared \
    $$GENERATED_SOURCES_DIR

INCLUDEPATH += \
    $$OUTPUT_DIR/include \
    $$WC_GENERATED_SOURCES_DIR

PREFIX_HEADER = $$PWD/../../WebKitTestRunnerPrefix.h
*-g++*:QMAKE_CXXFLAGS += "-include $$PREFIX_HEADER"

unix:!mac:!symbian {
    CONFIG += link_pkgconfig
    PKGCONFIG += fontconfig
}

TARGET = WTRInjectedBundle
DESTDIR = $$OUTPUT_DIR/lib
!CONFIG(standalone_package): CONFIG -= app_bundle