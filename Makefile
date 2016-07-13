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

ifeq ($(OS), Windows_NT)
    # assume we are building for the MinGW environment
    LIBDL =
    SHARED_EXT=dll
    FPIC=
else
    # let's assume "normal" UNIX such as linux
    LIBDL=-ldl
    SHARED_EXT=so
    FPIC=-fPIC
endif

SHELL = bash
CXX ?= g++
PREFIX ?= /usr/local
LLVM_CONFIG ?= llvm-config
LLVM_COMPONENTS= $(shell $(LLVM_CONFIG) --components)
LLVM_VERSION = $(shell $(LLVM_CONFIG) --version | cut -b 1-3)

LLVM_FULL_VERSION = $(shell $(LLVM_CONFIG) --version)
CLANG ?= clang
CLANG_VERSION = $(shell $(CLANG) --version)
LLVM_BINDIR = $(shell $(LLVM_CONFIG) --bindir | sed -e 's/\\/\//g' -e 's/\([a-zA-Z]\):/\/\1/g')
LLVM_LIBDIR = $(shell $(LLVM_CONFIG) --libdir | sed -e 's/\\/\//g' -e 's/\([a-zA-Z]\):/\/\1/g')
LLVM_AS = $(LLVM_BINDIR)/llvm-as
LLVM_NM = $(LLVM_BINDIR)/llvm-nm
LLVM_CXX_FLAGS = -std=c++11  $(filter-out -O% -g -fomit-frame-pointer -pedantic -W% -W, $(shell $(LLVM_CONFIG) --cxxflags | sed -e 's/\\/\//g' -e 's/\([a-zA-Z]\):/\/\1/g'))
OPTIMIZE ?= -O3
# This can be set to -m32 to get a 32-bit build of Halide on a 64-bit system.
# (Normally this can be done via pointing to a compiler that defaults to 32-bits,
#  but that is difficult in some testing situations because it requires having
#  such a compiler handy. One still needs to have 32-bit llvm libraries, etc.)
BUILD_BIT_SIZE ?=

GENGEN_DEPS ?= $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h $(ROOT_DIR)/tools/GenGen.cpp

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
ifeq ($(LLVM_VERSION_TIMES_10),39)
WITH_HEXAGON ?= $(findstring hexagon, $(LLVM_COMPONENTS))
else
WITH_HEXAGON ?=
endif
WITH_MIPS ?= $(findstring mips, $(LLVM_COMPONENTS))
WITH_AARCH64 ?= $(findstring aarch64, $(LLVM_COMPONENTS))
WITH_POWERPC ?= $(findstring powerpc, $(LLVM_COMPONENTS))
WITH_PTX ?= $(findstring nvptx, $(LLVM_COMPONENTS))
WITH_OPENCL ?= not-empty
WITH_METAL ?= not-empty
WITH_OPENGL ?= not-empty
WITH_RENDERSCRIPT ?= not-empty
ifeq ($(OS), Windows_NT)
    WITH_INTROSPECTION ?=
else
    WITH_INTROSPECTION ?= not-empty
endif
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

POWERPC_CXX_FLAGS=$(if $(WITH_POWERPC), -DWITH_POWERPC=1, )
POWERPC_LLVM_CONFIG_LIB=$(if $(WITH_POWERPC), powerpc, )

PTX_CXX_FLAGS=$(if $(WITH_PTX), -DWITH_PTX=1, )
PTX_LLVM_CONFIG_LIB=$(if $(WITH_PTX), nvptx, )
PTX_DEVICE_INITIAL_MODULES=$(if $(WITH_PTX), libdevice.compute_20.10.bc libdevice.compute_30.10.bc libdevice.compute_35.10.bc, )

OPENCL_CXX_FLAGS=$(if $(WITH_OPENCL), -DWITH_OPENCL=1, )
OPENCL_LLVM_CONFIG_LIB=$(if $(WITH_OPENCL), , )

METAL_CXX_FLAGS=$(if $(WITH_METAL), -DWITH_METAL=1, )
METAL_LLVM_CONFIG_LIB=$(if $(WITH_METAL), , )

OPENGL_CXX_FLAGS=$(if $(WITH_OPENGL), -DWITH_OPENGL=1, )

RENDERSCRIPT_CXX_FLAGS=$(if $(WITH_RENDERSCRIPT), -DWITH_RENDERSCRIPT=1, )

AARCH64_CXX_FLAGS=$(if $(WITH_AARCH64), -DWITH_AARCH64=1, )
AARCH64_LLVM_CONFIG_LIB=$(if $(WITH_AARCH64), aarch64, )

INTROSPECTION_CXX_FLAGS=$(if $(WITH_INTROSPECTION), -DWITH_INTROSPECTION, )
EXCEPTIONS_CXX_FLAGS=$(if $(WITH_EXCEPTIONS), -DWITH_EXCEPTIONS, )

HEXAGON_CXX_FLAGS=$(if $(WITH_HEXAGON), -DWITH_HEXAGON=1, )
HEXAGON_LLVM_CONFIG_LIB=$(if $(WITH_HEXAGON), hexagon, )

CXX_WARNING_FLAGS = -Wall -Werror -Wno-unused-function -Wcast-qual -Wignored-qualifiers -Wno-comment -Wsign-compare
CXX_FLAGS = $(CXX_WARNING_FLAGS) -fno-rtti -Woverloaded-virtual $(FPIC) $(OPTIMIZE) -fno-omit-frame-pointer -DCOMPILING_HALIDE $(BUILD_BIT_SIZE)

CXX_FLAGS += $(LLVM_CXX_FLAGS)
CXX_FLAGS += $(NATIVE_CLIENT_CXX_FLAGS)
CXX_FLAGS += $(PTX_CXX_FLAGS)
CXX_FLAGS += $(ARM_CXX_FLAGS)
CXX_FLAGS += $(HEXAGON_CXX_FLAGS)
CXX_FLAGS += $(AARCH64_CXX_FLAGS)
CXX_FLAGS += $(X86_CXX_FLAGS)
CXX_FLAGS += $(OPENCL_CXX_FLAGS)
CXX_FLAGS += $(METAL_CXX_FLAGS)
CXX_FLAGS += $(OPENGL_CXX_FLAGS)
CXX_FLAGS += $(RENDERSCRIPT_CXX_FLAGS)
CXX_FLAGS += $(MIPS_CXX_FLAGS)
CXX_FLAGS += $(POWERPC_CXX_FLAGS)
CXX_FLAGS += $(INTROSPECTION_CXX_FLAGS)
CXX_FLAGS += $(EXCEPTIONS_CXX_FLAGS)

