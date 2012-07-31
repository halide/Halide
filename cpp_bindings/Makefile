GXX = g++-4.6
ifneq ($(shell env -i command -v g++-4.7),)
GXX = g++-4.7
endif
CXX=$(GXX) -std=c++0x -Wall -Werror -ffast-math -fno-rtti -O3 -msse -Wl,-dead_strip

HEADERS = Expr.h Func.h Halide.h Image.h MLVal.h Reduction.h Tuple.h Type.h Uniform.h Util.h Var.h elf.h

all: Halide.a

# Set env USE_GPU=1 to build with the GPU target forced. Set env PY_BINDING=1 to build the library needed for the Python binding.
USE_GPU ?= 0
PY_BINDING ?= 0
GPU_CFLAGS = -DUSE_GPU=$(USE_GPU) -DHALIDE_PY_BINDING=$(PY_BINDING)

CUDA_LIBS ?= -L/usr/local/cuda/lib -lcuda
LLVM_DIR ?= ../llvm/Release+Asserts
LLVM_CONFIG_EXE=$(LLVM_DIR)/bin/llvm-config
LLVM_CFLAGS_CXX=$(shell $(LLVM_CONFIG_EXE) --cflags) -I$(LLVM_DIR)/../include
LLVM_LIBS_CXX=$(shell $(LLVM_CONFIG_EXE) --libs jit bitwriter bitreader x86 interpreter linker ptx ipo)
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

HALIDE_OBJS=$(addprefix _build/,_MLHalide.o MLVal.o elf.o Expr.o Type.o Util.o Image.o Func.o Var.o Tuple.o Reduction.o)

Halide.a: $(HALIDE_OBJS) ../src/_build/libllsupport_impl.a
	ld -r -o Halide.o $(HALIDE_OBJS) -L$(OCAML_LLVM_LIB_DIR) -L../src/_build -lllsupport_impl -L$(LLVM_DIR)/lib $(LLVM_LIBS_C) $(LLVM_LIBS_CXX) -L$(OCAML_LIB_DIR) -lasmrun -lunix -lcamlstr -lnums -lbigarray
	rm -f Halide.a
	ar q Halide.a Halide.o
	ranlib Halide.a

_build/%.o: %.cpp
	$(CXX) $(GPU_CFLAGS) $(LLVM_CFLAGS_CXX) -I $(OCAML_LIB_DIR) -c $< -o $@

_build/_MLHalide.o: ../src/_build/halide.cmxa
	mkdir -p _build
	ocamlfind ocamlopt -ccopt -O3 -output-obj -linkall -o _build/_MLHalide.o -package sexplib.syntax,batteries.syntax,batteries -linkpkg $(OCAML_LLVM_LIB_DIR)/llvm.cmxa $(OCAML_LLVM_LIB_DIR)/llvm_executionengine.cmxa $(OCAML_LLVM_LIB_DIR)/llvm_bitreader.cmxa ../src/_build/halide.cmxa

../src/_build/halide.cmxa: ../src/*.ml
	cd ../src && \
	ocamlbuild -use-ocamlfind halide.cmxa && \
	cd $(PWD)

../src/_build/libllsupport_impl.a: ../src/*.c
	cd ../src && \
	ocamlbuild -use-ocamlfind libllsupport_impl.a && \
	cd $(PWD)

../src/_build/: ../src/*.c
	cd ../src && \
	ocamlbuild -use-ocamlfind libllsupport_impl.a && \
	cd $(PWD)

halide: Halide.a $(HEADERS)
	mkdir -p halide
	cp $^ $@

halide.tgz: halide
	tar zcvf $@ $<

deploy: halide.tgz
	rm -rf halide

clean:
	rm -rf halide/*
	rm -f _build/*
	rm -f Halide.a Halide.o
