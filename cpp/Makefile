CXX = g++
LLVM_DIR = ../llvm
LLVM_CONFIG = $(LLVM_DIR)/Release+Asserts/bin/llvm-config
CXX_FLAGS = $(shell $(LLVM_CONFIG) --cflags) -Wall -Werror -fno-rtti -Os
LIBS = -L $(shell $(LLVM_CONFIG) --libdir) $(shell $(LLVM_CONFIG) --libs jit bitwriter bitreader x86 arm linker nvptx ipo)


OBJECTS = CodeGen.o CodeGen_X86.o IR.o IRMutator.o IRPrinter.o IRVisitor.o CodeGen_C.o Substitute.o ModulusRemainder.o Bounds.o Derivative.o Func.o Simplify.o IREquality.o Util.o Function.o IROperator.o Lower.o
INITIAL_MODULES = architecture.x86.initmod.o architecture.x86_avx.initmod.o architecture.ptx.initmod.o architecture.ptx_dev.initmod.o architecture.arm.initmod.o architecture.arm_android.initmod.o

all: libHalide.a test_all

libHalide.a: $(OBJECTS) $(INITIAL_MODULES)
	ld -r -o Halide.o $(OBJECTS) $(INITIAL_MODULES) $(LIBS)
	rm -f libHalide.a
	ar q libHalide.a Halide.o
	ranlib libHalide.a

INITMOD_OPTS_x86 = -march=corei7 -mattr=-avx
INITMOD_OPTS_ptx = -march=corei7 -mattr=-avx
INITMOD_OPTS_x86_avx = -march=corei7-avx -mattr=+avx
INITMOD_OPTS_arm = -m32
INITMOD_OPTS_arm_android = -m32
INITMOD_OPTS_ptx_dev = ""

architecture.%.initmod.c: architecture.%.stdlib.cpp architecture.%.stdlib.ll architecture.posix.stdlib.cpp
	clang $(INITMOD_OPTS_$*) -emit-llvm -O3 -S architecture.$*.stdlib.cpp -o - | \
	cat - architecture.$*.stdlib.ll | \
	llvm-as -o - | \
	python bitcode2cpp.py $* > $@

%.o: %.c
	$(CXX) -c $< -o $@

%.o: %.cpp %.h
	$(CXX) $(CXX_FLAGS) -c $< -o $@	

test_all: test_all.cpp libHalide.a
	$(CXX) $(CXX_FLAGS) -rdynamic test_all.cpp -L. -lHalide -o test_all -ldl -lpthread

clean:
	rm -f libHalide.a
	rm -f $(OBJECTS)
	rm -f architecture.*.initmod.c architecture.*.initmod.o
	rm -f test_all
