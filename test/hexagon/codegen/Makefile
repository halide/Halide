
HALIDE_TOP = $(shell pwd)/../../..
CC = /pkg/qct/software/gnu64/gcc/4.8.2/bin/g++
CXXFLAGS = "--std=c++11"
CLANG = hexagon-clang++
CURR_DIR = $(shell pwd)
LIT = $(shell ${LLVM_CONFIG} --src-root)/utils/lit/lit.py
FILECHECK_PATH = $(shell ${LLVM_CONFIG} --bindir)
FILECHECK = ${FILECHECK_PATH}/FileCheck
CLANG_CPP=/pkg/qct/software/llvm/build_tools/llvm34_140306/bin/clang++
LDFLAGS=-L/pkg/qct/software/llvm/build_tools/libc++_140306/lib -L/pkg/qct/software/llvm/build_tools/libedit_tw/lib -L ${HALIDE_TOP}/bin -Wl,--start-group -lHalide -lpthread -ldl  -lc++ -Wl,--end-group
INCLUDES=-I${HALIDE_TOP}/include -I${HALIDE_TOP}/test/hexagon/include -I/pkg/qct/software/llvm/build_tools/libc++_140306/include/c++/v1
SOURCES = $(wildcard  *.cpp)
EXES = $(addsuffix .out, $(basename $(SOURCES)))

%.out: %.cpp
	${CLANG_CPP} -g ${INCLUDES} -lHalide -lpthread -ldl ${LDFLAGS}  -O0 ${CXXFLAGS} -o $@ $<

.PHONY: all clean run check-codegen
all: run

clean:
	rm -rf *.out *.s \
	rm -rf Output

run: $(EXES)
	${LIT} ${CURR_DIR} --path=${FILECHECK_PATH} -v

check-codegen: run
