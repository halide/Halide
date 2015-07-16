
HALIDE_TOP = $(shell pwd)/../../..
CC = /pkg/qct/software/gnu64/gcc/4.8.2/bin/g++
CXXFLAGS = "--std=c++11"
CLANG = hexagon-clang++
CURR_DIR = $(shell pwd)
LIT = $(shell ${LLVM_CONFIG} --src-root)/utils/lit/lit.py
FILECHECK_PATH = $(shell ${LLVM_CONFIG} --bindir)
FILECHECK = ${FILECHECK_PATH}/FileCheck
NUMS = 1 2 3 4
SOURCES = $(wildcard  *.cpp)
EXES = $(addsuffix .out, $(basename $(SOURCES)))

%.out: %.cpp
	${CC} -g -I ${HALIDE_TOP}/include -L ${HALIDE_TOP}/bin -lHalide -lpthread -ldl  -O0  -I ${HALIDE_TOP}/test/hexagon/include  ${CXXFLAGS} -o $@ $<

.PHONY: all clean run check-codegen
all: run

clean:
	rm -rf *.out *.s \
	rm -rf Output

run: $(EXES)
	${LIT} ${CURR_DIR} --path=${FILECHECK_PATH} -v

check-codegen: run