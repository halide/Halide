OCAML_LIB_DIR=/usr/local/lib/ocaml
CXX=gcc-4.6 -std=c++0x -I$(OCAML_LIB_DIR) -I$(SRC_DIR) -Wall -g

all: test

LLVM_LIBS_CXX=$(shell llvm-config --libs codegen jit x86 bitwriter interpreter)
LLVM_LIBS_C=-lllvm -lllvm_executionengine -lllvm_bitwriter -lllvm_analysis -lllvm_target
OCAML_LIBS_C=-L$(OCAML_LIB_DIR) -lbigarray
LIBS=$(OCAML_LIBS_C) $(LLVM_LIBS_C) $(LLVM_LIBS_CXX) -ldl -lm -lstdc++
 
test: MLFImage.a test.cpp 
	$(CXX) test.cpp MLFImage.a -o test $(LIBS)

MLFImage.a: build/_MLFImage.o build/FImage.o build/MLVal.o build/elf.o
	cp $(OCAML_LIB_DIR)/libasmrun.a MLFImage.a
	ar q MLFImage.a build/_MLFImage.o build/MLVal.o build/FImage.o build/elf.o

build/%.o: %.cpp
	$(CXX) -c $< -o $@

OCAML_LIBS=bigarray.cmxa llvm.cmxa llvm_executionengine.cmxa
OCAML_MODULES=util.cmx ir.cmx ir_printer.cmx analysis.cmx cg_llvm.cmx vectorize.cmx unroll.cmx MLFImage.cmx
OCAMLC=ocamlopt -cc gcc-4.6 -I ../../src -ccopt -O2

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
