# 'make' builds libHalide.a, the internal test suite, and runs the internal test suite
# 'make run_tests' builds and runs all the end-to-end tests in the test subdirectory
# 'make {error,performance}_foo' builds and runs test/{...}/foo.cpp for any
#     cpp file in the corresponding subdirectoy of the test folder
# 'make test_foo' builds and runs test/correctness/foo.cpp for any
#     cpp file in the correctness/ subdirectoy of the test folder
# 'make test_apps' checks some of the apps build and run (but does not check their output)
# 'make time_compilation_tests' records the compile time for each test module into a csv file.
#     For correctness and performance tests this include halide build time and run time. For
#     the tests in test/generator/ this times only the halide build time.

SHELL = bash
CXX ?= g++
LLVM_CONFIG ?= llvm-config
LLVM_COMPONENTS= $(shell $(LLVM_CONFIG) --components)
LLVM_VERSION = $(shell $(LLVM_CONFIG) --version | cut -b 1-3)
LLVM_FULL_VERSION = $(shell $(LLVM_CONFIG) --version)
CLANG ?= clang
CLANG_VERSION = $(shell $(CLANG) --version)
LLVM_BINDIR = $(shell $(LLVM_CONFIG) --bindir)
LLVM_LIBDIR = $(shell $(LLVM_CONFIG) --libdir)
LLVM_AS = $(LLVM_BINDIR)/llvm-as
LLVM_NM = $(LLVM_BINDIR)/llvm-nm
LLVM_CXX_FLAGS = -std=c++11  $(filter-out -O% -g -fomit-frame-pointer -pedantic -Wcovered-switch-default, $(shell $(LLVM_CONFIG) --cxxflags))
OPTIMIZE ?= -O3
# This can be set to -m32 to get a 32-bit build of Halide on a 64-bit system.
# (Normally this can be done via pointing to a compiler that defaults to 32-bits,
#  but that is difficult in some testing situations because it requires having
#  such a compiler handy. One still needs to have 32-bit llvm libraries, etc.)
BUILD_BIT_SIZE ?=
TEST_CXX_FLAGS ?= -std=c++11 $(BUILD_BIT_SIZE) -g -fno-omit-frame-pointer -fno-rtti $(CXX_WARNING_FLAGS)
# The tutorials contain example code with warnings that we don't want to be flagged as errors.
TUTORIAL_CXX_FLAGS ?= -std=c++11 $(BUILD_BIT_SIZE) -g -fno-omit-frame-pointer -fno-rtti
GENGEN_DEPS ?= $(BIN_DIR)/libHalide.so $(INCLUDE_DIR)/Halide.h $(ROOT_DIR)/tools/GenGen.cpp

LLVM_VERSION_TIMES_10 = $(shell $(LLVM_CONFIG) --version | cut -b 1,3)
LLVM_CXX_FLAGS += -DLLVM_VERSION=$(LLVM_VERSION_TIMES_10)

# All WITH_* flags are either empty or not-empty. They do not behave
# like true/false values in most languages.  To turn one off, either
# edit this file, add "WITH_FOO=" (no assigned value) to the make
# line, or define an environment variable WITH_FOO that has an empty
# value.
WITH_NATIVE_CLIENT ?= $(findstring nacltransforms, $(LLVM_COMPONENTS))
WITH_X86 ?= $(findstring x86, $(LLVM_COMPONENTS))
WITH_ARM ?= $(findstring arm, $(LLVM_COMPONENTS))
WITH_MIPS ?= $(findstring mips, $(LLVM_COMPONENTS))
WITH_AARCH64 ?= $(findstring aarch64, $(LLVM_COMPONENTS))
WITH_PTX ?= $(findstring nvptx, $(LLVM_COMPONENTS))
WITH_OPENCL ?= not-empty
WITH_OPENGL ?= not-empty
WITH_RENDERSCRIPT ?= not-empty
WITH_INTROSPECTION ?= not-empty
WITH_EXCEPTIONS ?=

# If HL_TARGET or HL_JIT_TARGET aren't set, use host
HL_TARGET ?= host
HL_JIT_TARGET ?= host

NATIVE_CLIENT_CXX_FLAGS = $(if $(WITH_NATIVE_CLIENT), -DWITH_NATIVE_CLIENT=1, )
NATIVE_CLIENT_LLVM_CONFIG_LIB = $(if $(WITH_NATIVE_CLIENT), nacltransforms, )

X86_CXX_FLAGS=$(if $(WITH_X86), -DWITH_X86=1, )
X86_LLVM_CONFIG_LIB=$(if $(WITH_X86), x86, )

ARM_CXX_FLAGS=$(if $(WITH_ARM), -DWITH_ARM=1, )
ARM_LLVM_CONFIG_LIB=$(if $(WITH_ARM), arm, )

MIPS_CXX_FLAGS=$(if $(WITH_MIPS), -DWITH_MIPS=1, )
MIPS_LLVM_CONFIG_LIB=$(if $(WITH_MIPS), mips, )

PTX_CXX_FLAGS=$(if $(WITH_PTX), -DWITH_PTX=1, )
PTX_LLVM_CONFIG_LIB=$(if $(WITH_PTX), nvptx, )
PTX_DEVICE_INITIAL_MODULES=$(if $(WITH_PTX), libdevice.compute_20.10.bc libdevice.compute_30.10.bc libdevice.compute_35.10.bc, )

OPENCL_CXX_FLAGS=$(if $(WITH_OPENCL), -DWITH_OPENCL=1, )
OPENCL_LLVM_CONFIG_LIB=$(if $(WITH_OPENCL), , )

OPENGL_CXX_FLAGS=$(if $(WITH_OPENGL), -DWITH_OPENGL=1, )

RENDERSCRIPT_CXX_FLAGS=$(if $(WITH_RENDERSCRIPT), -DWITH_RENDERSCRIPT=1, )

AARCH64_CXX_FLAGS=$(if $(WITH_AARCH64), -DWITH_AARCH64=1, )
AARCH64_LLVM_CONFIG_LIB=$(if $(WITH_AARCH64), aarch64, )

INTROSPECTION_CXX_FLAGS=$(if $(WITH_INTROSPECTION), -DWITH_INTROSPECTION, )
EXCEPTIONS_CXX_FLAGS=$(if $(WITH_EXCEPTIONS), -DWITH_EXCEPTIONS, )

CXX_WARNING_FLAGS = -Wall -Werror -Wno-unused-function -Wcast-qual
CXX_FLAGS = $(CXX_WARNING_FLAGS) -fno-rtti -Woverloaded-virtual -fPIC $(OPTIMIZE) -fno-omit-frame-pointer -DCOMPILING_HALIDE $(BUILD_BIT_SIZE)
CXX_FLAGS += $(LLVM_CXX_FLAGS)
CXX_FLAGS += $(NATIVE_CLIENT_CXX_FLAGS)
CXX_FLAGS += $(PTX_CXX_FLAGS)
CXX_FLAGS += $(ARM_CXX_FLAGS)
CXX_FLAGS += $(AARCH64_CXX_FLAGS)
CXX_FLAGS += $(X86_CXX_FLAGS)
CXX_FLAGS += $(OPENCL_CXX_FLAGS)
CXX_FLAGS += $(OPENGL_CXX_FLAGS)
CXX_FLAGS += $(RENDERSCRIPT_CXX_FLAGS)
CXX_FLAGS += $(MIPS_CXX_FLAGS)
CXX_FLAGS += $(INTROSPECTION_CXX_FLAGS)
CXX_FLAGS += $(EXCEPTIONS_CXX_FLAGS)

ifeq ($(LLVM_VERSION_TIMES_10), 35)
LLVM_OLD_JIT_COMPONENT = jit
endif

print-%:
	@echo '$*=$($*)'

