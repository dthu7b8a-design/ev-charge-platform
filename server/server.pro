QT += core concurrent network sql
QT -= gui
TEMPLATE = app
CONFIG += console c++17
TARGET = charge_server
DESTDIR = $$OUT_PWD/../bin

HEADERS += database.h forecastservice.h router.h httpserver.h
SOURCES += main.cpp database.cpp forecastservice.cpp router.cpp httpserver.cpp
