QT += core gui qml quick quickcontrols2 network

CONFIG += c++17 release link_pkgconfig
TARGET = keyboardpop
TEMPLATE = app

PKGCONFIG += wayland-client xkbcommon
LIBS += -lLayerShellQtInterface

HEADERS += \
    src/backend.h \
    src/dictionary.h \
    src/keyboardview.h \
    src/personal.h \
    src/swipe.h \
    src/textsink.h \
    src/theme.h \
    src/touchwatcher.h \
    src/translator.h \
    src/waylandinput.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/dictionary.cpp \
    src/keyboardview.cpp \
    src/personal.cpp \
    src/swipe.cpp \
    src/theme.cpp \
    src/touchwatcher.cpp \
    src/translator.cpp \
    src/waylandinput.cpp

RESOURCES += src/resources.qrc

# The input-method and virtual-keyboard protocols are not in
# wayland-protocols, so they are vendored and turned into C at build time.
WAYLAND_PROTOCOLS = \
    protocols/input-method-unstable-v2.xml \
    protocols/virtual-keyboard-unstable-v1.xml

wayland_header.input = WAYLAND_PROTOCOLS
wayland_header.output = ${QMAKE_FILE_BASE}-client-protocol.h
wayland_header.commands = wayland-scanner client-header ${QMAKE_FILE_IN} ${QMAKE_FILE_OUT}
wayland_header.variable_out = GENERATED_FILES
wayland_header.CONFIG += target_predeps no_link

wayland_code.input = WAYLAND_PROTOCOLS
wayland_code.output = ${QMAKE_FILE_BASE}-protocol.c
wayland_code.commands = wayland-scanner private-code ${QMAKE_FILE_IN} ${QMAKE_FILE_OUT}
wayland_code.variable_out = SOURCES
wayland_code.depends = ${QMAKE_FILE_BASE}-client-protocol.h

QMAKE_EXTRA_COMPILERS += wayland_header wayland_code
INCLUDEPATH += $$OUT_PWD