# This is required on some hosts like powerpc64le-linux-gnu because we may build
# everything with -fno-exceptions.  Without -funwind-tables, libHalide.so fails
# to propagate exceptions and causes a test failure.
CXX_FLAGS += -funwind-tables

ifeq ($(LLVM_VERSION_TIMES_10), 35)
LLVM_OLD_JIT_COMPONENT = jit
endif

print-%:
	@echo '$*=$($*)'

ifeq ($(USE_LLVM_SHARED_LIB), )
LLVM_STATIC_LIBS = -L $(LLVM_LIBDIR) $(shell $(LLVM_CONFIG) --libs bitwriter bitreader linker ipo mcjit $(LLVM_OLD_JIT_COMPONENT) $(X86_LLVM_CONFIG_LIB) $(ARM_LLVM_CONFIG_LIB) $(OPENCL_LLVM_CONFIG_LIB) $(METAL_LLVM_CONFIG_LIB) $(NATIVE_CLIENT_LLVM_CONFIG_LIB) $(PTX_LLVM_CONFIG_LIB) $(AARCH64_LLVM_CONFIG_LIB) $(MIPS_LLVM_CONFIG_LIB) $(POWERPC_LLVM_CONFIG_LIB) $(HEXAGON_LLVM_CONFIG_LIB))
LLVM_SHARED_LIBS =
else
LLVM_STATIC_LIBS =
LLVM_SHARED_LIBS = -L $(LLVM_LIBDIR) -lLLVM-$(LLVM_FULL_VERSION)
endif

LLVM_LD_FLAGS = $(shell $(LLVM_CONFIG) --ldflags --system-libs | sed -e 's/\\/\//g' -e 's/\([a-zA-Z]\):/\/\1/g')

UNAME = $(shell uname)

TUTORIAL_CXX_FLAGS ?= -std=c++11 $(BUILD_BIT_SIZE) -g -fno-omit-frame-pointer -fno-rtti
# The tutorials contain example code with warnings that we don't want
# to be flagged as errors, so the test flags are the tutorial flags
# plus our warning flags.
TEST_CXX_FLAGS ?= $(TUTORIAL_CXX_FLAGS) $(CXX_WARNING_FLAGS)
TEST_LD_FLAGS = -L$(BIN_DIR) -lHalide -lpthread $(LIBDL) -lz

ifeq ($(UNAME), Linux)
TEST_LD_FLAGS += -rdynamic -Wl,--rpath=$(CURDIR)/$(BIN_DIR)
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

ifneq ($(WITH_METAL), )
ifneq (,$(findstring metal,$(HL_TARGET)))
TEST_METAL = 1
endif
endif

ifeq ($(UNAME), Linux)
ifneq ($(TEST_PTX), )
CUDA_LD_FLAGS ?= -L/usr/lib/nvidia-current -lcuda
endif
ifneq ($(TEST_OPENCL), )
OPENCL_LD_FLAGS ?= -lOpenCL
endif
OPENGL_LD_FLAGS ?= -lGL
HOST_OS=linux
endif

ifeq ($(UNAME), Darwin)
# Someone with an osx box with cuda installed please fix the line below
ifneq ($(TEST_PTX), )
CUDA_LD_FLAGS ?= -L/usr/local/cuda/lib -lcuda
endif
ifneq ($(TEST_OPENCL), )
OPENCL_LD_FLAGS ?= -framework OpenCL
endif
ifneq ($(TEST_METAL), )
STATIC_TEST_LIBS ?= -framework Metal
endif
OPENGL_LD_FLAGS ?= -framework OpenGL
HOST_OS=os_x
endif

ifneq ($(TEST_OPENCL), )
TEST_CXX_FLAGS += -DTEST_OPENCL
endif

ifneq ($(TEST_METAL), )
TEST_CXX_FLAGS += -DTEST_METAL
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
LIB_DIR     = lib
BIN_DIR     = bin
DISTRIB_DIR = distrib
INCLUDE_DIR = include
DOC_DIR     = doc
BUILD_DIR   = $(BIN_DIR)/build
FILTERS_DIR = $(BUILD_DIR)/filters
RUNTIMES_DIR = $(BUILD_DIR)/runtimes
TMP_DIR     = $(BUILD_DIR)/tmp

SOURCE_FILES = \
  AddImageChecks.cpp \
  AddParameterChecks.cpp \
  AlignLoads.cpp \
  AllocationBoundsInference.cpp \
  Associativity.cpp \
  BoundaryConditions.cpp \
  Bounds.cpp \
  BoundsInference.cpp \
  Buffer.cpp \
  Closure.cpp \
  CodeGen_ARM.cpp \
  CodeGen_C.cpp \
  CodeGen_GPU_Dev.cpp \
  CodeGen_GPU_Host.cpp \
  CodeGen_Hexagon.cpp \
  CodeGen_Internal.cpp \
  CodeGen_LLVM.cpp \
  CodeGen_MIPS.cpp \
  CodeGen_OpenCL_Dev.cpp \
  CodeGen_Metal_Dev.cpp \
  CodeGen_OpenGL_Dev.cpp \
  CodeGen_OpenGLCompute_Dev.cpp \
  CodeGen_PNaCl.cpp \
  CodeGen_Posix.cpp \
  CodeGen_PowerPC.cpp \
  CodeGen_PTX_Dev.cpp \
  CodeGen_Renderscript_Dev.cpp \
  CodeGen_X86.cpp \
  CPlusPlusMangle.cpp \
  CSE.cpp \
  Debug.cpp \
  DebugToFile.cpp \
  DeepCopy.cpp \
  Definition.cpp \
  Deinterleave.cpp \
  DeviceArgument.cpp \
  DeviceInterface.cpp \
  EarlyFree.cpp \
  EliminateBoolVectors.cpp \
  Error.cpp \
  FastIntegerDivide.cpp \
  FindCalls.cpp \
  Float16.cpp \
  Func.cpp \
  Function.cpp \
  FuseGPUThreadLoops.cpp \
  Generator.cpp \
  HexagonOffload.cpp \
  HexagonOptimize.cpp \
  Image.cpp \
  ImageParam.cpp \
  Interval.cpp \
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
  LoopCarry.cpp \
  Lower.cpp \
  MatlabWrapper.cpp \
  Memoization.cpp \
  Module.cpp \
  ModulusRemainder.cpp \
  Monotonic.cpp \
  ObjectInstanceRegistry.cpp \
  OutputImageParam.cpp \
  ParallelRVar.cpp \
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
  SimplifySpecializations.cpp \
  SkipStages.cpp \
  SlidingWindow.cpp \
  Solve.cpp \
  StmtToHtml.cpp \
  StorageFlattening.cpp \
  StorageFolding.cpp \
  Substitute.cpp \
  Target.cpp \
  Tracing.cpp \
  TrimNoOps.cpp \
  Tuple.cpp \
  Type.cpp \
  UnifyDuplicateLets.cpp \
  UniquifyVariableNames.cpp \
  UnrollLoops.cpp \
  Util.cpp \
  Var.cpp \
  VaryingAttributes.cpp \
  VectorizeLoops.cpp \
  WrapCalls.cpp

