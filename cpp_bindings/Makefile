CXX_FLAGS=-Wall -Werror -ffast-math -O3 -msse

HEADERS = MLVal.h Util.h Var.h Type.h Uniform.h Expr.h Image.h Func.h Reduction.h Uniform.h Tuple.h

all: libHalide.a Halide.h

# Set env USE_GPU=1 to build with the GPU target forced.
USE_GPU ?= 0
GPU_CFLAGS = -DUSE_GPU=$(USE_GPU)

CUDA_LIBS ?= -L/usr/local/cuda/lib -lcuda
LLVM_DIR ?= ../llvm/Release+Asserts
LLVM_CONFIG_EXE=$(LLVM_DIR)/bin/llvm-config
LLVM_CFLAGS_CXX=$(shell $(LLVM_CONFIG_EXE) --cflags) -I$(LLVM_DIR)/../include
LLVM_LIBS_CXX=$(shell $(LLVM_CONFIG_EXE) --libs jit bitwriter bitreader x86 interpreter linker nvptx ipo)
LLVM_LIBS_C=-lllvm -lllvm_executionengine -lllvm_bitwriter -lllvm_bitreader -lllvm_analysis -lllvm_target

OCAML_LLVM_LIB_DIR=$(LLVM_DIR)/lib/ocaml
OCAMLC=$(shell which ocamlc)
# TODO: conditionally test for lib/ocaml or lib/ocaml/std-lib for GODI
GODI_LIB_DIR=$(OCAMLC:bin/ocamlc=lib/ocaml/std-lib)
OCAML_LIB_DIR=$(shell test -d $(GODI_LIB_DIR) && echo $(GODI_LIB_DIR))
ifeq ($(OCAML_LIB_DIR),)
OCAML_LIB_DIR=$(OCAMLC:bin/ocamlc=lib/ocaml)
endif
LLVM_LDFLAGS=$(shell $(LLVM_CONFIG_EXE) --ldflags)
LIBS=$(OCAML_LIBS_C) $(LLVM_LDFLAGS) $(LLVM_LIBS_C) $(LLVM_LIBS_CXX) $(CUDA_LIBS) -ldl -lstdc++

HALIDE_OBJS=$(addprefix _build/,_MLHalide.o MLVal.o Expr.o Type.o Util.o Image.o Func.o Var.o Tuple.o Reduction.o)

libHalide.a: $(HALIDE_OBJS) ../src/_build/libllsupport_impl.a
	ld -r -o Halide.o $(HALIDE_OBJS) -L$(OCAML_LLVM_LIB_DIR) -L../src/_build -lllsupport_impl -L$(LLVM_DIR)/lib $(LLVM_LIBS_C) $(LLVM_LIBS_CXX) -L$(OCAML_LIB_DIR) -lasmrun -lunix -lcamlstr -lnums -lbigarray
	rm -f libHalide.a
	ar q libHalide.a Halide.o
	ranlib libHalide.a

_build/%.o: %.cpp
	$(CXX) $(CXX_FLAGS) $(GPU_CFLAGS) $(LLVM_CFLAGS_CXX) -I $(OCAML_LIB_DIR) -c $< -o $@

_build/_MLHalide.o: ../src/_build/halide.cmxa
	mkdir -p _build
	ocamlfind ocamlopt -ccopt -O3 -output-obj -linkall -o _build/_MLHalide.o -package sexplib.syntax -linkpkg $(OCAML_LLVM_LIB_DIR)/llvm.cmxa $(OCAML_LLVM_LIB_DIR)/llvm_executionengine.cmxa $(OCAML_LLVM_LIB_DIR)/llvm_bitreader.cmxa ../src/_build/halide.cmxa

../src/_build/halide.cmxa: ../src/*.ml
	make -C ../src lib.native

../src/_build/libllsupport_impl.a: ../src/*.c ../src/architecture.*.cpp ../src/architecture.*.ll
	make -C ../src lib.llsupport

Halide.h: $(HEADERS)
	cat $(HEADERS) | grep -v "include \"" > Halide.h

halide: libHalide.a Halide.h
	mkdir -p halide
	cp Halide.h libHalide.a $@

halide.tgz: halide
	tar zcvf $@ $<

deploy: halide.tgz
	rm -rf halide

clean:
	rm -rf halide/*
	rm -f _build/*
	rm -f libHalide.a Halide.o
