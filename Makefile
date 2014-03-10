# 'make' builds libHalide.a, the internal test suite, and runs the internal test suite
# 'make run_tests' builds and runs all the end-to-end tests in the test subdirectory
# 'make {error,performance}_foo' builds and runs test/{...}/foo.cpp for any
#     cpp file in the corresponding subdirectoy of the test folder
# 'make test_foo' builds and runs test/correctness/foo.cpp for any
#     cpp file in the correctness/ subdirectoy of the test folder
# 'make test_apps' checks some of the apps build and run (but does not check their output)

CXX ?= g++
LLVM_CONFIG ?= llvm-config
LLVM_COMPONENTS= $(shell $(LLVM_CONFIG) --components)
LLVM_VERSION = $(shell $(LLVM_CONFIG) --version | cut -b 1-3)
CLANG ?= clang
CLANG_VERSION = $(shell $(CLANG) --version)
LLVM_BINDIR = $(shell $(LLVM_CONFIG) --bindir)
LLVM_LIBDIR = $(shell $(LLVM_CONFIG) --libdir)
LLVM_AS = $(LLVM_BINDIR)/llvm-as
LLVM_NM = $(LLVM_BINDIR)/llvm-nm
LLVM_CXX_FLAGS = $(shell $(LLVM_CONFIG) --cppflags)
OPTIMIZE ?= -O3
# This can be set to -m32 to get a 32-bit build of Halide on a 64-bit system.
# (Normally this can be done via pointing to a compiler that defaults to 32-bits,
#  but that is difficult in some testing situations because it requires having
#  such a compiler handy. One still needs to have 32-bit llvm libraries, etc.)
BUILD_BIT_SIZE ?=

LLVM_VERSION_TIMES_10 = $(shell $(LLVM_CONFIG) --version | cut -b 1,3)
LLVM_CXX_FLAGS += -DLLVM_VERSION=$(LLVM_VERSION_TIMES_10)

WITH_NATIVE_CLIENT ?= $(findstring nacltransforms, $(LLVM_COMPONENTS))
WITH_X86 ?= $(findstring x86, $(LLVM_COMPONENTS))
WITH_ARM ?= $(findstring arm, $(LLVM_COMPONENTS))
WITH_ARM64 ?= $(findstring aarch64, $(LLVM_COMPONENTS))
WITH_OPENCL ?= 1
WITH_SPIR ?= 1

# turn off PTX for llvm 3.2
ifneq ($(LLVM_VERSION), 3.2)
WITH_PTX ?= $(findstring nvptx, $(LLVM_COMPONENTS))
endif

# turn on c++11 for llvm 3.5
ifeq ($(LLVM_VERSION_TIMES_10), 35)
LLVM_CXX_FLAGS += -std=c++11
endif

NATIVE_CLIENT_CXX_FLAGS = $(if $(WITH_NATIVE_CLIENT), -DWITH_NATIVE_CLIENT=1, )
NATIVE_CLIENT_LLVM_CONFIG_LIB = $(if $(WITH_NATIVE_CLIENT), nacltransforms, )

X86_CXX_FLAGS=$(if $(WITH_X86), -DWITH_X86=1, )
X86_LLVM_CONFIG_LIB=$(if $(WITH_X86), x86, )

ARM_CXX_FLAGS=$(if $(WITH_ARM), -DWITH_ARM=1, )
ARM_LLVM_CONFIG_LIB=$(if $(WITH_ARM), arm, )

PTX_CXX_FLAGS=$(if $(WITH_PTX), -DWITH_PTX=1, )
PTX_LLVM_CONFIG_LIB=$(if $(WITH_PTX), nvptx, )
PTX_DEVICE_INITIAL_MODULES=$(if $(WITH_PTX), libdevice.compute_20.10.bc libdevice.compute_30.10.bc libdevice.compute_35.10.bc, )

OPENCL_CXX_FLAGS=$(if $(WITH_OPENCL), -DWITH_OPENCL=1, )
OPENCL_LLVM_CONFIG_LIB=$(if $(WITH_OPENCL), , )

SPIR_CXX_FLAGS=$(if $(WITH_SPIR), -DWITH_SPIR=1, )
SPIR_LLVM_CONFIG_LIB=$(if $(WITH_SPIR), , )