ifeq ($(LLVM_VERSION_TIMES_10),35)
BITWRITER_VERSION=.35
else
BITWRITER_VERSION=
endif

BITWRITER_SOURCE_FILES = \
  BitWriter_3_2$(BITWRITER_VERSION)/BitcodeWriter.cpp \
  BitWriter_3_2$(BITWRITER_VERSION)/BitcodeWriterPass.cpp \
  BitWriter_3_2$(BITWRITER_VERSION)/ValueEnumerator.cpp

# The externally-visible header files that go into making Halide.h. Don't include anything here that includes llvm headers.
HEADER_FILES = \
  AddImageChecks.h \
  AddParameterChecks.h \
  AlignLoads.h \
  AllocationBoundsInference.h \
  Argument.h \
  Associativity.h \
  BoundaryConditions.h \
  Bounds.h \
  BoundsInference.h \
  Buffer.h \
  Closure.h \
  CodeGen_ARM.h \
  CodeGen_C.h \
  CodeGen_GPU_Dev.h \
  CodeGen_GPU_Host.h \
  CodeGen_LLVM.h \
  CodeGen_MIPS.h \
  CodeGen_OpenCL_Dev.h \
  CodeGen_Metal_Dev.h \
  CodeGen_OpenGL_Dev.h \
  CodeGen_OpenGLCompute_Dev.h \
  CodeGen_PNaCl.h \
  CodeGen_Posix.h \
  CodeGen_PowerPC.h \
  CodeGen_PTX_Dev.h \
  CodeGen_Renderscript_Dev.h \
  CodeGen_X86.h \
  ConciseCasts.h \
  CPlusPlusMangle.h \
  CSE.h \
  Debug.h \
  DebugToFile.h \
  DeepCopy.h \
  Definition.h \
  Deinterleave.h \
  DeviceArgument.h \
  DeviceInterface.h \
  EarlyFree.h \
  EliminateBoolVectors.h \
  Error.h \
  Expr.h \
  ExprUsesVar.h \
  Extern.h \
  FastIntegerDivide.h \
  FindCalls.h \
  Float16.h \
  Func.h \
  Function.h \
  FuseGPUThreadLoops.h \
  Generator.h \
  HexagonOffload.h \
  HexagonOptimize.h \
  runtime/HalideRuntime.h \
  Image.h \
  ImageParam.h \
  Interval.h \
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
  LoopCarry.h \
  Lower.h \
  MainPage.h \
  MatlabWrapper.h \
  Memoization.h \
  Module.h \
  ModulusRemainder.h \
  Monotonic.h \
  ObjectInstanceRegistry.h \
  Outputs.h \
  OutputImageParam.h \
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
  SimplifySpecializations.h \
  SkipStages.h \
  SlidingWindow.h \
  Solve.h \
  StmtToHtml.h \
  StorageFlattening.h \
  StorageFolding.h \
  Substitute.h \
  Target.h \
  Tracing.h \
  TrimNoOps.h \
  Tuple.h \
  Type.h \
  UnifyDuplicateLets.h \
  UniquifyVariableNames.h \
  UnrollLoops.h \
  Util.h \
  Var.h \
  VectorizeLoops.h \
  WrapCalls.h

OBJECTS = $(SOURCE_FILES:%.cpp=$(BUILD_DIR)/%.o)
OBJECTS += $(BITWRITER_SOURCE_FILES:%.cpp=$(BUILD_DIR)/%.o)
HEADERS = $(HEADER_FILES:%.h=$(SRC_DIR)/%.h)

RUNTIME_CPP_COMPONENTS = \
  aarch64_cpu_features \
  android_clock \
  android_host_cpu_count \
  android_io \
  android_opengl_context \
  android_tempfile \
  arm_cpu_features \
  cache \
  can_use_target \
  cuda \
  destructors \
  device_interface \
  errors \
  fake_thread_pool \
  float16_t \
  gcd_thread_pool \
  gpu_device_selection \
  hexagon_host \
  ios_io \
  linux_clock \
  linux_host_cpu_count \
  linux_opengl_context \
  matlab \
  metadata \
  metal \
  metal_objc_arm \
  metal_objc_x86 \
  mingw_math \
  mips_cpu_features \
  module_aot_ref_count \
  module_jit_ref_count \
  noos \
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
  posix_print \
  posix_tempfile \
  posix_threads \
  powerpc_cpu_features \
  profiler \
  profiler_inlined \
  qurt_allocator \
  qurt_hvx \
  renderscript \
  runtime_api \
  ssp \
  thread_pool \
  to_string \
  tracing \
  windows_clock \
  windows_cuda \
  windows_get_symbol \
  windows_io \
  windows_opencl \
  windows_tempfile \
  windows_threads \
  write_debug_image \
  x86_cpu_features

RUNTIME_LL_COMPONENTS = \
  aarch64 \
  arm \
  arm_no_neon \
  hvx_64 \
  hvx_128 \
  mips \
  pnacl_math \
  posix_math \
  powerpc \
  ptx_dev \
  renderscript_dev \
  win32_math \
  x86 \
  x86_avx \
  x86_sse41

RUNTIME_EXPORTED_INCLUDES = $(INCLUDE_DIR)/HalideRuntime.h \
                            $(INCLUDE_DIR)/HalideRuntimeCuda.h \
                            $(INCLUDE_DIR)/HalideRuntimeHexagonHost.h \
                            $(INCLUDE_DIR)/HalideRuntimeOpenCL.h \
                            $(INCLUDE_DIR)/HalideRuntimeOpenGL.h \
                            $(INCLUDE_DIR)/HalideRuntimeOpenGLCompute.h \
                            $(INCLUDE_DIR)/HalideRuntimeMetal.h	\
                            $(INCLUDE_DIR)/HalideRuntimeQurt.h \
                            $(INCLUDE_DIR)/HalideRuntimeRenderscript.h