ifeq ($(USE_LLVM_SHARED_LIB), )
LLVM_STATIC_LIBS = -L $(LLVM_LIBDIR) $(shell $(LLVM_CONFIG) --libs bitwriter bitreader linker ipo mcjit $(LLVM_OLD_JIT_COMPONENT) $(X86_LLVM_CONFIG_LIB) $(ARM_LLVM_CONFIG_LIB) $(OPENCL_LLVM_CONFIG_LIB) $(NATIVE_CLIENT_LLVM_CONFIG_LIB) $(PTX_LLVM_CONFIG_LIB) $(AARCH64_LLVM_CONFIG_LIB) $(MIPS_LLVM_CONFIG_LIB))
LLVM_SHARED_LIBS =
else
LLVM_STATIC_LIBS =
LLVM_SHARED_LIBS = -L $(LLVM_LIBDIR) -lLLVM-$(LLVM_FULL_VERSION)
endif

LLVM_LDFLAGS = $(shell $(LLVM_CONFIG) --ldflags --system-libs)

UNAME = $(shell uname)

OPENGL_LDFLAGS =
ifneq ($(WITH_OPENGL), )
ifeq ($(UNAME), Linux)
OPENGL_LDFLAGS = -lX11 -lGL
endif
ifeq ($(UNAME), Darwin)
OPENGL_LDFLAGS = -framework OpenGL -framework AGL
endif
endif

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

ifneq ($(WITH_RENDERSCRIPT), )
ifneq (,$(findstring renderscript,$(HL_TARGET)))
TEST_RENDERSCRIPT = 1
endif
endif

ifeq ($(UNAME), Linux)
TEST_CXX_FLAGS += -rdynamic
ifneq ($(TEST_PTX), )
CUDA_LDFLAGS ?= -L/usr/lib/nvidia-current -lcuda
endif
ifneq ($(TEST_OPENCL), )
OPENCL_LDFLAGS ?= -lOpenCL
endif
HOST_OS=linux
endif

ifeq ($(UNAME), Darwin)
# Someone with an osx box with cuda installed please fix the line below
ifneq ($(TEST_PTX), )
CUDA_LDFLAGS ?= -L/usr/local/cuda/lib -lcuda
endif
ifneq ($(TEST_OPENCL), )
OPENCL_LDFLAGS ?= -framework OpenCL
endif
HOST_OS=os_x
endif

ifneq ($(TEST_OPENCL), )
TEST_CXX_FLAGS += -DTEST_OPENCL
endif

ifneq ($(TEST_PTX), )
TEST_CXX_FLAGS += -DTEST_PTX
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

# We're building into the current directory $(CURDIR). Find the Halide
# repo root directory (the location of the makefile)
THIS_MAKEFILE = $(realpath $(filter %Makefile, $(MAKEFILE_LIST)))
ROOT_DIR = $(strip $(shell dirname $(THIS_MAKEFILE)))
SRC_DIR  = $(ROOT_DIR)/src

# The following directories are all relative to the output directory (i.e. $(CURDIR), not $(SRC_DIR))
BIN_DIR     = bin
DISTRIB_DIR = distrib
INCLUDE_DIR = include
DOC_DIR     = doc
BUILD_DIR   = $(BIN_DIR)/build
FILTERS_DIR = $(BUILD_DIR)/filters
TMP_DIR     = $(BUILD_DIR)/tmp

SOURCE_FILES = \
  AddImageChecks.cpp \
  AddParameterChecks.cpp \
  AllocationBoundsInference.cpp \
  BlockFlattening.cpp \
  BoundaryConditions.cpp \
  Bounds.cpp \
  BoundsInference.cpp \
  Buffer.cpp \
  CodeGen_ARM.cpp \
  CodeGen_C.cpp \
  CodeGen_GPU_Dev.cpp \
  CodeGen_GPU_Host.cpp \
  CodeGen_Internal.cpp \
  CodeGen_LLVM.cpp \
  CodeGen_MIPS.cpp \
  CodeGen_OpenCL_Dev.cpp \
  CodeGen_OpenGL_Dev.cpp \
  CodeGen_OpenGLCompute_Dev.cpp \
  CodeGen_PNaCl.cpp \
  CodeGen_Posix.cpp \
  CodeGen_PTX_Dev.cpp \
  CodeGen_Renderscript_Dev.cpp \
  CodeGen_X86.cpp \
  CSE.cpp \
  Debug.cpp \
  DebugToFile.cpp \
  Deinterleave.cpp \
  Derivative.cpp \
  DeviceInterface.cpp \
  EarlyFree.cpp \
  Error.cpp \
  ExprUsesVar.cpp \
  FastIntegerDivide.cpp \
  FindCalls.cpp \
  Func.cpp \
  Function.cpp \
  FuseGPUThreadLoops.cpp \
  Generator.cpp \
  Image.cpp \
  InjectHostDevBufferCopies.cpp \
  InjectImageIntrinsics.cpp \
  InjectOpenGLIntrinsics.cpp \
  Inline.cpp \
  InlineReductions.cpp \
  IntegerDivisionTable.cpp \
  Introspection.cpp \
  IR.cpp \
  IREquality.cpp \
  IRMatch.cpp \
  IRMutator.cpp \
  IROperator.cpp \
  IRPrinter.cpp \
  IRVisitor.cpp \
  JITModule.cpp \
  Lerp.cpp \
  LLVM_Output.cpp \
  LLVM_Runtime_Linker.cpp \
  Lower.cpp \
  MatlabWrapper.cpp \
  Memoization.cpp \
  Module.cpp \
  ModulusRemainder.cpp \
  ObjectInstanceRegistry.cpp \
  OneToOne.cpp \
  Output.cpp \
  ParallelRVar.cpp \
  Param.cpp \
  Parameter.cpp \
  PartitionLoops.cpp \
  Pipeline.cpp \
  PrintLoopNest.cpp \
  Profiling.cpp \
  Qualify.cpp \
  Random.cpp \
  RDom.cpp \
  RealizationOrder.cpp \
  Reduction.cpp \
  RemoveDeadAllocations.cpp \
  RemoveTrivialForLoops.cpp \
  RemoveUndef.cpp \
  Schedule.cpp \
  ScheduleFunctions.cpp \
  SelectGPUAPI.cpp \
  Simplify.cpp \
  SkipStages.cpp \
  SlidingWindow.cpp \
  Solve.cpp \
  StmtToHtml.cpp \
  StorageFlattening.cpp \
  StorageFolding.cpp \
  Substitute.cpp \
  Target.cpp \
  Tracing.cpp \
  Tuple.cpp \
  Type.cpp \
  UnifyDuplicateLets.cpp \
  UniquifyVariableNames.cpp \
  UnrollLoops.cpp \
  Util.cpp \
  Var.cpp \
  VaryingAttributes.cpp \
  VectorizeLoops.cpp

ifeq ($(LLVM_VERSION_TIMES_10),35)
BITWRITER_VERSION=.35
else
BITWRITER_VERSION = $(if $(WITH_NATIVE_CLIENT),.35,)
endif

BITWRITER_SOURCE_FILES = \
  BitWriter_3_2$(BITWRITER_VERSION)/BitcodeWriter.cpp \
  BitWriter_3_2$(BITWRITER_VERSION)/BitcodeWriterPass.cpp \
  BitWriter_3_2$(BITWRITER_VERSION)/ValueEnumerator.cpp

