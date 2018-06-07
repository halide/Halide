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
    COMMON_LD_FLAGS=-luuid -lole32 -lpthread -lz -Wl,--stack,8388608
    SHARED_EXT=dll
    FPIC=
else
    # let's assume "normal" UNIX such as linux
    COMMON_LD_FLAGS=-ldl -lpthread -lz
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
LLVM_SYSTEM_LIBS=$(shell ${LLVM_CONFIG} --system-libs --link-static | sed -e 's/[\/&]/\\&/g')
LLVM_AS = $(LLVM_BINDIR)/llvm-as
LLVM_NM = $(LLVM_BINDIR)/llvm-nm
LLVM_CXX_FLAGS = -std=c++11  $(filter-out -O% -g -fomit-frame-pointer -pedantic -W% -W, $(shell $(LLVM_CONFIG) --cxxflags | sed -e 's/\\/\//g' -e 's/\([a-zA-Z]\):/\/\1/g;s/-D/ -D/g;s/-O/ -O/g'))
OPTIMIZE ?= -O3
OPTIMIZE_FOR_BUILD_TIME ?= -O0

SANITIZER_FLAGS ?=

# TODO: this is suboptimal hackery; we should really add the relevant
# support libs for the sanitizer(s) as weak symbols in Codegen_LLVM.
# (Note also that, in general, most Sanitizers work most reliably with an all-Clang
# build system.)

ifneq (,$(findstring tsan,$(HL_TARGET)$(HL_JIT_TARGET)))

# Note that attempting to use TSAN with the JIT can produce false positives
# if libHalide is not also compiled with TSAN enabled; we tack the relevant
# flag onto OPTIMIZE here, but that's really only effective if you ensure
# to do a clean build before testing. (In general, most of the Sanitizers
# only work well when used in a completely clean environment.)
OPTIMIZE += -fsanitize=thread
SANITIZER_FLAGS += -fsanitize=thread

endif

ifneq (,$(findstring asan,$(HL_TARGET)$(HL_JIT_TARGET)))
OPTIMIZE += -fsanitize=address
SANITIZER_FLAGS += -fsanitize=address
endif

COMMON_LD_FLAGS += $(SANITIZER_FLAGS)

LLVM_VERSION_TIMES_10 = $(shell $(LLVM_CONFIG) --version | cut -b 1,3)

LLVM_CXX_FLAGS += -DLLVM_VERSION=$(LLVM_VERSION_TIMES_10)

# All WITH_* flags are either empty or not-empty. They do not behave
# like true/false values in most languages.  To turn one off, either
# edit this file, add "WITH_FOO=" (no assigned value) to the make
# line, or define an environment variable WITH_FOO that has an empty
# value.
WITH_X86 ?= $(findstring x86, $(LLVM_COMPONENTS))
WITH_ARM ?= $(findstring arm, $(LLVM_COMPONENTS))
WITH_HEXAGON ?= $(findstring hexagon, $(LLVM_COMPONENTS))
WITH_MIPS ?= $(findstring mips, $(LLVM_COMPONENTS))
WITH_AARCH64 ?= $(findstring aarch64, $(LLVM_COMPONENTS))
WITH_POWERPC ?= $(findstring powerpc, $(LLVM_COMPONENTS))
WITH_PTX ?= $(findstring nvptx, $(LLVM_COMPONENTS))
# AMDGPU target is WIP
WITH_AMDGPU ?= $(findstring amdgpu, $(LLVM_COMPONENTS))
WITH_OPENCL ?= not-empty
WITH_METAL ?= not-empty
WITH_OPENGL ?= not-empty
ifeq ($(OS), Windows_NT)
    WITH_INTROSPECTION ?=
else
    WITH_INTROSPECTION ?= not-empty
endif
WITH_EXCEPTIONS ?=
WITH_LLVM_INSIDE_SHARED_LIBHALIDE ?= not-empty

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

AMDGPU_CXX_FLAGS=$(if $(WITH_AMDGPU), -DWITH_AMDGPU=1, )
AMDGPU_LLVM_CONFIG_LIB=$(if $(WITH_AMDGPU), amdgpu, )
# TODO add bitcode files

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

LLVM_HAS_NO_RTTI = $(findstring -fno-rtti, $(LLVM_CXX_FLAGS))
WITH_RTTI ?= $(if $(LLVM_HAS_NO_RTTI),, not-empty)
RTTI_CXX_FLAGS=$(if $(WITH_RTTI), , -fno-rtti )

CXX_WARNING_FLAGS = -Wall -Werror -Wno-unused-function -Wcast-qual -Wignored-qualifiers -Wno-comment -Wsign-compare -Wno-unknown-warning-option -Wno-psabi
CXX_FLAGS = $(CXX_WARNING_FLAGS) $(RTTI_CXX_FLAGS) -Woverloaded-virtual $(FPIC) $(OPTIMIZE) -fno-omit-frame-pointer -DCOMPILING_HALIDE

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
CXX_FLAGS += $(AMDGPU_CXX_FLAGS)

# This is required on some hosts like powerpc64le-linux-gnu because we may build
# everything with -fno-exceptions.  Without -funwind-tables, libHalide.so fails
# to propagate exceptions and causes a test failure.
CXX_FLAGS += -funwind-tables

print-%:
	@echo '$*=$($*)'

LLVM_STATIC_LIBS = -L $(LLVM_LIBDIR) $(shell $(LLVM_CONFIG) --link-static --libs bitwriter bitreader linker ipo mcjit $(X86_LLVM_CONFIG_LIB) $(ARM_LLVM_CONFIG_LIB) $(OPENCL_LLVM_CONFIG_LIB) $(METAL_LLVM_CONFIG_LIB) $(PTX_LLVM_CONFIG_LIB) $(AARCH64_LLVM_CONFIG_LIB) $(MIPS_LLVM_CONFIG_LIB) $(POWERPC_LLVM_CONFIG_LIB) $(HEXAGON_LLVM_CONFIG_LIB) $(AMDGPU_LLVM_CONFIG_LIB))

# Add a rpath to the llvm used for linking, in case multiple llvms are
# installed. Bakes a path on the build system into the .so, so don't
# use this config for distributions.
LLVM_SHARED_LIBS = -Wl,-rpath=$(LLVM_LIBDIR) -L $(LLVM_LIBDIR) -lLLVM

LLVM_LIBS_FOR_SHARED_LIBHALIDE=$(if $(WITH_LLVM_INSIDE_SHARED_LIBHALIDE),$(LLVM_STATIC_LIBS),$(LLVM_SHARED_LIBS))

TUTORIAL_CXX_FLAGS ?= -std=c++11 -g -fno-omit-frame-pointer -fno-rtti -I $(ROOT_DIR)/tools $(SANITIZER_FLAGS)
# The tutorials contain example code with warnings that we don't want
# to be flagged as errors, so the test flags are the tutorial flags
# plus our warning flags.
# Also allow tests, via conditional compilation, to use the entire
# capability of the CPU being compiled on via -march=native. This
# presumes tests are run on the smae machine they are compiled on.
TEST_CXX_FLAGS ?= $(TUTORIAL_CXX_FLAGS) $(CXX_WARNING_FLAGS) -march=native
TEST_LD_FLAGS = -L$(BIN_DIR) -lHalide $(COMMON_LD_FLAGS)

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

ifeq ($(WITH_LLVM_INSIDE_SHARED_LIBHALIDE), )
TEST_LD_FLAGS += -Wl,--rpath=$(LLVM_LIBDIR)
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

# Workaround brew Cellar path for libpng-config output.
LIBJPEG_LINKER_PATH ?= $(shell echo $(LIBPNG_LIBS_DEFAULT) | sed -e'/-L.*[/][Cc]ellar[/]libpng/!d;s=\(.*\)/[Cc]ellar/libpng/.*=\1/lib=')
LIBJPEG_LIBS ?= $(LIBJPEG_LINKER_PATH) -ljpeg

# There's no libjpeg-config, unfortunately. We should look for
# jpeglib.h one directory level up from png.h . Also handle
# Mac OS brew installs where libpng-config returns paths
# into the PNG cellar.
LIBPNG_INCLUDE_DIRS = $(filter -I%,$(LIBPNG_CXX_FLAGS))
LIBJPEG_CXX_FLAGS ?= $(shell echo $(LIBPNG_INCLUDE_DIRS) | sed -e'/[Cc]ellar[/]libpng/!s=\(.*\)=\1/..=;s=\(.*\)/[Cc]ellar/libpng/.*=\1/include=')

