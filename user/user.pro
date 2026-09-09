QT += core network widgets webenginewidgets
TEMPLATE = app
CONFIG += c++17
TARGET = charge_user
DESTDIR = $$OUT_PWD/../bin

INCLUDEPATH += ../shared
DEPENDPATH += ../shared
LIBS += -L$$OUT_PWD/../lib -lcharge_shared
PRE_TARGETDEPS += $$OUT_PWD/../lib/libcharge_shared.a

HEADERS += userwindow.h
SOURCES += main.cpp userwindow.cpp
