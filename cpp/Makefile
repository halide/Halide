# 'make' builds libHalide.a, the internal test suite, and runs the internal test suite
# 'make tests' builds and runs all the end-to-end tests in the test subdirectory
# 'make test_foo' builds and runs test/foo.cpp for any cpp file in the test folder
# 'make test_apps' checks some of the apps build and run (but does not check their output)

CXX ?= g++
LLVM_DIR = ../llvm
LLVM_CONFIG = $(LLVM_DIR)/Release+Asserts/bin/llvm-config
LLVM_AS = $(LLVM_DIR)/Release+Asserts/bin/llvm-as
CLANG = clang
CXX_FLAGS = $(shell $(LLVM_CONFIG) --cflags) -Wall -Werror -fno-rtti -Os
LIBS = -L $(shell $(LLVM_CONFIG) --libdir) $(shell $(LLVM_CONFIG) --libs jit bitwriter bitreader x86 arm linker nvptx ipo)

SOURCE_FILES = CodeGen.cpp CodeGen_X86.cpp IR.cpp IRMutator.cpp IRPrinter.cpp IRVisitor.cpp CodeGen_C.cpp Substitute.cpp ModulusRemainder.cpp Bounds.cpp Derivative.cpp Func.cpp Simplify.cpp IREquality.cpp Util.cpp Function.cpp IROperator.cpp Lower.cpp Log.cpp Parameter.cpp Reduction.cpp RDom.cpp Tracing.cpp RemoveDeadLets.cpp StorageFlattening.cpp VectorizeLoops.cpp UnrollLoops.cpp BoundsInference.cpp IRMatch.cpp StmtCompiler.cpp integer_division_table.cpp SlidingWindow.cpp StorageFolding.cpp InlineReductions.cpp RemoveTrivialForLoops.cpp

SOURCES = $(SOURCE_FILES:%.cpp=src/%.cpp)

OBJECTS = $(SOURCE_FILES:%.cpp=build/%.o)

STDLIB_ARCHS = x86 x86_avx ptx ptx_dev arm arm_android

STDLIB_SOURCES = \
	$(STDLIB_ARCHS:%=src/architecture.%.stdlib.cpp) \
	$(STDLIB_ARCHS:%=src/architecture.%.stdlib.ll) \
	src/architecture.posix.stdlib.cpp
INITIAL_MODULES = $(STDLIB_ARCHS:%=build/architecture.%.initmod.o)

.PHONY: all
all: bin/libHalide.a bin/test_internal
	./bin/test_internal

bin/libHalide.a: $(OBJECTS) $(INITIAL_MODULES)
	@-mkdir -p bin
	ld -r -o build/Halide.o $(OBJECTS) $(INITIAL_MODULES) $(LIBS)
	rm -f bin/libHalide.a
	ar q bin/libHalide.a build/Halide.o
	ranlib bin/libHalide.a

INITMOD_OPTS_x86 = -march=corei7 -mattr=-avx
INITMOD_OPTS_ptx = -march=corei7 
INITMOD_OPTS_x86_avx = -march=corei7-avx -mattr=+avx
INITMOD_OPTS_arm = -m32
INITMOD_OPTS_arm_android = -m32
INITMOD_OPTS_ptx_dev = ""

-include $(OBJECTS:.o=.d)

build/architecture.%.initmod.cpp: bin/bitcode2cpp src/architecture.%.stdlib.cpp src/architecture.%.stdlib.ll src/architecture.posix.stdlib.cpp
	@-mkdir -p build
	$(CLANG) $(INITMOD_OPTS_$*) -emit-llvm -O3 -S src/architecture.$*.stdlib.cpp -o - | \
	cat - src/architecture.$*.stdlib.ll | \
	$(LLVM_AS) -o - | \
	./bin/bitcode2cpp $* > $@

bin/bitcode2cpp: src/bitcode2cpp.cpp
	@-mkdir -p bin
	$(CXX) $< -o $@

build/architecture.%.initmod.o: build/architecture.%.initmod.cpp
	$(CXX) -c $< -o $@ 

build/%.o: src/%.cpp src/%.h
	@-mkdir -p build
	$(CXX) $(CXX_FLAGS) -c $< -o $@ -MMD -MP -MF build/$*.d -MT build/$*.o 

.PHONY: clean
clean:
	rm -f bin/*
	rm -f build/*

.SECONDARY:

TESTS = $(shell ls test/*.cpp)

tests: $(TESTS:test/%.cpp=test_%)

bin/test_internal: test/internal.cpp bin/libHalide.a
	$(CXX) $(CXX_FLAGS) -rdynamic $< -Isrc -Lbin -lHalide -lpthread -ldl -o $@	

bin/test_%: test/%.cpp bin/libHalide.a
	$(CXX) -g $< -rdynamic -Isrc -Lbin -lHalide -lpthread -ldl -o $@	

test_%: bin/test_%
	@-mkdir -p tmp
	cd tmp ; ../$<
	@-echo


.PHONY: test_apps
test_apps: bin/libHalide.a
	make -C apps/bilateral_grid clean
	make -C apps/bilateral_grid out.png
	make -C apps/local_laplacian clean
	make -C apps/local_laplacian out.png
	make -C apps/interpolate clean
	make -C apps/interpolate out.png
	make -C apps/blur clean
	make -C apps/blur test
	./apps/blur/test
	make -C apps/wavelet filter
	cd apps/wavelet; ./filter input.png; cd ../..