IMAGE_IO_LIBS = $(LIBPNG_LIBS) $(LIBJPEG_LIBS)
IMAGE_IO_CXX_FLAGS = $(LIBPNG_CXX_FLAGS) $(LIBJPEG_CXX_FLAGS)

# We're building into the current directory $(CURDIR). Find the Halide
# repo root directory (the location of the makefile)
THIS_MAKEFILE = $(realpath $(filter %Makefile, $(MAKEFILE_LIST)))
ROOT_DIR = $(strip $(shell dirname $(THIS_MAKEFILE)))
SRC_DIR  = $(ROOT_DIR)/src

# Allow the user to specify PYBIND11_PATH as a relative path,
# but canonicalize it to an absolute path since the sub-makefile
# we call may have a different working dir.
ifdef PYBIND11_PATH
	REAL_PYBIND11_PATH = $(realpath $(PYBIND11_PATH))
else
	REAL_PYBIND11_PATH = /PYBIND11_PATH/is/undefined
endif

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
HEXAGON_RUNTIME_LIBS_DIR = src/runtime/hexagon_remote/bin
HEXAGON_RUNTIME_LIBS = \
  $(HEXAGON_RUNTIME_LIBS_DIR)/arm-32-android/libhalide_hexagon_host.so \
  $(HEXAGON_RUNTIME_LIBS_DIR)/arm-64-android/libhalide_hexagon_host.so \
  $(HEXAGON_RUNTIME_LIBS_DIR)/host/libhalide_hexagon_host.so \
  $(HEXAGON_RUNTIME_LIBS_DIR)/v60/hexagon_sim_remote \
  $(HEXAGON_RUNTIME_LIBS_DIR)/v60/libhalide_hexagon_remote_skel.so \
  $(HEXAGON_RUNTIME_LIBS_DIR)/v60/signed_by_debug/libhalide_hexagon_remote_skel.so

SOURCE_FILES = \
  AddImageChecks.cpp \
  AddParameterChecks.cpp \
  AlignLoads.cpp \
  AllocationBoundsInference.cpp \
  ApplySplit.cpp \
  AssociativeOpsTable.cpp \
  Associativity.cpp \
  AutoSchedule.cpp \
  AutoScheduleUtils.cpp \
  BoundaryConditions.cpp \
  Bounds.cpp \
  BoundsInference.cpp \
  BoundSmallAllocations.cpp \
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
  CopyElision.cpp \
  Debug.cpp \
  DebugArguments.cpp \
  DebugToFile.cpp \
  Definition.cpp \
  Deinterleave.cpp \
  DeviceArgument.cpp \
  DeviceInterface.cpp \
  Dimension.cpp \
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
  LICM.cpp \
  LLVM_Output.cpp \
  LLVM_Runtime_Linker.cpp \
  LoopCarry.cpp \
  Lower.cpp \
  LowerWarpShuffles.cpp \
  MatlabWrapper.cpp \
  Memoization.cpp \
  Module.cpp \
  ModulusRemainder.cpp \
  Monotonic.cpp \
  ObjectInstanceRegistry.cpp \
  OutputImageParam.cpp \
  ParallelRVar.cpp \
  ParamMap.cpp \
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
  RegionCosts.cpp \
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
  SplitTuples.cpp \
  StmtToHtml.cpp \
  StorageFlattening.cpp \
  StorageFolding.cpp \
  StrictifyFloat.cpp \
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
  AutoSchedule.h \
  AutoScheduleUtils.h \
  BoundaryConditions.h \
  Bounds.h \
  BoundsInference.h \
  BoundSmallAllocations.h \
  Buffer.h \
  Closure.h \
  CodeGen_ARM.h \
  CodeGen_C.h \
  CodeGen_GPU_Dev.h \
  CodeGen_GPU_Host.h \
  CodeGen_Internal.h \
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
  CopyElision.h \
  Debug.h \
  DebugArguments.h \
  DebugToFile.h \
  Definition.h \
  Deinterleave.h \
  DeviceArgument.h \
  DeviceInterface.h \
  Dimension.h \
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
  LICM.h \
  LLVM_Output.h \
  LLVM_Runtime_Linker.h \
  LoopCarry.h \
  Lower.h \
  LowerWarpShuffles.h \
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
  Param.h \
  ParamMap.h \
  Parameter.h \
  PartitionLoops.h \
  Pipeline.h \
  Prefetch.h \
  Profiling.h \
  Qualify.h \
  Random.h \
  RealizationOrder.h \
  RDom.h \
  Reduction.h \
  RegionCosts.h \
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
  SplitTuples.h \
  StmtToHtml.h \
  StorageFlattening.h \
  StorageFolding.h \
  StrictifyFloat.h \
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
  alignment_128 \
  alignment_32 \
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
  gpu_device_selection \
  hexagon_cpu_features \
  hexagon_host \
  ios_io \
  linux_clock \
  linux_host_cpu_count \
  linux_opengl_context \
  linux_yield \
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
  osx_yield \
  posix_allocator \
  posix_clock \
  posix_error_handler \
  posix_get_symbol \
  posix_io \
  posix_print \
  posix_tempfile \
  posix_threads \
  posix_threads_tsan \
  powerpc_cpu_features \
  prefetch \
  profiler \
  profiler_inlined \
  qurt_allocator \
  qurt_hvx \
  qurt_init_fini \
  qurt_threads \
  qurt_threads_tsan \
  qurt_yield \
  runtime_api \
  ssp \
  to_string \
  tracing \
  windows_clock \
  windows_cuda \
  windows_get_symbol \
  windows_io \
  windows_opencl \
  windows_profiler \
  windows_tempfile \
  windows_threads \
  windows_threads_tsan \
  windows_yield \
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
  x86_avx2 \
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
	$(CXX) -o /dev/null -shared $(OBJECTS) $(INITIAL_MODULES) -Wl,-t $(LLVM_STATIC_LIBS) $(LLVM_SYSTEM_LIBS) $(COMMON_LD_FLAGS) 2>&1| egrep "libLLVM" > $(BUILD_DIR)/llvm_objects/list.new
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
	$(CXX) -shared $(OBJECTS) $(INITIAL_MODULES) $(LLVM_LIBS_FOR_SHARED_LIBHALIDE) $(LLVM_SYSTEM_LIBS) $(COMMON_LD_FLAGS) -o $(BIN_DIR)/libHalide.$(SHARED_EXT)
ifeq ($(UNAME), Darwin)
	install_name_tool -id $(CURDIR)/$(BIN_DIR)/libHalide.$(SHARED_EXT) $(BIN_DIR)/libHalide.$(SHARED_EXT)
endif

$(INCLUDE_DIR)/Halide.h: $(SRC_DIR)/../LICENSE.txt $(HEADERS) $(BIN_DIR)/build_halide_h
	@mkdir -p $(@D)
	$(BIN_DIR)/build_halide_h $(SRC_DIR)/../LICENSE.txt $(HEADERS) > $(INCLUDE_DIR)/Halide.h
	# Also generate a precompiled version in the same folder so that anything compiled with a compatible set of flags can use it
	$(CXX) -std=c++11 $(TEST_CXX_FLAGS) -I$(ROOT_DIR) $(OPTIMIZE) -x c++-header $(INCLUDE_DIR)/Halide.h -o $(INCLUDE_DIR)/Halide.h.gch

$(INCLUDE_DIR)/HalideRuntime%: $(SRC_DIR)/runtime/HalideRuntime%
	echo Copying $<
	@mkdir -p $(@D)
	cp $< $(INCLUDE_DIR)/

$(INCLUDE_DIR)/HalideBuffer.h: $(SRC_DIR)/runtime/HalideBuffer.h
	echo Copying $<
	@mkdir -p $(@D)
	cp $< $(INCLUDE_DIR)/

$(BIN_DIR)/build_halide_h: $(ROOT_DIR)/tools/build_halide_h.cpp
	@-mkdir -p $(@D)
	$(CXX) -std=c++11 $< -o $@

-include $(OBJECTS:.o=.d)
-include $(INITIAL_MODULES:.o=.d)

# Compile generic 32- or 64-bit code
# (The 'nacl' is a red herring. This is just a generic 32-bit little-endian target.)
RUNTIME_TRIPLE_32 = "le32-unknown-nacl-unknown"
RUNTIME_TRIPLE_64 = "le64-unknown-unknown-unknown"

# win32 is tied to x86 due to the use of the __stdcall calling convention
RUNTIME_TRIPLE_WIN_32 = "i386-unknown-unknown-unknown"

