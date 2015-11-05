HALIDE_TOP = $(shell pwd)/../../
CC = /pkg/qct/software/gnu64/gcc/4.8.2/bin/g++
CXXFLAGS = "--std=c++11"
CLANG = hexagon-clang++

SUBDIRS = median \
  gaussian3x3_update_functions \
  sobel \
  integrate \
  dilate3x3 \
  gaussian5x5 \
  histogram \
  conv3x3a16

NOT_READY4PRIMTIME = \
  conv3x3a32 \
  nv12torgb888 \
  sigma3x3 \
  bilateral \
  epsilon

# Causes make error  camera_pipe


.PHONY: all run perf bmark-report

all: perf not-ready

run:
	for dir in $(SUBDIRS); do \
		export MALLOC_CHECK_=0; \
		$(MAKE) -C $$dir clean run; \
	done

clean:
	for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done

bmark-report:
	for dir in $(SUBDIRS); do \
		export MALLOC_CHECK_=0; \
		$(MAKE) -C $$dir bmark-report; \
	done

perf: run bmark-report

not-ready:
	for dir in $(NOT_READY4PRIMTIME); do \
		export MALLOC_CHECK_=0; \
		$(MAKE) -C $$dir TIMING=  EXTRA_FLAGS=-DNOVECTOR clean run; \
	done

