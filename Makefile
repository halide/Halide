# 'make' builds libHalide.a, the internal test suite, and runs the internal test suite
# 'make run_tests' builds and runs all the end-to-end tests in the test subdirectory
# 'make {correctness,error,performance}_foo' builds and runs test/{...}/foo.cpp for any
#     cpp file in the corresponding subdirectoy of the test folder
# 'make test_apps' checks some of the apps build and run (but does not check their output)

CXX ?= g++
LLVM_CONFIG ?= llvm-config
LLVM_COMPONENTS= $(shell $(LLVM_CONFIG) --components)
LLVM_VERSION = $(shell $(LLVM_CONFIG) --version)
CLANG ?= clang
CLANG_VERSION = $(shell $(CLANG) --version)
LLVM_BINDIR = $(shell $(LLVM_CONFIG) --bindir)
LLVM_LIBDIR = $(shell $(LLVM_CONFIG) --libdir)
LLVM_AS = $(LLVM_BINDIR)/llvm-as
LLVM_NM = $(LLVM_BINDIR)/llvm-nm
LLVM_CXX_FLAGS = $(shell $(LLVM_CONFIG) --cppflags)
OPTIMIZE ?= -O3

# llvm_config doesn't always point to the correct include
# directory. We haven't figured out why yet.
LLVM_CXX_FLAGS += -I$(shell $(LLVM_CONFIG) --src-root)/include

WITH_NATIVE_CLIENT ?= $(findstring nacltransforms, $(LLVM_COMPONENTS))
WITH_X86 ?= $(findstring x86, $(LLVM_COMPONENTS))
WITH_ARM ?= $(findstring arm, $(LLVM_COMPONENTS))
WITH_PTX ?= $(findstring nvptx, $(LLVM_COMPONENTS))
WITH_OPENCL ?= 1

NATIVE_CLIENT_CXX_FLAGS = $(if $(WITH_NATIVE_CLIENT), -DWITH_NATIVE_CLIENT=1, )
NATIVE_CLIENT_ARCHS =
NATIVE_CLIENT_LLVM_CONFIG_LIB = $(if $(WITH_NATIVE_CLIENT), nacltransforms, )

ifneq ($(WITH_NATIVE_CLIENT), )

ifneq ($(WITH_X86), )
ifndef NATIVE_CLIENT_X86_INCLUDE
$(error Compiling with x86 native client support but NATIVE_CLIENT_X86_INCLUDE not defined)
endif
NATIVE_CLIENT_ARCHS += x86_32_nacl x86_32_sse41_nacl x86_64_nacl x86_64_sse41_nacl x86_64_avx_nacl
endif

ifneq ($(WITH_ARM), )
ifndef NATIVE_CLIENT_ARM_INCLUDE
$(error Compiling with arm native client support but NATIVE_CLIENT_ARM_INCLUDE not defined)
endif
NATIVE_CLIENT_ARCHS += arm_nacl
endif

endif


X86_CXX_FLAGS=$(if $(WITH_X86), -DWITH_X86=1, )
X86_ARCHS=$(if $(WITH_X86), x86_32 x86_32_sse41 x86_64 x86_64_sse41 x86_64_avx, )
X86_LLVM_CONFIG_LIB=$(if $(WITH_X86), x86, )

ARM_CXX_FLAGS=$(if $(WITH_ARM), -DWITH_ARM=1, )
ARM_ARCHS=$(if $(WITH_ARM), arm arm_android arm_ios , )
ARM_LLVM_CONFIG_LIB=$(if $(WITH_ARM), arm, )

PTX_CXX_FLAGS=$(if $(WITH_PTX), -DWITH_PTX=1, )
PTX_ARCHS=$(if $(WITH_PTX), ptx_host ptx_host_debug ptx_dev, )
PTX_LLVM_CONFIG_LIB=$(if $(WITH_PTX), nvptx, )

OPENCL_CXX_FLAGS=$(if $(WITH_OPENCL), -DWITH_OPENCL=1, )
OPENCL_ARCHS=$(if $(WITH_OPENCL), opencl_host, )
OPENCL_LLVM_CONFIG_LIB=$(if $(WITH_OPENCL), , )