ARM64_CXX_FLAGS=$(if $(WITH_ARM64), -DWITH_ARM64=1, )
ARM64_LLVM_CONFIG_LIB=$(if $(WITH_ARM64), aarch64, )

CXX_FLAGS = -Wall -Werror -fno-rtti -Woverloaded-virtual -Wno-unused-function -fPIC $(OPTIMIZE) -DCOMPILING_HALIDE $(BUILD_BIT_SIZE)
CXX_FLAGS += $(LLVM_CXX_FLAGS)
CXX_FLAGS += $(NATIVE_CLIENT_CXX_FLAGS)
CXX_FLAGS += $(PTX_CXX_FLAGS)
CXX_FLAGS += $(ARM_CXX_FLAGS)
CXX_FLAGS += $(ARM64_CXX_FLAGS)
CXX_FLAGS += $(X86_CXX_FLAGS)
CXX_FLAGS += $(OPENCL_CXX_FLAGS)
CXX_FLAGS += $(SPIR_CXX_FLAGS)
LLVM_LIBS = -L $(LLVM_LIBDIR) $(shell $(LLVM_CONFIG) --libs bitwriter bitreader linker ipo mcjit jit $(X86_LLVM_CONFIG_LIB) $(ARM_LLVM_CONFIG_LIB) $(OPENCL_LLVM_CONFIG_LIB) $(SPIR_LLVM_CONFIG_LIB) $(NATIVE_CLIENT_LLVM_CONFIG_LIB) $(PTX_LLVM_CONFIG_LIB) $(ARM64_LLVM_CONFIG_LIB))
LLVM_LDFLAGS = $(shell $(LLVM_CONFIG) --ldflags)

# Remove some non-llvm libs that llvm-config has helpfully included
LIBS = $(filter-out -lrt -lz -lpthread -ldl , $(LLVM_LIBS))

ifneq ($(WITH_PTX), )
ifneq (,$(findstring ptx,$(HL_TARGET)))
TEST_PTX = 1
endif
ifneq (,$(findstring cuda,$(HL_TARGET)))
TEST_PTX = 1
endif
endif

ifneq ($(WITH_OPENCL), )
ifneq (,$(findstring opencl,$(HL_TARGET)))
TEST_OPENCL = 1
endif
endif
ifneq ($(WITH_SPIR), )
ifneq (,$(findstring spir,$(HL_TARGET)))
TEST_OPENCL = 1
endif
ifneq (,$(findstring spir64,$(HL_TARGET)))
TEST_OPENCL = 1
endif
endif

TEST_CXX_FLAGS ?= $(BUILD_BIT_SIZE)
UNAME = $(shell uname)
ifeq ($(UNAME), Linux)
TEST_CXX_FLAGS += -rdynamic
ifneq ($(TEST_PTX), )
STATIC_TEST_LIBS ?= -L/usr/lib/nvidia-current -lcuda
endif
HOST_OS=linux
endif

ifeq ($(UNAME), Darwin)
# Someone with an osx box with cuda installed please fix the line below
ifneq ($(TEST_PTX), )
STATIC_TEST_LIBS ?= -F/Library/Frameworks -framework CUDA
endif
HOST_OS=os_x
endif

ifneq ($(TEST_OPENCL), )
STATIC_TEST_LIBS ?= -lOpenCL
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

