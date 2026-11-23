QT += core network sql
QT -= gui
TEMPLATE = app
CONFIG += console c++17
TARGET = charge_server
DESTDIR = $$OUT_PWD/../bin

HEADERS += database.h router.h httpserver.h
SOURCES += main.cpp database.cpp router.cpp httpserver.cpp