INITIAL_MODULES = $(RUNTIME_CPP_COMPONENTS:%=$(BUILD_DIR)/initmod.%_32.o) \
                  $(RUNTIME_CPP_COMPONENTS:%=$(BUILD_DIR)/initmod.%_64.o) \
                  $(RUNTIME_CPP_COMPONENTS:%=$(BUILD_DIR)/initmod.%_32_debug.o) \
                  $(RUNTIME_CPP_COMPONENTS:%=$(BUILD_DIR)/initmod.%_64_debug.o) \
                  $(RUNTIME_LL_COMPONENTS:%=$(BUILD_DIR)/initmod.%_ll.o) \
                  $(PTX_DEVICE_INITIAL_MODULES:libdevice.%.bc=$(BUILD_DIR)/initmod_ptx.%_ll.o)

# Add the Hexagon simulator to the rpath on Linux. (Not supported elsewhere, so no else cases.)
ifeq ($(UNAME), Linux)
ifneq (,$(WITH_HEXAGON))
ifneq (,$(HL_HEXAGON_TOOLS))
TEST_LD_FLAGS += -Wl,--rpath=$(ROOT_DIR)/src/runtime/hexagon_remote/bin/host
TEST_LD_FLAGS += -Wl,--rpath=$(HL_HEXAGON_TOOLS)/lib/iss
endif
endif
endif

.PHONY: all
all: $(LIB_DIR)/libHalide.a $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h $(RUNTIME_EXPORTED_INCLUDES) test_internal

ifeq ($(USE_LLVM_SHARED_LIB), )
$(LIB_DIR)/libHalide.a: $(OBJECTS) $(INITIAL_MODULES)
	# Determine the relevant object files from llvm with a dummy
	# compilation. Passing -t to the linker gets it to list which
	# object files in which archives it uses to resolve
	# symbols. We only care about the libLLVM ones.
	@rm -rf $(BUILD_DIR)/llvm_objects
	@mkdir -p $(BUILD_DIR)/llvm_objects
	$(CXX) -o /dev/null -shared $(OBJECTS) $(INITIAL_MODULES) -Wl,-t $(LLVM_STATIC_LIBS) $(LIBDL) -lz -lpthread | egrep "libLLVM" > $(BUILD_DIR)/llvm_objects/list
	cat $(BUILD_DIR)/llvm_objects/list | sed = | sed "N;s/[()]/ /g;s/\n /\n/;s/\([0-9]*\)\n\([^ ]*\) \([^ ]*\)/ar x \2 \3; mv \3 llvm_\1_\3/" > $(BUILD_DIR)/llvm_objects/extract.sh
	# Extract the necessary object files from the llvm archives.
	cd $(BUILD_DIR)/llvm_objects; bash ./extract.sh
	# Archive together all the halide and llvm object files
	@-mkdir -p $(LIB_DIR)
	@rm -f $(LIB_DIR)/libHalide.a
	# ar breaks on MinGW with all objects at the same time.
	echo $(OBJECTS) $(INITIAL_MODULES) $(BUILD_DIR)/llvm_objects/llvm_*.o* | xargs -n200 ar q $(LIB_DIR)/libHalide.a
	ranlib $(LIB_DIR)/libHalide.a
else
$(LIB_DIR)/libHalide.a: $(OBJECTS) $(INITIAL_MODULES)
	@-mkdir -p $(BIN_DIR)
	@rm -f $(LIB_DIR)/libHalide.a
	ar q $(LIB_DIR)/libHalide.a $(OBJECTS) $(INITIAL_MODULES)
	ranlib $(LIB_DIR)/libHalide.a
endif

$(BIN_DIR)/libHalide.$(SHARED_EXT): $(LIB_DIR)/libHalide.a
	@-mkdir -p $(BIN_DIR)
	$(CXX) $(BUILD_BIT_SIZE) -shared $(OBJECTS) $(INITIAL_MODULES) $(LLVM_STATIC_LIBS) $(LLVM_LD_FLAGS) $(LLVM_SHARED_LIBS) $(LIBDL) -lz -lpthread -o $(BIN_DIR)/libHalide.$(SHARED_EXT)
ifeq ($(UNAME), Darwin)
	install_name_tool -id $(CURDIR)/$(BIN_DIR)/libHalide.$(SHARED_EXT) $(BIN_DIR)/libHalide.$(SHARED_EXT)
endif

$(INCLUDE_DIR)/Halide.h: $(HEADERS) $(SRC_DIR)/HalideFooter.h $(BIN_DIR)/build_halide_h
	mkdir -p $(INCLUDE_DIR)
	$(BIN_DIR)/build_halide_h $(HEADERS) $(SRC_DIR)/HalideFooter.h > $(INCLUDE_DIR)/Halide.h

$(INCLUDE_DIR)/HalideRuntime%: $(SRC_DIR)/runtime/HalideRuntime%
	echo Copying $<
	mkdir -p $(INCLUDE_DIR)
	cp $< $(INCLUDE_DIR)/

