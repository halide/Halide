CXX=g++-4.6 -std=c++0x -Wall -Werror -ffast-math -fno-rtti -O3 -msse -Wl,-dead_strip

all: FImage.a

# Set env USE_GPU=1 to build with the GPU target forced
USE_GPU ?= 0
GPU_CFLAGS = -DUSE_GPU=$(USE_GPU)

CUDA_LIBS ?= -L/usr/local/cuda/lib -lcuda
LLVM_DIR ?= ../llvm/Release+Asserts
LLVM_CONFIG_EXE=$(LLVM_DIR)/bin/llvm-config
LLVM_CFLAGS_CXX=$(shell $(LLVM_CONFIG_EXE) --cxxflags) -I$(LLVM_DIR)/../include
LLVM_LIBS_CXX=$(shell $(LLVM_CONFIG_EXE) --libs jit bitwriter bitreader x86 interpreter linker ptx ipo)
LLVM_LIBS_C=-lllvm -lllvm_executionengine -lllvm_bitwriter -lllvm_bitreader -lllvm_analysis -lllvm_target

OCAML_LLVM_LIB_DIR=$(LLVM_DIR)/lib/ocaml
OCAML_LIB_DIR=/usr/local/lib/ocaml
LLVM_LDFLAGS=$(shell $(LLVM_CONFIG_EXE) --ldflags)
LIBS=$(OCAML_LIBS_C) $(LLVM_LDFLAGS) $(LLVM_LIBS_C) $(LLVM_LIBS_CXX) $(CUDA_LIBS) -ldl -lstdc++
 
FIMAGE_OBJS=$(addprefix _build/,_MLFImage.o MLVal.o elf.o Expr.o Type.o Util.o Image.o Func.o Var.o Tuple.o Reduction.o)

FImage.a: $(FIMAGE_OBJS) ../src/_build/libllsupport_impl.a
	ld -r -o FImage.o $(OCAML_LIB_DIR)/libasmrun.a $(FIMAGE_OBJS) -L$(OCAML_LLVM_LIB_DIR) -L$(LLVM_DIR)/lib $(LLVM_LIBS_CXX) $(LLVM_LIBS_C) ../src/_build/libllsupport_impl.a
	rm -f FImage.a
	ar q FImage.a FImage.o
	ranlib FImage.a

_build/%.o: %.cpp 
	$(CXX) $(GPU_CFLAGS) $(LLVM_CFLAGS_CXX) -I $(OCAML_LIB_DIR) -c $< -o $@

_build/_MLFImage.o: ../src/_build/fimage.cmxa 
	mkdir -p _build
	ocamlopt -ccopt -O3 -output-obj -linkall -o _build/_MLFImage.o $(OCAML_LLVM_LIB_DIR)/llvm.cmxa $(OCAML_LLVM_LIB_DIR)/llvm_executionengine.cmxa $(OCAML_LLVM_LIB_DIR)/llvm_bitreader.cmxa ../src/_build/fimage.cmxa 

../src/_build/fimage.cmxa: ../src/*.ml
	cd ../src && \
	ocamlbuild fimage.cmxa && \
	cd $(PWD)	

../src/_build/libllsupport_impl.a: ../src/*.c
	cd ../src && \
	ocamlbuild libllsupport_impl.a && \
	cd $(PWD)	

../src/_build/: ../src/*.c
	cd ../src && \
	ocamlbuild libllsupport_impl.a && \
	cd $(PWD)	

clean:
	rm -f _build/*
	rm FImage.a FImage.o

