# 'make' builds libHalide.a, the internal test suite, and runs the internal test suite
# 'make run_tests' builds and runs all the end-to-end tests in the test subdirectory
# 'make {error,performance}_foo' builds and runs test/{...}/foo.cpp for any
#     cpp file in the corresponding subdirectory of the test folder
# 'make test_foo' builds and runs test/correctness/foo.cpp for any
#     cpp file in the correctness/ subdirectoy of the test folder
# 'make test_apps' checks some of the apps build and run (but does not check their output)
# 'make time_compilation_tests' records the compile time for each test module into a csv file.
#     For correctness and performance tests this include halide build time and run time. For
#     the tests in test/generator/ this times only the halide build time.

UNAME = $(shell uname)

ifeq ($(OS), Windows_NT)
    # assume we are building for the MinGW environment
    LIBDL =
    SHARED_EXT=dll
    FPIC=
else
    # let's assume "normal" UNIX such as linux
    LIBDL=-ldl
    FPIC=-fPIC
ifeq ($(UNAME), Darwin)
    SHARED_EXT=dylib
else
    SHARED_EXT=so
endif
endif

BAZEL ?= $(shell which bazel)

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
LLVM_CXX_FLAGS = -std=c++11  $(filter-out -O% -g -fomit-frame-pointer -pedantic -W% -W, $(shell $(LLVM_CONFIG) --cxxflags | sed -e 's/\\/\//g' -e 's/\([a-zA-Z]\):/\/\1/g;s/-D/ -D/g;s/-O/ -O/g'))
OPTIMIZE ?= -O3

LLVM_VERSION_TIMES_10 = $(shell $(LLVM_CONFIG) --version | cut -b 1,3)

LLVM_CXX_FLAGS += -DLLVM_VERSION=$(LLVM_VERSION_TIMES_10)

# All WITH_* flags are either empty or not-empty. They do not behave
# like true/false values in most languages.  To turn one off, either
# edit this file, add "WITH_FOO=" (no assigned value) to the make
# line, or define an environment variable WITH_FOO that has an empty
# value.
WITH_X86 ?= $(findstring x86, $(LLVM_COMPONENTS))
WITH_ARM ?= $(findstring arm, $(LLVM_COMPONENTS))
ifneq (,$(findstring $(LLVM_VERSION_TIMES_10), 39 40 50 60))
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
ifeq ($(OS), Windows_NT)
    WITH_INTROSPECTION ?=
else
    WITH_INTROSPECTION ?= not-empty
endif
WITH_EXCEPTIONS ?=

# If HL_TARGET or HL_JIT_TARGET aren't set, use host
HL_TARGET ?= host
HL_JIT_TARGET ?= host

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

AARCH64_CXX_FLAGS=$(if $(WITH_AARCH64), -DWITH_AARCH64=1, )
AARCH64_LLVM_CONFIG_LIB=$(if $(WITH_AARCH64), aarch64, )

INTROSPECTION_CXX_FLAGS=$(if $(WITH_INTROSPECTION), -DWITH_INTROSPECTION, )
EXCEPTIONS_CXX_FLAGS=$(if $(WITH_EXCEPTIONS), -DWITH_EXCEPTIONS, )

HEXAGON_CXX_FLAGS=$(if $(WITH_HEXAGON), -DWITH_HEXAGON=1, )
HEXAGON_LLVM_CONFIG_LIB=$(if $(WITH_HEXAGON), hexagon, )

CXX_WARNING_FLAGS = -Wall -Werror -Wno-unused-function -Wcast-qual -Wignored-qualifiers -Wno-comment -Wsign-compare -Wno-unknown-warning-option -Wno-psabi
CXX_FLAGS = $(CXX_WARNING_FLAGS) -fno-rtti -Woverloaded-virtual $(FPIC) $(OPTIMIZE) -fno-omit-frame-pointer -DCOMPILING_HALIDE

CXX_FLAGS += $(LLVM_CXX_FLAGS)
CXX_FLAGS += $(PTX_CXX_FLAGS)
CXX_FLAGS += $(ARM_CXX_FLAGS)
CXX_FLAGS += $(HEXAGON_CXX_FLAGS)
CXX_FLAGS += $(AARCH64_CXX_FLAGS)
CXX_FLAGS += $(X86_CXX_FLAGS)
CXX_FLAGS += $(OPENCL_CXX_FLAGS)
CXX_FLAGS += $(METAL_CXX_FLAGS)
CXX_FLAGS += $(OPENGL_CXX_FLAGS)
CXX_FLAGS += $(MIPS_CXX_FLAGS)
CXX_FLAGS += $(POWERPC_CXX_FLAGS)
CXX_FLAGS += $(INTROSPECTION_CXX_FLAGS)
CXX_FLAGS += $(EXCEPTIONS_CXX_FLAGS)

# This is required on some hosts like powerpc64le-linux-gnu because we may build
# everything with -fno-exceptions.  Without -funwind-tables, libHalide.so fails
# to propagate exceptions and causes a test failure.
CXX_FLAGS += -funwind-tables

print-%:
	@echo '$*=$($*)'

LLVM_STATIC_LIBS = -L $(LLVM_LIBDIR) $(shell $(LLVM_CONFIG) --libs bitwriter bitreader linker ipo mcjit $(X86_LLVM_CONFIG_LIB) $(ARM_LLVM_CONFIG_LIB) $(OPENCL_LLVM_CONFIG_LIB) $(METAL_LLVM_CONFIG_LIB) $(PTX_LLVM_CONFIG_LIB) $(AARCH64_LLVM_CONFIG_LIB) $(MIPS_LLVM_CONFIG_LIB) $(POWERPC_LLVM_CONFIG_LIB) $(HEXAGON_LLVM_CONFIG_LIB))

LLVM_LD_FLAGS = $(shell $(LLVM_CONFIG) --ldflags --system-libs | sed -e 's/\\/\//g' -e 's/\([a-zA-Z]\):/\/\1/g')

TUTORIAL_CXX_FLAGS ?= -std=c++11 -g -fno-omit-frame-pointer -fno-rtti -I $(ROOT_DIR)/tools
# The tutorials contain example code with warnings that we don't want
# to be flagged as errors, so the test flags are the tutorial flags
# plus our warning flags.
TEST_CXX_FLAGS ?= $(TUTORIAL_CXX_FLAGS) $(CXX_WARNING_FLAGS)
TEST_LD_FLAGS = -L$(BIN_DIR) -lHalide -lpthread $(LIBDL) -lz

# gcc 4.8 fires a bogus warning on old versions of png.h
CXX_VERSION = $(shell $(CXX) --version | head -n1)
ifneq (,$(findstring g++,$(CXX_VERSION)))
ifneq (,$(findstring 4.8,$(CXX_VERSION)))
TEST_CXX_FLAGS += -Wno-literal-suffix
endif
endif

ifeq ($(UNAME), Linux)
TEST_LD_FLAGS += -rdynamic -Wl,--rpath=$(CURDIR)/$(BIN_DIR)
endif

ifneq ($(WITH_PTX), )
ifneq (,$(findstring ptx,$(HL_TARGET)))
TEST_CUDA = 1
endif
ifneq (,$(findstring cuda,$(HL_TARGET)))
TEST_CUDA = 1
endif
endif

ifneq ($(WITH_OPENCL), )
ifneq (,$(findstring opencl,$(HL_TARGET)))
TEST_OPENCL = 1
endif
endif

ifneq ($(WITH_METAL), )
ifneq (,$(findstring metal,$(HL_TARGET)))
TEST_METAL = 1
endif
endif

ifeq ($(UNAME), Linux)
ifneq ($(TEST_CUDA), )
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
ifneq ($(TEST_CUDA), )
CUDA_LD_FLAGS ?= -L/usr/local/cuda/lib -lcuda
endif
ifneq ($(TEST_OPENCL), )
OPENCL_LD_FLAGS ?= -framework OpenCL
endif
ifneq ($(TEST_METAL), )
METAL_LD_FLAGS ?= -framework Metal -framework Foundation
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

ifneq ($(TEST_CUDA), )
TEST_CXX_FLAGS += -DTEST_CUDA
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

LIBJPEG_LIBS ?= -ljpeg

# There's no libjpeg-config, unfortunately. We should look for
# jpeglib.h one directory level up from png.h
LIBPNG_INCLUDE_DIRS = $(filter -I%,$(LIBPNG_CXX_FLAGS))
LIBJPEG_CXX_FLAGS ?= $(LIBPNG_INCLUDE_DIRS:=/..)

IMAGE_IO_LIBS = $(LIBPNG_LIBS) $(LIBJPEG_LIBS)
IMAGE_IO_CXX_FLAGS = $(LIBPNG_CXX_FLAGS) $(LIBJPEG_CXX_FLAGS)

