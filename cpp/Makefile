CXX = g++
LLVM_DIR = ../llvm
LLVM_CONFIG = $(LLVM_DIR)/Release+Asserts/bin/llvm-config
LLVM_AS = $(LLVM_DIR)/Release+Asserts/bin/llvm-as
CLANG = clang
CXX_FLAGS = $(shell $(LLVM_CONFIG) --cflags) -Wall -Werror -fno-rtti -Os
LIBS = -L $(shell $(LLVM_CONFIG) --libdir) $(shell $(LLVM_CONFIG) --libs jit bitwriter bitreader x86 arm linker nvptx ipo)

SOURCE_FILES = CodeGen.cpp CodeGen_X86.cpp IR.cpp IRMutator.cpp IRPrinter.cpp IRVisitor.cpp CodeGen_C.cpp Substitute.cpp ModulusRemainder.cpp Bounds.cpp Derivative.cpp Func.cpp Simplify.cpp IREquality.cpp Util.cpp Function.cpp IROperator.cpp Lower.cpp Log.cpp

SOURCES = $(SOURCE_FILES:%.cpp=src/%.cpp)

OBJECTS = $(SOURCE_FILES:%.cpp=build/%.o)

STDLIB_ARCHS = x86 x86_avx ptx ptx_dev arm arm_android

STDLIB_SOURCES = \
	$(STDLIB_ARCHS:%=src/architecture.%.stdlib.cpp) \
	$(STDLIB_ARCHS:%=src/architecture.%.stdlib.ll) \
	src/architecture.posix.stdlib.cpp
INITIAL_MODULES = $(STDLIB_ARCHS:%=build/architecture.%.initmod.o)

.PHONY: all
all: bin/libHalide.a bin/test_all

bin/libHalide.a: $(OBJECTS) $(INITIAL_MODULES)
	@-mkdir -p bin
	ld -r -o build/Halide.o $(OBJECTS) $(INITIAL_MODULES) $(LIBS)
	rm -f bin/libHalide.a
	ar q bin/libHalide.a build/Halide.o
	ranlib bin/libHalide.a

INITMOD_OPTS_x86 = -march=corei7 
INITMOD_OPTS_ptx = -march=corei7 
INITMOD_OPTS_x86_avx = -march=corei7-avx 
INITMOD_OPTS_arm = -m32
INITMOD_OPTS_arm_android = -m32
INITMOD_OPTS_ptx_dev = ""

-include $(OBJECTS:.o=.d)

build/architecture.%.initmod.c: src/architecture.%.stdlib.cpp src/architecture.%.stdlib.ll src/architecture.posix.stdlib.cpp
	@-mkdir -p build
	$(CLANG) $(INITMOD_OPTS_$*) -emit-llvm -O3 -S src/architecture.$*.stdlib.cpp -o - | \
	cat - src/architecture.$*.stdlib.ll | \
	$(LLVM_AS) -o - | \
	python util/bitcode2cpp.py $* > $@

build/%.o: build/%.c
	$(CXX) -c $< -o $@ 

build/%.o: src/%.cpp src/%.h
	@-mkdir -p build
	$(CXX) $(CXX_FLAGS) -c $< -o $@ -MMD -MP -MF build/$*.d -MT build/$*.o 

bin/test_all: src/test_all.cpp bin/libHalide.a
	@-mkdir -p bin
	$(CXX) $(CXX_FLAGS) -rdynamic src/test_all.cpp -Lbin -lHalide -o bin/test_all -ldl -lpthread

.PHONY: clean
clean:
	rm -f bin/*
	rm -f build/*

.SECONDARY:

TESTS = \
bound \
bounds \
bounds_inference \
bounds_inference_chunk \
bounds_inference_complex \
c_function \
chunk \
chunk_sharing \
convolution \
fibonacci \
footprint \
gameoflife \
gpu_large_alloc \
histogram \
histogram_equalize \
input_image_bounds_check \
jit_stress \
logical \
mod \
parallel \
parallel_alloc \
parallel_nested \
parallel_performance \
partial_application \
reduction \
reduction_schedule \
reduction_subregion \
rehydrate \
side_effects \
simd_op_check \
split_reuse_inner_name_bug \
split_store_compute \
two_vector_args \
uniform \
unrolled_reduction \
vector_bounds_inference \
vector_cast \
vector_extern \
vectorize \
vector_math

tests: $(TESTS:%=test_%)

bin/test_%: test/%.cpp bin/libHalide.a
	$(CXX) $< -Isrc -Lbin -lHalide -lpthread -ldl -o $@	

test_%: bin/test_%
	@-mkdir -p tmp
	@-echo
	cd tmp ; ../$<


