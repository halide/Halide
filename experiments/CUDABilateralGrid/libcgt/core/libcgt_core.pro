TEMPLATE = lib
CONFIG += qt thread debug_and_release staticlib

DESTDIR = "../lib"

CONFIG( debug, debug|release ) {
  TARGET = libcgt_cored
} else {
  TARGET = libcgt_core
  DEFINES += _SECURE_SCL=0
}

INCLUDEPATH += $(QTDIR)/include/QtCore
INCLUDEPATH += $(QTDIR)/include/QtGui
INCLUDEPATH += $(QTDIR)/include
LIBPATH += $(QTDIR)/lib

INCLUDEPATH += "./include"

QMAKE_CXXFLAGS += -std=c++0x -fpermissive
QMAKE_CXX=g++-4.6
DEFINES += _CRT_SECURE_NO_WARNINGS

#HEADERS += include/libcgt_core.h

#HEADERS += include/cameras/*.h
#SOURCES += src/cameras/*.cpp

HEADERS += include/color/ColorUtils.h
SOURCES += src/color/ColorUtils.cpp

HEADERS += include/common/*.h
SOURCES += src/common/*.cpp

#HEADERS += include/geometry/*.h
#SOURCES += src/geometry/*.cpp

HEADERS += include/imageproc/*.h
SOURCES += src/imageproc/*.cpp

#HEADERS += include/io/*.h
#SOURCES += src/io/*.cpp

#HEADERS += include/lights/*.h
#SOURCES += src/lights/*.cpp

HEADERS += include/math/*.h
SOURCES += src/math/*.cpp

HEADERS += include/time/*.h
SOURCES += src/time/*.cpp

HEADERS += include/vecmath/Vector*.h
SOURCES += src/vecmath/Vector*.cpp