# We're building into the current directory $(CURDIR). Find the Halide
# repo root directory (the location of the makefile)
THIS_MAKEFILE = $(realpath $(filter %Makefile, $(MAKEFILE_LIST)))
ROOT_DIR = $(strip $(shell dirname $(THIS_MAKEFILE)))
SRC_DIR  = $(ROOT_DIR)/src

TARGET=$(if $(HL_TARGET),$(HL_TARGET),host)

# The following directories are all relative to the output directory (i.e. $(CURDIR), not $(SRC_DIR))
LIB_DIR     = lib
BIN_DIR     = bin
DISTRIB_DIR = distrib
INCLUDE_DIR = include
DOC_DIR     = doc
BUILD_DIR   = $(BIN_DIR)/build
FILTERS_DIR = $(BIN_DIR)/$(TARGET)/build
TMP_DIR     = $(BUILD_DIR)/tmp

SOURCE_FILES = \
  AddImageChecks.cpp \
  AddParameterChecks.cpp \
  AlignLoads.cpp \
  AllocationBoundsInference.cpp \
  ApplySplit.cpp \
  AssociativeOpsTable.cpp \
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
  CodeGen_Posix.cpp \
  CodeGen_PowerPC.cpp \
  CodeGen_PTX_Dev.cpp \
  CodeGen_X86.cpp \
  CPlusPlusMangle.cpp \
  CSE.cpp \
  CanonicalizeGPUVars.cpp \
  Debug.cpp \
  DebugArguments.cpp \
  DebugToFile.cpp \
  Definition.cpp \
  Deinterleave.cpp \
  DeviceArgument.cpp \
  DeviceInterface.cpp \
  EarlyFree.cpp \
  Elf.cpp \
  EliminateBoolVectors.cpp \
  Error.cpp \
  FastIntegerDivide.cpp \
  FindCalls.cpp \
  Float16.cpp \
  Func.cpp \
  Function.cpp \
  FuseGPUThreadLoops.cpp \
  FuzzFloatStores.cpp \
  Generator.cpp \
  HexagonOffload.cpp \
  HexagonOptimize.cpp \
  ImageParam.cpp \
  InferArguments.cpp \
  InjectHostDevBufferCopies.cpp \
  InjectOpenGLIntrinsics.cpp \
  Inline.cpp \
  InlineReductions.cpp \
  IntegerDivisionTable.cpp \
  Interval.cpp \
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
  Prefetch.cpp \
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
  ScheduleParam.cpp \
  SelectGPUAPI.cpp \
  Simplify.cpp \
  SimplifySpecializations.cpp \
  SkipStages.cpp \
  SlidingWindow.cpp \
  Solve.cpp \
  SplitTuples.cpp \
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
  UnpackBuffers.cpp \
  UnrollLoops.cpp \
  Util.cpp \
  Var.cpp \
  VaryingAttributes.cpp \
  VectorizeLoops.cpp \
  WrapCalls.cpp \
  WrapExternStages.cpp

# The externally-visible header files that go into making Halide.h. Don't include anything here that includes llvm headers.
HEADER_FILES = \
  AddImageChecks.h \
  AddParameterChecks.h \
  AlignLoads.h \
  AllocationBoundsInference.h \
  ApplySplit.h \
  Argument.h \
  AssociativeOpsTable.h \
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
  CodeGen_Posix.h \
  CodeGen_PowerPC.h \
  CodeGen_PTX_Dev.h \
  CodeGen_X86.h \
  ConciseCasts.h \
  CPlusPlusMangle.h \
  CSE.h \
  CanonicalizeGPUVars.h \
  Debug.h \
  DebugArguments.h \
  DebugToFile.h \
  Definition.h \
  Deinterleave.h \
  DeviceArgument.h \
  DeviceInterface.h \
  EarlyFree.h \
  Elf.h \
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
  FunctionPtr.h \
  FuseGPUThreadLoops.h \
  FuzzFloatStores.h \
  Generator.h \
  HexagonOffload.h \
  HexagonOptimize.h \
  runtime/HalideRuntime.h \
  runtime/HalideBuffer.h \
  ImageParam.h \
  InferArguments.h \
  InjectHostDevBufferCopies.h \
  InjectOpenGLIntrinsics.h \
  Inline.h \
  InlineReductions.h \
  IntegerDivisionTable.h \
  Interval.h \
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
  Prefetch.h \
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
  ScheduleParam.h \
  Scope.h \
  SelectGPUAPI.h \
  Simplify.h \
  SimplifySpecializations.h \
  SkipStages.h \
  SlidingWindow.h \
  Solve.h \
  SplitTuples.h \
  StmtToHtml.h \
  StorageFlattening.h \
  StorageFolding.h \
  Substitute.h \
  Target.h \
  ThreadPool.h \
  Tracing.h \
  TrimNoOps.h \
  Tuple.h \
  Type.h \
  UnifyDuplicateLets.h \
  UniquifyVariableNames.h \
  UnpackBuffers.h \
  UnrollLoops.h \
  Util.h \
  Var.h \
  VaryingAttributes.h \
  VectorizeLoops.h \
  WrapCalls.h \
  WrapExternStages.h

OBJECTS = $(SOURCE_FILES:%.cpp=$(BUILD_DIR)/%.o)
HEADERS = $(HEADER_FILES:%.h=$(SRC_DIR)/%.h)

RUNTIME_CPP_COMPONENTS = \
  aarch64_cpu_features \
  android_clock \
  android_host_cpu_count \
  android_io \
  android_opengl_context \
  android_tempfile \
  arm_cpu_features \
  buffer_t \
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
  msan \
  msan_stubs \
  old_buffer_t \
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
  prefetch \
  profiler \
  profiler_inlined \
  qurt_allocator \
  qurt_hvx \
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
  posix_math \
  powerpc \
  ptx_dev \
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
                            $(INCLUDE_DIR)/HalideBuffer.h

INITIAL_MODULES = $(RUNTIME_CPP_COMPONENTS:%=$(BUILD_DIR)/initmod.%_32.o) \
                  $(RUNTIME_CPP_COMPONENTS:%=$(BUILD_DIR)/initmod.%_64.o) \
                  $(RUNTIME_CPP_COMPONENTS:%=$(BUILD_DIR)/initmod.%_32_debug.o) \
                  $(RUNTIME_CPP_COMPONENTS:%=$(BUILD_DIR)/initmod.%_64_debug.o) \
                  $(RUNTIME_EXPORTED_INCLUDES:$(INCLUDE_DIR)/%.h=$(BUILD_DIR)/initmod.%_h.o) \
                  $(BUILD_DIR)/initmod.inlined_c.o \
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

$(BUILD_DIR)/llvm_objects/list: $(OBJECTS) $(INITIAL_MODULES)
	# Determine the relevant object files from llvm with a dummy
	# compilation. Passing -t to the linker gets it to list which
	# object files in which archives it uses to resolve
	# symbols. We only care about the libLLVM ones.
	@mkdir -p $(@D)
	$(CXX) -o /dev/null -shared $(OBJECTS) $(INITIAL_MODULES) -Wl,-t $(LLVM_STATIC_LIBS) $(LIBDL) -lz -lpthread | egrep "libLLVM" > $(BUILD_DIR)/llvm_objects/list.new
	# if the list has changed since the previous build, or there
	# is no list from a previous build, then delete any old object
	# files and re-extract the required object files
	cd $(BUILD_DIR)/llvm_objects; \
	if cmp -s list.new list; \
	then \
	echo "No changes in LLVM deps"; \
	touch list; \
	else \
	rm -f llvm_*.o*; \
	cat list.new | sed = | sed "N;s/[()]/ /g;s/\n /\n/;s/\([0-9]*\)\n\([^ ]*\) \([^ ]*\)/ar x \2 \3; mv \3 llvm_\1_\3/" | bash -; \
	mv list.new list; \
	fi

$(LIB_DIR)/libHalide.a: $(OBJECTS) $(INITIAL_MODULES) $(BUILD_DIR)/llvm_objects/list
	# Archive together all the halide and llvm object files
	@mkdir -p $(@D)
	@rm -f $(LIB_DIR)/libHalide.a
	# ar breaks on MinGW with all objects at the same time.
	echo $(OBJECTS) $(INITIAL_MODULES) $(BUILD_DIR)/llvm_objects/llvm_*.o* | xargs -n200 ar q $(LIB_DIR)/libHalide.a
	ranlib $(LIB_DIR)/libHalide.a

$(BIN_DIR)/libHalide.$(SHARED_EXT): $(OBJECTS) $(INITIAL_MODULES)
	@mkdir -p $(@D)
	$(CXX) -shared $(OBJECTS) $(INITIAL_MODULES) $(LLVM_STATIC_LIBS) $(LLVM_LD_FLAGS) $(LIBDL) -lz -lpthread -o $(BIN_DIR)/libHalide.$(SHARED_EXT)