# The externally-visible header files that go into making Halide.h. Don't include anything here that includes llvm headers.
HEADER_FILES = \
  AddImageChecks.h \
  AddParameterChecks.h \
  AllocationBoundsInference.h \
  Argument.h \
  BlockFlattening.h \
  BoundaryConditions.h \
  Bounds.h \
  BoundsInference.h \
  Buffer.h \
  CodeGen_ARM.h \
  CodeGen_C.h \
  CodeGen_GPU_Dev.h \
  CodeGen_GPU_Host.h \
  CodeGen_LLVM.h \
  CodeGen_MIPS.h \
  CodeGen_OpenCL_Dev.h \
  CodeGen_OpenGL_Dev.h \
  CodeGen_OpenGLCompute_Dev.h \
  CodeGen_PNaCl.h \
  CodeGen_Posix.h \
  CodeGen_PTX_Dev.h \
  CodeGen_Renderscript_Dev.h \
  CodeGen_X86.h \
  CSE.h \
  Debug.h \
  DebugToFile.h \
  Deinterleave.h \
  Derivative.h \
  DeviceInterface.h \
  EarlyFree.h \
  Error.h \
  Expr.h \
  ExprUsesVar.h \
  Extern.h \
  FastIntegerDivide.h \
  FindCalls.h \
  Func.h \
  Function.h \
  FuseGPUThreadLoops.h \
  Generator.h \
  runtime/HalideRuntime.h \
  Image.h \
  InjectHostDevBufferCopies.h \
  InjectImageIntrinsics.h \
  InjectOpenGLIntrinsics.h \
  Inline.h \
  InlineReductions.h \
  IntegerDivisionTable.h \
  Introspection.h \
  IntrusivePtr.h \
  IREquality.h \
  IR.h \
  IRMatch.h \
  IRMutator.h \
  IROperator.h \
  IRPrinter.h \
  IRVisitor.h \
  JITModule.h \
  Lambda.h \
  Lerp.h \
  LLVM_Output.h \
  LLVM_Runtime_Linker.h \
  Lower.h \
  MainPage.h \
  MatlabWrapper.h \
  Memoization.h \
  Module.h \
  ModulusRemainder.h \
  ObjectInstanceRegistry.h \
  OneToOne.h \
  Output.h \
  ParallelRVar.h \
  Parameter.h \
  Param.h \
  PartitionLoops.h \
  Pipeline.h \
  Profiling.h \
  Qualify.h \
  Random.h \
  RealizationOrder.h \
  RDom.h \
  Reduction.h \
  RemoveDeadAllocations.h \
  RemoveTrivialForLoops.h \
  RemoveUndef.h \
  Schedule.h \
  ScheduleFunctions.h \
  Scope.h \
  SelectGPUAPI.h \
  Simplify.h \
  SkipStages.h \
  SlidingWindow.h \
  Solve.h \
  StmtToHtml.h \
  StorageFlattening.h \
  StorageFolding.h \
  Substitute.h \
  Target.h \
  Tracing.h \
  Tuple.h \
  Type.h \
  UnifyDuplicateLets.h \
  UniquifyVariableNames.h \
  UnrollLoops.h \
  Util.h \
  Var.h \
  VectorizeLoops.h

OBJECTS = $(SOURCE_FILES:%.cpp=$(BUILD_DIR)/%.o)
OBJECTS += $(BITWRITER_SOURCE_FILES:%.cpp=$(BUILD_DIR)/%.o)
HEADERS = $(HEADER_FILES:%.h=$(SRC_DIR)/%.h)

RUNTIME_CPP_COMPONENTS = \
  android_clock \
  android_host_cpu_count \
  android_io \
  android_opengl_context \
  cache \
  cuda \
  destructors \
  device_interface \
  fake_thread_pool \
  gcd_thread_pool \
  gpu_device_selection \
  ios_io \
  linux_clock \
  linux_host_cpu_count \
  linux_opengl_context \
  matlab \
  metadata \
  module_aot_ref_count \
  module_jit_ref_count \
  nacl_host_cpu_count \
  opencl \
  opengl \
  openglcompute \
  osx_clock \
  osx_get_symbol \
  osx_host_cpu_count \
  osx_opengl_context \
  posix_allocator \
  posix_clock \
  posix_error_handler \
  posix_get_symbol \
  posix_io \
  posix_math \
  posix_print \
  posix_thread_pool \
  profiler \
  profiler_inlined \
  renderscript \
  runtime_api \
  ssp \
  to_string \
  tracing \
  windows_clock \
  windows_cuda \
  windows_get_symbol \
  windows_io \
  windows_opencl \
  windows_thread_pool \
  write_debug_image

RUNTIME_LL_COMPONENTS = \
  aarch64 \
  arm \
  arm_no_neon \
  mips \
  pnacl_math \
  posix_math \
  ptx_dev \
  renderscript_dev \
  win32_math \
  x86 \
  x86_avx \
  x86_sse41

RUNTIME_EXPORTED_INCLUDES = $(INCLUDE_DIR)/HalideRuntime.h $(INCLUDE_DIR)/HalideRuntimeCuda.h \
                            $(INCLUDE_DIR)/HalideRuntimeOpenCL.h \
                            $(INCLUDE_DIR)/HalideRuntimeOpenGL.h \
                            $(INCLUDE_DIR)/HalideRuntimeOpenGLCompute.h \
                            $(INCLUDE_DIR)/HalideRuntimeRenderscript.h

INITIAL_MODULES = $(RUNTIME_CPP_COMPONENTS:%=$(BUILD_DIR)/initmod.%_32.o) \
                  $(RUNTIME_CPP_COMPONENTS:%=$(BUILD_DIR)/initmod.%_64.o) \
                  $(RUNTIME_CPP_COMPONENTS:%=$(BUILD_DIR)/initmod.%_32_debug.o) \
                  $(RUNTIME_CPP_COMPONENTS:%=$(BUILD_DIR)/initmod.%_64_debug.o) \
                  $(RUNTIME_LL_COMPONENTS:%=$(BUILD_DIR)/initmod.%_ll.o) \
                  $(PTX_DEVICE_INITIAL_MODULES:libdevice.%.bc=$(BUILD_DIR)/initmod_ptx.%_ll.o)

.PHONY: all
all: $(BIN_DIR)/libHalide.a $(BIN_DIR)/libHalide.so $(INCLUDE_DIR)/Halide.h $(RUNTIME_EXPORTED_INCLUDES) test_internal

