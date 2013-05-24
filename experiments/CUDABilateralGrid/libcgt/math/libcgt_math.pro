TEMPLATE = lib
CONFIG += qt thread debug_and_release staticlib

DESTDIR = "../lib"

# ===== libcgt_core =====
INCLUDEPATH += "../core/include"
LIBPATH += "../lib"

# ===== Qt =====
INCLUDEPATH += $(QTDIR)/include/QtCore
INCLUDEPATH += $(QTDIR)/include/QtGui
INCLUDEPATH += $(QTDIR)/include
LIBPATH += $(QTDIR)/lib

# ===== MKL =====
INCLUDEPATH += $(ICPP_COMPILER12)mkl/include
LIBPATH += $(ICPP_COMPILER12)mkl/lib/intel64

LIBS += mkl_core.lib mkl_intel_lp64.lib

# sequential
#LIBS += mkl_sequential.lib
# multi-threaded
LIBPATH += $(ICPP_COMPILER12)compiler/lib/intel64
LIBS += mkl_intel_thread.lib libiomp5md.lib # libiomp5mt.lib is the static library, for /MT

INCLUDEPATH += "./include"

QMAKE_CXXFLAGS += -MP4
DEFINES += _CRT_SECURE_NO_WARNINGS

CONFIG( debug, debug|release ) {
  TARGET = libcgt_mathd
  LIBS += libcgt_cored.lib

  INCLUDEPATH += $(SUITESPARSED)/UFconfig
  INCLUDEPATH += $(SUITESPARSED)/CHOLMOD/Include
  INCLUDEPATH += $(SUITESPARSED)/SPQR/Include

  LIBPATH += $(SUITESPARSED)/AMD/Lib
  LIBPATH += $(SUITESPARSED)/CAMD/Lib
  LIBPATH += $(SUITESPARSED)/COLAMD/Lib
  LIBPATH += $(SUITESPARSED)/CCOLAMD/Lib
  LIBPATH += $(SUITESPARSED)/CHOLMOD/Lib
  LIBPATH += $(SUITESPARSED)/SPQR/Lib

  LIBS += libamd.lib libcamd.lib libcolamd.lib libccolamd.lib libcholmod.lib libspqr.lib

} else {
  TARGET = libcgt_math
  DEFINES += _SECURE_SCL=0
  LIBS += libcgt_core.lib

  INCLUDEPATH += $(SUITESPARSE)/UFconfig
  INCLUDEPATH += $(SUITESPARSE)/CHOLMOD/Include
  INCLUDEPATH += $(SUITESPARSE)/SPQR/Include

  LIBPATH += $(SUITESPARSE)/AMD/Lib
  LIBPATH += $(SUITESPARSE)/CAMD/Lib
  LIBPATH += $(SUITESPARSE)/COLAMD/Lib
  LIBPATH += $(SUITESPARSE)/CCOLAMD/Lib
  LIBPATH += $(SUITESPARSE)/CHOLMOD/Lib
  LIBPATH += $(SUITESPARSE)/SPQR/Lib

  LIBS += libamd.lib libcamd.lib libcolamd.lib libccolamd.lib libcholmod.lib libspqr.lib
}

HEADERS += include/*.h
SOURCES += src/*.cpp
