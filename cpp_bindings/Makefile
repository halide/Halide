LLVM_DIR=../llvm/Debug+Asserts
LLVM_CONFIG_EXE=$(LLVM_DIR)/bin/llvm-config
OCAML_LLVM_LIB_DIR=$(LLVM_DIR)/lib/ocaml

OCAML_LIB_DIR=/usr/local/lib/ocaml
LLVM_CFLAGS_CXX=$(shell $(LLVM_CONFIG_EXE) --cxxflags)
CXX=gcc-4.6 -std=c++0x -I$(OCAML_LIB_DIR) -I$(SRC_DIR) -Wall -O1 $(LLVM_CFLAGS_CXX)

all: test

LLVM_LIBS_CXX=$(shell $(LLVM_CONFIG_EXE) --libs codegen jit x86 bitwriter interpreter)
LLVM_LIBS_C=-lllvm -lllvm_executionengine -lllvm_bitwriter -lllvm_analysis -lllvm_target
OCAML_LIBS_C=-L$(OCAML_LIB_DIR) -lbigarray
LLVM_LDFLAGS=$(shell $(LLVM_CONFIG_EXE) --ldflags)
LIBS=$(OCAML_LIBS_C) $(LLVM_LDFLAGS) $(LLVM_LIBS_C) $(LLVM_LIBS_CXX) -ldl -lstdc++
 
test: MLFImage.a test.cpp 
	$(CXX) test.cpp MLFImage.a -o test $(LIBS)

MLFImage.a: build/_MLFImage.o build/FImage.o build/MLVal.o build/elf.o
	cp $(OCAML_LIB_DIR)/libasmrun.a MLFImage.a
	ar q MLFImage.a build/_MLFImage.o build/MLVal.o build/FImage.o build/elf.o

build/%.o: %.cpp
	$(CXX) -c $< -o $@

OCAML_LIBS=bigarray.cmxa llvm.cmxa llvm_executionengine.cmxa
OCAML_MODULES=util.cmx ir.cmx ir_printer.cmx analysis.cmx cg_llvm.cmx vectorize.cmx unroll.cmx MLFImage.cmx
OCAML_SOURCES=$(OCAML_MODULES:%.cmx=%.ml)
OCAMLC=ocamlopt -cc gcc-4.6 -I ../$(OCAML_LLVM_LIB_DIR) -I ../../src -ccopt -O2

build/_MLFImage.o: $(addprefix build/,$(OCAML_MODULES))
	cd build && \
	cp ../MLFImage.ml . && \
	$(OCAMLC) $(OCAML_LIBS) $(OCAML_MODULES) -output-obj -o _MLFImage.o && \
	rm MLFImage.ml &&\
	cd ..

build/%.cmx: ../src/%.ml 
	cd build && \
	cp ../$< ./ && \
	$(OCAMLC) -c $(notdir $<) && \
	rm $(notdir $<) && \
	cd ..

build/%.cmx: %.ml
	cd build && \
	cp ../$< ./ && \
	$(OCAMLC) -c $(notdir $<) && \
	rm $(notdir $<) && \
	cd ..

clean:
	rm -f test build/*

dep: 		
	cd ../src/ 
	ocamldep *.ml | sed "s/\([^ ]*.cmx\)/build\/\1/g" | grep -v cmo > Makefile.dep
	cd $(PWD)

include Makefile.dep
