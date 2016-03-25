
HALIDE_TOP = $(shell pwd)/../../..
CXXFLAGS = "--std=c++11"
CLANG = hexagon-clang++
CURR_DIR = $(shell pwd)
LIT = $(shell ${LLVM_CONFIG} --prefix)/../utils/lit/lit.py
FILECHECK_PATH = $(shell ${LLVM_CONFIG} --bindir)
FILECHECK = ${FILECHECK_PATH}/FileCheck
LDFLAGS=-L ${HALIDE_TOP}/bin -Wl,--start-group -lHalide -lpthread -ldl   -Wl,--end-group
INCLUDES=-I${HALIDE_TOP}/include -I${HALIDE_TOP}/test/hexagon/include
SOURCES = $(wildcard  *.cpp)
EXES = $(addsuffix .out, $(basename $(SOURCES)))

%.out: %.cpp
	${CXX} -g ${INCLUDES} -lHalide -lpthread -ldl ${LDFLAGS}  -O0 ${CXXFLAGS} -o $@ $<

.PHONY: all clean run check-codegen
all: run

clean:
	rm -rf *.out *.s \
	rm -rf Output

run: $(EXES)
	LD_LIBRARY_PATH=${HALIDE_TOP}/bin ${LIT} ${CURR_DIR} --path=${FILECHECK_PATH} -v

check-codegen: run