ifeq ($(UNAME), Darwin)
	install_name_tool -id $(CURDIR)/$(BIN_DIR)/libHalide.$(SHARED_EXT) $(BIN_DIR)/libHalide.$(SHARED_EXT)
endif

$(INCLUDE_DIR)/Halide.h: $(HEADERS) $(SRC_DIR)/HalideFooter.h $(BIN_DIR)/build_halide_h
	@mkdir -p $(@D)
	$(BIN_DIR)/build_halide_h $(HEADERS) $(SRC_DIR)/HalideFooter.h > $(INCLUDE_DIR)/Halide.h

$(INCLUDE_DIR)/HalideRuntime%: $(SRC_DIR)/runtime/HalideRuntime%
	echo Copying $<
	@mkdir -p $(@D)
	cp $< $(INCLUDE_DIR)/

$(INCLUDE_DIR)/HalideBuffer.h: $(SRC_DIR)/runtime/HalideBuffer.h
	echo Copying $<
	@mkdir -p $(@D)
	cp $< $(INCLUDE_DIR)/

$(BIN_DIR)/build_halide_h: $(ROOT_DIR)/tools/build_halide_h.cpp
	$(CXX) $< -o $@

-include $(OBJECTS:.o=.d)
-include $(INITIAL_MODULES:.o=.d)

# Compile generic 32- or 64-bit code
# (The 'nacl' is a red herring. This is just a generic 32-bit little-endian target.)
RUNTIME_TRIPLE_32 = "le32-unknown-nacl-unknown"
RUNTIME_TRIPLE_64 = "le64-unknown-unknown-unknown"

# win32 is tied to x86 due to the use of the __stdcall calling convention
RUNTIME_TRIPLE_WIN_32 = "i386-unknown-unknown-unknown"

RUNTIME_CXX_FLAGS = -O3 -fno-vectorize -ffreestanding -fno-blocks -fno-exceptions -fno-unwind-tables
$(BUILD_DIR)/initmod.%_64.ll: $(SRC_DIR)/runtime/%.cpp $(BUILD_DIR)/clang_ok
	@mkdir -p $(@D)
	$(CLANG) $(CXX_WARNING_FLAGS) $(RUNTIME_CXX_FLAGS) -m64 -target $(RUNTIME_TRIPLE_64) -DCOMPILING_HALIDE_RUNTIME -DBITS_64 -emit-llvm -S $(SRC_DIR)/runtime/$*.cpp -o $@ -MMD -MP -MF $(BUILD_DIR)/initmod.$*_64.d

$(BUILD_DIR)/initmod.windows_%_32.ll: $(SRC_DIR)/runtime/windows_%.cpp $(BUILD_DIR)/clang_ok
	@mkdir -p $(@D)
	$(CLANG) $(CXX_WARNING_FLAGS) $(RUNTIME_CXX_FLAGS) -m32 -target $(RUNTIME_TRIPLE_WIN_32) -DCOMPILING_HALIDE_RUNTIME -DBITS_32 -emit-llvm -S $(SRC_DIR)/runtime/windows_$*.cpp -o $@ -MMD -MP -MF $(BUILD_DIR)/initmod.windows_$*_32.d

$(BUILD_DIR)/initmod.%_32.ll: $(SRC_DIR)/runtime/%.cpp $(BUILD_DIR)/clang_ok
	@mkdir -p $(@D)
	$(CLANG) $(CXX_WARNING_FLAGS) $(RUNTIME_CXX_FLAGS) -m32 -target $(RUNTIME_TRIPLE_32) -DCOMPILING_HALIDE_RUNTIME -DBITS_32 -emit-llvm -S $(SRC_DIR)/runtime/$*.cpp -o $@ -MMD -MP -MF $(BUILD_DIR)/initmod.$*_32.d

$(BUILD_DIR)/initmod.%_64_debug.ll: $(SRC_DIR)/runtime/%.cpp $(BUILD_DIR)/clang_ok
	@mkdir -p $(@D)
	$(CLANG) $(CXX_WARNING_FLAGS) -g -DDEBUG_RUNTIME $(RUNTIME_CXX_FLAGS) -m64 -target  $(RUNTIME_TRIPLE_64) -DCOMPILING_HALIDE_RUNTIME -DBITS_64 -emit-llvm -S $(SRC_DIR)/runtime/$*.cpp -o $@ -MMD -MP -MF $(BUILD_DIR)/initmod.$*_64_debug.d

$(BUILD_DIR)/initmod.windows_%_32_debug.ll: $(SRC_DIR)/runtime/windows_%.cpp $(BUILD_DIR)/clang_ok
	@mkdir -p $(@D)
	$(CLANG) $(CXX_WARNING_FLAGS) -g -DDEBUG_RUNTIME $(RUNTIME_CXX_FLAGS) -m32 -target $(RUNTIME_TRIPLE_WIN_32) -DCOMPILING_HALIDE_RUNTIME -DBITS_32 -emit-llvm -S $(SRC_DIR)/runtime/windows_$*.cpp -o $@ -MMD -MP -MF $(BUILD_DIR)/initmod.windows_$*_32_debug.d

$(BUILD_DIR)/initmod.%_32_debug.ll: $(SRC_DIR)/runtime/%.cpp $(BUILD_DIR)/clang_ok
	@mkdir -p $(@D)
	$(CLANG) $(CXX_WARNING_FLAGS) -g -DDEBUG_RUNTIME -O3 $(RUNTIME_CXX_FLAGS) -m32 -target $(RUNTIME_TRIPLE_32) -DCOMPILING_HALIDE_RUNTIME -DBITS_32 -emit-llvm -S $(SRC_DIR)/runtime/$*.cpp -o $@ -MMD -MP -MF $(BUILD_DIR)/initmod.$*_32_debug.d

$(BUILD_DIR)/initmod.%_ll.ll: $(SRC_DIR)/runtime/%.ll
	@mkdir -p $(@D)
	cp $(SRC_DIR)/runtime/$*.ll $(BUILD_DIR)/initmod.$*_ll.ll

$(BUILD_DIR)/initmod.%.bc: $(BUILD_DIR)/initmod.%.ll $(BUILD_DIR)/llvm_ok
	$(LLVM_AS) $(BUILD_DIR)/initmod.$*.ll -o $(BUILD_DIR)/initmod.$*.bc

$(BUILD_DIR)/initmod.%.cpp: $(BIN_DIR)/binary2cpp $(BUILD_DIR)/initmod.%.bc
	./$(BIN_DIR)/binary2cpp halide_internal_initmod_$* < $(BUILD_DIR)/initmod.$*.bc > $@

$(BUILD_DIR)/initmod.%_h.cpp: $(BIN_DIR)/binary2cpp $(SRC_DIR)/runtime/%.h
	./$(BIN_DIR)/binary2cpp halide_internal_runtime_header_$*_h < $(SRC_DIR)/runtime/$*.h > $@

# Any c in the runtime that must be inlined needs to be copy-pasted into the output for the C backend.
$(BUILD_DIR)/initmod.inlined_c.cpp: $(BIN_DIR)/binary2cpp $(SRC_DIR)/runtime/buffer_t.cpp
	./$(BIN_DIR)/binary2cpp halide_internal_initmod_inlined_c < $(SRC_DIR)/runtime/buffer_t.cpp > $@

$(BUILD_DIR)/initmod_ptx.%_ll.cpp: $(BIN_DIR)/binary2cpp $(SRC_DIR)/runtime/nvidia_libdevice_bitcode/libdevice.%.bc
	./$(BIN_DIR)/binary2cpp halide_internal_initmod_ptx_$(basename $*)_ll < $(SRC_DIR)/runtime/nvidia_libdevice_bitcode/libdevice.$*.bc > $@

$(BIN_DIR)/binary2cpp: $(ROOT_DIR)/tools/binary2cpp.cpp
	@mkdir -p $(@D)
	$(CXX) $< -o $@

$(BUILD_DIR)/initmod_ptx.%_ll.o: $(BUILD_DIR)/initmod_ptx.%_ll.cpp
	$(CXX) -c $< -o $@ -MMD -MP -MF $(BUILD_DIR)/$*.d -MT $(BUILD_DIR)/$*.o

$(BUILD_DIR)/initmod.%.o: $(BUILD_DIR)/initmod.%.cpp
	$(CXX) -c $< -o $@ -MMD -MP -MF $(BUILD_DIR)/$*.d -MT $(BUILD_DIR)/$*.o

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp $(SRC_DIR)/%.h $(BUILD_DIR)/llvm_ok
	@mkdir -p $(@D)
	$(CXX) $(CXX_FLAGS) -c $< -o $@ -MMD -MP -MF $(BUILD_DIR)/$*.d -MT $(BUILD_DIR)/$*.o