$(BIN_DIR)/build_halide_h: $(ROOT_DIR)/tools/build_halide_h.cpp
	$(CXX) $< -o $@

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
	rm -rf $(LIB_DIR)/*
	rm -rf $(BIN_DIR)/*
	rm -rf $(BUILD_DIR)/*
	rm -rf $(TMP_DIR)/*
	rm -rf $(FILTERS_DIR)/*
	rm -rf $(RUNTIMES_DIR)/*
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

test_correctness: $(CORRECTNESS_TESTS:$(ROOT_DIR)/test/correctness/%.cpp=correctness_%)
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
# For generator tests they time the compile time only. The times are recorded in CSV files.
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

build_tests: $(CORRECTNESS_TESTS:$(ROOT_DIR)/test/correctness/%.cpp=$(BIN_DIR)/correctness_%) \
	$(PERFORMANCE_TESTS:$(ROOT_DIR)/test/performance/%.cpp=$(BIN_DIR)/performance_%) \
	$(ERROR_TESTS:$(ROOT_DIR)/test/error/%.cpp=$(BIN_DIR)/error_%) \
	$(WARNING_TESTS:$(ROOT_DIR)/test/warning/%.cpp=$(BIN_DIR)/warning_%) \
	$(OPENGL_TESTS:$(ROOT_DIR)/test/opengl/%.cpp=$(BIN_DIR)/opengl_%) \
	$(GENERATOR_EXTERNAL_TESTS:$(ROOT_DIR)/test/generator/%_aottest.cpp=$(BIN_DIR)/generator_aot_%) \
	$(GENERATOR_EXTERNAL_TESTS:$(ROOT_DIR)/test/generator/%_jittest.cpp=$(BIN_DIR)/generator_jit_%) \
	$(RENDERSCRIPT_TESTS:$(ROOT_DIR)/test/renderscript/%.cpp=$(BIN_DIR)/renderscript_%)

time_compilation_tests: time_compilation_correctness time_compilation_performance time_compilation_generators

# Make an empty generator for generating runtimes.
$(BIN_DIR)/runtime.generator: $(ROOT_DIR)/tools/GenGen.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT)
	$(CXX) $(TEST_CXX_FLAGS) $< -I$(INCLUDE_DIR) $(TEST_LD_FLAGS) -o $@

# Generate a standalone runtime for a given target string
$(RUNTIMES_DIR)/runtime_%.a: $(BIN_DIR)/runtime.generator
	@mkdir -p $(RUNTIMES_DIR)
	$(CURDIR)/$< -r $(basename $(notdir $@)) -o $(CURDIR)/$(RUNTIMES_DIR) target=$*

$(BIN_DIR)/test_internal: $(ROOT_DIR)/test/internal.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT)
	$(CXX) $(TEST_CXX_FLAGS) $< -I$(SRC_DIR) $(TEST_LD_FLAGS) -o $@

# Correctness test that link against libHalide
$(BIN_DIR)/correctness_%: $(ROOT_DIR)/test/correctness/%.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h $(INCLUDE_DIR)/HalideRuntime.h
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE) $< -I$(INCLUDE_DIR) $(TEST_LD_FLAGS) -o $@

$(BIN_DIR)/performance_%: $(ROOT_DIR)/test/performance/%.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h $(ROOT_DIR)/apps/support/benchmark.h
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE) $< -I$(INCLUDE_DIR) $(TEST_LD_FLAGS) -o $@

# Error tests that link against libHalide
$(BIN_DIR)/error_%: $(ROOT_DIR)/test/error/%.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE) $< -I$(INCLUDE_DIR) $(TEST_LD_FLAGS) -o $@

$(BIN_DIR)/warning_%: $(ROOT_DIR)/test/warning/%.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE) $< -I$(INCLUDE_DIR) $(TEST_LD_FLAGS) -o $@

$(BIN_DIR)/opengl_%: $(ROOT_DIR)/test/opengl/%.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE) $< -I$(INCLUDE_DIR) -I$(SRC_DIR) $(TEST_LD_FLAGS) $(OPENGL_LD_FLAGS) -o $@

$(BIN_DIR)/renderscript_%: $(ROOT_DIR)/test/renderscript/%.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE) $< -I$(INCLUDE_DIR) -I$(SRC_DIR) $(TEST_LD_FLAGS) -o $@


# TODO(srj): this doesn't auto-delete, why not?
.INTERMEDIATE: $(BIN_DIR)/%.generator

# By default, %.generator is produced by building %_generator.cpp
# Note that the rule includes all _generator.cpp files, so that generators with define_extern
# usage can just add deps later.
$(BIN_DIR)/%.generator: $(ROOT_DIR)/test/generator/%_generator.cpp $(GENGEN_DEPS)
	@mkdir -p $(BIN_DIR)
	$(CXX) -g $(TEST_CXX_FLAGS) -I$(INCLUDE_DIR) $(filter %_generator.cpp,$^) $(ROOT_DIR)/tools/GenGen.cpp $(TEST_LD_FLAGS) -o $@

NON_EMPTY_TARGET=$(if $(HL_TARGET),$(HL_TARGET),host)
NAME_MANGLING_TARGET=$(NON_EMPTY_TARGET)-c_plus_plus_name_mangling

# By default, %.a/.h are produced by executing %.generator. Runtimes are not included in these.
$(FILTERS_DIR)/%.a: $(BIN_DIR)/%.generator
	@mkdir -p $(FILTERS_DIR)
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR); $(CURDIR)/$< -g $(notdir $*) -o $(CURDIR)/$(FILTERS_DIR) target=$(HL_TARGET)-no_runtime

$(FILTERS_DIR)/%.h: $(FILTERS_DIR)/%.a
	@echo $@ produced implicitly by $^

# If we want to use a Generator with custom GeneratorParams, we need to write
# custom rules: to pass the GeneratorParams, and to give a unique function and file name.
$(FILTERS_DIR)/cxx_mangling.a: $(BIN_DIR)/cxx_mangling.generator
	@mkdir -p $(FILTERS_DIR)
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR); $(CURDIR)/$< -g $(notdir $*) -o $(CURDIR)/$(FILTERS_DIR) target=$(HL_TARGET)-no_runtime-c_plus_plus_name_mangling -f "HalideTest::cxx_mangling"

$(FILTERS_DIR)/cxx_mangling_define_extern.a: $(BIN_DIR)/cxx_mangling_define_extern.generator
	@mkdir -p $(FILTERS_DIR)
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR); $(CURDIR)/$< -g $(notdir $*) -o $(CURDIR)/$(FILTERS_DIR) target=$(HL_TARGET)-no_runtime-c_plus_plus_name_mangling -f "HalideTest::cxx_mangling_define_extern"

$(FILTERS_DIR)/tiled_blur_interleaved.a: $(BIN_DIR)/tiled_blur.generator
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR); $(CURDIR)/$< -g tiled_blur -f tiled_blur_interleaved -o $(CURDIR)/$(FILTERS_DIR) target=$(HL_TARGET)-no_runtime is_interleaved=true

$(FILTERS_DIR)/tiled_blur_blur_interleaved.a: $(BIN_DIR)/tiled_blur_blur.generator
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR); $(CURDIR)/$< -g tiled_blur_blur -f tiled_blur_blur_interleaved -o $(CURDIR)/$(FILTERS_DIR) target=$(HL_TARGET)-no_runtime is_interleaved=true

# metadata_tester is built with and without user-context
$(FILTERS_DIR)/metadata_tester.a: $(BIN_DIR)/metadata_tester.generator
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR); $(CURDIR)/$< -f metadata_tester -o $(CURDIR)/$(FILTERS_DIR) target=$(HL_TARGET)-register_metadata-no_runtime

$(FILTERS_DIR)/metadata_tester_ucon.a: $(BIN_DIR)/metadata_tester.generator
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR); $(CURDIR)/$< -f metadata_tester_ucon -o $(CURDIR)/$(FILTERS_DIR) target=$(HL_TARGET)-user_context-register_metadata-no_runtime

$(BIN_DIR)/generator_aot_metadata_tester: $(FILTERS_DIR)/metadata_tester_ucon.a

$(FILTERS_DIR)/multitarget.a: $(BIN_DIR)/multitarget.generator
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR); $(LD_PATH_SETUP) $(CURDIR)/$< -f "HalideTest::multitarget" -o $(CURDIR)/$(FILTERS_DIR) target=$(HL_TARGET)-debug-no_runtime-c_plus_plus_name_mangling,$(HL_TARGET)-no_runtime-c_plus_plus_name_mangling  -e assembly,bitcode,cpp,h,html,static_library,stmt

# user_context needs to be generated with user_context as the first argument to its calls
$(FILTERS_DIR)/user_context.a: $(BIN_DIR)/user_context.generator
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR); $(CURDIR)/$< -o $(CURDIR)/$(FILTERS_DIR) target=$(HL_TARGET)-no_runtime-user_context

# ditto for user_context_insanity
$(FILTERS_DIR)/user_context_insanity.a: $(BIN_DIR)/user_context_insanity.generator
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR); $(CURDIR)/$< -o $(CURDIR)/$(FILTERS_DIR) target=$(HL_TARGET)-no_runtime-user_context

# Some .generators have additional dependencies (usually due to define_extern usage).
# These typically require two extra dependencies:
# (1) Ensuring the extra _generator.cpp is built into the .generator.
# (2) Ensuring the extra .a is linked into the final output.

# tiled_blur also needs tiled_blur_blur, due to an extern_generator dependency.
$(BIN_DIR)/tiled_blur.generator: $(ROOT_DIR)/test/generator/tiled_blur_blur_generator.cpp
# TODO(srj): we really want to say "anything that depends on tiled_blur.a also depends on tiled_blur_blur.a";
# is there a way to specify that in Make?
$(BIN_DIR)/generator_aot_tiled_blur: $(FILTERS_DIR)/tiled_blur_blur.a
$(BIN_DIR)/generator_aot_tiled_blur_interleaved: $(FILTERS_DIR)/tiled_blur_blur_interleaved.a
$(BIN_DIR)/generator_aot_cxx_mangling_define_extern: $(FILTERS_DIR)/cxx_mangling.a

# Usually, it's considered best practice to have one Generator per
# .cpp file, with the generator-name and filename matching;
# nested_externs_generators.cpp is a counterexample, and thus requires
# some special casing to get right.  First, make a special rule to
# build each of the Generators in nested_externs_generator.cpp (which
# all have the form nested_externs_*).
$(FILTERS_DIR)/nested_externs_%.a: $(BIN_DIR)/nested_externs.generator
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR); $(CURDIR)/$< -g nested_externs_$* -o $(CURDIR)/$(FILTERS_DIR) target=$(HL_TARGET)-no_runtime

$(BIN_DIR)/generator_aot_nested_externs: $(ROOT_DIR)/test/generator/nested_externs_aottest.cpp $(FILTERS_DIR)/nested_externs_root.a $(FILTERS_DIR)/nested_externs_inner.a $(FILTERS_DIR)/nested_externs_combine.a $(FILTERS_DIR)/nested_externs_leaf.a $(INCLUDE_DIR)/HalideRuntime.h $(RUNTIMES_DIR)/runtime_$(HL_TARGET).a
	$(CXX) $(TEST_CXX_FLAGS) $(filter-out %.h,$^) -I$(INCLUDE_DIR) -I$(FILTERS_DIR) -I $(ROOT_DIR)/apps/support -I $(SRC_DIR)/runtime -I$(ROOT_DIR)/tools -lpthread $(LIBDL) -o $@

# By default, %_aottest.cpp depends on $(FILTERS_DIR)/%.a/.h (but not libHalide).
$(BIN_DIR)/generator_aot_%: $(ROOT_DIR)/test/generator/%_aottest.cpp $(FILTERS_DIR)/%.a $(FILTERS_DIR)/%.h $(INCLUDE_DIR)/HalideRuntime.h $(RUNTIMES_DIR)/runtime_$(HL_TARGET).a
	$(CXX) $(TEST_CXX_FLAGS) $(filter-out %.h,$^) -I$(INCLUDE_DIR) -I$(FILTERS_DIR) -I $(ROOT_DIR)/apps/support -I $(SRC_DIR)/runtime -I$(ROOT_DIR)/tools -lpthread $(LIBDL) -o $@

$(BIN_DIR)/generator_aot_multitarget: $(ROOT_DIR)/test/generator/multitarget_aottest.cpp $(FILTERS_DIR)/multitarget.a $(FILTERS_DIR)/multitarget.h $(INCLUDE_DIR)/HalideRuntime.h $(RUNTIMES_DIR)/runtime_$(HL_TARGET).a
	$(CXX) $(TEST_CXX_FLAGS) $(filter-out %.h,$^) -I$(INCLUDE_DIR) -I$(FILTERS_DIR) -I $(ROOT_DIR)/apps/support -I $(SRC_DIR)/runtime -I$(ROOT_DIR)/tools -lpthread $(LIBDL) -o $@

# The matlab tests needs "-matlab" in the runtime
$(BIN_DIR)/generator_aot_matlab: $(ROOT_DIR)/test/generator/matlab_aottest.cpp $(FILTERS_DIR)/matlab.a $(FILTERS_DIR)/matlab.h $(INCLUDE_DIR)/HalideRuntime.h $(RUNTIMES_DIR)/runtime_$(HL_TARGET)-matlab.a
	$(CXX) $(TEST_CXX_FLAGS) $(filter-out %.h,$^) -I$(INCLUDE_DIR) -I$(FILTERS_DIR) -I $(ROOT_DIR)/apps/support -I $(SRC_DIR)/runtime -I$(ROOT_DIR)/tools -lpthread $(LIBDL) $(TEST_LD_FLAGS) -o $@

# acquire_release is the only test that explicitly uses CUDA/OpenCL APIs, so link only those here.
$(BIN_DIR)/generator_aot_acquire_release: $(ROOT_DIR)/test/generator/acquire_release_aottest.cpp $(FILTERS_DIR)/acquire_release.a $(FILTERS_DIR)/acquire_release.h $(INCLUDE_DIR)/HalideRuntime.h $(RUNTIMES_DIR)/runtime_$(HL_TARGET).a
	$(CXX) $(TEST_CXX_FLAGS) $(filter-out %.h,$^) -I$(INCLUDE_DIR) -I$(FILTERS_DIR) -I $(ROOT_DIR)/apps/support -I $(SRC_DIR)/runtime -I$(ROOT_DIR)/tools -lpthread $(LIBDL) $(OPENCL_LD_FLAGS) $(CUDA_LD_FLAGS) -o $@

# By default, %_jittest.cpp depends on libHalide. These are external tests that use the JIT.
$(BIN_DIR)/generator_jit_%: $(ROOT_DIR)/test/generator/%_jittest.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h
	$(CXX) $(TEST_CXX_FLAGS) $(filter-out %.h %.$(SHARED_EXT),$^) -I$(INCLUDE_DIR) -I$(FILTERS_DIR) -I $(ROOT_DIR)/apps/support $(TEST_LD_FLAGS) -o $@

# generator_aot_multitarget is run multiple times, with different env vars.
generator_aot_multitarget: $(BIN_DIR)/generator_aot_multitarget
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; HL_MULTITARGET_TEST_USE_DEBUG_FEATURE=0 $(LD_PATH_SETUP) $(CURDIR)/$<
	cd $(TMP_DIR) ; HL_MULTITARGET_TEST_USE_DEBUG_FEATURE=1 $(LD_PATH_SETUP) $(CURDIR)/$<
	@-echo

# nested externs doesn't actually contain a generator named
# "nested_externs", and has no internal tests in any case.
test_generator_nested_externs:
	@echo "Skipping"

$(BIN_DIR)/tutorial_%: $(ROOT_DIR)/tutorial/%.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h
	@ if [[ $@ == *_run ]]; then \
		export TUTORIAL=$* ;\
		export LESSON=`echo $${TUTORIAL} | cut -b1-9`; \
		make -f $(THIS_MAKEFILE) tutorial_$${TUTORIAL/run/generate}; \
		$(CXX) $(TUTORIAL_CXX_FLAGS) $(LIBPNG_CXX_FLAGS) $(OPTIMIZE) $< \
		-I$(TMP_DIR) $(TMP_DIR)/$${LESSON}_*.a -lpthread $(LIBDL) $(LIBPNG_LIBS) -lz -o $@; \
	else \
		$(CXX) $(TUTORIAL_CXX_FLAGS) $(LIBPNG_CXX_FLAGS) $(OPTIMIZE) $< \
		-I$(INCLUDE_DIR) -I$(ROOT_DIR)/tools $(TEST_LD_FLAGS) $(LIBPNG_LIBS) -o $@;\
	fi

$(BIN_DIR)/tutorial_lesson_15_generators: $(ROOT_DIR)/tutorial/lesson_15_generators.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h
	$(CXX) $(TUTORIAL_CXX_FLAGS) $(LIBPNG_CXX_FLAGS) $(OPTIMIZE) $< $(ROOT_DIR)/tools/GenGen.cpp \
	-I$(INCLUDE_DIR) $(TEST_LD_FLAGS) $(LIBPNG_LIBS) -o $@

tutorial_lesson_15_generators: $(ROOT_DIR)/tutorial/lesson_15_generators_usage.sh $(BIN_DIR)/tutorial_lesson_15_generators
	@-mkdir -p $(TMP_DIR)
	cp $(BIN_DIR)/tutorial_lesson_15_generators $(TMP_DIR)/lesson_15_generate; \
	cd $(TMP_DIR); \
	source $(ROOT_DIR)/tutorial/lesson_15_generators_usage.sh
	@-echo

$(BIN_DIR)/tutorial_lesson_16_rgb_generate: $(ROOT_DIR)/tutorial/lesson_16_rgb_generate.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h
	$(CXX) $(TUTORIAL_CXX_FLAGS) $(LIBPNG_CXX_FLAGS) $(OPTIMIZE) $< $(ROOT_DIR)/tools/GenGen.cpp \
	-I$(INCLUDE_DIR) $(TEST_LD_FLAGS) $(LIBPNG_LIBS) -o $@

$(BIN_DIR)/tutorial_lesson_16_rgb_run: $(ROOT_DIR)/tutorial/lesson_16_rgb_run.cpp $(BIN_DIR)/tutorial_lesson_16_rgb_generate
	@-mkdir -p $(TMP_DIR)
	# Run the generator
	$(BIN_DIR)/tutorial_lesson_16_rgb_generate -o $(TMP_DIR) -f brighten_planar      target=host layout=planar
	$(BIN_DIR)/tutorial_lesson_16_rgb_generate -o $(TMP_DIR) -f brighten_interleaved target=host-no_runtime layout=interleaved
	$(BIN_DIR)/tutorial_lesson_16_rgb_generate -o $(TMP_DIR) -f brighten_either      target=host-no_runtime layout=either
	$(BIN_DIR)/tutorial_lesson_16_rgb_generate -o $(TMP_DIR) -f brighten_specialized target=host-no_runtime layout=specialized
	# Compile the runner
	$(CXX) $(TUTORIAL_CXX_FLAGS) $(LIBPNG_CXX_FLAGS) $(OPTIMIZE) $< \
	-I$(INCLUDE_DIR) -L$(BIN_DIR) -I $(TMP_DIR) $(TMP_DIR)/brighten_*.a \
        -lHalide $(TEST_LD_FLAGS) -lpthread $(LIBDL) $(LIBPNG_LIBS) -lz -o $@
	@-echo

test_internal: $(BIN_DIR)/test_internal
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; $(CURDIR)/$<
	@-echo

correctness_%: $(BIN_DIR)/correctness_%
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; $(CURDIR)/$<
	@-echo

valgrind_%: $(BIN_DIR)/correctness_%
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; valgrind --error-exitcode=-1 $(CURDIR)/$<
	@-echo

# This test is *supposed* to do an out-of-bounds read, so skip it when testing under valgrind
valgrind_tracing_stack: $(BIN_DIR)/correctness_tracing_stack
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; $(CURDIR)/$(BIN_DIR)/correctness_tracing_stack
	@-echo

performance_%: $(BIN_DIR)/performance_%
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; $(CURDIR)/$<
	@-echo

error_%: $(BIN_DIR)/error_%
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; $(CURDIR)/$< 2>&1 | egrep --q "terminating with uncaught exception|^terminate called|^Error"
	@-echo

warning_%: $(BIN_DIR)/warning_%
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; $(CURDIR)/$< 2>&1 | egrep --q "^Warning"
	@-echo

opengl_%: HL_JIT_TARGET ?= host-opengl
opengl_%: $(BIN_DIR)/opengl_%
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; HL_JIT_TARGET=$(HL_JIT_TARGET) $(CURDIR)/$< 2>&1
	@-echo

renderscript_jit_%: HL_JIT_TARGET = host-renderscript
renderscript_jit_%: HL_TARGET =
renderscript_aot_%: HL_TARGET = arm-32-android-armv7s-renderscript
renderscript_aot_%: HL_JIT_TARGET =
renderscript_%_error: $(BIN_DIR)/renderscript_%_error
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; HL_JIT_TARGET=$(HL_JIT_TARGET) HL_TARGET=$(HL_TARGET) $(CURDIR)/$< 2>&1 | egrep --q "terminating with uncaught exception|^terminate called|^Error"
	@-echo
renderscript_%: $(BIN_DIR)/renderscript_%
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; HL_JIT_TARGET=$(HL_JIT_TARGET) HL_TARGET=$(HL_TARGET) $(CURDIR)/$<
	@-echo

generator_%: $(BIN_DIR)/generator_%
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; $(CURDIR)/$<
	@-echo

$(TMP_DIR)/images/%.png: $(ROOT_DIR)/tutorial/images/%.png
	@-mkdir -p $(TMP_DIR)/images
	cp $< $(TMP_DIR)/images/

tutorial_%: $(BIN_DIR)/tutorial_% $(TMP_DIR)/images/rgb.png $(TMP_DIR)/images/gray.png
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; $(CURDIR)/$<
	@-echo

time_compilation_test_%: $(BIN_DIR)/test_%
	$(TIME_COMPILATION) compile_times_correctness.csv make -f $(THIS_MAKEFILE) $(@:time_compilation_test_%=test_%)

time_compilation_performance_%: $(BIN_DIR)/performance_%
	$(TIME_COMPILATION) compile_times_performance.csv make -f $(THIS_MAKEFILE) $(@:time_compilation_performance_%=performance_%)

time_compilation_opengl_%: $(BIN_DIR)/opengl_%
	$(TIME_COMPILATION) compile_times_opengl.csv make -f $(THIS_MAKEFILE) $(@:time_compilation_opengl_%=opengl_%)

time_compilation_renderscript_%: $(BIN_DIR)/renderscript_%
	$(TIME_COMPILATION) compile_times_renderscript.csv make -f $(THIS_MAKEFILE) $(@:time_compilation_renderscript_%=renderscript_%)

time_compilation_generator_%: $(BIN_DIR)/%.generator
	$(TIME_COMPILATION) compile_times_generator.csv make -f $(THIS_MAKEFILE) $(@:time_compilation_generator_%=$(FILTERS_DIR)/%.a)

time_compilation_generator_tiled_blur_interleaved: $(BIN_DIR)/tiled_blur.generator
	$(TIME_COMPILATION) compile_times_generator.csv make -f $(THIS_MAKEFILE) $(FILTERS_DIR)/tiled_blur_interleaved.a

.PHONY: test_apps
test_apps: $(LIB_DIR)/libHalide.a $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h $(INCLUDE_DIR)/HalideRuntime.h
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
	        $(ROOT_DIR)/apps/fft \
	        $(ROOT_DIR)/apps/images \
	        $(ROOT_DIR)/apps/support \
                apps; \
	  cp -r $(ROOT_DIR)/tools .; \
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
	make -C apps/fft bench_16x16  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	make -C apps/fft bench_32x32  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	make -C apps/fft bench_48x48  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	cd apps/HelloMatlab; HALIDE_PATH=$(CURDIR) HALIDE_CXX=$(CXX) ./run_blur.sh

.PHONY: test_python
test_python: $(LIB_DIR)/libHalide.a
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

ifneq (,$(findstring clang version 3.9,$(CLANG_VERSION)))
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

ifneq (,$(findstring $(LLVM_VERSION_TIMES_10), 35 36 37 38 39))
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

install: $(LIB_DIR)/libHalide.a $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h $(RUNTIME_EXPORTED_INCLUDES)
	mkdir -p $(PREFIX)/include $(PREFIX)/bin $(PREFIX)/lib $(PREFIX)/share/halide/tutorial/images $(PREFIX)/share/halide/tools $(PREFIX)/share/halide/tutorial/figures
	cp $(LIB_DIR)/libHalide.a $(BIN_DIR)/libHalide.$(SHARED_EXT) $(PREFIX)/lib
	cp $(INCLUDE_DIR)/Halide.h $(PREFIX)/include
	cp $(INCLUDE_DIR)/HalideRuntim*.h $(PREFIX)/include
	cp $(ROOT_DIR)/tutorial/images/*.png $(PREFIX)/share/halide/tutorial/images
	cp $(ROOT_DIR)/tutorial/figures/*.gif $(PREFIX)/share/halide/tutorial/figures
	cp $(ROOT_DIR)/tutorial/figures/*.jpg $(PREFIX)/share/halide/tutorial/figures
	cp $(ROOT_DIR)/tutorial/figures/*.mp4 $(PREFIX)/share/halide/tutorial/figures
	cp $(ROOT_DIR)/tutorial/*.cpp $(PREFIX)/share/halide/tutorial
	cp $(ROOT_DIR)/tutorial/*.h $(PREFIX)/share/halide/tutorial
	cp $(ROOT_DIR)/tutorial/*.sh $(PREFIX)/share/halide/tutorial
	cp $(ROOT_DIR)/tools/mex_halide.m $(PREFIX)/share/halide/tools
	cp $(ROOT_DIR)/tools/GenGen.cpp $(PREFIX)/share/halide/tools
	cp $(ROOT_DIR)/tools/halide_image.h $(PREFIX)/share/halide/tools
	cp $(ROOT_DIR)/tools/halide_image_io.h $(PREFIX)/share/halide/tools
	cp $(ROOT_DIR)/tools/halide_image_info.h $(PREFIX)/share/halide/tools

$(DISTRIB_DIR)/halide.tgz: $(LIB_DIR)/libHalide.a $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h $(RUNTIME_EXPORTED_INCLUDES)
	mkdir -p $(DISTRIB_DIR)/include $(DISTRIB_DIR)/bin $(DISTRIB_DIR)/lib $(DISTRIB_DIR)/tutorial $(DISTRIB_DIR)/tutorial/images $(DISTRIB_DIR)/tools $(DISTRIB_DIR)/tutorial/figures
	cp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(DISTRIB_DIR)/bin
	cp $(LIB_DIR)/libHalide.a $(DISTRIB_DIR)/lib
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
	cp $(ROOT_DIR)/tools/halide_image_info.h $(DISTRIB_DIR)/tools
	cp $(ROOT_DIR)/README.md $(DISTRIB_DIR)
	ln -sf $(DISTRIB_DIR) halide
	tar -czf $(DISTRIB_DIR)/halide.tgz halide/bin halide/lib halide/include halide/tutorial halide/README.md halide/tools/mex_halide.m halide/tools/GenGen.cpp halide/tools/halide_image.h halide/tools/halide_image_io.h halide/tools/halide_image_info.h
	rm -rf halide

.PHONY: distrib
distrib: $(DISTRIB_DIR)/halide.tgz

$(BIN_DIR)/HalideTraceViz: $(ROOT_DIR)/util/HalideTraceViz.cpp
	$(CXX) $(OPTIMIZE) -std=c++11 $< -I$(INCLUDE_DIR) -L$(BIN_DIR) -o $@
