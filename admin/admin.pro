QT += core network widgets charts
TEMPLATE = app
CONFIG += c++17
TARGET = charge_admin
DESTDIR = $$OUT_PWD/../bin

INCLUDEPATH += ../shared
DEPENDPATH += ../shared
LIBS += -L$$OUT_PWD/../lib -lcharge_shared
PRE_TARGETDEPS += $$OUT_PWD/../lib/libcharge_shared.a

HEADERS += adminwindow.h
SOURCES += main.cpp adminwindow.cpp