.PHONY: clean
clean:
	rm -rf $(LIB_DIR)
	rm -rf $(BIN_DIR)
	rm -rf $(BUILD_DIR)
	rm -rf $(TMP_DIR)
	rm -rf $(FILTERS_DIR)
	rm -rf $(INCLUDE_DIR)
	rm -rf $(DOC_DIR)
	rm -rf $(DISTRIB_DIR)

.SECONDARY:

CORRECTNESS_TESTS = $(shell ls $(ROOT_DIR)/test/correctness/*.cpp) $(shell ls $(ROOT_DIR)/test/correctness/*.c)
PERFORMANCE_TESTS = $(shell ls $(ROOT_DIR)/test/performance/*.cpp)
ERROR_TESTS = $(shell ls $(ROOT_DIR)/test/error/*.cpp)
WARNING_TESTS = $(shell ls $(ROOT_DIR)/test/warning/*.cpp)
OPENGL_TESTS := $(shell ls $(ROOT_DIR)/test/opengl/*.cpp)
GENERATOR_EXTERNAL_TESTS := $(shell ls $(ROOT_DIR)/test/generator/*test.cpp)
GENERATOR_EXTERNAL_TEST_GENERATORS := $(shell ls $(ROOT_DIR)/test/generator/*_generator.cpp)
TUTORIALS = $(filter-out %_generate.cpp, $(shell ls $(ROOT_DIR)/tutorial/*.cpp))

-include $(OPENGL_TESTS:$(ROOT_DIR)/test/opengl/%.cpp=$(BUILD_DIR)/test_opengl_%.d)

test_correctness: $(CORRECTNESS_TESTS:$(ROOT_DIR)/test/correctness/%.cpp=correctness_%) $(CORRECTNESS_TESTS:$(ROOT_DIR)/test/correctness/%.c=correctness_%)
test_performance: $(PERFORMANCE_TESTS:$(ROOT_DIR)/test/performance/%.cpp=performance_%)
test_errors: $(ERROR_TESTS:$(ROOT_DIR)/test/error/%.cpp=error_%)
test_warnings: $(WARNING_TESTS:$(ROOT_DIR)/test/warning/%.cpp=warning_%)
test_tutorials: $(TUTORIALS:$(ROOT_DIR)/tutorial/%.cpp=tutorial_%)
test_valgrind: $(CORRECTNESS_TESTS:$(ROOT_DIR)/test/correctness/%.cpp=valgrind_%)
test_avx512: $(CORRECTNESS_TESTS:$(ROOT_DIR)/test/correctness/%.cpp=avx512_%)
test_opengl: $(OPENGL_TESTS:$(ROOT_DIR)/test/opengl/%.cpp=opengl_%)

# There are three types of tests for generators:
# 1) Externally-written aot-based tests
# 1) Externally-written aot-based tests (compiled using C++ backend)
# 2) Externally-written JIT-based tests
GENERATOR_AOT_TESTS = $(GENERATOR_EXTERNAL_TESTS:$(ROOT_DIR)/test/generator/%_aottest.cpp=generator_aot_%)
GENERATOR_AOTCPP_TESTS = $(GENERATOR_EXTERNAL_TESTS:$(ROOT_DIR)/test/generator/%_aottest.cpp=generator_aotcpp_%)
GENERATOR_JIT_TESTS = $(GENERATOR_EXTERNAL_TESTS:$(ROOT_DIR)/test/generator/%_jittest.cpp=generator_jit_%)

# multitarget test doesn't make any sense for the CPP backend; just skip it.
GENERATOR_AOTCPP_TESTS := $(filter-out generator_aotcpp_multitarget,$(GENERATOR_AOTCPP_TESTS))

# Note that many of the AOT-CPP tests are broken right now;
# remove AOT-CPP tests that don't (yet) work for C++ backend
# (each tagged with the *known* blocking issue(s))

# https://github.com/halide/Halide/issues/2084 (only if opencl enabled)
GENERATOR_AOTCPP_TESTS := $(filter-out generator_aotcpp_acquire_release,$(GENERATOR_AOTCPP_TESTS))

# https://github.com/halide/Halide/issues/2084 (only if opencl enabled)
GENERATOR_AOTCPP_TESTS := $(filter-out generator_aotcpp_define_extern_opencl,$(GENERATOR_AOTCPP_TESTS))

# https://github.com/halide/Halide/issues/2084 (only if opencl enabled)
GENERATOR_AOTCPP_TESTS := $(filter-out generator_aotcpp_gpu_object_lifetime,$(GENERATOR_AOTCPP_TESTS))

# https://github.com/halide/Halide/issues/2084 (only if opencl enabled)
GENERATOR_AOTCPP_TESTS := $(filter-out generator_aotcpp_gpu_only,$(GENERATOR_AOTCPP_TESTS))

# https://github.com/halide/Halide/issues/2084 (only if opencl enabled))
GENERATOR_AOTCPP_TESTS := $(filter-out generator_aotcpp_cleanup_on_error,$(GENERATOR_AOTCPP_TESTS))

# https://github.com/halide/Halide/issues/2084 (only if opencl enabled)
GENERATOR_AOTCPP_TESTS := $(filter-out generator_aotcpp_old_buffer_t,$(GENERATOR_AOTCPP_TESTS))

# https://github.com/halide/Halide/issues/2071
GENERATOR_AOTCPP_TESTS := $(filter-out generator_aotcpp_user_context,$(GENERATOR_AOTCPP_TESTS))

# https://github.com/halide/Halide/issues/2071
GENERATOR_AOTCPP_TESTS := $(filter-out generator_aotcpp_argvcall,$(GENERATOR_AOTCPP_TESTS))

# https://github.com/halide/Halide/issues/2071
GENERATOR_AOTCPP_TESTS := $(filter-out generator_aotcpp_metadata_tester,$(GENERATOR_AOTCPP_TESTS))

# https://github.com/halide/Halide/issues/2071
GENERATOR_AOTCPP_TESTS := $(filter-out generator_aotcpp_cxx_mangling,$(GENERATOR_AOTCPP_TESTS))

# https://github.com/halide/Halide/issues/2075
GENERATOR_AOTCPP_TESTS := $(filter-out generator_aotcpp_msan,$(GENERATOR_AOTCPP_TESTS))

# https://github.com/halide/Halide/issues/2075
GENERATOR_AOTCPP_TESTS := $(filter-out generator_aotcpp_memory_profiler_mandelbrot,$(GENERATOR_AOTCPP_TESTS))

# https://github.com/halide/Halide/issues/2082
GENERATOR_AOTCPP_TESTS := $(filter-out generator_aotcpp_matlab,$(GENERATOR_AOTCPP_TESTS))

# This is just a test to ensure than RunGen builds and links for "normal" Generators;
# not all will work directly (e.g. due to unusual runtime requirements), so blacklist them to
# simplify the world.
GENERATOR_BUILD_RUNGEN_TESTS = $(GENERATOR_EXTERNAL_TEST_GENERATORS:$(ROOT_DIR)/test/generator/%_generator.cpp=$(BIN_DIR)/%.rungen)
# TODO: define_extern_opencl should be able to work with a little effort to move the define_extern
# into a separate file.
GENERATOR_BUILD_RUNGEN_TESTS := $(filter-out $(BIN_DIR)/define_extern_opencl.rungen,$(GENERATOR_BUILD_RUNGEN_TESTS))
GENERATOR_BUILD_RUNGEN_TESTS := $(filter-out $(BIN_DIR)/matlab.rungen,$(GENERATOR_BUILD_RUNGEN_TESTS))
GENERATOR_BUILD_RUNGEN_TESTS := $(filter-out $(BIN_DIR)/msan.rungen,$(GENERATOR_BUILD_RUNGEN_TESTS))
GENERATOR_BUILD_RUNGEN_TESTS := $(filter-out $(BIN_DIR)/nested_externs.rungen,$(GENERATOR_BUILD_RUNGEN_TESTS))
GENERATOR_BUILD_RUNGEN_TESTS := $(filter-out $(BIN_DIR)/old_buffer_t.rungen,$(GENERATOR_BUILD_RUNGEN_TESTS))

test_generators_aot: $(GENERATOR_AOT_TESTS)
test_generators_jit: $(GENERATOR_JIT_TESTS)
test_generators_aotcpp: $(GENERATOR_AOTCPP_TESTS)
test_rungen: $(GENERATOR_BUILD_RUNGEN_TESTS)

test_generators: test_generators_aot test_generators_jit test_rungen

ALL_TESTS = test_internal test_correctness test_errors test_tutorials test_warnings test_generators

# These targets perform timings of each test. For most tests this includes Halide JIT compile times, and run times.
# For generator tests they time the compile time only. The times are recorded in CSV files.
time_compilation_correctness: init_time_compilation_correctness $(CORRECTNESS_TESTS:$(ROOT_DIR)/test/correctness/%.cpp=time_compilation_test_%)
time_compilation_performance: init_time_compilation_performance $(PERFORMANCE_TESTS:$(ROOT_DIR)/test/performance/%.cpp=time_compilation_performance_%)
time_compilation_opengl: init_time_compilation_opengl $(OPENGL_TESTS:$(ROOT_DIR)/test/opengl/%.cpp=time_compilation_opengl_%)
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
	$(GENERATOR_EXTERNAL_TESTS:$(ROOT_DIR)/test/generator/%_aottest.cpp=$(BIN_DIR)/$(TARGET)/generator_aot_%) \
	$(GENERATOR_EXTERNAL_TESTS:$(ROOT_DIR)/test/generator/%_jittest.cpp=$(BIN_DIR)/generator_jit_%)

time_compilation_tests: time_compilation_correctness time_compilation_performance time_compilation_generators

$(BIN_DIR)/test_internal: $(ROOT_DIR)/test/internal.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT)
	@mkdir -p $(@D)
	$(CXX) $(TEST_CXX_FLAGS) $< -I$(SRC_DIR) $(TEST_LD_FLAGS) -o $@

# Correctness test that link against libHalide
$(BIN_DIR)/correctness_%: $(ROOT_DIR)/test/correctness/%.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h $(RUNTIME_EXPORTED_INCLUDES)
	@mkdir -p $(@D)
	$(CXX) $(TEST_CXX_FLAGS) -I$(ROOT_DIR) $(OPTIMIZE) $< -I$(INCLUDE_DIR) $(TEST_LD_FLAGS) -o $@

# Correctness tests that do NOT link against libHalide
$(BIN_DIR)/correctness_plain_c_includes: $(ROOT_DIR)/test/correctness/plain_c_includes.c $(RUNTIME_EXPORTED_INCLUDES)
	$(CXX) -x c -Wall -Werror -I$(ROOT_DIR) $(OPTIMIZE) $< -I$(ROOT_DIR)/src/runtime -o $@

ifeq ($(UNAME), Darwin)
WEAK_BUFFER_LINKAGE_FLAGS=-Wl,-U,_halide_weak_device_free
else
ifneq (,$(findstring MINGW,$(UNAME)))
WEAK_BUFFER_LINKAGE_FLAGS=-Wl,--defsym=_halide_weak_device_free=0,--defsym=halide_device_free=0
else
WEAK_BUFFER_LINKAGE_FLAGS=
endif
endif

# Note that this test must *not* link in either libHalide, or a Halide runtime;
# this test should be usable without either. (Note that this requires an extra
# linker directive on Darwin to ensure halide_weak_device_free() is in fact weak.)
$(BIN_DIR)/correctness_halide_buffer: $(ROOT_DIR)/test/correctness/halide_buffer.cpp $(INCLUDE_DIR)/HalideBuffer.h $(RUNTIME_EXPORTED_INCLUDES)
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE) $< -I$(INCLUDE_DIR) $(WEAK_BUFFER_LINKAGE_FLAGS) -o $@

# The image_io test additionally needs to link to libpng and
# libjpeg.
$(BIN_DIR)/correctness_image_io: $(ROOT_DIR)/test/correctness/image_io.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h $(RUNTIME_EXPORTED_INCLUDES)
	$(CXX) $(TEST_CXX_FLAGS) $(IMAGE_IO_CXX_FLAGS) -I$(ROOT_DIR) $(OPTIMIZE) $< -I$(INCLUDE_DIR) $(TEST_LD_FLAGS) $(IMAGE_IO_LIBS) -o $@

$(BIN_DIR)/performance_%: $(ROOT_DIR)/test/performance/%.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE) $< -I$(INCLUDE_DIR) $(TEST_LD_FLAGS) -o $@

# Error tests that link against libHalide
$(BIN_DIR)/error_%: $(ROOT_DIR)/test/error/%.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h
	$(CXX) $(TEST_CXX_FLAGS) -I$(ROOT_DIR) $(OPTIMIZE) $< -I$(INCLUDE_DIR) $(TEST_LD_FLAGS) -o $@

$(BIN_DIR)/warning_%: $(ROOT_DIR)/test/warning/%.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE) $< -I$(INCLUDE_DIR) $(TEST_LD_FLAGS) -o $@

$(BIN_DIR)/opengl_%: $(ROOT_DIR)/test/opengl/%.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE) $< -I$(INCLUDE_DIR) -I$(SRC_DIR) $(TEST_LD_FLAGS) $(OPENGL_LD_FLAGS) -o $@ -MMD -MF $(BUILD_DIR)/test_opengl_$*.d

# ------------------------------------------------------------------------------
# Use vpath to specify which directories we want to look in for Generator sources.
vpath %_generator.cpp $(ROOT_DIR)/test/generator/

GENERATOR_HALIDE_INCLUDES_DIR = $(INCLUDE_DIR)
GENERATOR_HALIDE_TOOLS_DIR = $(ROOT_DIR)/tools
GENERATOR_LIBHALIDE_PATH = $(BIN_DIR)/libHalide.$(SHARED_EXT)

GENERATOR_BIN_DIR = $(BIN_DIR)
GENERATOR_TARGET = $(TARGET)

GENERATOR_CXX_FLAGS = $(TEST_CXX_FLAGS)

GENERATOR_GENERATOR_LD_FLAGS = $(TEST_LD_FLAGS)

GENERATOR_IMAGE_IO_LIBS      ?= $(IMAGE_IO_LIBS)
GENERATOR_IMAGE_IO_CXX_FLAGS ?= $(IMAGE_IO_CXX_FLAGS)

include HalideGenerator.mk

# ------------------------------------------------------------------------------

# Customizations for the actual Generators we have in test/generator:

# Build _externs.cpp into _externs.o by default.
$(GENERATOR_FILTERS_DIR)/%_externs.o: $(ROOT_DIR)/test/generator/%_externs.cpp
	@mkdir -p $(@D)
	$(CXX) $(TEST_CXX_FLAGS) -c $< -I$(INCLUDE_DIR) -I$(GENERATOR_FILTERS_DIR) -o $@

$(FILTERS_DIR)/cxx_mangling.a: GENERATOR_EXTRA_FEATURES=c_plus_plus_name_mangling
$(FILTERS_DIR)/cxx_mangling.a: GENERATOR_FUNCNAME=HalideTest::AnotherNamespace::cxx_mangling
$(FILTERS_DIR)/cxx_mangling.a: GENERATOR_FILTER_DEPS=$(FILTERS_DIR)/cxx_mangling_externs.o

# Also build with a gpu target to ensure that the GPU-Host generation
# code handles name mangling properly. (Note that we don't need to
# run this code, just check for link errors.)
$(FILTERS_DIR)/cxx_mangling_gpu.a: GENERATOR_GENERATOR_EXECUTABLE=$(BIN_DIR)/cxx_mangling.generator
$(FILTERS_DIR)/cxx_mangling_gpu.a: GENERATOR_FUNCNAME=HalideTest::cxx_mangling_gpu
$(FILTERS_DIR)/cxx_mangling_gpu.a: GENERATOR_EXTRA_FEATURES=c_plus_plus_name_mangling-cuda

$(FILTERS_DIR)/cxx_mangling_define_extern.a: GENERATOR_EXTRA_FEATURES=c_plus_plus_name_mangling-user_context
$(FILTERS_DIR)/cxx_mangling_define_extern.a: GENERATOR_FUNCNAME=HalideTest::cxx_mangling_define_extern
$(FILTERS_DIR)/cxx_mangling_define_extern.a: GENERATOR_FILTER_DEPS=$(FILTERS_DIR)/cxx_mangling_define_extern_externs.o $(FILTERS_DIR)/cxx_mangling.a

$(FILTERS_DIR)/cxx_mangling_define_extern_externs.o: $(FILTERS_DIR)/cxx_mangling.h

$(FILTERS_DIR)/matlab.a: GENERATOR_EXTRA_FEATURES=matlab

METADATA_TESTER_GENERATOR_ARGS=\
	input.type=uint8 input.dim=3 \
	type_only_input_buffer.dim=3 \
	dim_only_input_buffer.type=uint8 \
	untyped_input_buffer.type=uint8 untyped_input_buffer.dim=3 \
	output.type=float32,float32 output.dim=3 \
	input_not_nod.type=uint8 input_not_nod.dim=3 \
	input_nod.dim=3 \
	input_not.type=uint8 \
	array_input.size=2 \
	array_i8.size=2 \
	array_i16.size=2 \
	array_i32.size=2 \
	array_h.size=2 \
	array_outputs.size=2

# metadata_tester is built with and without user-context
$(FILTERS_DIR)/metadata_tester.a: GENERATOR_ARGS=$(METADATA_TESTER_GENERATOR_ARGS)

# metadata_tester_ucon uses the same Generator as metadata_tester, with different target features
$(FILTERS_DIR)/metadata_tester_ucon.a: GENERATOR_GENERATOR_EXECUTABLE=$(BIN_DIR)/metadata_tester.generator
$(FILTERS_DIR)/metadata_tester_ucon.a: GENERATOR_EXTRA_FEATURES=user_context
$(FILTERS_DIR)/metadata_tester_ucon.a: GENERATOR_ARGS=$(METADATA_TESTER_GENERATOR_ARGS)

$(FILTERS_DIR)/multitarget.a: GENERATOR_FUNCNAME=HalideTest::multitarget
$(FILTERS_DIR)/multitarget.a: GENERATOR_EXTRA_FEATURES=debug-c_plus_plus_name_mangling,$(TARGET)-no_runtime-c_plus_plus_name_mangling

$(FILTERS_DIR)/nested_externs_%.a: GENERATOR_GENERATOR_EXECUTABLE=$(BIN_DIR)/nested_externs.generator
$(FILTERS_DIR)/nested_externs_%.a: GENERATOR_GENERATOR_NAME=$*

$(FILTERS_DIR)/pyramid.a: GENERATOR_ARGS=levels=10

$(FILTERS_DIR)/msan.a: GENERATOR_TARGET_WITH_FEATURES=$(TARGET)-msan

$(FILTERS_DIR)/stubtest.a: GENERATOR_ARGS=$(STUBTEST_GENERATOR_ARGS)

# TODO: this is an ugly way to force the dep for stub.h, we'd prefer to dep on .generator vs _generator.o
$(BUILD_DIR)/stubuser_generator.o: $(FILTERS_DIR)/stubtest.stub.h
$(BIN_DIR)/stubuser.generator: $(BUILD_DIR)/stubtest_generator.o

$(FILTERS_DIR)/stubuser.a: GENERATOR_GENERATOR_NAME=stubuser

$(FILTERS_DIR)/tiled_blur.a: GENERATOR_FILTER_DEPS=$(FILTERS_DIR)/blur2x2.a
# When GENERATOR_FILTER_DEPS contains the output of another Generator,
# we must add an explicit dependency here to ensure it is built in the 
# correct order.
$(FILTERS_DIR)/tiled_blur.a: $(FILTERS_DIR)/blur2x2.a

$(FILTERS_DIR)/user_context.a: GENERATOR_EXTRA_FEATURES=user_context

$(FILTERS_DIR)/user_context_insanity.a: GENERATOR_EXTRA_FEATURES=user_context

# stubtest has input and output funcs with undefined types and array sizes; this is fine for stub
# usage (the types can be inferred), but for AOT compilation, we must make the types
# concrete via generator args. Also note that setting 'vectorize=true' is redundant (that's the default), 
# but verifies that setting ScheduleParam via generator_args works properly.
STUBTEST_GENERATOR_ARGS=\
    untyped_buffer_input.type=uint8 untyped_buffer_input.dim=3 \
	simple_input.type=float32 \
	array_input.type=float32 array_input.size=2 \
	int_arg.size=2 \
	tuple_output.type=float32,float32 \
	vectorize=true

# It is not always possible to cross compile between 32-bit and 64-bit via the clang build as part of llvm.
# These next two rules can fail the compilation and produce zero length bitcode blobs.
# If the zero length blob is actually used, the test will fail anyway, but usually only the bitness
# of the target is used.
$(BUILD_DIR)/external_code_extern_bitcode_32.cpp : $(ROOT_DIR)/test/generator/external_code_extern.cpp $(BIN_DIR)/binary2cpp
	$(CLANG) $(CXX_WARNING_FLAGS) -O3 -c -m32 -target $(RUNTIME_TRIPLE_32) -emit-llvm $< -o $(BUILD_DIR)/external_code_extern_32.bc || echo -n > $(BUILD_DIR)/external_code_extern_32.bc
	./$(BIN_DIR)/binary2cpp external_code_extern_bitcode_32 < $(BUILD_DIR)/external_code_extern_32.bc > $@

$(BUILD_DIR)/external_code_extern_bitcode_64.cpp : $(ROOT_DIR)/test/generator/external_code_extern.cpp $(BIN_DIR)/binary2cpp
	$(CLANG) $(CXX_WARNING_FLAGS) -O3 -c -m64 -target $(RUNTIME_TRIPLE_64) -emit-llvm $< -o $(BUILD_DIR)/external_code_extern_64.bc || echo -n > $(BUILD_DIR)/external_code_extern_64.bc
	./$(BIN_DIR)/binary2cpp external_code_extern_bitcode_64 < $(BUILD_DIR)/external_code_extern_64.bc > $@

$(BUILD_DIR)/external_code_extern_cpp_source.cpp : $(ROOT_DIR)/test/generator/external_code_extern.cpp $(BIN_DIR)/binary2cpp
	./$(BIN_DIR)/binary2cpp external_code_extern_cpp_source < $(ROOT_DIR)/test/generator/external_code_extern.cpp > $@

$(BIN_DIR)/external_code.generator: GENERATOR_GENERATOR_DEPS=\
	$(BUILD_DIR)/external_code_extern_bitcode_32.cpp \
	$(BUILD_DIR)/external_code_extern_bitcode_64.cpp \
	$(BUILD_DIR)/external_code_extern_cpp_source.cpp

$(FILTERS_DIR)/external_code.a: GENERATOR_ARGS="external_code_is_bitcode=true"
#$(FILTERS_DIR)/external_code.a: GENERATOR_FUNCNAME=external_code

# TODO -- doesn't really work
$(FILTERS_DIR)/external_code.cpp: GENERATOR_GENERATOR_EXECUTABLE=$(BIN_DIR)/external_code.generator
$(FILTERS_DIR)/external_code.cpp: GENERATOR_ARGS="external_code_is_bitcode=false"
$(FILTERS_DIR)/external_code.cpp: GENERATOR_FUNCNAME=external_code

# ------------------------------------------------------------------------------

# General rules for building Generator tests. These are targeted at the
# Generators in test/generator, but could (and should) be generalized elsewhere.

# Note that we need SECONDEXPANSION enabled several of these to work.
.SECONDEXPANSION:

GENERATOR_AOTTEST_CXX_FLAGS=$(TEST_CXX_FLAGS) -Wno-unknown-pragmas
GENERATOR_AOTTEST_INCLUDES=-I$(INCLUDE_DIR) -I$(FILTERS_DIR) -I$(ROOT_DIR) -I $(SRC_DIR)/runtime -I$(ROOT_DIR)/tools

# Runtime libraries to link in (if any)
GENERATOR_AOTTEST_RUNTIME_LIBS=$(GENERATOR_RUNTIME_LIB)

GENERATOR_AOTTEST_EXTRA_LD_FLAGS=

GENERATOR_AOTTEST_EXTRA_DEPS=
GENERATOR_AOTTEST_DEPS=$(FILTERS_DIR)/$*.a $(FILTERS_DIR)/$*.h $(GENERATOR_AOTTEST_EXTRA_DEPS)

GENERATOR_JITTEST_DEPS=$(FILTERS_DIR)/$*.stub.h $(BUILD_DIR)/$*_generator.o

GENERATOR_AOTTEST_LD_FLAGS=-lpthread $(LIBDL)
ifneq ($(TEST_METAL), )
# Unlike cuda and opencl, which dynamically go find the appropriate symbols, metal requires actual linking.
GENERATOR_AOTTEST_LD_FLAGS+=$(METAL_LD_FLAGS)
endif

# By default, %_aottest.cpp depends on $(FILTERS_DIR)/%.a/.h (but not libHalide).
$(BIN_DIR)/$(TARGET)/generator_aot_%: $(ROOT_DIR)/test/generator/%_aottest.cpp $(RUNTIME_EXPORTED_INCLUDES) $$(GENERATOR_AOTTEST_DEPS) $$(GENERATOR_AOTTEST_RUNTIME_LIBS)
	@mkdir -p $(BIN_DIR)/$(TARGET)
	$(CXX) $(GENERATOR_AOTTEST_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) $(GENERATOR_AOTTEST_INCLUDES) $(GENERATOR_AOTTEST_LD_FLAGS) $(GENERATOR_AOTTEST_EXTRA_LD_FLAGS) -o $@

# Also make AOT testing targets that depends on the .cpp output (rather than .a).
$(BIN_DIR)/$(TARGET)/generator_aotcpp_%: $(ROOT_DIR)/test/generator/%_aottest.cpp $(FILTERS_DIR)/%.cpp $(FILTERS_DIR)/%.h $(RUNTIME_EXPORTED_INCLUDES) $$(GENERATOR_AOTTEST_RUNTIME_LIBS)
	@mkdir -p $(BIN_DIR)/$(TARGET)
	$(CXX) $(GENERATOR_AOTTEST_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) $(GENERATOR_AOTTEST_INCLUDES) $(GENERATOR_AOTTEST_LD_FLAGS) $(GENERATOR_AOTTEST_EXTRA_LD_FLAGS) -o $@

# By default, %_jittest.cpp depends on libHalide, plus the stubs for the Generator. These are external tests that use the JIT.
$(BIN_DIR)/generator_jit_%: $(ROOT_DIR)/test/generator/%_jittest.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h $$(GENERATOR_JITTEST_DEPS)
	$(CXX) -g $(TEST_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) -I$(INCLUDE_DIR) -I$(FILTERS_DIR) -I $(ROOT_DIR)/apps/support $(TEST_LD_FLAGS) -o $@

# ------------------------------------------------------------------------------

# also depends on cxx_mangling_gpu
$(BIN_DIR)/$(TARGET)/generator_aot_cxx_mangling: GENERATOR_AOTTEST_EXTRA_DEPS=$(FILTERS_DIR)/cxx_mangling_gpu.a

# also depends on metadata_tester_ucon
$(BIN_DIR)/$(TARGET)/generator_aot_metadata_tester: GENERATOR_AOTTEST_EXTRA_DEPS=$(FILTERS_DIR)/metadata_tester_ucon.a

# MSAN test doesn't use the standard runtime
$(BIN_DIR)/$(TARGET)/generator_aot_msan: GENERATOR_AOTTEST_RUNTIME_LIBS=

# depends on non-obvious set of deps
$(BIN_DIR)/$(TARGET)/generator_aot_nested_externs: GENERATOR_AOTTEST_DEPS=\
	$(FILTERS_DIR)/nested_externs_combine.a \
	$(FILTERS_DIR)/nested_externs_inner.a \
	$(FILTERS_DIR)/nested_externs_leaf.a \
	$(FILTERS_DIR)/nested_externs_root.a

# The matlab tests needs "-matlab" in the runtime
$(BIN_DIR)/$(TARGET)/generator_aot_matlab: GENERATOR_AOTTEST_RUNTIME_LIBS=$(BIN_DIR)/$(TARGET)-matlab/build/runtime.a

$(BIN_DIR)/$(TARGET)/generator_aot_acquire_release: GENERATOR_AOTTEST_EXTRA_LD_FLAGS=$(OPENCL_LD_FLAGS) $(CUDA_LD_FLAGS)

$(BIN_DIR)/$(TARGET)/generator_aot_define_extern_opencl: GENERATOR_AOTTEST_EXTRA_LD_FLAGS=$(OPENCL_LD_FLAGS)

# TODO -- yuck
$(BIN_DIR)/$(TARGET)/generator_aotcpp_external_code: GENERATOR_AOTTEST_DEPS=$(FILTERS_DIR)/external_code_cpp.a

# generator_aot_multitarget is run multiple times, with different env vars.
generator_aot_multitarget: $(BIN_DIR)/$(TARGET)/generator_aot_multitarget
	@mkdir -p $(@D)
	HL_MULTITARGET_TEST_USE_DEBUG_FEATURE=0 $(CURDIR)/$<
	HL_MULTITARGET_TEST_USE_DEBUG_FEATURE=1 $(CURDIR)/$<
	@-echo


$(BIN_DIR)/tutorial_%: $(ROOT_DIR)/tutorial/%.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h
	@ if [[ $@ == *_run ]]; then \
		export TUTORIAL=$* ;\
		export LESSON=`echo $${TUTORIAL} | cut -b1-9`; \
		make -f $(THIS_MAKEFILE) tutorial_$${TUTORIAL/run/generate}; \
		$(CXX) $(TUTORIAL_CXX_FLAGS) $(IMAGE_IO_CXX_FLAGS) $(OPTIMIZE) $< \
		-I$(TMP_DIR) -I$(INCLUDE_DIR) $(TMP_DIR)/$${LESSON}_*.a $(GENERATOR_AOTTEST_LD_FLAGS) $(IMAGE_IO_LIBS) -lz -o $@; \
	else \
		$(CXX) $(TUTORIAL_CXX_FLAGS) $(IMAGE_IO_CXX_FLAGS) $(OPTIMIZE) $< \
		-I$(INCLUDE_DIR) -I$(ROOT_DIR)/tools $(TEST_LD_FLAGS) $(IMAGE_IO_LIBS) -o $@;\
	fi

$(BIN_DIR)/tutorial_lesson_15_generators: $(ROOT_DIR)/tutorial/lesson_15_generators.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h $(BUILD_DIR)/GenGen.o
	$(CXX) $(TUTORIAL_CXX_FLAGS) $(IMAGE_IO_CXX_FLAGS) $(OPTIMIZE) $< $(BUILD_DIR)/GenGen.o \
	-I$(INCLUDE_DIR) $(TEST_LD_FLAGS) $(IMAGE_IO_LIBS) -o $@

tutorial_lesson_15_generators: $(ROOT_DIR)/tutorial/lesson_15_generators_usage.sh $(BIN_DIR)/tutorial_lesson_15_generators
	@-mkdir -p $(TMP_DIR)
	cp $(BIN_DIR)/tutorial_lesson_15_generators $(TMP_DIR)/lesson_15_generate; \
	cd $(TMP_DIR); \
	PATH="$${PATH}:$(CURDIR)/$(BIN_DIR)" source $(ROOT_DIR)/tutorial/lesson_15_generators_usage.sh
	@-echo

$(BIN_DIR)/tutorial_lesson_16_rgb_generate: $(ROOT_DIR)/tutorial/lesson_16_rgb_generate.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h $(BUILD_DIR)/GenGen.o
	$(CXX) $(TUTORIAL_CXX_FLAGS) $(IMAGE_IO_CXX_FLAGS) $(OPTIMIZE) $< $(BUILD_DIR)/GenGen.o \
	-I$(INCLUDE_DIR) $(TEST_LD_FLAGS) $(IMAGE_IO_LIBS) -o $@

$(BIN_DIR)/tutorial_lesson_16_rgb_run: $(ROOT_DIR)/tutorial/lesson_16_rgb_run.cpp $(BIN_DIR)/tutorial_lesson_16_rgb_generate
	@-mkdir -p $(TMP_DIR)
	# Run the generator
	$(BIN_DIR)/tutorial_lesson_16_rgb_generate -o $(TMP_DIR) -f brighten_planar      target=host layout=planar
	$(BIN_DIR)/tutorial_lesson_16_rgb_generate -o $(TMP_DIR) -f brighten_interleaved target=host-no_runtime layout=interleaved
	$(BIN_DIR)/tutorial_lesson_16_rgb_generate -o $(TMP_DIR) -f brighten_either      target=host-no_runtime layout=either
	$(BIN_DIR)/tutorial_lesson_16_rgb_generate -o $(TMP_DIR) -f brighten_specialized target=host-no_runtime layout=specialized
	# Compile the runner
	$(CXX) $(TUTORIAL_CXX_FLAGS) $(IMAGE_IO_CXX_FLAGS) $(OPTIMIZE) $< \
	-I$(INCLUDE_DIR) -L$(BIN_DIR) -I $(TMP_DIR) $(TMP_DIR)/brighten_*.a \
        -lHalide $(TEST_LD_FLAGS) -lpthread $(LIBDL) $(IMAGE_IO_LIBS) -lz -o $@
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

# Use Intel SDE to emulate an avx 512 processor.
avx512_%: $(BIN_DIR)/correctness_%
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; sde -cnl -- $(CURDIR)/$<
	cd $(TMP_DIR) ; sde -knl -- $(CURDIR)/$<
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
	cd $(TMP_DIR) ; $(CURDIR)/$< 2>&1 | egrep --q "terminating with uncaught exception|^terminate called|^Error|Assertion.*failed"
	@-echo

warning_%: $(BIN_DIR)/warning_%
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; $(CURDIR)/$< 2>&1 | egrep --q "^Warning"
	@-echo

opengl_%: $(BIN_DIR)/opengl_%
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; $(CURDIR)/$< 2>&1
	@-echo

generator_jit_%: $(BIN_DIR)/generator_jit_%
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; $(CURDIR)/$<
	@-echo

generator_aot_%: $(BIN_DIR)/$(TARGET)/generator_aot_%
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; $(CURDIR)/$<
	@-echo

generator_aotcpp_%: $(BIN_DIR)/$(TARGET)/generator_aotcpp_%
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

time_compilation_generator_%: $(BIN_DIR)/%.generator
	$(TIME_COMPILATION) compile_times_generator.csv make -f $(THIS_MAKEFILE) $(@:time_compilation_generator_%=$(FILTERS_DIR)/%.a)

.PHONY: test_apps
test_apps: $(LIB_DIR)/libHalide.a $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h $(RUNTIME_EXPORTED_INCLUDES)
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
	        $(ROOT_DIR)/apps/HelloMatlab \
	        $(ROOT_DIR)/apps/fft \
	        $(ROOT_DIR)/apps/linear_algebra \
	        $(ROOT_DIR)/apps/images \
	        $(ROOT_DIR)/apps/support \
                apps; \
	  cp -r $(ROOT_DIR)/tools .; \
	fi
	make -C apps/bilateral_grid clean  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	make -C apps/bilateral_grid bin/out.png  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	make -C apps/local_laplacian clean  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	make -C apps/local_laplacian bin/out.png  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	make -C apps/interpolate clean  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	make -C apps/interpolate bin/out.png  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	make -C apps/blur clean  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	make -C apps/blur bin/test  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	apps/blur/bin/test
	make -C apps/wavelet clean  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	make -C apps/wavelet test  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	make -C apps/c_backend clean  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	make -C apps/c_backend test  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	make -C apps/fft bench_16x16  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	make -C apps/fft bench_32x32  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	make -C apps/fft bench_48x48  HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR)
	cd apps/HelloMatlab; HALIDE_PATH=$(CURDIR) HALIDE_CXX="$(CXX)" ./run_blur.sh
	# Only test the linear algebra app if cblas.h exists in the expected place
	if [ -f /usr/include/cblas.h ]; then \
	  make -C apps/linear_algebra clean HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR) ; \
	  make -C apps/linear_algebra test HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR) ; \
	fi

# Bazel depends on the distrib archive being built
.PHONY: test_bazel
test_bazel: $(DISTRIB_DIR)/halide.tgz
	# Only test bazeldemo if Bazel is installed
	if [ -z "$(BAZEL)" ]; then echo "Bazel is not installed"; exit 1; fi
	mkdir -p apps
	# Make a local copy of the apps if we're building out-of-tree,
	# because the app Makefiles are written to build in-tree
	if [ "$(ROOT_DIR)" != "$(CURDIR)" ]; then \
	  echo "Building out-of-tree, so making local copy of apps"; \
	  cp -r $(ROOT_DIR)/apps/bazeldemo apps; \
	  cp -r $(ROOT_DIR)/tools .; \
	fi
	cd apps/bazeldemo; \
	CXX=`echo ${CXX} | sed 's/ccache //'` \
	CC=`echo ${CC} | sed 's/ccache //'` \
	bazel build --verbose_failures :all 

.PHONY: test_python
test_python: $(LIB_DIR)/libHalide.a $(INCLUDE_DIR)/Halide.h
	mkdir -p python_bindings
	make -C python_bindings -f $(ROOT_DIR)/python_bindings/Makefile test

# It's just for compiling the runtime, so earlier clangs *might* work,
# but best to peg it to the minimum llvm version.
ifneq (,$(findstring clang version 3.7,$(CLANG_VERSION)))
CLANG_OK=yes
endif

ifneq (,$(findstring clang version 3.8,$(CLANG_VERSION)))
CLANG_OK=yes
endif

ifneq (,$(findstring clang version 3.9,$(CLANG_VERSION)))
CLANG_OK=yes
endif

ifneq (,$(findstring clang version 4.0,$(CLANG_VERSION)))
CLANG_OK=yes
endif

ifneq (,$(findstring clang version 5.0,$(CLANG_VERSION)))
CLANG_OK=yes
endif

ifneq (,$(findstring clang version 6.0,$(CLANG_VERSION)))
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
	@echo "Can't find clang or version of clang too old (we need 3.7 or greater):"
	@echo "You can override this check by setting CLANG_OK=y"
	echo '$(CLANG_VERSION)'
	echo $(findstring version 3,$(CLANG_VERSION))
	echo $(findstring version 3.0,$(CLANG_VERSION))
	$(CLANG) --version
	@exit 1
endif

ifneq (,$(findstring $(LLVM_VERSION_TIMES_10), 37 38 39 40 50 60))
LLVM_OK=yes
endif

ifneq ($(LLVM_OK), )
$(BUILD_DIR)/llvm_ok:
	@echo "Found a new enough version of llvm"
	mkdir -p $(BUILD_DIR)
	touch $(BUILD_DIR)/llvm_ok
else
$(BUILD_DIR)/llvm_ok:
	@echo "Can't find llvm or version of llvm too old (we need 3.7 or greater):"
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
	cp $(INCLUDE_DIR)/HalideBuffer.h $(PREFIX)/include
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
	cp $(ROOT_DIR)/tools/RunGen.cpp $(PREFIX)/share/halide/tools
	cp $(ROOT_DIR)/tools/RunGenStubs.cpp $(PREFIX)/share/halide/tools
	cp $(ROOT_DIR)/tools/halide_image.h $(PREFIX)/share/halide/tools
	cp $(ROOT_DIR)/tools/halide_image_io.h $(PREFIX)/share/halide/tools
	cp $(ROOT_DIR)/tools/halide_image_info.h $(PREFIX)/share/halide/tools
ifeq ($(UNAME), Darwin)
	install_name_tool -id $(PREFIX)/lib/libHalide.$(SHARED_EXT) $(PREFIX)/lib/libHalide.$(SHARED_EXT)
endif

$(BUILD_DIR)/halide_config.bzl: $(ROOT_DIR)/bazel/create_halide_config.sh
	@mkdir -p $(@D)
	$< > $@

$(DISTRIB_DIR)/halide.tgz: $(LIB_DIR)/libHalide.a $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h $(RUNTIME_EXPORTED_INCLUDES) $(ROOT_DIR)/bazel/* $(BUILD_DIR)/halide_config.bzl
	mkdir -p $(DISTRIB_DIR)/include $(DISTRIB_DIR)/bin $(DISTRIB_DIR)/lib $(DISTRIB_DIR)/tutorial $(DISTRIB_DIR)/tutorial/images $(DISTRIB_DIR)/tools $(DISTRIB_DIR)/tutorial/figures
	cp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(DISTRIB_DIR)/bin
	cp $(LIB_DIR)/libHalide.a $(DISTRIB_DIR)/lib
	cp $(INCLUDE_DIR)/Halide.h $(DISTRIB_DIR)/include
	cp $(INCLUDE_DIR)/HalideBuffer.h $(DISTRIB_DIR)/include
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
	cp $(ROOT_DIR)/tools/RunGen.cpp $(DISTRIB_DIR)/tools
	cp $(ROOT_DIR)/tools/RunGenStubs.cpp $(DISTRIB_DIR)/tools
	cp $(ROOT_DIR)/tools/halide_benchmark.h $(DISTRIB_DIR)/tools
	cp $(ROOT_DIR)/tools/halide_image.h $(DISTRIB_DIR)/tools
	cp $(ROOT_DIR)/tools/halide_image_io.h $(DISTRIB_DIR)/tools
	cp $(ROOT_DIR)/tools/halide_image_info.h $(DISTRIB_DIR)/tools
	cp $(ROOT_DIR)/README.md $(DISTRIB_DIR)
	cp $(ROOT_DIR)/bazel/BUILD $(DISTRIB_DIR)
	cp $(ROOT_DIR)/bazel/halide.bzl $(DISTRIB_DIR)
	cp $(ROOT_DIR)/bazel/README_bazel.md $(DISTRIB_DIR)
	cp $(ROOT_DIR)/bazel/WORKSPACE $(DISTRIB_DIR)
	cp $(BUILD_DIR)/halide_config.bzl $(DISTRIB_DIR)
	ln -sf $(DISTRIB_DIR) halide
	tar -czf $(DISTRIB_DIR)/halide.tgz \
		halide/bin \
		halide/lib \
		halide/include \
		halide/tutorial \
		halide/BUILD \
		halide/README.md \
		halide/README_bazel.md \
		halide/WORKSPACE \
		halide/*.bzl \
		halide/tools/mex_halide.m \
		halide/tools/*.cpp \
		halide/tools/halide_benchmark.h \
		halide/tools/halide_image.h \
		halide/tools/halide_image_io.h \
		halide/tools/halide_image_info.h
	rm -rf halide

.PHONY: distrib
distrib: $(DISTRIB_DIR)/halide.tgz

$(BIN_DIR)/HalideTraceViz: $(ROOT_DIR)/util/HalideTraceViz.cpp $(INCLUDE_DIR)/HalideRuntime.h
	$(CXX) $(OPTIMIZE) -std=c++11 $< -I$(INCLUDE_DIR) -L$(BIN_DIR) -o $@