SOURCE_FILES = CodeGen.cpp CodeGen_Internal.cpp CodeGen_X86.cpp CodeGen_GPU_Host.cpp CodeGen_PTX_Dev.cpp CodeGen_OpenCL_Dev.cpp CodeGen_SPIR_Dev.cpp CodeGen_GPU_Dev.cpp CodeGen_Posix.cpp CodeGen_ARM.cpp IR.cpp IRMutator.cpp IRPrinter.cpp IRVisitor.cpp FindCalls.cpp CodeGen_C.cpp Substitute.cpp ModulusRemainder.cpp Bounds.cpp Derivative.cpp OneToOne.cpp Func.cpp Simplify.cpp IREquality.cpp Util.cpp Function.cpp IROperator.cpp Lower.cpp Debug.cpp Parameter.cpp Reduction.cpp RDom.cpp Profiling.cpp Tracing.cpp StorageFlattening.cpp VectorizeLoops.cpp UnrollLoops.cpp BoundsInference.cpp IRMatch.cpp StmtCompiler.cpp IntegerDivisionTable.cpp SlidingWindow.cpp StorageFolding.cpp InlineReductions.cpp RemoveTrivialForLoops.cpp Deinterleave.cpp DebugToFile.cpp Type.cpp JITCompiledModule.cpp EarlyFree.cpp UniquifyVariableNames.cpp CSE.cpp Tuple.cpp Lerp.cpp Target.cpp SkipStages.cpp SpecializeClampedRamps.cpp RemoveUndef.cpp FastIntegerDivide.cpp AllocationBoundsInference.cpp Inline.cpp Qualify.cpp UnifyDuplicateLets.cpp CodeGen_PNaCl.cpp ExprUsesVar.cpp Random.cpp

# The externally-visible header files that go into making Halide.h. Don't include anything here that includes llvm headers.
HEADER_FILES = Util.h Type.h Argument.h Bounds.h BoundsInference.h Buffer.h buffer_t.h CodeGen_C.h CodeGen.h CodeGen_X86.h CodeGen_GPU_Host.h CodeGen_PTX_Dev.h CodeGen_OpenCL_Dev.h CodeGen_SPIR_Dev.h CodeGen_GPU_Dev.h Deinterleave.h Derivative.h OneToOne.h Extern.h Func.h Function.h Image.h InlineReductions.h IntegerDivisionTable.h IntrusivePtr.h IREquality.h IR.h IRMatch.h IRMutator.h IROperator.h IRPrinter.h IRVisitor.h FindCalls.h JITCompiledModule.h Lambda.h Debug.h Lower.h MainPage.h ModulusRemainder.h Parameter.h Param.h RDom.h Reduction.h RemoveTrivialForLoops.h Schedule.h Scope.h Simplify.h SlidingWindow.h StmtCompiler.h StorageFlattening.h StorageFolding.h Substitute.h Profiling.h Tracing.h UnrollLoops.h Var.h VectorizeLoops.h CodeGen_Posix.h CodeGen_ARM.h DebugToFile.h EarlyFree.h UniquifyVariableNames.h CSE.h Tuple.h Lerp.h Target.h SkipStages.h SpecializeClampedRamps.h RemoveUndef.h FastIntegerDivide.h AllocationBoundsInference.h Inline.h Qualify.h UnifyDuplicateLets.h CodeGen_PNaCl.h ExprUsesVar.h Random.h

SOURCES = $(SOURCE_FILES:%.cpp=src/%.cpp)
OBJECTS = $(SOURCE_FILES:%.cpp=$(BUILD_DIR)/%.o)
HEADERS = $(HEADER_FILES:%.h=src/%.h)

RUNTIME_CPP_COMPONENTS = android_io cuda fake_thread_pool gcd_thread_pool ios_io android_clock linux_clock nogpu opencl posix_allocator posix_clock osx_clock windows_clock posix_error_handler posix_io nacl_io osx_io posix_math posix_thread_pool android_host_cpu_count linux_host_cpu_count osx_host_cpu_count tracing write_debug_image cuda_debug opencl_debug windows_io windows_thread_pool ssp
RUNTIME_LL_COMPONENTS = arm posix_math ptx_dev spir_dev spir64_dev spir_common_dev x86_avx x86 x86_sse41 pnacl_math

INITIAL_MODULES = $(RUNTIME_CPP_COMPONENTS:%=$(BUILD_DIR)/initmod.%_32.o) $(RUNTIME_CPP_COMPONENTS:%=$(BUILD_DIR)/initmod.%_64.o) $(RUNTIME_LL_COMPONENTS:%=$(BUILD_DIR)/initmod.%_ll.o) $(PTX_DEVICE_INITIAL_MODULES:libdevice.%.bc=$(BUILD_DIR)/initmod_ptx.%_ll.o)

.PHONY: all
all: $(BIN_DIR)/libHalide.a $(BIN_DIR)/libHalide.so include/Halide.h include/HalideRuntime.h test_internal

