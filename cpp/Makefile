# 'make' builds libHalide.a, the internal test suite, and runs the internal test suite
# 'make tests' builds and runs all the end-to-end tests in the test subdirectory
# 'make test_foo' builds and runs test/foo.cpp for any cpp file in the test folder
# 'make test_apps' checks some of the apps build and run (but does not check their output)

CXX ?= g++
LLVM_CONFIG ?= llvm-config
LLVM_VERSION = $(shell $(LLVM_CONFIG) --version)
CLANG ?= clang
CLANG_VERSION = $(shell $(CLANG) --version)
LLVM_BINDIR = $(shell $(LLVM_CONFIG) --bindir)
LLVM_AS = $(LLVM_BINDIR)/llvm-as
CXX_FLAGS = $(shell $(LLVM_CONFIG) --cflags) -Wall -Werror -fno-rtti -Woverloaded-virtual -Wno-unused-function -Os
LIBS = -L $(shell $(LLVM_CONFIG) --libdir) $(shell $(LLVM_CONFIG) --libs bitwriter bitreader x86 arm linker nvptx ipo mcjit jit)
TEST_CXX_FLAGS=
UNAME = $(shell uname)
ifeq ($(UNAME), Linux)
TEST_CXX_FLAGS += -rdynamic
endif

SOURCE_FILES = CodeGen.cpp CodeGen_X86.cpp CodeGen_Posix.cpp CodeGen_ARM.cpp IR.cpp IRMutator.cpp IRPrinter.cpp IRVisitor.cpp CodeGen_C.cpp Substitute.cpp ModulusRemainder.cpp Bounds.cpp Derivative.cpp Func.cpp Simplify.cpp IREquality.cpp Util.cpp Function.cpp IROperator.cpp Lower.cpp Log.cpp Parameter.cpp Reduction.cpp RDom.cpp Tracing.cpp RemoveDeadLets.cpp StorageFlattening.cpp VectorizeLoops.cpp UnrollLoops.cpp BoundsInference.cpp IRMatch.cpp StmtCompiler.cpp integer_division_table.cpp SlidingWindow.cpp StorageFolding.cpp InlineReductions.cpp RemoveTrivialForLoops.cpp Deinterleave.cpp DebugToFile.cpp

HEADER_FILES = Util.h Type.h Argument.h Bounds.h BoundsInference.h Buffer.h buffer_t.h CodeGen_C.h CodeGen.h CodeGen_X86.h Deinterleave.h Derivative.h Extern.h Func.h Function.h Image.h InlineReductions.h integer_division_table.h IntrusivePtr.h IREquality.h IR.h IRMatch.h IRMutator.h IROperator.h IRPrinter.h IRVisitor.h JITCompiledModule.h Lambda.h Log.h Lower.h MainPage.h ModulusRemainder.h Parameter.h Param.h RDom.h Reduction.h RemoveDeadLets.h RemoveTrivialForLoops.h Schedule.h Scope.h Simplify.h SlidingWindow.h StmtCompiler.h StorageFlattening.h StorageFolding.h Substitute.h Tracing.h UnrollLoops.h Var.h VectorizeLoops.h CodeGen_Posix.h CodeGen_ARM.h DebugToFile.h

SOURCES = $(SOURCE_FILES:%.cpp=src/%.cpp)
OBJECTS = $(SOURCE_FILES:%.cpp=build/%.o)
HEADERS = $(HEADER_FILES:%.h=src/%.h)

STDLIB_ARCHS = x86 x86_avx arm arm_android

INITIAL_MODULES = $(STDLIB_ARCHS:%=build/initmod.%.o)

.PHONY: all
all: bin/libHalide.a bin/libHalide.so include/Halide.h test_internal

bin/libHalide.a: $(OBJECTS) $(INITIAL_MODULES)
	@-mkdir -p bin
	ld -r -o build/Halide.o $(OBJECTS) $(INITIAL_MODULES) $(LIBS)
	rm -f bin/libHalide.a
	ar q bin/libHalide.a build/Halide.o
	ranlib bin/libHalide.a

bin/libHalide.so: bin/libHalide.a
	$(CXX) -shared $(OBJECTS) $(INITIAL_MODULES) $(LIBS) -o bin/libHalide.so

