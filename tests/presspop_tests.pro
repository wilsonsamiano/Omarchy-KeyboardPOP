QT += core gui qml quick testlib
CONFIG += c++17 testcase
TARGET = presspop_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/dictionary.h ../src/keyboardview.h ../src/personal.h ../src/swipe.h \
           ../src/textsink.h ../src/theme.h ../src/translator.h
SOURCES += presspop_tests.cpp ../src/backend.cpp ../src/dictionary.cpp ../src/keyboardview.cpp \
           ../src/personal.cpp ../src/swipe.cpp ../src/theme.cpp ../src/translator.cpp

# The word lists, straight from the source tree.
DEFINES += DATA_DIR=\\\"$$PWD/../src/data\\\"
