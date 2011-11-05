CXX=g++-4.6 -std=c++0x -Wall -Werror -ffast-math -fno-rtti -O3 -msse -Wl,-dead_strip

all: test test_functions test_chunk test_vector_cast

#test: FImage.a test.cpp
#	$(CXX) test.cpp FImage.a -o test 

test_%: test_%.cpp FImage.a
	$(CXX) FImage.a $< -o $@

#test_vector_cast: FImage.a test_vector_cast.cpp
#	$(CXX) test_vector_cast.cpp -o test_vector_cast

#test_functions: FImage.a test_functions.cpp
#	$(CXX) test_functions.cpp FImage.a -o test_functions 

#test_chunk: FImage.a test_chunk.cpp
#	$(CXX) test_chunk.cpp FImage.a -o test_chunk 

# Everything above this line just uses FImage.a and FImage.h. It has
# no knowledge of llvm or ocaml
#------------------------------------------------
# Everything below this line exists to build FImage.a

LLVM_DIR ?= ../llvm/Release+Asserts
LLVM_CONFIG_EXE=$(LLVM_DIR)/bin/llvm-config
LLVM_CFLAGS_CXX=$(shell $(LLVM_CONFIG_EXE) --cxxflags)
LLVM_LIBS_CXX=$(shell $(LLVM_CONFIG_EXE) --libs jit bitwriter bitreader x86 interpreter)
LLVM_LIBS_C=-lllvm -lllvm_executionengine -lllvm_bitwriter -lllvm_bitreader -lllvm_analysis -lllvm_target

OCAML_LLVM_LIB_DIR=$(LLVM_DIR)/lib/ocaml
OCAML_LIB_DIR=/usr/local/lib/ocaml
LLVM_LDFLAGS=$(shell $(LLVM_CONFIG_EXE) --ldflags)
LIBS=$(OCAML_LIBS_C) $(LLVM_LDFLAGS) $(LLVM_LIBS_C) $(LLVM_LIBS_CXX) -ldl -lstdc++
 
FIMAGE_OBJS=$(addprefix _build/,_MLFImage.o MLVal.o elf.o Expr.o Type.o Util.o Image.o Func.o)

FImage.a: $(FIMAGE_OBJS)
	ld -r -o FImage.o $(OCAML_LIB_DIR)/libasmrun.a $(FIMAGE_OBJS) -L$(OCAML_LLVM_LIB_DIR) $(LLVM_LIBS_CXX) $(LLVM_LIBS_C)
	rm -f FImage.a
	ar q FImage.a FImage.o
	ranlib FImage.a

_build/%.o: %.cpp
	$(CXX) $(LLVM_CFLAGS_CXX) -I $(OCAML_LIB_DIR) -c $< -o $@

_build/_MLFImage.o: ../src/_build/fimage.cmxa
	ocamlopt -ccopt -O3 -output-obj -linkall -o _build/_MLFImage.o $(OCAML_LLVM_LIB_DIR)/llvm.cmxa $(OCAML_LLVM_LIB_DIR)/llvm_executionengine.cmxa $(OCAML_LLVM_LIB_DIR)/llvm_bitreader.cmxa ../src/_build/fimage.cmxa

../src/_build/fimage.cmxa: ../src/*.ml
	cd ../src && \
	ocamlbuild fimage.cmxa && \
	cd $(PWD)	

clean:
	rm -f test test_functions test_chunk _build/*
	rm FImage.a FImage.o