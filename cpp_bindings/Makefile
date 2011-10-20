CXX=g++-4.6 -std=c++0x -Wall -Werror -ffast-math -O2 -msse 

all: test test_functions test_chunk

test: FImage.a 
	$(CXX) test.cpp FImage.a -o test 

test_functions: FImage.a
	$(CXX) test_functions.cpp FImage.a -o test_functions 

test_chunk: FImage.a 
	$(CXX) test_chunk.cpp FImage.a -o test_chunk 

# Everything above this line just uses FImage.a and FImage.h. It has
# no knowledge of llvm or ocaml
#------------------------------------------------
# Everything below this line exists to build FImage.a

LLVM_DIR ?= ../llvm/Debug+Asserts
LLVM_CONFIG_EXE=$(LLVM_DIR)/bin/llvm-config
LLVM_CFLAGS_CXX=$(shell $(LLVM_CONFIG_EXE) --cxxflags)
LLVM_LIBS_CXX=$(shell $(LLVM_CONFIG_EXE) --libs jit bitwriter x86 interpreter)
LLVM_LIBS_C=-lllvm -lllvm_executionengine -lllvm_bitwriter -lllvm_analysis -lllvm_target

OCAML_LLVM_LIB_DIR=$(LLVM_DIR)/lib/ocaml
OCAML_LIB_DIR=/usr/local/lib/ocaml
LLVM_LDFLAGS=$(shell $(LLVM_CONFIG_EXE) --ldflags)
LIBS=$(OCAML_LIBS_C) $(LLVM_LDFLAGS) $(LLVM_LIBS_C) $(LLVM_LIBS_CXX) -ldl -lstdc++
 
FImage.a: _build/_MLFImage.o _build/FImage.o _build/MLVal.o _build/elf.o
	ld -r -o FImage.o _build/_MLFimage.o $(OCAML_LIB_DIR)/libasmrun.a _build/MLVal.o _build/FImage.o _build/elf.o -L$(OCAML_LLVM_LIB_DIR) $(LLVM_LIBS_CXX) $(LLVM_LIBS_C)
	rm -f FImage.a
	ar q FImage.a FImage.o
	ranlib FImage.a

_build/%.o: %.cpp
	$(CXX) $(LLVM_CFLAGS_CXX) -I $(OCAML_LIB_DIR) -c $< -o $@

_build/_MLFImage.o: ../src/_build/fimage.cmxa
	ocamlopt -ccopt -O3 -output-obj -linkall -o _build/_MLFImage.o $(OCAML_LLVM_LIB_DIR)/llvm.cmxa $(OCAML_LLVM_LIB_DIR)/llvm_executionengine.cmxa ../src/_build/fimage.cmxa

../src/_build/fimage.cmxa: ../src/*.ml
	cd ../src && \
	ocamlbuild fimage.cmxa && \
	cd $(PWD)	

clean:
	rm -f test test_functions test_chunk _build/*
	rm FImage.a FImage.o