CXX_FLAGS = -Wall -Werror -fno-rtti -Woverloaded-virtual -Wno-unused-function -fPIC $(OPTIMIZE)
CXX_FLAGS += $(LLVM_CXX_FLAGS)
CXX_FLAGS += $(NATIVE_CLIENT_CXX_FLAGS)
CXX_FLAGS += $(PTX_CXX_FLAGS)
CXX_FLAGS += $(ARM_CXX_FLAGS)
CXX_FLAGS += $(X86_CXX_FLAGS)
CXX_FLAGS += $(OPENCL_CXX_FLAGS)
LIBS = -L $(LLVM_LIBDIR) $(shell $(LLVM_CONFIG) --libs bitwriter bitreader linker ipo mcjit jit $(X86_LLVM_CONFIG_LIB) $(ARM_LLVM_CONFIG_LIB) $(OPENCL_LLVM_CONFIG_LIB) $(NATIVE_CLIENT_LLVM_CONFIG_LIB) $(PTX_LLVM_CONFIG_LIB))

TEST_CXX_FLAGS ?=
UNAME = $(shell uname)
ifeq ($(UNAME), Linux)
TEST_CXX_FLAGS += -rdynamic
HOST_OS=linux
endif

ifeq ($(UNAME), Darwin)
HOST_OS=os_x
endif

# Compiling the tutorials requires libpng
LIBPNG_LIBS_DEFAULT = $(shell libpng-config --ldflags)
LIBPNG_CXX_FLAGS ?= $(shell libpng-config --cflags)
# Workaround for libpng-config pointing to 64-bit versions on linux even when we're building for 32-bit
ifneq (,$(findstring -m32,$(CXX)))
ifneq (,$(findstring x86_64,$(LIBPNG_LIBS_DEFAULT)))
LIBPNG_LIBS ?= -lpng
endif
endif
LIBPNG_LIBS ?= $(LIBPNG_LIBS_DEFAULT)

ifdef BUILD_PREFIX
BUILD_DIR = build/$(BUILD_PREFIX)
BIN_DIR = bin/$(BUILD_PREFIX)
DISTRIB_DIR=distrib/$(BUILD_PREFIX)
else
BUILD_DIR = build
BIN_DIR = bin
DISTRIB_DIR=distrib
endif

SOURCE_FILES = CodeGen.cpp CodeGen_Internal.cpp CodeGen_X86.cpp CodeGen_GPU_Host.cpp CodeGen_PTX_Dev.cpp CodeGen_OpenCL_Dev.cpp CodeGen_GPU_Dev.cpp CodeGen_Posix.cpp CodeGen_ARM.cpp IR.cpp IRMutator.cpp IRPrinter.cpp IRVisitor.cpp CodeGen_C.cpp Substitute.cpp ModulusRemainder.cpp Bounds.cpp Derivative.cpp OneToOne.cpp Func.cpp Simplify.cpp IREquality.cpp Util.cpp Function.cpp IROperator.cpp Lower.cpp Debug.cpp Parameter.cpp Reduction.cpp RDom.cpp Profiling.cpp Tracing.cpp StorageFlattening.cpp VectorizeLoops.cpp UnrollLoops.cpp BoundsInference.cpp IRMatch.cpp StmtCompiler.cpp integer_division_table.cpp SlidingWindow.cpp StorageFolding.cpp InlineReductions.cpp RemoveTrivialForLoops.cpp Deinterleave.cpp DebugToFile.cpp Type.cpp JITCompiledModule.cpp EarlyFree.cpp UniquifyVariableNames.cpp CSE.cpp Tuple.cpp Lerp.cpp Target.cpp

# The externally-visible header files that go into making Halide.h. Don't include anything here that includes llvm headers.
HEADER_FILES = Util.h Type.h Argument.h Bounds.h BoundsInference.h Buffer.h buffer_t.h CodeGen_C.h CodeGen.h CodeGen_X86.h CodeGen_GPU_Host.h CodeGen_PTX_Dev.h CodeGen_OpenCL_Dev.h CodeGen_GPU_Dev.h Deinterleave.h Derivative.h OneToOne.h Extern.h Func.h Function.h Image.h InlineReductions.h integer_division_table.h IntrusivePtr.h IREquality.h IR.h IRMatch.h IRMutator.h IROperator.h IRPrinter.h IRVisitor.h JITCompiledModule.h Lambda.h Debug.h Lower.h MainPage.h ModulusRemainder.h Parameter.h Param.h RDom.h Reduction.h RemoveTrivialForLoops.h Schedule.h Scope.h Simplify.h SlidingWindow.h StmtCompiler.h StorageFlattening.h StorageFolding.h Substitute.h Profiling.h Tracing.h UnrollLoops.h Var.h VectorizeLoops.h CodeGen_Posix.h CodeGen_ARM.h DebugToFile.h EarlyFree.h UniquifyVariableNames.h CSE.h Tuple.h Lerp.h Target.h