RUNTIME_CXX_FLAGS = -O3 -fno-vectorize -ffreestanding -fno-blocks -fno-exceptions -fno-unwind-tables -fpic
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
GENERATOR_EXTERNAL_TEST_GENERATOR := $(shell ls $(ROOT_DIR)/test/generator/*_generator.cpp)
TUTORIALS = $(filter-out %_generate.cpp, $(shell ls $(ROOT_DIR)/tutorial/*.cpp))
AUTO_SCHEDULE_TESTS = $(shell ls $(ROOT_DIR)/test/auto_schedule/*.cpp)

-include $(OPENGL_TESTS:$(ROOT_DIR)/test/opengl/%.cpp=$(BUILD_DIR)/test_opengl_%.d)

test_correctness: $(CORRECTNESS_TESTS:$(ROOT_DIR)/test/correctness/%.cpp=correctness_%) $(CORRECTNESS_TESTS:$(ROOT_DIR)/test/correctness/%.c=correctness_%)
test_performance: $(PERFORMANCE_TESTS:$(ROOT_DIR)/test/performance/%.cpp=performance_%)
test_error: $(ERROR_TESTS:$(ROOT_DIR)/test/error/%.cpp=error_%)
test_warning: $(WARNING_TESTS:$(ROOT_DIR)/test/warning/%.cpp=warning_%)
test_tutorial: $(TUTORIALS:$(ROOT_DIR)/tutorial/%.cpp=tutorial_%)
test_valgrind: $(CORRECTNESS_TESTS:$(ROOT_DIR)/test/correctness/%.cpp=valgrind_%)
test_avx512: $(CORRECTNESS_TESTS:$(ROOT_DIR)/test/correctness/%.cpp=avx512_%)
test_opengl: $(OPENGL_TESTS:$(ROOT_DIR)/test/opengl/%.cpp=opengl_%)
test_auto_schedule: $(AUTO_SCHEDULE_TESTS:$(ROOT_DIR)/test/auto_schedule/%.cpp=auto_schedule_%)

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

# https://github.com/halide/Halide/issues/2084 (only if opencl enabled)
GENERATOR_AOTCPP_TESTS := $(filter-out generator_aotcpp_buffer_copy,$(GENERATOR_AOTCPP_TESTS))

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

test_aotcpp_generator: $(GENERATOR_AOTCPP_TESTS)

# This is just a test to ensure than RunGen builds and links for a critical mass of Generators;
# not all will work directly (e.g. due to missing define_externs at link time), so we blacklist
# those known to be broken for plausible reasons.
GENERATOR_BUILD_RUNGEN_TESTS = $(GENERATOR_EXTERNAL_TEST_GENERATOR:$(ROOT_DIR)/test/generator/%_generator.cpp=$(FILTERS_DIR)/%.rungen)
GENERATOR_BUILD_RUNGEN_TESTS := $(filter-out $(FILTERS_DIR)/cxx_mangling_define_extern.rungen,$(GENERATOR_BUILD_RUNGEN_TESTS))
GENERATOR_BUILD_RUNGEN_TESTS := $(filter-out $(FILTERS_DIR)/define_extern_opencl.rungen,$(GENERATOR_BUILD_RUNGEN_TESTS))
GENERATOR_BUILD_RUNGEN_TESTS := $(filter-out $(FILTERS_DIR)/matlab.rungen,$(GENERATOR_BUILD_RUNGEN_TESTS))
GENERATOR_BUILD_RUNGEN_TESTS := $(filter-out $(FILTERS_DIR)/msan.rungen,$(GENERATOR_BUILD_RUNGEN_TESTS))
GENERATOR_BUILD_RUNGEN_TESTS := $(filter-out $(FILTERS_DIR)/multitarget.rungen,$(GENERATOR_BUILD_RUNGEN_TESTS))
GENERATOR_BUILD_RUNGEN_TESTS := $(filter-out $(FILTERS_DIR)/nested_externs.rungen,$(GENERATOR_BUILD_RUNGEN_TESTS))
GENERATOR_BUILD_RUNGEN_TESTS := $(filter-out $(FILTERS_DIR)/old_buffer_t.rungen,$(GENERATOR_BUILD_RUNGEN_TESTS))
GENERATOR_BUILD_RUNGEN_TESTS := $(filter-out $(FILTERS_DIR)/tiled_blur.rungen,$(GENERATOR_BUILD_RUNGEN_TESTS))
test_rungen: $(GENERATOR_BUILD_RUNGEN_TESTS)

test_generator: $(GENERATOR_AOT_TESTS) $(GENERATOR_AOTCPP_TESTS) $(GENERATOR_JIT_TESTS) $(GENERATOR_BUILD_RUNGEN_TESTS)

ALL_TESTS = test_internal test_correctness test_error test_tutorial test_warning test_generator

# These targets perform timings of each test. For most tests this includes Halide JIT compile times, and run times.
# For generator tests they time the compile time only. The times are recorded in CSV files.
time_compilation_correctness: init_time_compilation_correctness $(CORRECTNESS_TESTS:$(ROOT_DIR)/test/correctness/%.cpp=time_compilation_test_%)
time_compilation_performance: init_time_compilation_performance $(PERFORMANCE_TESTS:$(ROOT_DIR)/test/performance/%.cpp=time_compilation_performance_%)
time_compilation_opengl: init_time_compilation_opengl $(OPENGL_TESTS:$(ROOT_DIR)/test/opengl/%.cpp=time_compilation_opengl_%)
time_compilation_generator: init_time_compilation_generator $(GENERATOR_TESTS:$(ROOT_DIR)/test/generator/%_aottest.cpp=time_compilation_generator_%)

init_time_compilation_%:
	echo "TEST,User (s),System (s),Real" > $(@:init_time_compilation_%=compile_times_%.csv)

TIME_COMPILATION ?= /usr/bin/time -a -f "$@,%U,%S,%E" -o

run_tests: $(ALL_TESTS)
	make -f $(THIS_MAKEFILE) test_performance test_auto_schedule

build_tests: $(CORRECTNESS_TESTS:$(ROOT_DIR)/test/correctness/%.cpp=$(BIN_DIR)/correctness_%) \
	$(PERFORMANCE_TESTS:$(ROOT_DIR)/test/performance/%.cpp=$(BIN_DIR)/performance_%) \
	$(ERROR_TESTS:$(ROOT_DIR)/test/error/%.cpp=$(BIN_DIR)/error_%) \
	$(WARNING_TESTS:$(ROOT_DIR)/test/warning/%.cpp=$(BIN_DIR)/warning_%) \
	$(OPENGL_TESTS:$(ROOT_DIR)/test/opengl/%.cpp=$(BIN_DIR)/opengl_%) \
	$(GENERATOR_EXTERNAL_TESTS:$(ROOT_DIR)/test/generator/%_aottest.cpp=$(BIN_DIR)/$(TARGET)/generator_aot_%) \
	$(GENERATOR_EXTERNAL_TESTS:$(ROOT_DIR)/test/generator/%_jittest.cpp=$(BIN_DIR)/generator_jit_%) \
	$(AUTO_SCHEDULE_TESTS:$(ROOT_DIR)/test/auto_schedule/%.cpp=$(BIN_DIR)/auto_schedule_%)

clean_generator:
	rm -rf $(BIN_DIR)/*.generator
	rm -rf $(BIN_DIR)/*/runtime.a
	rm -rf $(FILTERS_DIR)
	rm -rf $(BIN_DIR)/*/generator_*
	rm -rf $(BUILD_DIR)/*_generator.o
	rm -f $(BUILD_DIR)/GenGen.o
	rm -f $(BUILD_DIR)/RunGen.o

time_compilation_tests: time_compilation_correctness time_compilation_performance time_compilation_generator

LIBHALIDE_DEPS ?= $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h

$(BUILD_DIR)/GenGen.o: $(ROOT_DIR)/tools/GenGen.cpp $(INCLUDE_DIR)/Halide.h
	@mkdir -p $(@D)
	$(CXX) -c $< $(TEST_CXX_FLAGS) -I$(INCLUDE_DIR) -o $@

# Make an empty generator for generating runtimes.
$(BIN_DIR)/runtime.generator: $(BUILD_DIR)/GenGen.o $(BIN_DIR)/libHalide.$(SHARED_EXT)
	@mkdir -p $(@D)
	$(CXX) $< $(TEST_LD_FLAGS) -o $@

# Generate a standalone runtime for a given target string
$(BIN_DIR)/%/runtime.a: $(BIN_DIR)/runtime.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -r runtime -o $(CURDIR)/$(BIN_DIR)/$* target=$*

$(BIN_DIR)/test_internal: $(ROOT_DIR)/test/internal.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT)
	@mkdir -p $(@D)
	$(CXX) $(TEST_CXX_FLAGS) $< -I$(SRC_DIR) $(TEST_LD_FLAGS) -o $@

# Correctness test that link against libHalide
$(BIN_DIR)/correctness_%: $(ROOT_DIR)/test/correctness/%.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h $(RUNTIME_EXPORTED_INCLUDES)
	@mkdir -p $(@D)
	$(CXX) $(TEST_CXX_FLAGS) -I$(ROOT_DIR) $(OPTIMIZE_FOR_BUILD_TIME) $< -I$(INCLUDE_DIR) $(TEST_LD_FLAGS) -o $@

# Correctness tests that do NOT link against libHalide
$(BIN_DIR)/correctness_plain_c_includes: $(ROOT_DIR)/test/correctness/plain_c_includes.c $(RUNTIME_EXPORTED_INCLUDES)
	$(CXX) -x c -Wall -Werror -I$(ROOT_DIR) $(OPTIMIZE_FOR_BUILD_TIME) $< -I$(ROOT_DIR)/src/runtime -o $@

# Note that this test must *not* link in either libHalide, or a Halide runtime;
# this test should be usable without either.
$(BIN_DIR)/correctness_halide_buffer: $(ROOT_DIR)/test/correctness/halide_buffer.cpp $(INCLUDE_DIR)/HalideBuffer.h $(RUNTIME_EXPORTED_INCLUDES)
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE_FOR_BUILD_TIME) $< -I$(INCLUDE_DIR) -o $@

# The image_io test additionally needs to link to libpng and
# libjpeg.
$(BIN_DIR)/correctness_image_io: $(ROOT_DIR)/test/correctness/image_io.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h $(RUNTIME_EXPORTED_INCLUDES)
	$(CXX) $(TEST_CXX_FLAGS) $(IMAGE_IO_CXX_FLAGS) -I$(ROOT_DIR) $(OPTIMIZE_FOR_BUILD_TIME) $< -I$(INCLUDE_DIR) $(TEST_LD_FLAGS) $(IMAGE_IO_LIBS) -o $@

$(BIN_DIR)/performance_%: $(ROOT_DIR)/test/performance/%.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE) $< -I$(INCLUDE_DIR) -I$(ROOT_DIR) $(TEST_LD_FLAGS) -o $@

# Error tests that link against libHalide
$(BIN_DIR)/error_%: $(ROOT_DIR)/test/error/%.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h
	$(CXX) $(TEST_CXX_FLAGS) -I$(ROOT_DIR) $(OPTIMIZE_FOR_BUILD_TIME) $< -I$(INCLUDE_DIR) $(TEST_LD_FLAGS) -o $@

$(BIN_DIR)/warning_%: $(ROOT_DIR)/test/warning/%.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE_FOR_BUILD_TIME) $< -I$(INCLUDE_DIR) $(TEST_LD_FLAGS) -o $@

$(BIN_DIR)/opengl_%: $(ROOT_DIR)/test/opengl/%.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h $(INCLUDE_DIR)/HalideRuntime.h $(INCLUDE_DIR)/HalideRuntimeOpenGL.h
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE_FOR_BUILD_TIME) $< -I$(INCLUDE_DIR) -I$(SRC_DIR) $(TEST_LD_FLAGS) $(OPENGL_LD_FLAGS) -o $@ -MMD -MF $(BUILD_DIR)/test_opengl_$*.d

# Auto schedule tests that link against libHalide
$(BIN_DIR)/auto_schedule_%: $(ROOT_DIR)/test/auto_schedule/%.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE_FOR_BUILD_TIME) $< -I$(INCLUDE_DIR) $(TEST_LD_FLAGS) -o $@

# TODO(srj): this doesn't auto-delete, why not?
.INTERMEDIATE: $(BIN_DIR)/%.generator

# By default, %.generator is produced by building %_generator.cpp
# Note that the rule includes all _generator.cpp files, so that generator with define_extern
# usage can just add deps later.
$(BUILD_DIR)/%_generator.o: $(ROOT_DIR)/test/generator/%_generator.cpp $(INCLUDE_DIR)/Halide.h
	@mkdir -p $(@D)
	$(CXX) $(TEST_CXX_FLAGS) -I$(INCLUDE_DIR) -I$(CURDIR)/$(FILTERS_DIR) -c $< -o $@

$(BIN_DIR)/%.generator: $(BUILD_DIR)/GenGen.o $(BIN_DIR)/libHalide.$(SHARED_EXT) $(BUILD_DIR)/%_generator.o
	@mkdir -p $(@D)
	$(CXX) $(filter %.cpp %.o %.a,$^) $(TEST_LD_FLAGS) -o $@

# It is not always possible to cross compile between 32-bit and 64-bit via the clang build as part of llvm
# These next two rules can fail the compilationa nd produce zero length bitcode blobs.
# If the zero length blob is actually used, the test will fail anyway, but usually only the bitness
# of the target is used.
$(BUILD_DIR)/external_code_extern_bitcode_32.cpp : $(ROOT_DIR)/test/generator/external_code_extern.cpp $(BIN_DIR)/binary2cpp
	@mkdir -p $(@D)
	$(CLANG) $(CXX_WARNING_FLAGS) -O3 -c -m32 -target $(RUNTIME_TRIPLE_32) -emit-llvm $< -o $(BUILD_DIR)/external_code_extern_32.bc || echo -n > $(BUILD_DIR)/external_code_extern_32.bc
	./$(BIN_DIR)/binary2cpp external_code_extern_bitcode_32 < $(BUILD_DIR)/external_code_extern_32.bc > $@

$(BUILD_DIR)/external_code_extern_bitcode_64.cpp : $(ROOT_DIR)/test/generator/external_code_extern.cpp $(BIN_DIR)/binary2cpp
	@mkdir -p $(@D)
	$(CLANG) $(CXX_WARNING_FLAGS) -O3 -c -m64 -target $(RUNTIME_TRIPLE_64) -emit-llvm $< -o $(BUILD_DIR)/external_code_extern_64.bc || echo -n > $(BUILD_DIR)/external_code_extern_64.bc
	./$(BIN_DIR)/binary2cpp external_code_extern_bitcode_64 < $(BUILD_DIR)/external_code_extern_64.bc > $@

$(BUILD_DIR)/external_code_extern_cpp_source.cpp : $(ROOT_DIR)/test/generator/external_code_extern.cpp $(BIN_DIR)/binary2cpp
	@mkdir -p $(@D)
	./$(BIN_DIR)/binary2cpp external_code_extern_cpp_source < $(ROOT_DIR)/test/generator/external_code_extern.cpp > $@

$(BIN_DIR)/external_code.generator: $(BUILD_DIR)/GenGen.o $(BIN_DIR)/libHalide.$(SHARED_EXT) $(BUILD_DIR)/external_code_generator.o $(BUILD_DIR)/external_code_extern_bitcode_32.cpp $(BUILD_DIR)/external_code_extern_bitcode_64.cpp $(BUILD_DIR)/external_code_extern_cpp_source.cpp
	@mkdir -p $(@D)
	$(CXX) $(filter %.cpp %.o %.a,$^) $(TEST_LD_FLAGS) -o $@

NAME_MANGLING_TARGET=$(NON_EMPTY_TARGET)-c_plus_plus_name_mangling

GEN_AOT_OUTPUTS=-e static_library,h,cpp

# By default, %.a/.h are produced by executing %.generator. Runtimes are not included in these.
# (We explicitly also generate .cpp output here as well, as additional test surface for the C++ backend.)
$(FILTERS_DIR)/%.a: $(BIN_DIR)/%.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g $* $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime

$(FILTERS_DIR)/%.h: $(FILTERS_DIR)/%.a
	@echo $@ produced implicitly by $^

$(FILTERS_DIR)/%.cpp: $(FILTERS_DIR)/%.a
	@echo $@ produced implicitly by $^

$(FILTERS_DIR)/%.stub.h: $(BIN_DIR)/%.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g $* -n $* -o $(CURDIR)/$(FILTERS_DIR) -e cpp_stub

$(FILTERS_DIR)/cxx_mangling_externs.o: $(ROOT_DIR)/test/generator/cxx_mangling_externs.cpp
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) -c $(filter-out %.h,$^) $(GEN_AOT_INCLUDES) -o $@

# If we want to use a Generator with custom GeneratorParams, we need to write
# custom rules: to pass the GeneratorParams, and to give a unique function and file name.
$(FILTERS_DIR)/cxx_mangling.a: $(BIN_DIR)/cxx_mangling.generator $(FILTERS_DIR)/cxx_mangling_externs.o
	@mkdir -p $(@D)
	$(CURDIR)/$< -g cxx_mangling $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime-c_plus_plus_name_mangling -f "HalideTest::AnotherNamespace::cxx_mangling"
	$(ROOT_DIR)/tools/makelib.sh $@ $@ $(FILTERS_DIR)/cxx_mangling_externs.o

ifneq ($(TEST_CUDA), )
# Also build with a gpu target to ensure that the GPU-Host generation
# code handles name mangling properly. (Note that we don't need to
# run this code, just check for link errors.)
$(FILTERS_DIR)/cxx_mangling_gpu.a: $(BIN_DIR)/cxx_mangling.generator $(FILTERS_DIR)/cxx_mangling_externs.o
	@mkdir -p $(@D)
	$(CURDIR)/$< -g cxx_mangling $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime-c_plus_plus_name_mangling-cuda -f "HalideTest::cxx_mangling_gpu"
	$(ROOT_DIR)/tools/makelib.sh $@ $@ $(FILTERS_DIR)/cxx_mangling_externs.o
endif

$(FILTERS_DIR)/cxx_mangling_define_extern_externs.o: $(ROOT_DIR)/test/generator/cxx_mangling_define_extern_externs.cpp $(FILTERS_DIR)/cxx_mangling.h
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) -c $(filter-out %.h,$^) $(GEN_AOT_INCLUDES) -o $@

$(FILTERS_DIR)/cxx_mangling_define_extern.a: $(BIN_DIR)/cxx_mangling_define_extern.generator $(FILTERS_DIR)/cxx_mangling_define_extern_externs.o
	@mkdir -p $(@D)
	$(CURDIR)/$< -g cxx_mangling_define_extern $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime-c_plus_plus_name_mangling-user_context -f "HalideTest::cxx_mangling_define_extern"
	$(ROOT_DIR)/tools/makelib.sh $@ $@  $(FILTERS_DIR)/cxx_mangling_define_extern_externs.o

# This tests the specific features gated by the legacy_buffer_wrappers flag, thus
# we need to enable it for this Generator.
$(FILTERS_DIR)/old_buffer_t.a: $(BIN_DIR)/old_buffer_t.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g old_buffer_t -f old_buffer_t $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime-legacy_buffer_wrappers

# pyramid needs a custom arg.
$(FILTERS_DIR)/pyramid.a: $(BIN_DIR)/pyramid.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g pyramid -f pyramid $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime levels=10

# memory_profiler_mandelbrot need profiler set
$(FILTERS_DIR)/memory_profiler_mandelbrot.a: $(BIN_DIR)/memory_profiler_mandelbrot.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g memory_profiler_mandelbrot -f memory_profiler_mandelbrot $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime-profile

$(FILTERS_DIR)/alias_with_offset_42.a: $(BIN_DIR)/alias.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g alias_with_offset_42 -f alias_with_offset_42 $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime

METADATA_TESTER_GENERATOR_ARGS=\
	input.type=uint8 input.dim=3 \
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
	buffer_array_input2.dim=3 \
	buffer_array_input3.type=float32 \
	buffer_array_input4.dim=3 \
	buffer_array_input4.type=float32 \
	buffer_array_input5.size=2 \
	buffer_array_input6.size=2 \
	buffer_array_input6.dim=3 \
	buffer_array_input7.size=2 \
	buffer_array_input7.type=float32 \
	buffer_array_input8.size=2 \
	buffer_array_input8.dim=3 \
	buffer_array_input8.type=float32 \
	array_outputs.size=2 \
	array_outputs7.size=2 \
	array_outputs8.size=2 \
	array_outputs9.size=2

# metadata_tester is built with and without user-context
$(FILTERS_DIR)/metadata_tester.a: $(BIN_DIR)/metadata_tester.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g metadata_tester -f metadata_tester $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime $(METADATA_TESTER_GENERATOR_ARGS)

$(FILTERS_DIR)/metadata_tester_ucon.a: $(BIN_DIR)/metadata_tester.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g metadata_tester -f metadata_tester_ucon $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-user_context-no_runtime $(METADATA_TESTER_GENERATOR_ARGS)

$(BIN_DIR)/$(TARGET)/generator_aot_metadata_tester: $(FILTERS_DIR)/metadata_tester_ucon.a

$(FILTERS_DIR)/multitarget.a: $(BIN_DIR)/multitarget.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g multitarget -f "HalideTest::multitarget" $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-debug-no_runtime-c_plus_plus_name_mangling,$(TARGET)-no_runtime-c_plus_plus_name_mangling  -e assembly,bitcode,cpp,h,html,static_library,stmt

$(FILTERS_DIR)/msan.a: $(BIN_DIR)/msan.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g msan -f msan $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-msan

# user_context needs to be generated with user_context as the first argument to its calls
$(FILTERS_DIR)/user_context.a: $(BIN_DIR)/user_context.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g user_context $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime-user_context

# ditto for user_context_insanity
$(FILTERS_DIR)/user_context_insanity.a: $(BIN_DIR)/user_context_insanity.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g user_context_insanity $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime-user_context

# matlab needs to be generated with matlab in TARGET
$(FILTERS_DIR)/matlab.a: $(BIN_DIR)/matlab.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g matlab $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime-matlab

# Some .generators have additional dependencies (usually due to define_extern usage).
# These typically require two extra dependencies:
# (1) Ensuring the extra _generator.cpp is built into the .generator.
# (2) Ensuring the extra .a is linked into the final output.

# TODO(srj): we really want to say "anything that depends on tiled_blur.a also depends on blur2x2.a";
# is there a way to specify that in Make?
$(BIN_DIR)/$(TARGET)/generator_aot_tiled_blur: $(FILTERS_DIR)/blur2x2.a
ifneq ($(TEST_CUDA), )
$(BIN_DIR)/$(TARGET)/generator_aot_cxx_mangling: $(FILTERS_DIR)/cxx_mangling_gpu.a
endif
$(BIN_DIR)/$(TARGET)/generator_aot_cxx_mangling_define_extern: $(FILTERS_DIR)/cxx_mangling.a

$(BIN_DIR)/$(TARGET)/generator_aotcpp_tiled_blur: $(FILTERS_DIR)/blur2x2.cpp
ifneq ($(TEST_CUDA), )
$(BIN_DIR)/$(TARGET)/generator_aotcpp_cxx_mangling: $(FILTERS_DIR)/cxx_mangling_gpu.cpp
endif
$(BIN_DIR)/$(TARGET)/generator_aotcpp_cxx_mangling: $(FILTERS_DIR)/cxx_mangling_externs.o
$(BIN_DIR)/$(TARGET)/generator_aotcpp_cxx_mangling_define_extern: $(FILTERS_DIR)/cxx_mangling.cpp $(FILTERS_DIR)/cxx_mangling_externs.o $(FILTERS_DIR)/cxx_mangling_define_extern_externs.o

$(BUILD_DIR)/stubuser_generator.o: $(FILTERS_DIR)/stubtest.stub.h
$(BIN_DIR)/stubuser.generator: $(BUILD_DIR)/stubtest_generator.o

# stubtest has input and output funcs with undefined types and array sizes; this is fine for stub
# usage (the types can be inferred), but for AOT compilation, we must make the types
# concrete via generator args.
STUBTEST_GENERATOR_ARGS=\
    untyped_buffer_input.type=uint8 untyped_buffer_input.dim=3 \
	simple_input.type=float32 \
	array_input.type=float32 array_input.size=2 \
	int_arg.size=2 \
	tuple_output.type=float32,float32 \
	vectorize=true

$(FILTERS_DIR)/stubtest.a: $(BIN_DIR)/stubtest.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g stubtest -f stubtest $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime $(STUBTEST_GENERATOR_ARGS)

$(FILTERS_DIR)/external_code.a: $(BIN_DIR)/external_code.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g external_code -e static_library,h -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime external_code_is_bitcode=true

$(FILTERS_DIR)/external_code.cpp: $(BIN_DIR)/external_code.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g external_code -e cpp -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime external_code_is_bitcode=false

# Usually, it's considered best practice to have one Generator per
# .cpp file, with the generator-name and filename matching;
# nested_externs_generators.cpp is a counterexample, and thus requires
# some special casing to get right.  First, make a special rule to
# build each of the Generators in nested_externs_generator.cpp (which
# all have the form nested_externs_*).
$(FILTERS_DIR)/nested_externs_%.a: $(BIN_DIR)/nested_externs.generator
	$(CURDIR)/$< -g nested_externs_$* $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime

GEN_AOT_CXX_FLAGS=$(TEST_CXX_FLAGS) -Wno-unknown-pragmas
GEN_AOT_INCLUDES=-I$(INCLUDE_DIR) -I$(FILTERS_DIR) -I$(ROOT_DIR) -I $(ROOT_DIR)/apps/support -I $(SRC_DIR)/runtime -I$(ROOT_DIR)/tools
GEN_AOT_LD_FLAGS=$(COMMON_LD_FLAGS)

ifneq ($(TEST_METAL), )
# Unlike cuda and opencl, which dynamically go find the appropriate symbols, metal requires actual linking.
GEN_AOT_LD_FLAGS+=$(METAL_LD_FLAGS)
endif

# By default, %_aottest.cpp depends on $(FILTERS_DIR)/%.a/.h (but not libHalide).
$(BIN_DIR)/$(TARGET)/generator_aot_%: $(ROOT_DIR)/test/generator/%_aottest.cpp $(FILTERS_DIR)/%.a $(FILTERS_DIR)/%.h $(RUNTIME_EXPORTED_INCLUDES) $(BIN_DIR)/$(TARGET)/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) -o $@

# Also make AOT testing targets that depends on the .cpp output (rather than .a).
$(BIN_DIR)/$(TARGET)/generator_aotcpp_%: $(ROOT_DIR)/test/generator/%_aottest.cpp $(FILTERS_DIR)/%.cpp $(FILTERS_DIR)/%.h $(RUNTIME_EXPORTED_INCLUDES) $(BIN_DIR)/$(TARGET)/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) -o $@

# MSAN test doesn't use the standard runtime
$(BIN_DIR)/$(TARGET)/generator_aot_msan: $(ROOT_DIR)/test/generator/msan_aottest.cpp $(FILTERS_DIR)/msan.a $(FILTERS_DIR)/msan.h $(RUNTIME_EXPORTED_INCLUDES)
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter-out %.h,$^) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) -o $@

# alias has additional deps to link in
$(BIN_DIR)/$(TARGET)/generator_aot_alias: $(ROOT_DIR)/test/generator/alias_aottest.cpp $(FILTERS_DIR)/alias.a $(FILTERS_DIR)/alias_with_offset_42.a $(RUNTIME_EXPORTED_INCLUDES) $(BIN_DIR)/$(TARGET)/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) -o $@

$(BIN_DIR)/$(TARGET)/generator_aotcpp_alias: $(ROOT_DIR)/test/generator/alias_aottest.cpp $(FILTERS_DIR)/alias.cpp $(FILTERS_DIR)/alias_with_offset_42.cpp $(RUNTIME_EXPORTED_INCLUDES) $(BIN_DIR)/$(TARGET)/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) -o $@

# nested_externs has additional deps to link in
$(BIN_DIR)/$(TARGET)/generator_aot_nested_externs: $(ROOT_DIR)/test/generator/nested_externs_aottest.cpp $(FILTERS_DIR)/nested_externs_root.a $(FILTERS_DIR)/nested_externs_inner.a $(FILTERS_DIR)/nested_externs_combine.a $(FILTERS_DIR)/nested_externs_leaf.a $(RUNTIME_EXPORTED_INCLUDES) $(BIN_DIR)/$(TARGET)/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) -o $@

$(BIN_DIR)/$(TARGET)/generator_aotcpp_nested_externs: $(ROOT_DIR)/test/generator/nested_externs_aottest.cpp $(FILTERS_DIR)/nested_externs_root.cpp $(FILTERS_DIR)/nested_externs_inner.cpp $(FILTERS_DIR)/nested_externs_combine.cpp $(FILTERS_DIR)/nested_externs_leaf.cpp $(RUNTIME_EXPORTED_INCLUDES) $(BIN_DIR)/$(TARGET)/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) -o $@

# The matlab tests needs "-matlab" in the runtime
$(BIN_DIR)/$(TARGET)/generator_aot_matlab: $(ROOT_DIR)/test/generator/matlab_aottest.cpp $(FILTERS_DIR)/matlab.a $(FILTERS_DIR)/matlab.h $(RUNTIME_EXPORTED_INCLUDES) $(BIN_DIR)/$(TARGET)-matlab/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) $(TEST_LD_FLAGS) -o $@

$(BIN_DIR)/$(TARGET)/generator_aotcpp_matlab: $(ROOT_DIR)/test/generator/matlab_aottest.cpp $(FILTERS_DIR)/matlab.cpp $(FILTERS_DIR)/matlab.h $(RUNTIME_EXPORTED_INCLUDES) $(BIN_DIR)/$(TARGET)-matlab/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) $(TEST_LD_FLAGS) -o $@

# The gpu object lifetime test needs the debug runtime
$(BIN_DIR)/$(TARGET)/generator_aot_gpu_object_lifetime: $(ROOT_DIR)/test/generator/gpu_object_lifetime_aottest.cpp $(FILTERS_DIR)/gpu_object_lifetime.a $(FILTERS_DIR)/gpu_object_lifetime.h $(RUNTIME_EXPORTED_INCLUDES) $(BIN_DIR)/$(TARGET)-debug/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) $(TEST_LD_FLAGS) -o $@

# acquire_release explicitly uses CUDA/OpenCL APIs, so link those here.
$(BIN_DIR)/$(TARGET)/generator_aot_acquire_release: $(ROOT_DIR)/test/generator/acquire_release_aottest.cpp $(FILTERS_DIR)/acquire_release.a $(FILTERS_DIR)/acquire_release.h $(RUNTIME_EXPORTED_INCLUDES) $(BIN_DIR)/$(TARGET)/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) $(OPENCL_LD_FLAGS) $(CUDA_LD_FLAGS) -o $@

$(BIN_DIR)/$(TARGET)/generator_aotcpp_acquire_release: $(ROOT_DIR)/test/generator/acquire_release_aottest.cpp $(FILTERS_DIR)/acquire_release.cpp $(FILTERS_DIR)/acquire_release.h $(RUNTIME_EXPORTED_INCLUDES) $(BIN_DIR)/$(TARGET)/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) $(OPENCL_LD_FLAGS) $(CUDA_LD_FLAGS) -o $@

# define_extern_opencl explicitly uses OpenCL APIs, so link those here.
$(BIN_DIR)/$(TARGET)/generator_aot_define_extern_opencl: $(ROOT_DIR)/test/generator/define_extern_opencl_aottest.cpp $(FILTERS_DIR)/define_extern_opencl.a $(FILTERS_DIR)/define_extern_opencl.h $(RUNTIME_EXPORTED_INCLUDES) $(BIN_DIR)/$(TARGET)/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) $(OPENCL_LD_FLAGS) -o $@

$(BIN_DIR)/$(TARGET)/generator_aotcpp_define_extern_opencl: $(ROOT_DIR)/test/generator/define_extern_opencl_aottest.cpp $(FILTERS_DIR)/define_extern_opencl.cpp $(FILTERS_DIR)/define_extern_opencl.h $(RUNTIME_EXPORTED_INCLUDES) $(BIN_DIR)/$(TARGET)/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) $(OPENCL_LD_FLAGS) -o $@

# By default, %_jittest.cpp depends on libHalide, plus the stubs for the Generator. These are external tests that use the JIT.
$(BIN_DIR)/generator_jit_%: $(ROOT_DIR)/test/generator/%_jittest.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h $(FILTERS_DIR)/%.stub.h $(BUILD_DIR)/%_generator.o
	@mkdir -p $(@D)
	$(CXX) -g $(TEST_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) -I$(INCLUDE_DIR) -I$(FILTERS_DIR) -I $(ROOT_DIR)/apps/support $(TEST_LD_FLAGS) -o $@

# generator_aot_multitarget is run multiple times, with different env vars.
generator_aot_multitarget: $(BIN_DIR)/$(TARGET)/generator_aot_multitarget
	@mkdir -p $(@D)
	HL_MULTITARGET_TEST_USE_DEBUG_FEATURE=0 $(CURDIR)/$<
	HL_MULTITARGET_TEST_USE_DEBUG_FEATURE=1 $(CURDIR)/$<
	@-echo

# nested externs doesn't actually contain a generator named
# "nested_externs", and has no internal tests in any case.
test_generator_nested_externs:
	@echo "Skipping"

$(BUILD_DIR)/RunGen.o: $(ROOT_DIR)/tools/RunGen.cpp $(RUNTIME_EXPORTED_INCLUDES)
	@mkdir -p $(@D)
	$(CXX) -c $< $(TEST_CXX_FLAGS) $(IMAGE_IO_CXX_FLAGS) -I$(INCLUDE_DIR) -I $(SRC_DIR)/runtime -I$(ROOT_DIR)/tools -o $@

$(FILTERS_DIR)/%.rungen: $(BUILD_DIR)/RunGen.o $(BIN_DIR)/$(TARGET)/runtime.a $(ROOT_DIR)/tools/RunGenStubs.cpp $(FILTERS_DIR)/%.a
	@mkdir -p $(@D)
	$(CXX) -std=c++11 -DHL_RUNGEN_FILTER_HEADER=\"$*.h\" -I$(FILTERS_DIR) $^ $(GEN_AOT_LD_FLAGS) $(IMAGE_IO_LIBS) -o $@

RUNARGS ?=

$(FILTERS_DIR)/%.run: $(FILTERS_DIR)/%.rungen
	$(CURDIR)/$< $(RUNARGS)
	@-echo

$(BIN_DIR)/tutorial_%: $(ROOT_DIR)/tutorial/%.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h
	@ if [[ $@ == *_run ]]; then \
		export TUTORIAL=$* ;\
		export LESSON=`echo $${TUTORIAL} | cut -b1-9`; \
		make -f $(THIS_MAKEFILE) tutorial_$${TUTORIAL/run/generate}; \
		$(CXX) $(TUTORIAL_CXX_FLAGS) $(IMAGE_IO_CXX_FLAGS) $(OPTIMIZE_FOR_BUILD_TIME) $< \
		-I$(TMP_DIR) -I$(INCLUDE_DIR) $(TMP_DIR)/$${LESSON}_*.a $(GEN_AOT_LD_FLAGS) $(IMAGE_IO_LIBS) -lz -o $@; \
	else \
		$(CXX) $(TUTORIAL_CXX_FLAGS) $(IMAGE_IO_CXX_FLAGS) $(OPTIMIZE_FOR_BUILD_TIME) $< \
		-I$(INCLUDE_DIR) -I$(ROOT_DIR)/tools $(TEST_LD_FLAGS) $(IMAGE_IO_LIBS) -o $@;\
	fi

$(BIN_DIR)/tutorial_lesson_15_generators: $(ROOT_DIR)/tutorial/lesson_15_generators.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h $(BUILD_DIR)/GenGen.o
	$(CXX) $(TUTORIAL_CXX_FLAGS) $(IMAGE_IO_CXX_FLAGS) $(OPTIMIZE_FOR_BUILD_TIME) $< $(BUILD_DIR)/GenGen.o \
	-I$(INCLUDE_DIR) $(TEST_LD_FLAGS) $(IMAGE_IO_LIBS) -o $@

tutorial_lesson_15_generators: $(ROOT_DIR)/tutorial/lesson_15_generators_usage.sh $(BIN_DIR)/tutorial_lesson_15_generators
	@-mkdir -p $(TMP_DIR)
	cp $(BIN_DIR)/tutorial_lesson_15_generators $(TMP_DIR)/lesson_15_generate; \
	cd $(TMP_DIR); \
	PATH="$${PATH}:$(CURDIR)/$(BIN_DIR)" source $(ROOT_DIR)/tutorial/lesson_15_generators_usage.sh
	@-echo

$(BIN_DIR)/tutorial_lesson_16_rgb_generate: $(ROOT_DIR)/tutorial/lesson_16_rgb_generate.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h $(BUILD_DIR)/GenGen.o
	$(CXX) $(TUTORIAL_CXX_FLAGS) $(IMAGE_IO_CXX_FLAGS) $(OPTIMIZE_FOR_BUILD_TIME) $< $(BUILD_DIR)/GenGen.o \
	-I$(INCLUDE_DIR) $(TEST_LD_FLAGS) $(IMAGE_IO_LIBS) -o $@

$(BIN_DIR)/tutorial_lesson_16_rgb_run: $(ROOT_DIR)/tutorial/lesson_16_rgb_run.cpp $(BIN_DIR)/tutorial_lesson_16_rgb_generate
	@-mkdir -p $(TMP_DIR)
	# Run the generator
	$(BIN_DIR)/tutorial_lesson_16_rgb_generate -g brighten -o $(TMP_DIR) -f brighten_planar      target=host layout=planar
	$(BIN_DIR)/tutorial_lesson_16_rgb_generate -g brighten -o $(TMP_DIR) -f brighten_interleaved target=host-no_runtime layout=interleaved
	$(BIN_DIR)/tutorial_lesson_16_rgb_generate -g brighten -o $(TMP_DIR) -f brighten_either      target=host-no_runtime layout=either
	$(BIN_DIR)/tutorial_lesson_16_rgb_generate -g brighten -o $(TMP_DIR) -f brighten_specialized target=host-no_runtime layout=specialized
	# Compile the runner
	$(CXX) $(TUTORIAL_CXX_FLAGS) $(IMAGE_IO_CXX_FLAGS) $(OPTIMIZE_FOR_BUILD_TIME) $< \
	-I$(INCLUDE_DIR) -L$(BIN_DIR) -I $(TMP_DIR) $(TMP_DIR)/brighten_*.a \
        -lHalide $(TEST_LD_FLAGS) $(COMMON_LD_FLAGS) $(IMAGE_IO_LIBS) -o $@
	@-echo

$(BIN_DIR)/tutorial_lesson_21_auto_scheduler_generate: $(ROOT_DIR)/tutorial/lesson_21_auto_scheduler_generate.cpp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h $(BUILD_DIR)/GenGen.o
	$(CXX) $(TUTORIAL_CXX_FLAGS) $(IMAGE_IO_CXX_FLAGS) $(OPTIMIZE_FOR_BUILD_TIME) $< $(BUILD_DIR)/GenGen.o \
	-I$(INCLUDE_DIR) $(TEST_LD_FLAGS) $(IMAGE_IO_LIBS) -o $@

# The values in MachineParams are:
# - the maximum level of parallelism available,
# - the size of the last-level cache (in KB),
# - the ratio between the cost of a miss at the last level cache and the cost
#   of arithmetic on the target architecture
# ...in that order.
LESSON_21_MACHINE_PARAMS = 32,16777216,40

$(BIN_DIR)/tutorial_lesson_21_auto_scheduler_run: $(ROOT_DIR)/tutorial/lesson_21_auto_scheduler_run.cpp $(BIN_DIR)/tutorial_lesson_21_auto_scheduler_generate
	@-mkdir -p $(TMP_DIR)
	# Run the generator
	$(BIN_DIR)/tutorial_lesson_21_auto_scheduler_generate -g auto_schedule_gen -o $(TMP_DIR) -e static_library,h,schedule -f auto_schedule_false target=host            auto_schedule=false
	$(BIN_DIR)/tutorial_lesson_21_auto_scheduler_generate -g auto_schedule_gen -o $(TMP_DIR) -e static_library,h,schedule -f auto_schedule_true  target=host-no_runtime auto_schedule=true machine_params=$(LESSON_21_MACHINE_PARAMS)
	# Compile the runner
	$(CXX) $(TUTORIAL_CXX_FLAGS) $(IMAGE_IO_CXX_FLAGS) $(OPTIMIZE_FOR_BUILD_TIME) $< \
	-I$(INCLUDE_DIR) -L$(BIN_DIR) -I $(TMP_DIR) $(TMP_DIR)/auto_schedule_*.a \
        -lHalide $(TEST_LD_FLAGS) $(COMMON_LD_FLAGS) $(IMAGE_IO_LIBS) -o $@
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

auto_schedule_%: $(BIN_DIR)/auto_schedule_%
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

TEST_APPS=\
	HelloMatlab \
	bilateral_grid \
	blur \
	camera_pipe \
	c_backend \
	conv_layer \
	fft \
	interpolate \
	lens_blur \
	linear_algebra \
	local_laplacian \
	nl_means \
	resize \
	wavelet \

.PHONY: test_apps
test_apps: distrib
	@for APP in $(TEST_APPS); do \
		echo Testing app $${APP}... ; \
		make -C $(ROOT_DIR)/apps/$${APP} test HALIDE_BIN_PATH=$(CURDIR) HALIDE_SRC_PATH=$(ROOT_DIR) BIN=$(CURDIR)/$(BIN_DIR)/apps/$${APP} || exit 1 ; \
	done

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

.PHONY: test_python2
test_python2: distrib
	make -C $(ROOT_DIR)/python_bindings \
		-f $(ROOT_DIR)/python_bindings/Makefile \
		test \
		HALIDE_DISTRIB_PATH=$(CURDIR)/$(DISTRIB_DIR) \
		BIN=$(CURDIR)/$(BIN_DIR)/python2_bindings \
		PYTHON=python \
		PYBIND11_PATH=$(REAL_PYBIND11_PATH)

.PHONY: test_python
test_python: distrib
	make -C $(ROOT_DIR)/python_bindings \
		-f $(ROOT_DIR)/python_bindings/Makefile \
		test \
		HALIDE_DISTRIB_PATH=$(CURDIR)/$(DISTRIB_DIR) \
		BIN=$(CURDIR)/$(BIN_DIR)/python3_bindings \
		PYTHON=python3 \
		PYBIND11_PATH=$(REAL_PYBIND11_PATH)

# It's just for compiling the runtime, so earlier clangs *might* work,
# but best to peg it to the minimum llvm version.
ifneq (,$(findstring clang version 3.7,$(CLANG_VERSION)))
CLANG_OK=yes
endif

ifneq (,$(findstring clang version 3.8,$(CLANG_VERSION)))
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

ifneq (,$(findstring clang version 7.0,$(CLANG_VERSION)))
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

ifneq (,$(findstring $(LLVM_VERSION_TIMES_10), 40 50 60 70))
LLVM_OK=yes
endif

ifneq ($(LLVM_OK), )
$(BUILD_DIR)/llvm_ok: $(BUILD_DIR)/rtti_ok
	@echo "Found a new enough version of llvm"
ifeq ($(LLVM_VERSION_TIMES_10), 40)
	@echo
	@echo "*** Warning: LLVM 4.x is no longer actively tested with Halide; consider using a newer LLVM version. ***"
	@echo
endif
	mkdir -p $(BUILD_DIR)
	touch $(BUILD_DIR)/llvm_ok
else
$(BUILD_DIR)/llvm_ok:
	@echo "Can't find llvm or version of llvm too old (we need 4.0 or greater):"
	@echo "You can override this check by setting LLVM_OK=y"
	$(LLVM_CONFIG) --version
	@exit 1
endif

ifneq ($(WITH_RTTI), )
ifneq ($(LLVM_HAS_NO_RTTI), )
else
RTTI_OK=yes # Enabled in Halide and LLVM
endif
else
RTTI_OK=yes # Enabled in LLVM but not in Halide
endif

ifneq ($(RTTI_OK), )
$(BUILD_DIR)/rtti_ok:
	mkdir -p $(BUILD_DIR)
	touch $(BUILD_DIR)/rtti_ok
else
$(BUILD_DIR)/rtti_ok:
	@echo "Can't enable RTTI - llvm was compiled without it."
	@echo "LLVM c++ flags: " $(LLVM_CXX_FLAGS)
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

# This is a specialized 'install' for users who need Hexagon support libraries as well.
install_qc: install $(HEXAGON_RUNTIME_LIBS)
	mkdir -p $(PREFIX)/lib/arm-32-android $(PREFIX)/lib/arm-64-android $(PREFIX)/lib/host $(PREFIX)/lib/v60 $(PREFIX)/tools
	cp $(ROOT_DIR)/tools/sim_qurt/libsim_qurt.a $(PREFIX)/lib
	cp $(HEXAGON_RUNTIME_LIBS_DIR)/arm-32-android/* $(PREFIX)/lib/arm-32-android
	cp $(HEXAGON_RUNTIME_LIBS_DIR)/arm-64-android/* $(PREFIX)/lib/arm-64-android
	cp $(HEXAGON_RUNTIME_LIBS_DIR)/host/* $(PREFIX)/lib/host
	cp -r $(HEXAGON_RUNTIME_LIBS_DIR)/v60/* $(PREFIX)/lib/v60
	ln -sf $(PREFIX)/share/halide/tools/GenGen.cpp $(PREFIX)/tools/GenGen.cpp
	ln -sf $(PREFIX)/lib/v60/hexagon_sim_remote $(PREFIX)/bin/hexagon_sim_remote

# We need to capture the system libraries that we'll need to link
# against, so that downstream consumers of our build rules don't
# have to guess what's necessary on their system; call
# llvm-config and capture the result in config files that
# we include in our distribution.
$(BUILD_DIR)/halide_config.%: $(ROOT_DIR)/tools/halide_config.%.tpl
	@mkdir -p $(@D)
	cat $< | sed -e 's/@HALIDE_SYSTEM_LIBS_RAW@/${LLVM_SYSTEM_LIBS}/g' > $@

$(DISTRIB_DIR)/halide.tgz: $(LIB_DIR)/libHalide.a \
						   $(BIN_DIR)/libHalide.$(SHARED_EXT) \
						   $(INCLUDE_DIR)/Halide.h \
						   $(RUNTIME_EXPORTED_INCLUDES) \
						   $(ROOT_DIR)/README*.md \
						   $(ROOT_DIR)/bazel/* \
						   $(BUILD_DIR)/halide_config.bzl \
						   $(BUILD_DIR)/halide_config.cmake \
						   $(ROOT_DIR)/halide.cmake
	mkdir -p $(DISTRIB_DIR)/include \
	         $(DISTRIB_DIR)/bin \
	         $(DISTRIB_DIR)/lib \
	         $(DISTRIB_DIR)/tutorial \
	         $(DISTRIB_DIR)/tutorial/images \
	         $(DISTRIB_DIR)/tools \
	         $(DISTRIB_DIR)/tutorial/figures
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
	cp $(ROOT_DIR)/tools/halide_trace_config.h $(DISTRIB_DIR)/tools
	cp $(ROOT_DIR)/README*.md $(DISTRIB_DIR)
	cp $(ROOT_DIR)/bazel/BUILD $(DISTRIB_DIR)
	cp $(ROOT_DIR)/bazel/halide.bzl $(DISTRIB_DIR)
	cp $(ROOT_DIR)/bazel/README_bazel.md $(DISTRIB_DIR)
	cp $(ROOT_DIR)/bazel/WORKSPACE $(DISTRIB_DIR)
	cp $(BUILD_DIR)/halide_config.* $(DISTRIB_DIR)
	cp $(ROOT_DIR)/halide.cmake $(DISTRIB_DIR)
	ln -sf $(DISTRIB_DIR) halide
	tar -czf $(DISTRIB_DIR)/halide.tgz \
		halide/bin \
		halide/lib \
		halide/include \
		halide/tutorial \
		halide/BUILD \
		halide/README*.md \
		halide/README_bazel.md \
		halide/WORKSPACE \
		halide/*.bzl \
		halide/*.cmake \
		halide/tools/mex_halide.m \
		halide/tools/*.cpp \
		halide/tools/halide_benchmark.h \
		halide/tools/halide_image.h \
		halide/tools/halide_image_io.h \
		halide/tools/halide_image_info.h \
		halide/tools/halide_trace_config.h
	rm -rf halide

.PHONY: distrib
distrib: $(DISTRIB_DIR)/halide.tgz

$(BIN_DIR)/HalideTraceViz: $(ROOT_DIR)/util/HalideTraceViz.cpp $(INCLUDE_DIR)/HalideRuntime.h $(ROOT_DIR)/tools/halide_image_io.h $(ROOT_DIR)/tools/halide_trace_config.h
	$(CXX) $(OPTIMIZE) -std=c++11 $(filter %.cpp,$^) -I$(INCLUDE_DIR) -I$(ROOT_DIR)/tools -L$(BIN_DIR) -o $@

$(BIN_DIR)/HalideTraceDump: $(ROOT_DIR)/util/HalideTraceDump.cpp $(ROOT_DIR)/util/HalideTraceUtils.cpp $(INCLUDE_DIR)/HalideRuntime.h $(ROOT_DIR)/tools/halide_image_io.h
	$(CXX) $(OPTIMIZE) -std=c++11 $(filter %.cpp,$^) -I$(INCLUDE_DIR) -I$(ROOT_DIR)/tools -I$(ROOT_DIR)/src/runtime -L$(BIN_DIR) $(IMAGE_IO_CXX_FLAGS) $(IMAGE_IO_LIBS) -o $@
