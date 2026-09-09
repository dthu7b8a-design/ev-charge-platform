QT += core network widgets
TEMPLATE = lib
CONFIG += staticlib c++17
TARGET = charge_shared
DESTDIR = $$OUT_PWD/../lib

HEADERS += apiclient.h theme.h
SOURCES += apiclient.cpp theme.cpp