ifeq ($(USE_LLVM_SHARED_LIB), )
$(BIN_DIR)/libHalide.a: $(OBJECTS) $(INITIAL_MODULES)
	# Determine the relevant object files from llvm with a dummy
	# compilation. Passing -t to the linker gets it to list which
	# object files in which archives it uses to resolve
	# symbols. We only care about the libLLVM ones.
	@rm -rf $(BUILD_DIR)/llvm_objects
	@mkdir -p $(BUILD_DIR)/llvm_objects
	$(CXX) -o /dev/null -shared $(OBJECTS) $(INITIAL_MODULES) -Wl,-t $(LLVM_STATIC_LIBS) -ldl -lz -lpthread | grep libLLVM | sed "s/[()]/ /g" > $(BUILD_DIR)/llvm_objects/list
	# Extract the necessary object files from the llvm archives.
	cd $(BUILD_DIR)/llvm_objects; xargs -n2 ar x < list
	# Archive together all the halide and llvm object files
	@-mkdir -p $(BIN_DIR)
	@rm -f $(BIN_DIR)/libHalide.a
	ar q $(BIN_DIR)/libHalide.a $(OBJECTS) $(INITIAL_MODULES) $(BUILD_DIR)/llvm_objects/*.o
	ranlib $(BIN_DIR)/libHalide.a
else
$(BIN_DIR)/libHalide.a: $(OBJECTS) $(INITIAL_MODULES)
	@-mkdir -p $(BIN_DIR)
	@rm -f $(BIN_DIR)/libHalide.a
	ar q $(BIN_DIR)/libHalide.a $(OBJECTS) $(INITIAL_MODULES)
	ranlib $(BIN_DIR)/libHalide.a
endif

$(BIN_DIR)/libHalide.so: $(BIN_DIR)/libHalide.a
	$(CXX) $(BUILD_BIT_SIZE) -shared $(OBJECTS) $(INITIAL_MODULES) $(LLVM_STATIC_LIBS) $(LLVM_LDFLAGS) $(LLVM_SHARED_LIBS) -ldl -lz -lpthread -o $(BIN_DIR)/libHalide.so

$(INCLUDE_DIR)/Halide.h: $(HEADERS) $(SRC_DIR)/HalideFooter.h $(BIN_DIR)/build_halide_h
	mkdir -p $(INCLUDE_DIR)
	$(BIN_DIR)/build_halide_h $(HEADERS) $(SRC_DIR)/HalideFooter.h > $(INCLUDE_DIR)/Halide.h

$(INCLUDE_DIR)/HalideRuntime%: $(SRC_DIR)/runtime/HalideRuntime%
	echo Copying $<
	mkdir -p $(INCLUDE_DIR)
	cp $< $(INCLUDE_DIR)/

$(BIN_DIR)/build_halide_h: $(ROOT_DIR)/tools/build_halide_h.cpp
	g++ $< -o $@

-include $(OBJECTS:.o=.d)
-include $(INITIAL_MODULES:.o=.d)

ifeq ($(LLVM_VERSION_TIMES_10),35)
RUNTIME_TRIPLE_32 = "i386-unknown-unknown-unknown"
RUNTIME_TRIPLE_64 = "x86_64-unknown-unknown-unknown"
else
# Compile generic 32- or 64-bit code
RUNTIME_TRIPLE_32 = "le32-unknown-nacl-unknown"
RUNTIME_TRIPLE_64 = "le64-unknown-unknown-unknown"
endif

# win32 is tied to x86 due to the use of the __stdcall calling convention
RUNTIME_TRIPLE_WIN_32 = "i386-unknown-unknown-unknown"

# -m64 isn't respected unless we also use a 64-bit target
$(BUILD_DIR)/initmod.%_64.ll: $(SRC_DIR)/runtime/%.cpp $(BUILD_DIR)/clang_ok
	@-mkdir -p $(BUILD_DIR)
	$(CLANG) $(CXX_WARNING_FLAGS) -O3 -ffreestanding -fno-blocks -fno-exceptions -fno-unwind-tables -m64 -target $(RUNTIME_TRIPLE_64) -DCOMPILING_HALIDE_RUNTIME -DBITS_64 -emit-llvm -S $(SRC_DIR)/runtime/$*.cpp -o $@ -MMD -MP -MF $(BUILD_DIR)/initmod.$*_64.d

$(BUILD_DIR)/initmod.windows_%_32.ll: $(SRC_DIR)/runtime/windows_%.cpp $(BUILD_DIR)/clang_ok
	@-mkdir -p $(BUILD_DIR)
	$(CLANG) $(CXX_WARNING_FLAGS) -O3 -ffreestanding -fno-blocks -fno-exceptions -fno-unwind-tables -m32 -target $(RUNTIME_TRIPLE_WIN_32) -DCOMPILING_HALIDE_RUNTIME -DBITS_32 -emit-llvm -S $(SRC_DIR)/runtime/windows_$*.cpp -o $@ -MMD -MP -MF $(BUILD_DIR)/initmod.windows_$*_32.d

$(BUILD_DIR)/initmod.%_32.ll: $(SRC_DIR)/runtime/%.cpp $(BUILD_DIR)/clang_ok
	@-mkdir -p $(BUILD_DIR)
	$(CLANG) $(CXX_WARNING_FLAGS) -O3 -ffreestanding -fno-blocks -fno-exceptions -fno-unwind-tables -m32 -target $(RUNTIME_TRIPLE_32) -DCOMPILING_HALIDE_RUNTIME -DBITS_32 -emit-llvm -S $(SRC_DIR)/runtime/$*.cpp -o $@ -MMD -MP -MF $(BUILD_DIR)/initmod.$*_32.d

$(BUILD_DIR)/initmod.%_64_debug.ll: $(SRC_DIR)/runtime/%.cpp $(BUILD_DIR)/clang_ok
	@-mkdir -p $(BUILD_DIR)
	$(CLANG) $(CXX_WARNING_FLAGS) -g -DDEBUG_RUNTIME -O3 -ffreestanding -fno-blocks -fno-exceptions -m64 -target  $(RUNTIME_TRIPLE_64) -DCOMPILING_HALIDE_RUNTIME -DBITS_64 -emit-llvm -S $(SRC_DIR)/runtime/$*.cpp -o $@ -MMD -MP -MF $(BUILD_DIR)/initmod.$*_64_debug.d

$(BUILD_DIR)/initmod.windows_%_32_debug.ll: $(SRC_DIR)/runtime/windows_%.cpp $(BUILD_DIR)/clang_ok
	@-mkdir -p $(BUILD_DIR)
	$(CLANG) $(CXX_WARNING_FLAGS) -g -DDEBUG_RUNTIME -O3 -ffreestanding -fno-blocks -fno-exceptions -m32 -target $(RUNTIME_TRIPLE_WIN_32) -DCOMPILING_HALIDE_RUNTIME -DBITS_32 -emit-llvm -S $(SRC_DIR)/runtime/windows_$*.cpp -o $@ -MMD -MP -MF $(BUILD_DIR)/initmod.windows_$*_32_debug.d

$(BUILD_DIR)/initmod.%_32_debug.ll: $(SRC_DIR)/runtime/%.cpp $(BUILD_DIR)/clang_ok
	@-mkdir -p $(BUILD_DIR)
	$(CLANG) $(CXX_WARNING_FLAGS) -g -DDEBUG_RUNTIME -O3 -ffreestanding -fno-blocks -fno-exceptions -m32 -target $(RUNTIME_TRIPLE_32) -DCOMPILING_HALIDE_RUNTIME -DBITS_32 -emit-llvm -S $(SRC_DIR)/runtime/$*.cpp -o $@ -MMD -MP -MF $(BUILD_DIR)/initmod.$*_32_debug.d

$(BUILD_DIR)/initmod.%_ll.ll: $(SRC_DIR)/runtime/%.ll
	@-mkdir -p $(BUILD_DIR)
	cp $(SRC_DIR)/runtime/$*.ll $(BUILD_DIR)/initmod.$*_ll.ll

$(BUILD_DIR)/initmod.%.bc: $(BUILD_DIR)/initmod.%.ll $(BUILD_DIR)/llvm_ok
	$(LLVM_AS) $(BUILD_DIR)/initmod.$*.ll -o $(BUILD_DIR)/initmod.$*.bc

$(BUILD_DIR)/initmod.%.cpp: $(BIN_DIR)/bitcode2cpp $(BUILD_DIR)/initmod.%.bc
	./$(BIN_DIR)/bitcode2cpp $* < $(BUILD_DIR)/initmod.$*.bc > $@

$(BUILD_DIR)/initmod_ptx.%_ll.cpp: $(BIN_DIR)/bitcode2cpp $(SRC_DIR)/runtime/nvidia_libdevice_bitcode/libdevice.%.bc
	./$(BIN_DIR)/bitcode2cpp ptx_$(basename $*)_ll < $(SRC_DIR)/runtime/nvidia_libdevice_bitcode/libdevice.$*.bc > $@

$(BIN_DIR)/bitcode2cpp: $(ROOT_DIR)/tools/bitcode2cpp.cpp
	@-mkdir -p $(BIN_DIR)
	$(CXX) $< -o $@

$(BUILD_DIR)/initmod_ptx.%_ll.o: $(BUILD_DIR)/initmod_ptx.%_ll.cpp
	$(CXX) -c $< -o $@ -MMD -MP -MF $(BUILD_DIR)/$*.d -MT $(BUILD_DIR)/$*.o

$(BUILD_DIR)/initmod.%.o: $(BUILD_DIR)/initmod.%.cpp
	$(CXX) $(BUILD_BIT_SIZE) -c $< -o $@ -MMD -MP -MF $(BUILD_DIR)/$*.d -MT $(BUILD_DIR)/$*.o

$(BUILD_DIR)/BitWriter_3_2$(BITWRITER_VERSION)/%.o: $(SRC_DIR)/BitWriter_3_2$(BITWRITER_VERSION)/%.cpp $(BUILD_DIR)/llvm_ok
	@-mkdir -p $(BUILD_DIR)/BitWriter_3_2$(BITWRITER_VERSION)
	$(CXX) $(CXX_FLAGS) -c $< -o $@ -MMD -MP -MF $(BUILD_DIR)/BitWriter_3_2$(BITWRITER_VERSION)/$*.d -MT $(BUILD_DIR)/BitWriter_3_2$(BITWRITER_VERSION)/$*.o

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp $(SRC_DIR)/%.h $(BUILD_DIR)/llvm_ok
	@-mkdir -p $(BUILD_DIR)
	$(CXX) $(CXX_FLAGS) -c $< -o $@ -MMD -MP -MF $(BUILD_DIR)/$*.d -MT $(BUILD_DIR)/$*.o

.PHONY: clean
clean:
	rm -rf $(BIN_DIR)/*
	rm -rf $(BUILD_DIR)/*
	rm -rf $(TMP_DIR)/*
	rm -rf $(FILTERS_DIR)/*
	rm -rf $(INCLUDE_DIR)/*
	rm -rf $(DOC_DIR)/*

.SECONDARY:

CORRECTNESS_TESTS = $(shell ls $(ROOT_DIR)/test/correctness/*.cpp)
PERFORMANCE_TESTS = $(shell ls $(ROOT_DIR)/test/performance/*.cpp)
ERROR_TESTS = $(shell ls $(ROOT_DIR)/test/error/*.cpp)
WARNING_TESTS = $(shell ls $(ROOT_DIR)/test/warning/*.cpp)
OPENGL_TESTS := $(shell ls $(ROOT_DIR)/test/opengl/*.cpp)
RENDERSCRIPT_TESTS := $(shell ls $(ROOT_DIR)/test/renderscript/*.cpp)
GENERATOR_EXTERNAL_TESTS := $(shell ls $(ROOT_DIR)/test/generator/*test.cpp)
TUTORIALS = $(filter-out %_generate.cpp, $(shell ls $(ROOT_DIR)/tutorial/*.cpp))

ifeq ($(UNAME), Darwin)
LD_PATH_SETUP = DYLD_LIBRARY_PATH=${DYLD_LIBRARY_PATH}:$(CURDIR)/$(BIN_DIR)
else
LD_PATH_SETUP = LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:$(CURDIR)/$(BIN_DIR)
endif

test_correctness: $(CORRECTNESS_TESTS:$(ROOT_DIR)/test/correctness/%.cpp=test_%)
test_performance: $(PERFORMANCE_TESTS:$(ROOT_DIR)/test/performance/%.cpp=performance_%)
test_errors: $(ERROR_TESTS:$(ROOT_DIR)/test/error/%.cpp=error_%)
test_warnings: $(WARNING_TESTS:$(ROOT_DIR)/test/warning/%.cpp=warning_%)
test_tutorials: $(TUTORIALS:$(ROOT_DIR)/tutorial/%.cpp=tutorial_%)
test_valgrind: $(CORRECTNESS_TESTS:$(ROOT_DIR)/test/correctness/%.cpp=valgrind_%)
test_opengl: $(OPENGL_TESTS:$(ROOT_DIR)/test/opengl/%.cpp=opengl_%)
test_renderscript: $(RENDERSCRIPT_TESTS:$(ROOT_DIR)/test/renderscript/%.cpp=renderscript_%)

# There are two types of tests for generators:
# 1) Externally-written aot-based tests
# 2) Externally-written JIT-based tests
test_generators:  \
  $(GENERATOR_EXTERNAL_TESTS:$(ROOT_DIR)/test/generator/%_aottest.cpp=generator_aot_%)  \
  $(GENERATOR_EXTERNAL_TESTS:$(ROOT_DIR)/test/generator/%_jittest.cpp=generator_jit_%)

ALL_TESTS = test_internal test_correctness test_errors test_tutorials test_warnings test_generators test_renderscript

# These targets perform timings of each test. For most tests this includes Halide JIT compile times, and run times.
# For static and generator tests they time the compile time only. The times are recorded in CSV files.
time_compilation_correctness: init_time_compilation_correctness $(CORRECTNESS_TESTS:$(ROOT_DIR)/test/correctness/%.cpp=time_compilation_test_%)
time_compilation_performance: init_time_compilation_performance $(PERFORMANCE_TESTS:$(ROOT_DIR)/test/performance/%.cpp=time_compilation_performance_%)
time_compilation_opengl: init_time_compilation_opengl $(OPENGL_TESTS:$(ROOT_DIR)/test/opengl/%.cpp=time_compilation_opengl_%)
time_compilation_renderscript: init_time_compilation_renderscript $(RENDERSCRIPT_TESTS:$(ROOT_DIR)/test/renderscript/%.cpp=time_compilation_renderscript_%)
time_compilation_generators: init_time_compilation_generator $(GENERATOR_TESTS:$(ROOT_DIR)/test/generator/%_aottest.cpp=time_compilation_generator_%)

init_time_compilation_%:
	echo "TEST,User (s),System (s),Real" > $(@:init_time_compilation_%=compile_times_%.csv)

TIME_COMPILATION ?= /usr/bin/time -a -f "$@,%U,%S,%E" -o

run_tests: $(ALL_TESTS)
	make -f $(THIS_MAKEFILE) test_performance

build_tests: $(CORRECTNESS_TESTS:$(ROOT_DIR)/test/correctness/%.cpp=$(BIN_DIR)/test_%) \
	$(PERFORMANCE_TESTS:$(ROOT_DIR)/test/performance/%.cpp=$(BIN_DIR)/performance_%) \
	$(ERROR_TESTS:$(ROOT_DIR)/test/error/%.cpp=$(BIN_DIR)/error_%) \
	$(WARNING_TESTS:$(ROOT_DIR)/test/error/%.cpp=$(BIN_DIR)/warning_%)

time_compilation_tests: time_compilation_correctness time_compilation_performance time_compilation_static time_compilation_generators

$(BIN_DIR)/test_internal: $(ROOT_DIR)/test/internal.cpp $(BIN_DIR)/libHalide.so
	$(CXX) $(CXX_FLAGS)  $< -I$(SRC_DIR) -L$(BIN_DIR) -lHalide $(LLVM_LDFLAGS) -lpthread -ldl -lz -o $@

$(BIN_DIR)/test_%: $(ROOT_DIR)/test/correctness/%.cpp $(BIN_DIR)/libHalide.so $(INCLUDE_DIR)/Halide.h $(INCLUDE_DIR)/HalideRuntime.h
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE) $< -I$(INCLUDE_DIR) -L$(BIN_DIR) -lHalide $(LLVM_LDFLAGS) -lpthread -ldl -lz -o $@

$(BIN_DIR)/performance_%: $(ROOT_DIR)/test/performance/%.cpp $(BIN_DIR)/libHalide.so $(INCLUDE_DIR)/Halide.h $(ROOT_DIR)/apps/support/benchmark.h
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE) $< -I$(INCLUDE_DIR) -L$(BIN_DIR) -lHalide $(LLVM_LDFLAGS) -lpthread -ldl -lz -o $@

$(BIN_DIR)/error_%: $(ROOT_DIR)/test/error/%.cpp $(BIN_DIR)/libHalide.so $(INCLUDE_DIR)/Halide.h
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE) $< -I$(INCLUDE_DIR) -L$(BIN_DIR) -lHalide $(LLVM_LDFLAGS) -lpthread -ldl -lz -o $@

$(BIN_DIR)/warning_%: $(ROOT_DIR)/test/warning/%.cpp $(BIN_DIR)/libHalide.so $(INCLUDE_DIR)/Halide.h
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE) $< -I$(INCLUDE_DIR) -L$(BIN_DIR) -lHalide $(LLVM_LDFLAGS) -lpthread -ldl -lz -o $@

$(BIN_DIR)/opengl_%: $(ROOT_DIR)/test/opengl/%.cpp $(BIN_DIR)/libHalide.so $(INCLUDE_DIR)/Halide.h
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE) $< -I$(INCLUDE_DIR) -I$(SRC_DIR) -L$(BIN_DIR) -lHalide $(LLVM_LDFLAGS) -lpthread -ldl -lz -o $@

$(BIN_DIR)/renderscript_%: $(ROOT_DIR)/test/renderscript/%.cpp $(BIN_DIR)/libHalide.so $(INCLUDE_DIR)/Halide.h
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE) $< -I$(INCLUDE_DIR) -I$(SRC_DIR) -L$(BIN_DIR) -lHalide $(LLVM_LDFLAGS) -lpthread -ldl -lz -o $@

$(TMP_DIR)/static/%/%.o: $(BIN_DIR)/static_%_generate
	@-mkdir -p $(TMP_DIR)/static/$*
	cd $(TMP_DIR)/static/$*; $(LD_PATH_SETUP) $(CURDIR)/$<
	@-echo

# TODO(srj): this doesn't auto-delete, why not?
.INTERMEDIATE: $(FILTERS_DIR)/%.generator

# By default, %.generator is produced by building %_generator.cpp
# Note that the rule includes all _generator.cpp files, so that generators with define_extern
# usage can just add deps later.
$(FILTERS_DIR)/%.generator: $(ROOT_DIR)/test/generator/%_generator.cpp $(GENGEN_DEPS)
	@mkdir -p $(FILTERS_DIR)
	$(CXX) -std=c++11 -g $(CXX_WARNING_FLAGS) -fno-rtti -I$(INCLUDE_DIR) $(filter %_generator.cpp,$^) $(ROOT_DIR)/tools/GenGen.cpp -L$(BIN_DIR) -lHalide -lz -lpthread -ldl -o $@

# By default, %.o/.h are produced by executing %.generator
$(FILTERS_DIR)/%.o $(FILTERS_DIR)/%.h: $(FILTERS_DIR)/%.generator
	@mkdir -p $(FILTERS_DIR)
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR); $(LD_PATH_SETUP) $(CURDIR)/$< -g $(notdir $*) -o $(CURDIR)/$(FILTERS_DIR) target=$(HL_TARGET)

# If we want to use a Generator with custom GeneratorParams, we need to write
# custom rules: to pass the GeneratorParams, and to give a unique function and file name.
$(FILTERS_DIR)/tiled_blur_interleaved.o $(FILTERS_DIR)/tiled_blur_interleaved.h: $(FILTERS_DIR)/tiled_blur.generator
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR); $(LD_PATH_SETUP) $(CURDIR)/$< -g tiled_blur -f tiled_blur_interleaved -o $(CURDIR)/$(FILTERS_DIR) target=$(HL_TARGET) is_interleaved=true

$(FILTERS_DIR)/tiled_blur_blur_interleaved.o $(FILTERS_DIR)/tiled_blur_blur_interleaved.h: $(FILTERS_DIR)/tiled_blur_blur.generator
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR); $(LD_PATH_SETUP) $(CURDIR)/$< -g tiled_blur_blur -f tiled_blur_blur_interleaved -o $(CURDIR)/$(FILTERS_DIR) target=$(HL_TARGET) is_interleaved=true

# metadata_tester is built with and without user-context
$(FILTERS_DIR)/metadata_tester.o $(FILTERS_DIR)/metadata_tester.h: $(FILTERS_DIR)/metadata_tester.generator
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR); $(LD_PATH_SETUP) $(CURDIR)/$< -f metadata_tester -o $(CURDIR)/$(FILTERS_DIR) target=$(HL_TARGET)-register_metadata

$(FILTERS_DIR)/metadata_tester_ucon.o $(FILTERS_DIR)/metadata_tester_ucon.h: $(FILTERS_DIR)/metadata_tester.generator
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR); $(LD_PATH_SETUP) $(CURDIR)/$< -f metadata_tester_ucon -o $(CURDIR)/$(FILTERS_DIR) target=$(HL_TARGET)-user_context-register_metadata

$(BIN_DIR)/generator_aot_metadata_tester: $(FILTERS_DIR)/metadata_tester_ucon.o

# user_context needs to be generated with user_context as the first argument to its calls
$(FILTERS_DIR)/user_context.o $(FILTERS_DIR)/user_context.h: $(FILTERS_DIR)/user_context.generator
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR); $(LD_PATH_SETUP) $(CURDIR)/$< -o $(CURDIR)/$(FILTERS_DIR) target=$(HL_TARGET)-user_context

# ditto for user_context_insanity
$(FILTERS_DIR)/user_context_insanity.o $(FILTERS_DIR)/user_context_insanity.h: $(FILTERS_DIR)/user_context_insanity.generator
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR); $(LD_PATH_SETUP) $(CURDIR)/$< -o $(CURDIR)/$(FILTERS_DIR) target=$(HL_TARGET)-user_context

# Some .generators have additional dependencies (usually due to define_extern usage).
# These typically require two extra dependencies:
# (1) Ensuring the extra _generator.cpp is built into the .generator.
# (2) Ensuring the extra .o is linked into the final output.

# tiled_blur also needs tiled_blur_blur, due to an extern_generator dependency.
$(FILTERS_DIR)/tiled_blur.generator: $(ROOT_DIR)/test/generator/tiled_blur_blur_generator.cpp
# TODO(srj): we really want to say "anything that depends on tiled_blur.o also depends on tiled_blur_blur.o";
# is there a way to specify that in Make?
$(BIN_DIR)/generator_aot_tiled_blur: $(FILTERS_DIR)/tiled_blur_blur.o
$(BIN_DIR)/generator_aot_tiled_blur_interleaved: $(FILTERS_DIR)/tiled_blur_blur_interleaved.o

# Usually, it's considered best practice to have one Generator per
# .cpp file, with the generator-name and filename matching;
# nested_externs_generators.cpp is a counterexample, and thus requires
# some special casing to get right.  First, make a special rule to
# build each of the Generators in nested_externs_generator.cpp (which
# all have the form nested_externs_*). We'll build them without
# including the Halide runtime in each, to also test that
# functionality.
$(FILTERS_DIR)/nested_externs_%.o $(FILTERS_DIR)/nested_externs_%.h: $(FILTERS_DIR)/nested_externs.generator
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR); $(LD_PATH_SETUP) $(CURDIR)/$< -g nested_externs_$* -o $(CURDIR)/$(FILTERS_DIR) target=$(HL_TARGET)-no_runtime

$(FILTERS_DIR)/nested_externs_runtime.o: $(FILTERS_DIR)/nested_externs.generator
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR); $(LD_PATH_SETUP) $(CURDIR)/$< -r nested_externs_runtime.o -o $(CURDIR)/$(FILTERS_DIR) target=$(HL_TARGET)

# Synthesize 'nested_externs.o' based on the four generator products we need:
$(FILTERS_DIR)/nested_externs.o: $(FILTERS_DIR)/nested_externs_leaf.o $(FILTERS_DIR)/nested_externs_inner.o $(FILTERS_DIR)/nested_externs_combine.o $(FILTERS_DIR)/nested_externs_root.o $(FILTERS_DIR)/nested_externs_runtime.o
	$(LD) -r $(FILTERS_DIR)/nested_externs_*.o -o $(FILTERS_DIR)/nested_externs.o

# Synthesize 'nested_externs.h' based on the four generator products we need:
$(FILTERS_DIR)/nested_externs.h: $(FILTERS_DIR)/nested_externs.o
	cat $(FILTERS_DIR)/nested_externs_*.h > $(FILTERS_DIR)/nested_externs.h

# By default, %_aottest.cpp depends on $(FILTERS_DIR)/%.o/.h (but not libHalide).
$(BIN_DIR)/generator_aot_%: $(ROOT_DIR)/test/generator/%_aottest.cpp $(FILTERS_DIR)/%.o $(FILTERS_DIR)/%.h $(INCLUDE_DIR)/HalideRuntime.h
	$(CXX) $(TEST_CXX_FLAGS) $(filter-out %.h,$^) -I$(INCLUDE_DIR) -I$(FILTERS_DIR) -I $(ROOT_DIR)/apps/support -I $(SRC_DIR)/runtime -I$(ROOT_DIR)/tools -lpthread -ldl -o $@

# acquire_release is the only test that explicitly uses CUDA/OpenCL APIs, so link only those here.
$(BIN_DIR)/generator_aot_acquire_release: $(ROOT_DIR)/test/generator/acquire_release_aottest.cpp $(FILTERS_DIR)/acquire_release.o $(FILTERS_DIR)/acquire_release.h $(INCLUDE_DIR)/HalideRuntime.h
	$(CXX) $(TEST_CXX_FLAGS) $(filter-out %.h,$^) -I$(INCLUDE_DIR) -I$(FILTERS_DIR) -I $(ROOT_DIR)/apps/support -I $(SRC_DIR)/runtime -I$(ROOT_DIR)/tools -lpthread -ldl $(OPENCL_LDFLAGS) $(CUDA_LDFLAGS) -o $@

# By default, %_jittest.cpp depends on libHalide. These are external tests that use the JIT.
$(BIN_DIR)/generator_jit_%: $(ROOT_DIR)/test/generator/%_jittest.cpp $(BIN_DIR)/libHalide.so $(INCLUDE_DIR)/Halide.h
	$(CXX) $(TEST_CXX_FLAGS) $(filter-out %.h %.so,$^) -I$(INCLUDE_DIR) -I$(FILTERS_DIR) -I $(ROOT_DIR)/apps/support -I $(SRC_DIR)/runtime -L$(BIN_DIR) -lHalide $(LLVM_LDFLAGS) -lpthread -ldl -lz -o $@

# nested externs doesn't actually contain a generator named
# "nested_externs", and has no internal tests in any case.
test_generator_nested_externs:
	@echo "Skipping"

$(BIN_DIR)/tutorial_%: $(ROOT_DIR)/tutorial/%.cpp $(BIN_DIR)/libHalide.so $(INCLUDE_DIR)/Halide.h
	@ if [[ $@ == *_run ]]; then \
		export TUTORIAL=$* ;\
		export LESSON=`echo $${TUTORIAL} | cut -b1-9`; \
		make -f $(THIS_MAKEFILE) tutorial_$${TUTORIAL/run/generate}; \
		$(CXX) $(TUTORIAL_CXX_FLAGS) $(LIBPNG_CXX_FLAGS) $(OPTIMIZE) $< \
		-I$(TMP_DIR) $(TMP_DIR)/$${LESSON}_*.o -lpthread -ldl -lz $(LIBPNG_LIBS) -o $@; \
	else \
		$(CXX) $(TUTORIAL_CXX_FLAGS) $(LIBPNG_CXX_FLAGS) $(OPTIMIZE) $< \
		-I$(INCLUDE_DIR) -I$(ROOT_DIR)/tools -L$(BIN_DIR) -lHalide $(LLVM_LDFLAGS) -lpthread -ldl -lz $(LIBPNG_LIBS) -o $@;\
	fi

$(BIN_DIR)/tutorial_lesson_15_generators: $(ROOT_DIR)/tutorial/lesson_15_generators.cpp $(BIN_DIR)/libHalide.so $(INCLUDE_DIR)/Halide.h
	$(CXX) $(TUTORIAL_CXX_FLAGS) $(LIBPNG_CXX_FLAGS) $(OPTIMIZE) $< $(ROOT_DIR)/tools/GenGen.cpp \
	-I$(INCLUDE_DIR) -L$(BIN_DIR) -lHalide $(LLVM_LDFLAGS) -lpthread -ldl -lz $(LIBPNG_LIBS) -o $@;\

tutorial_lesson_15_generators: $(ROOT_DIR)/tutorial/lesson_15_generators_usage.sh $(BIN_DIR)/tutorial_lesson_15_generators
	@-mkdir -p $(TMP_DIR)
	cp $(BIN_DIR)/tutorial_lesson_15_generators $(TMP_DIR)/lesson_15_generate; \
	cd $(TMP_DIR); \
	$(LD_PATH_SETUP) bash $(ROOT_DIR)/tutorial/lesson_15_generators_usage.sh
	@-echo

test_%: $(BIN_DIR)/test_%
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; $(LD_PATH_SETUP) $(CURDIR)/$<
	@-echo

valgrind_%: $(BIN_DIR)/test_%
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; $(LD_PATH_SETUP) valgrind --error-exitcode=-1 $(CURDIR)/$<
	@-echo

# This test is *supposed* to do an out-of-bounds read, so skip it when testing under valgrind
valgrind_tracing_stack: $(BIN_DIR)/test_tracing_stack
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; $(LD_PATH_SETUP) $(CURDIR)/$(BIN_DIR)/test_tracing_stack
	@-echo

performance_%: $(BIN_DIR)/performance_%
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; $(LD_PATH_SETUP) $(CURDIR)/$<
	@-echo

error_%: $(BIN_DIR)/error_%
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; $(LD_PATH_SETUP) $(CURDIR)/$< 2>&1 | egrep --q "terminating with uncaught exception|^terminate called|^Error"
	@-echo

warning_%: $(BIN_DIR)/warning_%
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; $(LD_PATH_SETUP) $(CURDIR)/$< 2>&1 | egrep --q "^Warning"
	@-echo

opengl_%: HL_JIT_TARGET ?= host-opengl
opengl_%: $(BIN_DIR)/opengl_%
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; HL_JIT_TARGET=$(HL_JIT_TARGET) $(LD_PATH_SETUP) $(CURDIR)/$< 2>&1
	@-echo

renderscript_jit_%: HL_JIT_TARGET = host-renderscript
renderscript_jit_%: HL_TARGET =
renderscript_aot_%: HL_TARGET = arm-32-android-armv7s-renderscript
renderscript_aot_%: HL_JIT_TARGET =
renderscript_%_error: $(BIN_DIR)/renderscript_%_error
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; HL_JIT_TARGET=$(HL_JIT_TARGET) HL_TARGET=$(HL_TARGET) $(LD_PATH_SETUP) $(CURDIR)/$< 2>&1 | egrep --q "terminating with uncaught exception|^terminate called|^Error"
	@-echo
renderscript_%: $(BIN_DIR)/renderscript_%
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; HL_JIT_TARGET=$(HL_JIT_TARGET) HL_TARGET=$(HL_TARGET) $(LD_PATH_SETUP) $(CURDIR)/$<
	@-echo

generator_%: $(BIN_DIR)/generator_%
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; $(LD_PATH_SETUP) $(CURDIR)/$<
	@-echo

$(TMP_DIR)/images/%.png: $(ROOT_DIR)/tutorial/images/%.png
	@-mkdir -p $(TMP_DIR)/images
	cp $< $(TMP_DIR)/images/

tutorial_%: $(BIN_DIR)/tutorial_% $(TMP_DIR)/images/rgb.png $(TMP_DIR)/images/gray.png
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; $(LD_PATH_SETUP) $(CURDIR)/$<
	@-echo

time_compilation_test_%: $(BIN_DIR)/test_%
	$(TIME_COMPILATION) compile_times_correctness.csv make -f $(THIS_MAKEFILE) $(@:time_compilation_test_%=test_%)

time_compilation_performance_%: $(BIN_DIR)/performance_%
	$(TIME_COMPILATION) compile_times_performance.csv make -f $(THIS_MAKEFILE) $(@:time_compilation_performance_%=performance_%)

time_compilation_opengl_%: $(BIN_DIR)/opengl_%
	$(TIME_COMPILATION) compile_times_opengl.csv make -f $(THIS_MAKEFILE) $(@:time_compilation_opengl_%=opengl_%)

time_compilation_renderscript_%: $(BIN_DIR)/renderscript_%
	$(TIME_COMPILATION) compile_times_renderscript.csv make -f $(THIS_MAKEFILE) $(@:time_compilation_renderscript_%=renderscript_%)

time_compilation_static_%: $(BIN_DIR)/static_%_generate
	$(TIME_COMPILATION) compile_times_static.csv make -f $(THIS_MAKEFILE) $(@:time_compilation_static_%=$(TMP_DIR)/static/%/%.o)

time_compilation_generator_%: $(FILTERS_DIR)/%.generator
	$(TIME_COMPILATION) compile_times_generator.csv make -f $(THIS_MAKEFILE) $(@:time_compilation_generator_%=$(FILTERS_DIR)/%.o)

time_compilation_generator_tiled_blur_interleaved: $(FILTERS_DIR)/tiled_blur.generator
	$(TIME_COMPILATION) compile_times_generator.csv make -f $(THIS_MAKEFILE) $(FILTERS_DIR)/tiled_blur_interleaved.o

.PHONY: test_apps
test_apps: $(BIN_DIR)/libHalide.a $(BIN_DIR)/libHalide.so $(INCLUDE_DIR)/Halide.h $(INCLUDE_DIR)/HalideRuntime.h
	mkdir -p apps
	# Make a local copy of the apps if we're building out-of-tree,
	# because the app Makefiles are written to build in-tree
	if [ "$(ROOT_DIR)" != "$(CURDIR)" ]; then \
	  echo "Building out-of-tree, so making local copy of apps"; \
	  cp -r $(ROOT_DIR)/apps/bilateral_grid \
	        $(ROOT_DIR)/apps/local_laplacian \
	        $(ROOT_DIR)/apps/interpolate \
	        $(ROOT_DIR)/apps/blur \
	        $(ROOT_DIR)/apps/wavelet \
	        $(ROOT_DIR)/apps/c_backend \
	        $(ROOT_DIR)/apps/modules \
	        $(ROOT_DIR)/apps/HelloMatlab \
	        $(ROOT_DIR)/apps/images \
	        $(ROOT_DIR)/apps/support \
                apps; \
	  mkdir -p tools; \
	  cp $(ROOT_DIR)/tools/* tools/; \
        fi
	make -C apps/bilateral_grid clean  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	make -C apps/bilateral_grid out.png  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	make -C apps/local_laplacian clean  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	make -C apps/local_laplacian out.png  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	make -C apps/interpolate clean  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	make -C apps/interpolate out.png  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	make -C apps/blur clean  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	make -C apps/blur test  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	apps/blur/test
	make -C apps/wavelet clean  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	make -C apps/wavelet test  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	make -C apps/c_backend clean  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	make -C apps/c_backend test  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	make -C apps/modules clean  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	make -C apps/modules out.png  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	cd apps/HelloMatlab; HALIDE_PATH=$(CURDIR) ./run_blur.sh

.PHONY: test_python
test_python: $(BIN_DIR)/libHalide.a
	mkdir -p python_bindings
	make -C python_bindings -f $(ROOT_DIR)/python_bindings/Makefile test

# It's just for compiling the runtime, so Clang <3.5 *might* work,
# but best to peg it to the minimum llvm version.
ifneq (,$(findstring clang version 3.5,$(CLANG_VERSION)))
CLANG_OK=yes
endif

ifneq (,$(findstring clang version 3.6,$(CLANG_VERSION)))
CLANG_OK=yes
endif

ifneq (,$(findstring clang version 3.7,$(CLANG_VERSION)))
CLANG_OK=yes
endif

ifneq (,$(findstring clang version 3.8,$(CLANG_VERSION)))
CLANG_OK=yes
endif

ifneq (,$(findstring Apple clang version 4.0,$(CLANG_VERSION)))
CLANG_OK=yes
endif

ifneq (,$(findstring Apple LLVM version 5.0,$(CLANG_VERSION)))
CLANG_OK=yes
endif

ifneq ($(CLANG_OK), )
$(BUILD_DIR)/clang_ok:
	@echo "Found a new enough version of clang"
	mkdir -p $(BUILD_DIR)
	touch $(BUILD_DIR)/clang_ok
else
$(BUILD_DIR)/clang_ok:
	@echo "Can't find clang or version of clang too old (we need 3.5 or greater):"
	@echo "You can override this check by setting CLANG_OK=y"
	echo '$(CLANG_VERSION)'
	echo $(findstring version 3,$(CLANG_VERSION))
	echo $(findstring version 3.0,$(CLANG_VERSION))
	$(CLANG) --version
	@exit 1
endif

ifneq (,$(findstring $(LLVM_VERSION_TIMES_10), 35 36 37 38))
LLVM_OK=yes
endif

ifneq ($(LLVM_OK), )
$(BUILD_DIR)/llvm_ok:
	@echo "Found a new enough version of llvm"
	mkdir -p $(BUILD_DIR)
	touch $(BUILD_DIR)/llvm_ok
else
$(BUILD_DIR)/llvm_ok:
	@echo "Can't find llvm or version of llvm too old (we need 3.5 or greater):"
	@echo "You can override this check by setting LLVM_OK=y"
	$(LLVM_CONFIG) --version
	@exit 1
endif

.PHONY: doc
$(DOC_DIR): doc
doc: $(SRC_DIR) Doxyfile
	doxygen

Doxyfile: Doxyfile.in
	@echo "Generating $@"
	@sed -e "s#@CMAKE_BINARY_DIR@#$(shell pwd)#g" \
	     -e "s#@CMAKE_SOURCE_DIR@#$(shell pwd)#g" \
	    $< > $@

$(DISTRIB_DIR)/halide.tgz: $(BIN_DIR)/libHalide.a $(BIN_DIR)/libHalide.so $(INCLUDE_DIR)/Halide.h $(INCLUDE_DIR)/HalideRuntime.h
	mkdir -p $(DISTRIB_DIR)/include $(DISTRIB_DIR)/bin $(DISTRIB_DIR)/tutorial $(DISTRIB_DIR)/tutorial/images $(DISTRIB_DIR)/tools $(DISTRIB_DIR)/tutorial/figures
	cp $(BIN_DIR)/libHalide.a $(BIN_DIR)/libHalide.so $(DISTRIB_DIR)/bin
	cp $(INCLUDE_DIR)/Halide.h $(DISTRIB_DIR)/include
	cp $(INCLUDE_DIR)/HalideRuntim*.h $(DISTRIB_DIR)/include
	cp $(ROOT_DIR)/tutorial/images/*.png $(DISTRIB_DIR)/tutorial/images
	cp $(ROOT_DIR)/tutorial/figures/*.gif $(DISTRIB_DIR)/tutorial/figures
	cp $(ROOT_DIR)/tutorial/figures/*.jpg $(DISTRIB_DIR)/tutorial/figures
	cp $(ROOT_DIR)/tutorial/figures/*.mp4 $(DISTRIB_DIR)/tutorial/figures
	cp $(ROOT_DIR)/tutorial/*.cpp $(DISTRIB_DIR)/tutorial
	cp $(ROOT_DIR)/tutorial/*.h $(DISTRIB_DIR)/tutorial
	cp $(ROOT_DIR)/tutorial/*.sh $(DISTRIB_DIR)/tutorial
	cp $(ROOT_DIR)/tools/mex_halide.m $(DISTRIB_DIR)/tools
	cp $(ROOT_DIR)/tools/GenGen.cpp $(DISTRIB_DIR)/tools
	cp $(ROOT_DIR)/tools/halide_image.h $(DISTRIB_DIR)/tools
	cp $(ROOT_DIR)/tools/halide_image_io.h $(DISTRIB_DIR)/tools
	cp $(ROOT_DIR)/README.md $(DISTRIB_DIR)
	ln -sf $(DISTRIB_DIR) halide
	tar -czf $(DISTRIB_DIR)/halide.tgz halide/bin halide/include halide/tutorial halide/README.md halide/tools/mex_halide.m halide/tools/GenGen.cpp halide/tools/halide_image.h halide/tools/halide_image_io.h
	rm halide

.PHONY: distrib
distrib: $(DISTRIB_DIR)/halide.tgz

$(BIN_DIR)/HalideProf: $(ROOT_DIR)/util/HalideProf.cpp
	$(CXX) $(OPTIMIZE) $< -I$(INCLUDE_DIR) -L$(BIN_DIR) -o $@

$(BIN_DIR)/HalideTraceViz: $(ROOT_DIR)/util/HalideTraceViz.cpp
	$(CXX) $(OPTIMIZE) -std=c++11 $< -I$(INCLUDE_DIR) -L$(BIN_DIR) -o $@