SOURCES = $(SOURCE_FILES:%.cpp=src/%.cpp)
OBJECTS = $(SOURCE_FILES:%.cpp=$(BUILD_DIR)/%.o)
HEADERS = $(HEADER_FILES:%.h=src/%.h)

RUNTIME_CPP_COMPONENTS = android_io cuda fake_thread_pool gcd_thread_pool ios_io android_clock linux_clock nogpu opencl posix_allocator posix_clock posix_error_handler posix_io posix_math posix_thread_pool android_host_cpu_count linux_host_cpu_count osx_host_cpu_count tracing write_debug_image cuda_debug opencl_debug
RUNTIME_LL_COMPONENTS = arm posix_math ptx_dev x86_avx x86 x86_sse41

INITIAL_MODULES = $(RUNTIME_CPP_COMPONENTS:%=$(BUILD_DIR)/initmod.%_32.o) $(RUNTIME_CPP_COMPONENTS:%=$(BUILD_DIR)/initmod.%_64.o) $(RUNTIME_LL_COMPONENTS:%=$(BUILD_DIR)/initmod.%_ll.o)

.PHONY: all
all: $(BIN_DIR)/libHalide.a $(BIN_DIR)/libHalide.so include/Halide.h include/HalideRuntime.h test_internal

$(BIN_DIR)/libHalide.a: $(OBJECTS) $(INITIAL_MODULES)
	@-mkdir -p $(BIN_DIR)
	$(LD) -r -o $(BUILD_DIR)/Halide.o $(OBJECTS) $(INITIAL_MODULES) $(LIBS)
	rm -f $(BIN_DIR)/libHalide.a
	ar q $(BIN_DIR)/libHalide.a $(BUILD_DIR)/Halide.o
	ranlib $(BIN_DIR)/libHalide.a

$(BIN_DIR)/libHalide.so: $(BIN_DIR)/libHalide.a
	$(CXX) -shared $(OBJECTS) $(INITIAL_MODULES) $(LIBS) -o $(BIN_DIR)/libHalide.so

include/Halide.h: $(HEADERS) $(BIN_DIR)/build_halide_h
	mkdir -p include
	cd src; ../$(BIN_DIR)/build_halide_h $(HEADER_FILES) > ../include/Halide.h; cd ..

include/HalideRuntime.h: src/runtime/HalideRuntime.h
	mkdir -p include
	cp src/runtime/HalideRuntime.h include/

$(BIN_DIR)/build_halide_h: src/build_halide_h.cpp
	g++ $< -o $@

-include $(OBJECTS:.o=.d)

$(BUILD_DIR)/initmod.%_64.ll: src/runtime/%.cpp $(BUILD_DIR)/clang_ok
	@-mkdir -p $(BUILD_DIR)
	$(CLANG) -fno-blocks -m64 -DBITS_64 -emit-llvm -O3 -S src/runtime/$*.cpp -o $@

$(BUILD_DIR)/initmod.%_32.ll: src/runtime/%.cpp $(BUILD_DIR)/clang_ok
	@-mkdir -p $(BUILD_DIR)
	$(CLANG) -fno-blocks -m32 -DBITS_32 -emit-llvm -O3 -S src/runtime/$*.cpp -o $@

$(BUILD_DIR)/initmod.%_ll.ll: src/runtime/%.ll
	cp src/runtime/$*.ll $(BUILD_DIR)/initmod.$*_ll.ll

$(BUILD_DIR)/initmod.%.bc: $(BUILD_DIR)/initmod.%.ll $(BUILD_DIR)/llvm_ok
	$(LLVM_AS) $(BUILD_DIR)/initmod.$*.ll -o $(BUILD_DIR)/initmod.$*.bc

