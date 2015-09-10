HALIDE_TOP = $(shell pwd)/../../
CC = /pkg/qct/software/gnu64/gcc/4.8.2/bin/g++
CXXFLAGS = "--std=c++11"
CLANG = hexagon-clang++

SUBDIRS = median \
  gaussian3x3_update_functions \
  sobel \
  integrate \
  dilate3x3 \
  conv3x3a16

.PHONY: all run perf bmark-report

all: perf

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
