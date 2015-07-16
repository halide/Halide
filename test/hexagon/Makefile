HALIDE_TOP = $(shell pwd)/../../
CC = /pkg/qct/software/gnu64/gcc/4.8.2/bin/g++
CXXFLAGS = "--std=c++11"
CLANG = hexagon-clang++

SUBDIRS = codegen
.PHONY: all check check-codegen clean

all: check-codegen

check-codegen:
	cd codegen && $(MAKE) all

clean:
	for dir in $(SUBDIRS); do \
		cd $$dir && $(MAKE) clean; \
	done



check: check-codegen
