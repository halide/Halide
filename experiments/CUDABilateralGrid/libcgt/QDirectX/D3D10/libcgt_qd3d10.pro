TEMPLATE = lib
CONFIG += qt console thread debug_and_release staticlib

DESTDIR = "../../lib"

INCLUDEPATH += ./include

INCLUDEPATH += $(QTDIR)/include/QtCore
INCLUDEPATH += $(QTDIR)/include/QtGui
INCLUDEPATH += $(QTDIR)/include
INCLUDEPATH += $(DXSDK_DIR)include
INCLUDEPATH += ../../core/include
LIBPATH += $(QTDIR)/lib

CONFIG( debug, debug|release ) {
  TARGET = libcgt_qd3d10d
} else {
  TARGET = libcgt_qd3d10
  DEFINES += _SECURE_SCL=0
}

QMAKE_CXXFLAGS += -MP4
DEFINES += _CRT_SECURE_NO_WARNINGS

#
HEADERS = include/*.h
SOURCES = src/*.cpp