$(BIN_DIR)/libHalide.a: $(OBJECTS) $(INITIAL_MODULES)
	@-mkdir -p $(BIN_DIR)
	$(LD) -r -o $(BUILD_DIR)/Halide.o $(OBJECTS) $(INITIAL_MODULES) $(LIBS)
	rm -f $(BIN_DIR)/libHalide.a
	ar q $(BIN_DIR)/libHalide.a $(BUILD_DIR)/Halide.o
	ranlib $(BIN_DIR)/libHalide.a

$(BIN_DIR)/libHalide.so: $(BIN_DIR)/libHalide.a
	$(CXX) $(BUILD_BIT_SIZE) -shared $(OBJECTS) $(INITIAL_MODULES) $(LIBS) $(LLVM_LDFLAGS) -ldl -lpthread -o $(BIN_DIR)/libHalide.so

include/Halide.h: $(HEADERS) $(BIN_DIR)/build_halide_h
	mkdir -p include
	cd src; ../$(BIN_DIR)/build_halide_h $(HEADER_FILES) > ../include/Halide.h; cd ..

include/HalideRuntime.h: src/runtime/HalideRuntime.h
	mkdir -p include
	cp src/runtime/HalideRuntime.h include/

$(BIN_DIR)/build_halide_h: tools/build_halide_h.cpp
	g++ $< -o $@

msvc/initmod.cpp: $(INITIAL_MODULES)
	echo "extern \"C\" {" > msvc/initmod.cpp
	cat $(BUILD_DIR)/initmod*.cpp >> msvc/initmod.cpp
	echo "}" >> msvc/initmod.cpp

-include $(OBJECTS:.o=.d)

$(BUILD_DIR)/initmod.%_64.ll: src/runtime/%.cpp $(BUILD_DIR)/clang_ok
	@-mkdir -p $(BUILD_DIR)
	$(CLANG) -nobuiltininc -fno-blocks -m64 -target "x86_64-unknown-unknown-unknown" -DCOMPILING_HALIDE -DBITS_64 -emit-llvm -O3 -S src/runtime/$*.cpp -o $@ # -m64 isn't respected unless we also use a 64-bit target

$(BUILD_DIR)/initmod.%_32.ll: src/runtime/%.cpp $(BUILD_DIR)/clang_ok
	@-mkdir -p $(BUILD_DIR)
	$(CLANG) -nobuiltininc -fno-blocks -m32 -DCOMPILING_HALIDE -DBITS_32 -emit-llvm -O3 -S src/runtime/$*.cpp -o $@

$(BUILD_DIR)/initmod.%_ll.ll: src/runtime/%.ll
	@-mkdir -p $(BUILD_DIR)
	cp src/runtime/$*.ll $(BUILD_DIR)/initmod.$*_ll.ll

$(BUILD_DIR)/initmod.%.bc: $(BUILD_DIR)/initmod.%.ll $(BUILD_DIR)/llvm_ok
	$(LLVM_AS) $(BUILD_DIR)/initmod.$*.ll -o $(BUILD_DIR)/initmod.$*.bc

$(BUILD_DIR)/initmod.%.cpp: $(BIN_DIR)/bitcode2cpp $(BUILD_DIR)/initmod.%.bc
	./$(BIN_DIR)/bitcode2cpp $* < $(BUILD_DIR)/initmod.$*.bc > $@

$(BUILD_DIR)/initmod_ptx.%_ll.cpp: $(BIN_DIR)/bitcode2cpp src/runtime/nvidia_libdevice_bitcode/libdevice.%.bc
	./$(BIN_DIR)/bitcode2cpp ptx_$(basename $*)_ll < src/runtime/nvidia_libdevice_bitcode/libdevice.$*.bc > $@

$(BIN_DIR)/bitcode2cpp: tools/bitcode2cpp.cpp
	@-mkdir -p $(BIN_DIR)
	$(CXX) $< -o $@

$(BUILD_DIR)/initmod_ptx.%_ll.o: $(BUILD_DIR)/initmod_ptx.%_ll.cpp
	$(CXX) -c $< -o $@ -MMD -MP -MF $(BUILD_DIR)/$*.d -MT $(BUILD_DIR)/$*.o