$(BUILD_DIR)/initmod.%.cpp: $(BIN_DIR)/bitcode2cpp $(BUILD_DIR)/initmod.%.bc
	./$(BIN_DIR)/bitcode2cpp $* < $(BUILD_DIR)/initmod.$*.bc > $@

$(BIN_DIR)/bitcode2cpp: src/bitcode2cpp.cpp
	@-mkdir -p $(BIN_DIR)
	$(CXX) $< -o $@

$(BUILD_DIR)/initmod.%.o: $(BUILD_DIR)/initmod.%.cpp
	$(CXX) -c $< -o $@ -MMD -MP -MF $(BUILD_DIR)/$*.d -MT $(BUILD_DIR)/$*.o

$(BUILD_DIR)/%.o: src/%.cpp src/%.h $(BUILD_DIR)/llvm_ok
	@-mkdir -p $(BUILD_DIR)
	$(CXX) $(CXX_FLAGS) -c $< -o $@ -MMD -MP -MF $(BUILD_DIR)/$*.d -MT $(BUILD_DIR)/$*.o

.PHONY: clean
clean:
	rm -rf $(BIN_DIR)/*
	rm -rf $(BUILD_DIR)/*
	rm -rf $(DISTRIB_DIR)/*
	rm -rf include/*
	rm -rf doc

.SECONDARY:

CORRECTNESS_TESTS = $(shell ls test/correctness/*.cpp)
PERFORMANCE_TESTS = $(shell ls test/performance/*.cpp)
ERROR_TESTS = $(shell ls test/error/*.cpp)
TUTORIALS = $(shell ls tutorial/*.cpp)

test_correctness: $(CORRECTNESS_TESTS:test/correctness/%.cpp=test_%)
test_performance: $(PERFORMANCE_TESTS:test/performance/%.cpp=performance_%)
test_errors: $(ERROR_TESTS:test/error/%.cpp=error_%)
test_tutorials: $(TUTORIALS:tutorial/%.cpp=tutorial_%)
test_valgrind: $(CORRECTNESS_TESTS:test/correctness/%.cpp=valgrind_%)

run_tests: test_correctness test_errors test_tutorials
	make test_performance

build_tests: $(CORRECTNESS_TESTS:test/correctness/%.cpp=$(BIN_DIR)/test_%) \
	$(PERFORMANCE_TESTS:test/performance/%.cpp=$(BIN_DIR)/performance_%) \
	$(ERROR_TESTS:test/error/%.cpp=$(BIN_DIR)/error_%) \
	$(TUTORIALS:tutorial/%.cpp=$(BIN_DIR)/tutorial_%)

$(BIN_DIR)/test_internal: test/internal.cpp $(BIN_DIR)/libHalide.so
	$(CXX) $(CXX_FLAGS)  $< -Isrc -L$(BIN_DIR) -lHalide -lpthread -ldl -o $@

$(BIN_DIR)/test_%: test/correctness/%.cpp $(BIN_DIR)/libHalide.so include/Halide.h
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE) $< -Iinclude -L$(BIN_DIR) -lHalide -lpthread -ldl -o $@

$(BIN_DIR)/performance_%: test/performance/%.cpp $(BIN_DIR)/libHalide.so include/Halide.h test/performance/clock.h
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE) $< -Iinclude -L$(BIN_DIR) -lHalide -lpthread -ldl -o $@

$(BIN_DIR)/error_%: test/error/%.cpp $(BIN_DIR)/libHalide.so include/Halide.h
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE) $< -Iinclude -L$(BIN_DIR) -lHalide -lpthread -ldl -o $@

$(BIN_DIR)/tutorial_%: tutorial/%.cpp $(BIN_DIR)/libHalide.so include/Halide.h
	$(CXX) $(TEST_CXX_FLAGS) $(LIBPNG_CXX_FLAGS) $(OPTIMIZE) $< -Iinclude -L$(BIN_DIR) -lHalide -lpthread -ldl $(LIBPNG_LIBS) -o $@

test_%: $(BIN_DIR)/test_%
	@-mkdir -p tmp
	cd tmp ; DYLD_LIBRARY_PATH=../$(BIN_DIR) LD_LIBRARY_PATH=../$(BIN_DIR) ../$<
	@-echo

valgrind_%: $(BIN_DIR)/test_%
	@-mkdir -p tmp
	cd tmp ; DYLD_LIBRARY_PATH=../$(BIN_DIR) LD_LIBRARY_PATH=../$(BIN_DIR) valgrind --error-exitcode=-1 ../$<
	@-echo

# This test is *supposed* to do an out-of-bounds read, so skip it when testing under valgrind
valgrind_tracing_stack: $(BIN_DIR)/test_tracing_stack
	@-mkdir -p tmp
	cd tmp ; DYLD_LIBRARY_PATH=../$(BIN_DIR) LD_LIBRARY_PATH=../$(BIN_DIR) ../$(BIN_DIR)/test_tracing_stack
	@-echo

performance_%: $(BIN_DIR)/performance_%
	@-mkdir -p tmp
	cd tmp ; DYLD_LIBRARY_PATH=../$(BIN_DIR) LD_LIBRARY_PATH=../$(BIN_DIR) ../$<
	@-echo

error_%: $(BIN_DIR)/error_%
	@-mkdir -p tmp
	cd tmp ; DYLD_LIBRARY_PATH=../$(BIN_DIR) LD_LIBRARY_PATH=../$(BIN_DIR) ../$< 2>&1 | egrep --q "Assertion.*failed"
	@-echo

tutorial_%: $(BIN_DIR)/tutorial_%
	@-mkdir -p tmp
	cd tmp ; DYLD_LIBRARY_PATH=../$(BIN_DIR) LD_LIBRARY_PATH=../$(BIN_DIR) ../$<
	@-echo

.PHONY: test_apps
test_apps: $(BIN_DIR)/libHalide.a include/Halide.h
	make -C apps/bilateral_grid clean
	make -C apps/bilateral_grid out.png
	make -C apps/local_laplacian clean
	make -C apps/local_laplacian out.png
	make -C apps/interpolate clean
	make -C apps/interpolate out.png
	make -C apps/blur clean
	make -C apps/blur test
	./apps/blur/test
	make -C apps/wavelet clean
	make -C apps/wavelet test
	make -C apps/c_backend clean
	make -C apps/c_backend test

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

ifneq (,$findstring 3.3.,$(LLVM_VERSION))
LLVM_OK=yes
endif
ifneq (,$findstring 3.2.,$(LLVM_VERSION))
LLVM_OK=yes
endif

ifdef CLANG_OK
$(BUILD_DIR)/clang_ok:
	@echo "Found a new enough version of clang"
	mkdir -p $(BUILD_DIR)
	touch $(BUILD_DIR)/clang_ok
else
$(BUILD_DIR)/clang_ok:
	@echo "Can't find clang or version of clang too old (we need 3.1 or greater):"
	@echo "You can override this check by setting CLANG_OK=y"
	echo '$(CLANG_VERSION)'
	echo $(findstring version 3,$(CLANG_VERSION))
	echo $(findstring version 3.0,$(CLANG_VERSION))
	$(CLANG) --version
	@exit 1
endif

ifdef LLVM_OK
$(BUILD_DIR)/llvm_ok:
	@echo "Found a new enough version of llvm"
	mkdir -p $(BUILD_DIR)
	touch $(BUILD_DIR)/llvm_ok
else
$(BUILD_DIR)/llvm_ok:
	@echo "Can't find llvm or version of llvm too old (we need 3.2 or greater):"
	@echo "You can override this check by setting LLVM_OK=y"
	$(LLVM_CONFIG) --version
	@exit 1
endif

.PHONY: doc
docs: doc
doc: src test
	doxygen

$(DISTRIB_DIR)/halide.tgz: all
	mkdir -p $(DISTRIB_DIR)/include $(DISTRIB_DIR)/lib
	cp $(BIN_DIR)/libHalide.a $(BIN_DIR)/libHalide.so $(DISTRIB_DIR)/lib
	cp include/Halide.h $(DISTRIB_DIR)/include
	cp include/HalideRuntime.h $(DISTRIB_DIR)/include
	tar -czf $(DISTRIB_DIR)/halide.tgz -C $(DISTRIB_DIR) lib include

distrib: $(DISTRIB_DIR)/halide.tgz

$(BIN_DIR)/HalideProf: util/HalideProf.cpp
	$(CXX) $(OPTIMIZE) $< -Iinclude -L$(BIN_DIR) -o $@