include/Halide.h: $(HEADERS) bin/build_halide_h
	mkdir -p include
	cd src; ../bin/build_halide_h $(HEADER_FILES) > ../include/Halide.h; cd ..

bin/build_halide_h: src/build_halide_h.cpp
	g++ $< -o $@

RUNTIME_OPTS_x86 = -march=corei7 
RUNTIME_OPTS_x86_avx = -march=corei7-avx 
RUNTIME_OPTS_arm = -m32 
RUNTIME_OPTS_arm_android = -m32 
RUNTIME_LL_STUBS_x86 = src/runtime/x86.ll
RUNTIME_LL_STUBS_x86_avx = src/runtime/x86.ll src/runtime/x86_avx.ll
RUNTIME_LL_STUBS_arm = src/runtime/arm.ll
RUNTIME_LL_STUBS_arm_android = src/runtime/arm.ll

-include $(OBJECTS:.o=.d)

build/initmod.%.cpp: bin/bitcode2cpp src/runtime/*.cpp src/runtime/*.ll build/llvm_ok build/clang_ok
	@-mkdir -p build
	$(CLANG) $(RUNTIME_OPTS_$*) -emit-llvm -O3 -S src/runtime/runtime.$*.cpp -o - | \
	cat - $(RUNTIME_LL_STUBS_$*) | \
	$(LLVM_AS) -o - | \
	./bin/bitcode2cpp $* > $@

bin/bitcode2cpp: src/bitcode2cpp.cpp
	@-mkdir -p bin
	$(CXX) $< -o $@

build/initmod.%.o: build/initmod.%.cpp
	$(CXX) -c $< -o $@ -MMD -MP -MF build/$*.d -MT build/$*.o

build/%.o: src/%.cpp src/%.h build/llvm_ok
	@-mkdir -p build
	$(CXX) $(CXX_FLAGS) -c $< -o $@ -MMD -MP -MF build/$*.d -MT build/$*.o 

.PHONY: clean
clean:
	rm -rf bin/*
	rm -rf build/*
	rm -rf include/*

.SECONDARY:

TESTS = $(shell ls test/*.cpp)

# TODO: move this implementation into Makefile.tests which contains a .NOTPARALLEL rule?
tests: $(TESTS:test/%.cpp=test_%)

bin/test_internal: test/internal.cpp bin/libHalide.so
	$(CXX) $(CXX_FLAGS)  $< -Isrc -Lbin -lHalide -lpthread -ldl -o $@	

bin/test_%: test/%.cpp bin/libHalide.so include/Halide.h
	$(CXX) $(TEST_CXX_FLAGS) -g $<  -Iinclude -Lbin -lHalide -lpthread -ldl -o $@	

test_%: bin/test_%
	@-mkdir -p tmp
	cd tmp ; DYLD_LIBRARY_PATH=../bin LD_LIBRARY_PATH=../bin ../$<
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


ifneq (,$(findstring version 3.,$(CLANG_VERSION)))
ifeq (,$(findstring version 3.0,$(CLANG_VERSION)))
CLANG_OK=yes
endif
endif

ifneq (,$(findstring Apple clang version 4.0,$(CLANG_VERSION)))
CLANG_OK=yes
endif

ifneq (,$(findstring 3.,$(LLVM_VERSION)))
ifeq (,$(findstring 3.0,$(LLVM_VERSION)))
ifeq (,$(findstring 3.1,$(LLVM_VERSION)))
LLVM_OK=yes
endif
endif
endif

ifdef CLANG_OK
build/clang_ok:
	@echo "Found a new enough version of clang"
	mkdir -p build
	touch build/clang_ok
else
build/clang_ok:
	@echo "Can't find clang or version of clang too old (we need 3.1 or greater):"
	@echo "You can override this check by setting CLANG_OK=y"
	echo '$(CLANG_VERSION)'
	echo $(findstring version 3,$(CLANG_VERSION))
	echo $(findstring version 3.0,$(CLANG_VERSION))
	$(CLANG) --version
	@exit 1
endif

ifdef LLVM_OK
build/llvm_ok:
	@echo "Found a new enough version of llvm"
	mkdir -p build
	touch build/llvm_ok
else
build/llvm_ok:
	@echo "Can't find llvm or version of llvm too old (we need 3.2 or greater):"
	@echo "You can override this check by setting LLVM_OK=y"
	$(LLVM_CONFIG) --version
	@exit 1
endif