$(BUILD_DIR)/initmod.%.o: $(BUILD_DIR)/initmod.%.cpp
	$(CXX) $(BUILD_BIT_SIZE) -c $< -o $@ -MMD -MP -MF $(BUILD_DIR)/$*.d -MT $(BUILD_DIR)/$*.o

$(BUILD_DIR)/%.o: src/%.cpp src/%.h $(BUILD_DIR)/llvm_ok
	@-mkdir -p $(BUILD_DIR)
	$(CXX) $(CXX_FLAGS) -c $< -o $@ -MMD -MP -MF $(BUILD_DIR)/$*.d -MT $(BUILD_DIR)/$*.o

.PHONY: clean
clean:
	rm -rf $(BIN_DIR)/*
	rm -rf $(BUILD_DIR)/*
	rm -rf include/*
	rm -rf doc

.SECONDARY:

CORRECTNESS_TESTS = $(shell ls test/correctness/*.cpp)
STATIC_TESTS = $(shell ls test/static/*_generate.cpp)
PERFORMANCE_TESTS = $(shell ls test/performance/*.cpp)
ERROR_TESTS = $(shell ls test/error/*.cpp)
WARNING_TESTS = $(shell ls test/warning/*.cpp)
TUTORIALS = $(shell ls tutorial/*.cpp)

STATIC_TEST_CXX ?= $(CXX)

LD_PATH_SETUP = DYLD_LIBRARY_PATH=${DYLD_LIBRARY_PATH}:../$(BIN_DIR) LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:../$(BIN_DIR)
LD_PATH_SETUP2 = DYLD_LIBRARY_PATH=${DYLD_LIBRARY_PATH}:../../$(BIN_DIR) LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:../../$(BIN_DIR)

test_correctness: $(CORRECTNESS_TESTS:test/correctness/%.cpp=test_%)
test_static: $(STATIC_TESTS:test/static/%_generate.cpp=static_%)
test_performance: $(PERFORMANCE_TESTS:test/performance/%.cpp=performance_%)
test_errors: $(ERROR_TESTS:test/error/%.cpp=error_%)
test_warnings: $(WARNING_TESTS:test/warning/%.cpp=warning_%)
test_tutorials: $(TUTORIALS:tutorial/%.cpp=tutorial_%)
test_valgrind: $(CORRECTNESS_TESTS:test/correctness/%.cpp=valgrind_%)

run_tests: test_correctness test_errors test_tutorials test_static
	make test_performance

build_tests: $(CORRECTNESS_TESTS:test/correctness/%.cpp=$(BIN_DIR)/test_%) \
	$(PERFORMANCE_TESTS:test/performance/%.cpp=$(BIN_DIR)/performance_%) \
	$(ERROR_TESTS:test/error/%.cpp=$(BIN_DIR)/error_%) \
	$(WARNING_TESTS:test/error/%.cpp=$(BIN_DIR)/warning_%) \
	$(STATIC_TESTS:test/static/%_generate.cpp=$(BIN_DIR)/static_%_generate) \
	$(TUTORIALS:tutorial/%.cpp=$(BIN_DIR)/tutorial_%)

$(BIN_DIR)/test_internal: test/internal.cpp $(BIN_DIR)/libHalide.so
	$(CXX) $(CXX_FLAGS)  $< -Isrc -L$(BIN_DIR) -lHalide $(LLVM_LDFLAGS) -lpthread -ldl -o $@

$(BIN_DIR)/test_%: test/correctness/%.cpp $(BIN_DIR)/libHalide.so include/Halide.h include/HalideRuntime.h
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE) $< -Iinclude -L$(BIN_DIR) -lHalide $(LLVM_LDFLAGS) -lpthread -ldl -o $@

$(BIN_DIR)/performance_%: test/performance/%.cpp $(BIN_DIR)/libHalide.so include/Halide.h test/performance/clock.h
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE) $< -Iinclude -L$(BIN_DIR) -lHalide $(LLVM_LDFLAGS) -lpthread -ldl -o $@

$(BIN_DIR)/error_%: test/error/%.cpp $(BIN_DIR)/libHalide.so include/Halide.h
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE) $< -Iinclude -L$(BIN_DIR) -lHalide $(LLVM_LDFLAGS) -lpthread -ldl -o $@

$(BIN_DIR)/warning_%: test/warning/%.cpp $(BIN_DIR)/libHalide.so include/Halide.h
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE) $< -Iinclude -L$(BIN_DIR) -lHalide $(LLVM_LDFLAGS) -lpthread -ldl -o $@

$(BIN_DIR)/static_%_generate: test/static/%_generate.cpp $(BIN_DIR)/libHalide.so include/Halide.h
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE) $< -Iinclude -L$(BIN_DIR) -lHalide $(LLVM_LDFLAGS) -lpthread -ldl -o $@

tmp/static/%.o: $(BIN_DIR)/static_%_generate
	@-mkdir -p tmp/static
	cd tmp/static; $(LD_PATH_SETUP2) ../../$<
	@-echo

$(BIN_DIR)/static_%_test: test/static/%_test.cpp $(BIN_DIR)/static_%_generate tmp/static/%.o include/HalideRuntime.h
	$(STATIC_TEST_CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE) -I tmp/static -I apps/support tmp/static/$*.o $< -lpthread $(STATIC_TEST_LIBS) -o $@

$(BIN_DIR)/tutorial_%: tutorial/%.cpp $(BIN_DIR)/libHalide.so include/Halide.h
	$(CXX) $(TEST_CXX_FLAGS) $(LIBPNG_CXX_FLAGS) $(OPTIMIZE) $< -Iinclude -L$(BIN_DIR) -lHalide $(LLVM_LDFLAGS) -lpthread -ldl $(LIBPNG_LIBS) -o $@

test_%: $(BIN_DIR)/test_%
	@-mkdir -p tmp
	cd tmp ; $(LD_PATH_SETUP) ../$<
	@-echo

static_%: $(BIN_DIR)/static_%_test
	@-mkdir -p tmp
	cd tmp ; $(LD_PATH_SETUP) $(STATIC_TEST_RUNNER) ../$<
	@-echo

valgrind_%: $(BIN_DIR)/test_%
	@-mkdir -p tmp
	cd tmp ; $(LD_PATH_SETUP) valgrind --error-exitcode=-1 ../$<
	@-echo

# This test is *supposed* to do an out-of-bounds read, so skip it when testing under valgrind
valgrind_tracing_stack: $(BIN_DIR)/test_tracing_stack
	@-mkdir -p tmp
	cd tmp ; $(LD_PATH_SETUP) ../$(BIN_DIR)/test_tracing_stack
	@-echo

performance_%: $(BIN_DIR)/performance_%
	@-mkdir -p tmp
	cd tmp ; $(LD_PATH_SETUP) ../$<
	@-echo

error_%: $(BIN_DIR)/error_%
	@-mkdir -p tmp
	cd tmp ; $(LD_PATH_SETUP) ../$< 2>&1 | egrep --q "Assertion.*failed"
	@-echo

warning_%: $(BIN_DIR)/warning_%
	@-mkdir -p tmp
	cd tmp ; $(LD_PATH_SETUP) ../$< 2>&1 | egrep --q "^Warning: "
	@-echo

tutorial_%: $(BIN_DIR)/tutorial_%
	@-mkdir -p tmp
	cd tmp ; $(LD_PATH_SETUP) ../$<
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

ifneq (,$(findstring Apple LLVM version 5.0,$(CLANG_VERSION)))
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

$(DISTRIB_DIR)/halide.tgz: $(BIN_DIR)/libHalide.a $(BIN_DIR)/libHalide.so include/Halide.h include/HalideRuntime.h
	mkdir -p $(DISTRIB_DIR)/include $(DISTRIB_DIR)/lib
	cp $(BIN_DIR)/libHalide.a $(BIN_DIR)/libHalide.so $(DISTRIB_DIR)/lib
	cp include/Halide.h $(DISTRIB_DIR)/include
	cp include/HalideRuntime.h $(DISTRIB_DIR)/include
	tar -czf $(DISTRIB_DIR)/halide.tgz -C $(DISTRIB_DIR) lib include

distrib: $(DISTRIB_DIR)/halide.tgz

$(BIN_DIR)/HalideProf: util/HalideProf.cpp
	$(CXX) $(OPTIMIZE) $< -Iinclude -L$(BIN_DIR) -o $@
