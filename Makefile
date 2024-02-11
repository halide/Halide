# 'make' builds libHalide.a, the internal test suite, and runs the internal test suite
# 'make run_tests' builds and runs all the end-to-end tests in the test subdirectory
# 'make {error,performance}_foo' builds and runs test/{...}/foo.cpp for any
#     c_source file in the corresponding subdirectory of the test folder
# 'make correctness_foo' builds and runs test/correctness/foo.cpp for any
#     c_source file in the correctness/ subdirectory of the test folder
# 'make test_apps' checks some of the apps build and run (but does not check their output)
# 'make time_compilation_tests' records the compile time for each test module into a csv file.
#     For correctness and performance tests this include halide build time and run time. For
#     the tests in test/generator/ this times only the halide build time.

# Disable built-in makefile rules for all apps to avoid pointless file-system
# scanning and general weirdness resulting from implicit rules.
MAKEFLAGS += --no-builtin-rules
.SUFFIXES:

UNAME = $(shell uname)

ifeq ($(OS), Windows_NT)
    $(error Halide no longer supports the MinGW environment. Please use MSVC through CMake instead.)
else
    # let's assume "normal" UNIX such as linux
    COMMON_LD_FLAGS=$(LDFLAGS) -ldl -lpthread -lz
    FPIC=-fPIC
ifeq ($(UNAME), Darwin)
    SHARED_EXT=dylib
else
    SHARED_EXT=so
endif
endif

# We want to build Halide plugins as .so on all posixy systems, including OSX.
# This is called out as a named var to make it clear that the use
# is deliberate, not an accident.
PLUGIN_EXT=so

ifeq ($(UNAME), Darwin)
  # Anything that we us install_name_tool on needs these linker flags
  # to ensure there is enough padding for install_name_tool to use
  INSTALL_NAME_TOOL_LD_FLAGS=-Wl,-headerpad_max_install_names
else
  INSTALL_NAME_TOOL_LD_FLAGS=
endif

ifeq ($(UNAME), Darwin)
define alwayslink
	-Wl,-force_load,$(1)
endef
else
define alwayslink
	-Wl,--whole-archive $(1) -Wl,-no-whole-archive
endef
endif

SHELL = bash
CXX ?= g++
PREFIX ?= /usr/local
LLVM_CONFIG ?= llvm-config
LLVM_COMPONENTS= $(shell $(LLVM_CONFIG) --components)
LLVM_VERSION = $(shell $(LLVM_CONFIG) --version | sed 's/\([0-9][0-9]*\)\.\([0-9]\).*/\1.\2/')

LLVM_FULL_VERSION = $(shell $(LLVM_CONFIG) --version)
LLVM_BINDIR = $(shell $(LLVM_CONFIG) --bindir | sed -e 's/\\/\//g' -e 's/\([a-zA-Z]\):/\/\1/g')
LLVM_LIBDIR = $(shell $(LLVM_CONFIG) --libdir | sed -e 's/\\/\//g' -e 's/\([a-zA-Z]\):/\/\1/g')
# Apparently there is no llvm_config flag to get canonical paths to tools,
# so we'll just construct one relative to --src-root and hope that is stable everywhere.
LLVM_SYSTEM_LIBS=$(shell ${LLVM_CONFIG} --system-libs --link-static | sed -e 's/[\/&]/\\&/g' | sed 's/-llibxml2.tbd/-lxml2/')
LLVM_AS = $(LLVM_BINDIR)/llvm-as
LLVM_NM = $(LLVM_BINDIR)/llvm-nm
# Note, removing -D_GLIBCXX_ASSERTIONS is a workaround for https://reviews.llvm.org/D142279
LLVM_CXX_FLAGS = -std=c++17  $(filter-out -O% -g -fomit-frame-pointer -pedantic -W% -W, $(shell $(LLVM_CONFIG) --cxxflags | sed -e 's/ -D_GLIBCXX_ASSERTIONS / /g' -e 's/\\/\//g' -e 's/\([a-zA-Z]\):/\/\1/g;s/-D/ -D/g;s/-O/ -O/;s/c++14/c++17/g'))
OPTIMIZE ?= -O3
OPTIMIZE_FOR_BUILD_TIME ?= -O0

CLANG ?= $(LLVM_BINDIR)/clang
CLANG_VERSION = $(shell $(CLANG) --version)

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

LLVM_VERSION_TIMES_10 = $(shell $(LLVM_CONFIG) --version | sed 's/\([0-9][0-9]*\)\.\([0-9]\).*/\1\2/')

LLVM_CXX_FLAGS += -DLLVM_VERSION=$(LLVM_VERSION_TIMES_10)

# All WITH_* flags are either empty or not-empty. They do not behave
# like true/false values in most languages.  To turn one off, either
# edit this file, add "WITH_FOO=" (no assigned value) to the make
# line, or define an environment variable WITH_FOO that has an empty
# value.
WITH_X86 ?= $(findstring x86, $(LLVM_COMPONENTS))
WITH_ARM ?= $(findstring arm, $(LLVM_COMPONENTS))
WITH_HEXAGON ?= $(findstring hexagon, $(LLVM_COMPONENTS))
ifeq ($(shell test $(LLVM_VERSION_TIMES_10) -ge 170; echo $$?),0)
WITH_RISCV ?= $(findstring riscv, $(LLVM_COMPONENTS))
else
# leave WITH_RISCV undefined
endif
WITH_AARCH64 ?= $(findstring aarch64, $(LLVM_COMPONENTS))
WITH_POWERPC ?= $(findstring powerpc, $(LLVM_COMPONENTS))
WITH_NVPTX ?= $(findstring nvptx, $(LLVM_COMPONENTS))
WITH_WEBASSEMBLY ?= $(findstring webassembly, $(LLVM_COMPONENTS))
# AMDGPU target is WIP
WITH_AMDGPU ?= $(findstring amdgpu, $(LLVM_COMPONENTS))
WITH_OPENCL ?= not-empty
WITH_METAL ?= not-empty
WITH_D3D12 ?= not-empty
WITH_VULKAN ?= not-empty
WITH_SPIRV ?= not-empty
WITH_WEBGPU ?= not-empty
WITH_INTROSPECTION ?= not-empty
WITH_EXCEPTIONS ?=
WITH_LLVM_INSIDE_SHARED_LIBHALIDE ?= not-empty


# If HL_TARGET or HL_JIT_TARGET aren't set, use host
HL_TARGET ?= host
HL_JIT_TARGET ?= host

X86_CXX_FLAGS=$(if $(WITH_X86), -DWITH_X86, )
X86_LLVM_CONFIG_LIB=$(if $(WITH_X86), x86, )

ARM_CXX_FLAGS=$(if $(WITH_ARM), -DWITH_ARM, )
ARM_LLVM_CONFIG_LIB=$(if $(WITH_ARM), arm, )

POWERPC_CXX_FLAGS=$(if $(WITH_POWERPC), -DWITH_POWERPC, )
POWERPC_LLVM_CONFIG_LIB=$(if $(WITH_POWERPC), powerpc, )

PTX_CXX_FLAGS=$(if $(WITH_NVPTX), -DWITH_NVPTX, )
PTX_LLVM_CONFIG_LIB=$(if $(WITH_NVPTX), nvptx, )
PTX_DEVICE_INITIAL_MODULES=$(if $(WITH_NVPTX), libdevice.compute_20.10.bc libdevice.compute_30.10.bc libdevice.compute_35.10.bc, )

AMDGPU_CXX_FLAGS=$(if $(WITH_AMDGPU), -DWITH_AMDGPU, )
AMDGPU_LLVM_CONFIG_LIB=$(if $(WITH_AMDGPU), amdgpu, )
# TODO add bitcode files

OPENCL_CXX_FLAGS=$(if $(WITH_OPENCL), -DWITH_OPENCL, )
OPENCL_LLVM_CONFIG_LIB=$(if $(WITH_OPENCL), , )

METAL_CXX_FLAGS=$(if $(WITH_METAL), -DWITH_METAL, )
METAL_LLVM_CONFIG_LIB=$(if $(WITH_METAL), , )

D3D12_CXX_FLAGS=$(if $(WITH_D3D12), -DWITH_D3D12, )
D3D12_LLVM_CONFIG_LIB=$(if $(WITH_D3D12), , )

WEBGPU_CXX_FLAGS=$(if $(WITH_WEBGPU), -DWITH_WEBGPU, )

AARCH64_CXX_FLAGS=$(if $(WITH_AARCH64), -DWITH_AARCH64, )
AARCH64_LLVM_CONFIG_LIB=$(if $(WITH_AARCH64), aarch64, )

RISCV_CXX_FLAGS=$(if $(WITH_RISCV), -DWITH_RISCV, )
RISCV_LLVM_CONFIG_LIB=$(if $(WITH_RISCV), riscv, )

INTROSPECTION_CXX_FLAGS=$(if $(WITH_INTROSPECTION), -DWITH_INTROSPECTION, )
EXCEPTIONS_CXX_FLAGS=$(if $(WITH_EXCEPTIONS), -DHALIDE_WITH_EXCEPTIONS -fexceptions, )

HEXAGON_CXX_FLAGS=$(if $(WITH_HEXAGON), -DWITH_HEXAGON, )
HEXAGON_LLVM_CONFIG_LIB=$(if $(WITH_HEXAGON), hexagon, )

SPIRV_CXX_FLAGS=$(if $(WITH_SPIRV), -DWITH_SPIRV -isystem $(ROOT_DIR)/dependencies/spirv/include, )
SPIRV_LLVM_CONFIG_LIB=$(if $(WITH_SPIRV), , )

VULKAN_CXX_FLAGS=$(if $(WITH_VULKAN), -DWITH_VULKAN, )
VULKAN_LLVM_CONFIG_LIB=$(if $(WITH_VULKAN), , )

WEBASSEMBLY_CXX_FLAGS=$(if $(WITH_WEBASSEMBLY), -DWITH_WEBASSEMBLY, )
WEBASSEMBLY_LLVM_CONFIG_LIB=$(if $(WITH_WEBASSEMBLY), webassembly, )

LLVM_HAS_NO_RTTI = $(findstring -fno-rtti, $(LLVM_CXX_FLAGS))
WITH_RTTI ?= $(if $(LLVM_HAS_NO_RTTI),, not-empty)
RTTI_CXX_FLAGS=$(if $(WITH_RTTI), , -fno-rtti )

CXX_VERSION = $(shell $(CXX) --version | head -n1)
CXX_WARNING_FLAGS = -Wall -Werror -Wno-unused-function -Wcast-qual -Wignored-qualifiers -Wno-comment -Wsign-compare -Wno-unknown-warning-option -Wno-psabi -Wno-mismatched-new-delete
ifneq (,$(findstring g++,$(CXX_VERSION)))
GCC_MAJOR_VERSION := $(shell $(CXX) -dumpfullversion -dumpversion | cut -f1 -d.)
GCC_MINOR_VERSION := $(shell $(CXX) -dumpfullversion -dumpversion | cut -f2 -d.)
ifeq (1,$(shell expr $(GCC_MAJOR_VERSION) \> 5 \| $(GCC_MAJOR_VERSION) = 5 \& $(GCC_MINOR_VERSION) \>= 1))
CXX_WARNING_FLAGS += -Wsuggest-override
endif
endif

ifneq (,$(findstring clang,$(CXX_VERSION)))
LLVM_CXX_FLAGS_LIBCPP := $(findstring -stdlib=libc++, $(LLVM_CXX_FLAGS))
endif

CXX_FLAGS = $(CXXFLAGS) $(CXX_WARNING_FLAGS) $(RTTI_CXX_FLAGS) -Woverloaded-virtual $(FPIC) $(OPTIMIZE) -fno-omit-frame-pointer -DCOMPILING_HALIDE
CXX_FLAGS += $(LLVM_CXX_FLAGS)
CXX_FLAGS += $(PTX_CXX_FLAGS)
CXX_FLAGS += $(ARM_CXX_FLAGS)
CXX_FLAGS += $(HEXAGON_CXX_FLAGS)
CXX_FLAGS += $(AARCH64_CXX_FLAGS)
CXX_FLAGS += $(X86_CXX_FLAGS)
CXX_FLAGS += $(OPENCL_CXX_FLAGS)
CXX_FLAGS += $(METAL_CXX_FLAGS)
CXX_FLAGS += $(D3D12_CXX_FLAGS)
CXX_FLAGS += $(WEBGPU_CXX_FLAGS)
CXX_FLAGS += $(POWERPC_CXX_FLAGS)
CXX_FLAGS += $(INTROSPECTION_CXX_FLAGS)
CXX_FLAGS += $(EXCEPTIONS_CXX_FLAGS)
CXX_FLAGS += $(AMDGPU_CXX_FLAGS)
CXX_FLAGS += $(RISCV_CXX_FLAGS)
CXX_FLAGS += $(SPIRV_CXX_FLAGS)
CXX_FLAGS += $(VULKAN_CXX_FLAGS)
CXX_FLAGS += $(WEBASSEMBLY_CXX_FLAGS)

# Serialization requires flatc and flatbuffers.h
# On ubuntu, this requires packages flatbuffers-compiler and libflatbuffers-dev
ifneq (,$(shell which flatc))
CXX_FLAGS += -DWITH_SERIALIZATION -I $(BUILD_DIR) -I $(shell which flatc | sed 's/bin.flatc/include/')
# Note: if updating here, be sure to update in CMakeLists.txt as well
HALIDE_SERIALIZATION_VERSION_MINOR ?= 1
HALIDE_SERIALIZATION_VERSION_PATCH ?= 0
endif

# This is required on some hosts like powerpc64le-linux-gnu because we may build
# everything with -fno-exceptions.  Without -funwind-tables, libHalide.so fails
# to propagate exceptions and causes a test failure.
CXX_FLAGS += -funwind-tables

print-%:
	@echo '$*=$($*)'

LLVM_STATIC_LIBFILES = \
	bitwriter \
	bitreader \
	linker \
	ipo \
	passes \
	orcjit \
	$(X86_LLVM_CONFIG_LIB) \
	$(ARM_LLVM_CONFIG_LIB) \
	$(OPENCL_LLVM_CONFIG_LIB) \
	$(METAL_LLVM_CONFIG_LIB) \
	$(PTX_LLVM_CONFIG_LIB) \
	$(AARCH64_LLVM_CONFIG_LIB) \
	$(POWERPC_LLVM_CONFIG_LIB) \
	$(HEXAGON_LLVM_CONFIG_LIB) \
	$(AMDGPU_LLVM_CONFIG_LIB) \
	$(SPIRV_LLVM_CONFIG_LIB) \
	$(VULKAN_LLVM_CONFIG_LIB) \
	$(WEBASSEMBLY_LLVM_CONFIG_LIB) \
	$(RISCV_LLVM_CONFIG_LIB)

LLVM_STATIC_LIBS = -L $(LLVM_LIBDIR) $(shell $(LLVM_CONFIG) --link-static --libfiles $(LLVM_STATIC_LIBFILES) | sed -e 's/\\/\//g' -e 's/\([a-zA-Z]\):/\/\1/g')

# Add a rpath to the llvm used for linking, in case multiple llvms are
# installed. Bakes a path on the build system into the .so, so don't
# use this config for distributions.
LLVM_SHARED_LIBS = -Wl,-rpath=$(LLVM_LIBDIR) -L $(LLVM_LIBDIR) -lLLVM

LLVM_LIBS_FOR_SHARED_LIBHALIDE=$(if $(WITH_LLVM_INSIDE_SHARED_LIBHALIDE),$(LLVM_STATIC_LIBS),$(LLVM_SHARED_LIBS))

TUTORIAL_CXX_FLAGS ?= -std=c++17 -g -fno-omit-frame-pointer $(RTTI_CXX_FLAGS) -I $(ROOT_DIR)/tools $(SANITIZER_FLAGS) $(LLVM_CXX_FLAGS_LIBCPP) $(EXCEPTIONS_CXX_FLAGS)
# The tutorials contain example code with warnings that we don't want
# to be flagged as errors, so the test flags are the tutorial flags
# plus our warning flags.
# Also allow tests, via conditional compilation, to use the entire
# capability of the CPU being compiled on via -march=native. This
# presumes tests are run on the same machine they are compiled on.
TEST_CXX_FLAGS ?= $(TUTORIAL_CXX_FLAGS) $(CXX_WARNING_FLAGS)
TEST_LD_FLAGS = -L$(BIN_DIR) -lHalide $(COMMON_LD_FLAGS)

# In the tests, some of our expectations change depending on the llvm version
TEST_CXX_FLAGS += -DLLVM_VERSION=$(LLVM_VERSION_TIMES_10)

# In the tests, default to exporting no symbols that aren't explicitly exported
TEST_CXX_FLAGS += -fvisibility=hidden -fvisibility-inlines-hidden

# gcc 4.8 fires a bogus warning on old versions of png.h
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

ifneq ($(WITH_NVPTX), )
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

ifneq ($(WITH_VULKAN), )
ifneq (,$(findstring vulkan,$(HL_TARGET)))
TEST_VULKAN = 1
endif
endif

ifeq ($(UNAME), Linux)
ifneq ($(TEST_CUDA), )
CUDA_LD_FLAGS ?= -L/usr/lib/nvidia-current -lcuda
endif
ifneq ($(TEST_OPENCL), )
OPENCL_LD_FLAGS ?= -lOpenCL
endif
ifneq ($(TEST_VULKAN), )
VULKAN_LD_FLAGS ?= -lvulkan
endif
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
ifneq ($(TEST_VULKAN), )
# The Vulkan loader is distributed as a dylib on OSX (not a framework)
VULKAN_LD_FLAGS ?= -lvulkan
endif
ifneq ($(TEST_METAL), )
METAL_LD_FLAGS ?= -framework Metal -framework Foundation
endif
HOST_OS=os_x
endif

ifneq ($(TEST_OPENCL), )
TEST_CXX_FLAGS += -DTEST_OPENCL
endif

ifneq ($(TEST_VULKAN), )
TEST_CXX_FLAGS += -DTEST_VULKAN
endif

ifneq ($(TEST_METAL), )
# Using Metal APIs requires writing Objective-C++ (or Swift). Add ObjC++
# to allow tests to create and destroy Metal contexts, etc. This requires
# tests to be valid Objective-C++, e.g. avoiding using the identifier "id"
# in certain ways. In practice this is not enough of a problem to justify
# the work to limit which files are compiled this way.
TEST_CXX_FLAGS += -DTEST_METAL -ObjC++ -Werror,-Wunused-command-line-argument
endif

ifneq ($(TEST_CUDA), )
TEST_CXX_FLAGS += -DTEST_CUDA
TEST_CXX_FLAGS += -I/usr/local/cuda/include
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

TARGET=$(if $(HL_TARGET),$(HL_TARGET),host)

# The following directories are all relative to the output directory (i.e. $(CURDIR), not $(SRC_DIR))
LIB_DIR     = lib
BIN_DIR     = bin
DISTRIB_DIR = distrib
INCLUDE_DIR = include
SHARE_DIR   = share
DOC_DIR     = $(SHARE_DIR)/doc/Halide
BUILD_DIR   = $(BIN_DIR)/build
FILTERS_DIR = $(BIN_DIR)/$(TARGET)/build
TMP_DIR     = $(BUILD_DIR)/tmp
HEXAGON_RUNTIME_LIBS_DIR = src/runtime/hexagon_remote/bin
HEXAGON_RUNTIME_LIBS = \
  $(HEXAGON_RUNTIME_LIBS_DIR)/arm-32-android/libhalide_hexagon_host.so \
  $(HEXAGON_RUNTIME_LIBS_DIR)/arm-64-android/libhalide_hexagon_host.so \
  $(HEXAGON_RUNTIME_LIBS_DIR)/host/libhalide_hexagon_host.so \
  $(HEXAGON_RUNTIME_LIBS_DIR)/v65/hexagon_sim_remote \
  $(HEXAGON_RUNTIME_LIBS_DIR)/v65/libhalide_hexagon_remote_skel.so \
  $(HEXAGON_RUNTIME_LIBS_DIR)/v65/signed_by_debug/libhalide_hexagon_remote_skel.so

# Keep this list sorted in alphabetical order.
SOURCE_FILES = \
  AbstractGenerator.cpp \
  AddAtomicMutex.cpp \
  AddImageChecks.cpp \
  AddParameterChecks.cpp \
  AddSplitFactorChecks.cpp \
  AlignLoads.cpp \
  AllocationBoundsInference.cpp \
  ApplySplit.cpp \
  Argument.cpp \
  AssociativeOpsTable.cpp \
  Associativity.cpp \
  AsyncProducers.cpp \
  AutoScheduleUtils.cpp \
  BoundaryConditions.cpp \
  Bounds.cpp \
  BoundsInference.cpp \
  BoundConstantExtentLoops.cpp \
  BoundSmallAllocations.cpp \
  Buffer.cpp \
  Callable.cpp \
  CanonicalizeGPUVars.cpp \
  Closure.cpp \
  ClampUnsafeAccesses.cpp \
  CodeGen_ARM.cpp \
  CodeGen_C.cpp \
  CodeGen_D3D12Compute_Dev.cpp \
  CodeGen_GPU_Dev.cpp \
  CodeGen_Hexagon.cpp \
  CodeGen_Internal.cpp \
  CodeGen_LLVM.cpp \
  CodeGen_Metal_Dev.cpp \
  CodeGen_OpenCL_Dev.cpp \
  CodeGen_Vulkan_Dev.cpp \
  CodeGen_Posix.cpp \
  CodeGen_PowerPC.cpp \
  CodeGen_PTX_Dev.cpp \
  CodeGen_PyTorch.cpp \
  CodeGen_RISCV.cpp \
  CodeGen_WebAssembly.cpp \
  CodeGen_WebGPU_Dev.cpp \
  CodeGen_X86.cpp \
  CompilerLogger.cpp \
  CPlusPlusMangle.cpp \
  CSE.cpp \
  Debug.cpp \
  DebugArguments.cpp \
  DebugToFile.cpp \
  Definition.cpp \
  Deinterleave.cpp \
  Derivative.cpp \
  DerivativeUtils.cpp \
  Deserialization.cpp \
  DeviceArgument.cpp \
  DeviceInterface.cpp \
  Dimension.cpp \
  DistributeShifts.cpp \
  EarlyFree.cpp \
  Elf.cpp \
  EliminateBoolVectors.cpp \
  EmulateFloat16Math.cpp \
  Error.cpp \
  Expr.cpp \
  ExtractTileOperations.cpp \
  FastIntegerDivide.cpp \
  FindCalls.cpp \
  FindIntrinsics.cpp \
  FlattenNestedRamps.cpp \
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
  Lambda.cpp \
  Lerp.cpp \
  LICM.cpp \
  LLVM_Output.cpp \
  LLVM_Runtime_Linker.cpp \
  LoopCarry.cpp \
  Lower.cpp \
  LowerParallelTasks.cpp \
  LowerWarpShuffles.cpp \
  Memoization.cpp \
  Module.cpp \
  ModulusRemainder.cpp \
  Monotonic.cpp \
  ObjectInstanceRegistry.cpp \
  OffloadGPULoops.cpp \
  OptimizeShuffles.cpp \
  OutputImageParam.cpp \
  ParallelRVar.cpp \
  Parameter.cpp \
  PartitionLoops.cpp \
  Pipeline.cpp \
  Prefetch.cpp \
  PrintLoopNest.cpp \
  Profiling.cpp \
  PurifyIndexMath.cpp \
  PythonExtensionGen.cpp \
  Qualify.cpp \
  Random.cpp \
  RDom.cpp \
  Realization.cpp \
  RealizationOrder.cpp \
  RebaseLoopsToZero.cpp \
  Reduction.cpp \
  RegionCosts.cpp \
  RemoveDeadAllocations.cpp \
  RemoveExternLoops.cpp \
  RemoveUndef.cpp \
  Schedule.cpp \
  ScheduleFunctions.cpp \
  SelectGPUAPI.cpp \
  Serialization.cpp \
  Simplify.cpp \
  Simplify_Add.cpp \
  Simplify_And.cpp \
  Simplify_Call.cpp \
  Simplify_Cast.cpp \
  Simplify_Reinterpret.cpp \
  Simplify_Div.cpp \
  Simplify_EQ.cpp \
  Simplify_Exprs.cpp \
  Simplify_Let.cpp \
  Simplify_LT.cpp \
  Simplify_Max.cpp \
  Simplify_Min.cpp \
  Simplify_Mod.cpp \
  Simplify_Mul.cpp \
  Simplify_Not.cpp \
  Simplify_Or.cpp \
  Simplify_Select.cpp \
  Simplify_Shuffle.cpp \
  Simplify_Stmts.cpp \
  Simplify_Sub.cpp \
  SimplifyCorrelatedDifferences.cpp \
  SimplifySpecializations.cpp \
  SkipStages.cpp \
  SlidingWindow.cpp \
  Solve.cpp \
  SpirvIR.cpp \
  SplitTuples.cpp \
  StageStridedLoads.cpp \
  StmtToHTML.cpp \
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
  UnsafePromises.cpp \
  Util.cpp \
  Var.cpp \
  VectorizeLoops.cpp \
  WasmExecutor.cpp \
  WrapCalls.cpp

 C_TEMPLATE_FILES = \
   CodeGen_C_prologue \
   CodeGen_C_vectors

HTML_TEMPLATE_FILES = \
   StmtToHTML_dependencies.html \
   StmtToHTML.js \
   StmtToHTML.css

# The externally-visible header files that go into making Halide.h.
# Don't include anything here that includes llvm headers.
# Also *don't* include anything that's only used internally (eg SpirvIR.h).
# Keep this list sorted in alphabetical order.
HEADER_FILES = \
  AbstractGenerator.h \
  AddAtomicMutex.h \
  AddImageChecks.h \
  AddParameterChecks.h \
  AddSplitFactorChecks.h \
  AlignLoads.h \
  AllocationBoundsInference.h \
  ApplySplit.h \
  Argument.h \
  AssociativeOpsTable.h \
  Associativity.h \
  AsyncProducers.h \
  AutoScheduleUtils.h \
  BoundaryConditions.h \
  Bounds.h \
  BoundsInference.h \
  BoundConstantExtentLoops.h \
  BoundSmallAllocations.h \
  Buffer.h \
  Callable.h \
  CanonicalizeGPUVars.h \
  ClampUnsafeAccesses.h \
  Closure.h \
  CodeGen_C.h \
  CodeGen_D3D12Compute_Dev.h \
  CodeGen_GPU_Dev.h \
  CodeGen_Internal.h \
  CodeGen_LLVM.h \
  CodeGen_Metal_Dev.h \
  CodeGen_OpenCL_Dev.h \
  CodeGen_Vulkan_Dev.h \
  CodeGen_Posix.h \
  CodeGen_PTX_Dev.h \
  CodeGen_PyTorch.h \
  CodeGen_Targets.h \
  CodeGen_WebGPU_Dev.h \
  CompilerLogger.h \
  ConciseCasts.h \
  CPlusPlusMangle.h \
  CSE.h \
  Debug.h \
  DebugArguments.h \
  DebugToFile.h \
  Definition.h \
  Deinterleave.h \
  Derivative.h \
  DerivativeUtils.h \
  Deserialization.h \
  DeviceAPI.h \
  DeviceArgument.h \
  DeviceInterface.h \
  Dimension.h \
  DistributeShifts.h \
  EarlyFree.h \
  Elf.h \
  EliminateBoolVectors.h \
  EmulateFloat16Math.h \
  Error.h \
  Expr.h \
  ExprUsesVar.h \
  Extern.h \
  ExternFuncArgument.h \
  ExtractTileOperations.h \
  FastIntegerDivide.h \
  FindCalls.h \
  FindIntrinsics.h \
  FlattenNestedRamps.h \
  Float16.h \
  Func.h \
  Function.h \
  FunctionPtr.h \
  FuseGPUThreadLoops.h \
  FuzzFloatStores.h \
  Generator.h \
  HexagonOffload.h \
  HexagonOptimize.h \
  ImageParam.h \
  InferArguments.h \
  InjectHostDevBufferCopies.h \
  Inline.h \
  InlineReductions.h \
  IntegerDivisionTable.h \
  Interval.h \
  Introspection.h \
  IntrusivePtr.h \
  IR.h \
  IREquality.h \
  IRMatch.h \
  IRMutator.h \
  IROperator.h \
  IRPrinter.h \
  IRVisitor.h \
  WasmExecutor.h \
  JITModule.h \
  Lambda.h \
  Lerp.h \
  LICM.h \
  LLVM_Output.h \
  LLVM_Runtime_Linker.h \
  LoopCarry.h \
  Lower.h \
  LowerParallelTasks.h \
  LowerWarpShuffles.h \
  MainPage.h \
  Memoization.h \
  Module.h \
  ModulusRemainder.h \
  Monotonic.h \
  ObjectInstanceRegistry.h \
  OffloadGPULoops.h \
  OptimizeShuffles.h \
  OutputImageParam.h \
  ParallelRVar.h \
  Param.h \
  Parameter.h \
  PartitionLoops.h \
  Pipeline.h \
  Prefetch.h \
  Profiling.h \
  PurifyIndexMath.h \
  PythonExtensionGen.h \
  Qualify.h \
  Random.h \
  Realization.h \
  RDom.h \
  RealizationOrder.h \
  RebaseLoopsToZero.h \
  Reduction.h \
  RegionCosts.h \
  RemoveDeadAllocations.h \
  RemoveExternLoops.h \
  RemoveUndef.h \
  runtime/HalideBuffer.h \
  runtime/HalideRuntime.h \
  Schedule.h \
  ScheduleFunctions.h \
  Scope.h \
  SelectGPUAPI.h \
  Serialization.h \
  Simplify.h \
  SimplifyCorrelatedDifferences.h \
  SimplifySpecializations.h \
  SkipStages.h \
  SlidingWindow.h \
  Solve.h \
  SplitTuples.h \
  StageStridedLoads.h \
  StmtToHTML.h \
  StorageFlattening.h \
  StorageFolding.h \
  StrictifyFloat.h \
  Substitute.h \
  Target.h \
  Tracing.h \
  TrimNoOps.h \
  Tuple.h \
  Type.h \
  UnifyDuplicateLets.h \
  UniquifyVariableNames.h \
  UnpackBuffers.h \
  UnrollLoops.h \
  UnsafePromises.h \
  Util.h \
  Var.h \
  VectorizeLoops.h \
  WrapCalls.h

OBJECTS = $(SOURCE_FILES:%.cpp=$(BUILD_DIR)/%.o)
HEADERS = $(HEADER_FILES:%.h=$(SRC_DIR)/%.h)

RUNTIME_CPP_COMPONENTS = \
  aarch64_cpu_features \
  alignment_128 \
  alignment_32 \
  alignment_64 \
  allocation_cache \
  android_clock \
  android_host_cpu_count \
  android_io \
  arm_cpu_features \
  cache \
  can_use_target \
  cuda \
  destructors \
  device_interface \
  errors \
  fake_get_symbol \
  fake_thread_pool \
  float16_t \
  fopen \
  fopen_lfs \
  force_include_types \
  fuchsia_clock \
  fuchsia_host_cpu_count \
  fuchsia_yield \
  gpu_device_selection \
  halide_buffer_t \
  hexagon_cache_allocator \
  hexagon_cpu_features \
  hexagon_dma \
  hexagon_dma_pool \
  hexagon_host \
  ios_io \
  linux_clock \
  linux_host_cpu_count \
  linux_yield \
  metal \
  metal_objc_arm \
  metal_objc_x86 \
  module_aot_ref_count \
  module_jit_ref_count \
  msan \
  msan_stubs \
  opencl \
  osx_clock \
  osx_get_symbol \
  osx_host_cpu_count \
  osx_yield \
  posix_aligned_alloc \
  posix_allocator \
  posix_clock \
  posix_error_handler \
  posix_get_symbol \
  posix_io \
  posix_print \
  posix_threads \
  posix_threads_tsan \
  posix_timer_profiler \
  powerpc_cpu_features \
  prefetch \
  profiler \
  profiler_inlined \
  pseudostack \
  qurt_allocator \
  qurt_hvx \
  qurt_hvx_vtcm \
  qurt_threads \
  qurt_threads_tsan \
  qurt_yield \
  riscv_cpu_features \
  runtime_api \
  timer_profiler \
  to_string \
  trace_helper \
  tracing \
  wasm_cpu_features \
  webgpu_dawn \
  webgpu_emscripten \
  windows_clock \
  windows_cuda \
  windows_d3d12compute_arm \
  windows_d3d12compute_x86 \
  windows_get_symbol \
  windows_io \
  windows_opencl \
  windows_profiler \
  windows_threads \
  windows_threads_tsan \
  windows_vulkan \
  windows_yield \
  write_debug_image \
  vulkan \
  x86_cpu_features \

RUNTIME_LL_COMPONENTS = \
  aarch64 \
  arm \
  arm_no_neon \
  hvx_128 \
  posix_math \
  powerpc \
  ptx_dev \
  wasm_math \
  win32_math \
  x86 \
  x86_amx \
  x86_avx \
  x86_avx2 \
  x86_avx512 \
  x86_sse41

RUNTIME_EXPORTED_INCLUDES = $(INCLUDE_DIR)/HalideRuntime.h \
                            $(INCLUDE_DIR)/HalideRuntimeD3D12Compute.h \
                            $(INCLUDE_DIR)/HalideRuntimeCuda.h \
                            $(INCLUDE_DIR)/HalideRuntimeHexagonDma.h \
                            $(INCLUDE_DIR)/HalideRuntimeHexagonHost.h \
                            $(INCLUDE_DIR)/HalideRuntimeOpenCL.h \
                            $(INCLUDE_DIR)/HalideRuntimeMetal.h	\
                            $(INCLUDE_DIR)/HalideRuntimeQurt.h \
                            $(INCLUDE_DIR)/HalideRuntimeVulkan.h \
                            $(INCLUDE_DIR)/HalideRuntimeWebGPU.h \
                            $(INCLUDE_DIR)/HalideBuffer.h \
                            $(INCLUDE_DIR)/HalidePyTorchHelpers.h \
                            $(INCLUDE_DIR)/HalidePyTorchCudaHelpers.h

INITIAL_MODULES = $(RUNTIME_CPP_COMPONENTS:%=$(BUILD_DIR)/initmod.%_32.o) \
                  $(RUNTIME_CPP_COMPONENTS:%=$(BUILD_DIR)/initmod.%_64.o) \
                  $(RUNTIME_CPP_COMPONENTS:%=$(BUILD_DIR)/initmod.%_32_debug.o) \
                  $(RUNTIME_CPP_COMPONENTS:%=$(BUILD_DIR)/initmod.%_64_debug.o) \
                  $(RUNTIME_EXPORTED_INCLUDES:$(INCLUDE_DIR)/%.h=$(BUILD_DIR)/initmod.%_h.o) \
                  $(C_TEMPLATE_FILES:%=$(BUILD_DIR)/c_template.%.o) \
                  $(HTML_TEMPLATE_FILES:%=$(BUILD_DIR)/html_template.%.o) \
                  $(BUILD_DIR)/initmod.inlined_c.o \
                  $(RUNTIME_LL_COMPONENTS:%=$(BUILD_DIR)/initmod.%_ll.o) \
                  $(PTX_DEVICE_INITIAL_MODULES:libdevice.%.bc=$(BUILD_DIR)/initmod_ptx.%_ll.o)

TEST_DEPS = $(BIN_DIR)/libHalide.$(SHARED_EXT) $(INCLUDE_DIR)/Halide.h $(RUNTIME_EXPORTED_INCLUDES)
ifneq (,$(WITH_EXCEPTIONS))
# The tests will link libHalide, but also the object file that
# installs a global exception handler.
TEST_DEPS += $(BUILD_DIR)/terminate_handler.o
TEST_LD_FLAGS += $(BUILD_DIR)/terminate_handler.o
endif

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
all: distrib test_internal

# Depending on which linker we're using,
# we need a different invocation to get the
# linker map file.
ifeq ($(UNAME), Darwin)
    MAP_FLAGS= -Wl,-map -Wl,$(BUILD_DIR)/llvm_objects/list.all
else
    MAP_FLAGS= -Wl,-Map=$(BUILD_DIR)/llvm_objects/list.all
endif

$(BUILD_DIR)/llvm_objects/list: $(OBJECTS) $(INITIAL_MODULES)
	# Determine the relevant object files from llvm with a dummy
	# compilation. Passing -map to the linker gets it to list, as
	# part of the linker map file, the object files in which archives it uses to
	# resolve symbols. We only care about the libLLVM ones, which we will filter below.
	@mkdir -p $(@D)
	$(CXX) -o /dev/null -shared $(MAP_FLAGS) $(OBJECTS) $(INITIAL_MODULES) $(LLVM_STATIC_LIBS) $(LLVM_SYSTEM_LIBS) $(COMMON_LD_FLAGS) > /dev/null
	# if the list has changed since the previous build, or there
	# is no list from a previous build, then delete any old object
	# files and re-extract the required object files
	cd $(BUILD_DIR)/llvm_objects; \
	cat list.all | LANG=C sed -n 's/^[^\/]*\(\/[^ ()]*libLLVM.*[.]a\)[^a-zA-Z]*\([^ ()]*[.]o\).*$$/\1 \2/p' | sort | uniq > list.new; \
	rm list.all; \
	if cmp -s list.new list; \
	then \
	echo "No changes in LLVM deps"; \
	touch list; \
	else \
	rm -f llvm_*.o*; \
	cat list.new | sed = | sed "N;s/\n /\n/;s/\([0-9]*\)\n\([^ ]*\) \([^ ]*\)/ar x \2 \3; mv \3 llvm_\1_\3/" | bash - ; \
	mv list.new list; \
	fi

$(LIB_DIR)/libHalide.a: $(OBJECTS) $(INITIAL_MODULES) $(BUILD_DIR)/llvm_objects/list
	# Archive together all the halide and llvm object files
	@mkdir -p $(@D)
	@rm -f $(LIB_DIR)/libHalide.a
	ar q $(LIB_DIR)/libHalide.a $(OBJECTS) $(INITIAL_MODULES) $(BUILD_DIR)/llvm_objects/llvm_*.o*
	ranlib $(LIB_DIR)/libHalide.a

ifeq ($(UNAME), Linux)
LIBHALIDE_SONAME_FLAGS=-Wl,-soname,libHalide.so
else
LIBHALIDE_SONAME_FLAGS=
endif

ifeq ($(UNAME), Linux)
LIBHALIDE_EXPORTS=-Wl,--version-script=$(ROOT_DIR)/src/exported_symbols.ldscript
else
LIBHALIDE_EXPORTS=-Wl,-exported_symbols_list $(ROOT_DIR)/src/exported_symbols.osx
endif

$(BIN_DIR)/libHalide.$(SHARED_EXT): $(OBJECTS) $(INITIAL_MODULES)
	@mkdir -p $(@D)
	$(CXX) -shared $(LIBHALIDE_EXPORTS) $(OBJECTS) $(INITIAL_MODULES) $(LLVM_LIBS_FOR_SHARED_LIBHALIDE) $(LLVM_SYSTEM_LIBS) $(COMMON_LD_FLAGS) $(INSTALL_NAME_TOOL_LD_FLAGS) $(LIBHALIDE_SONAME_FLAGS) -o $(BIN_DIR)/libHalide.$(SHARED_EXT)
ifeq ($(UNAME), Darwin)
	install_name_tool -id $(CURDIR)/$(BIN_DIR)/libHalide.$(SHARED_EXT) $(BIN_DIR)/libHalide.$(SHARED_EXT)
endif

$(INCLUDE_DIR)/Halide.h: $(SRC_DIR)/../LICENSE.txt $(HEADERS) $(BIN_DIR)/build_halide_h
	@mkdir -p $(@D)
	$(BIN_DIR)/build_halide_h $(SRC_DIR)/../LICENSE.txt $(HEADERS) > $(INCLUDE_DIR)/Halide.h
	# Also generate a precompiled version in the same folder so that anything compiled with a compatible set of flags can use it
	@mkdir -p $(INCLUDE_DIR)/Halide.h.gch
	$(CXX) -std=c++17 $(TEST_CXX_FLAGS) -I$(ROOT_DIR) $(OPTIMIZE) -x c++-header $(INCLUDE_DIR)/Halide.h -o $(INCLUDE_DIR)/Halide.h.gch/Halide.default.gch
	$(CXX) -std=c++17 $(TEST_CXX_FLAGS) -I$(ROOT_DIR) $(OPTIMIZE_FOR_BUILD_TIME) -x c++-header $(INCLUDE_DIR)/Halide.h -o $(INCLUDE_DIR)/Halide.h.gch/Halide.test.gch

$(INCLUDE_DIR)/HalideRuntime%: $(SRC_DIR)/runtime/HalideRuntime%
	echo Copying $<
	@mkdir -p $(@D)
	cp $< $(INCLUDE_DIR)/

$(INCLUDE_DIR)/HalideBuffer.h: $(SRC_DIR)/runtime/HalideBuffer.h
	echo Copying $<
	@mkdir -p $(@D)
	cp $< $(INCLUDE_DIR)/

$(INCLUDE_DIR)/HalidePyTorchHelpers.h: $(SRC_DIR)/runtime/HalidePyTorchHelpers.h
	echo Copying $<
	@mkdir -p $(@D)
	cp $< $(INCLUDE_DIR)/

$(INCLUDE_DIR)/HalidePyTorchCudaHelpers.h: $(SRC_DIR)/runtime/HalidePyTorchCudaHelpers.h
	echo Copying $<
	@mkdir -p $(@D)
	cp $< $(INCLUDE_DIR)/

$(BIN_DIR)/build_halide_h: $(ROOT_DIR)/tools/build_halide_h.cpp
	@-mkdir -p $(@D)
	$(CXX) -std=c++17 $< -o $@

-include $(OBJECTS:.o=.d)
-include $(INITIAL_MODULES:.o=.d)

.SECONDARY:

# Compile generic 32- or 64-bit code
# (The 'nacl' is a red herring. This is just a generic 32-bit little-endian target.)
RUNTIME_TRIPLE_32 = "le32-unknown-nacl-unknown"
RUNTIME_TRIPLE_64 = "le64-unknown-unknown-unknown"

# Windows requires special handling.  The generic windows_* modules must have -fpic elided
# and (for 64 bit) must set wchar to be 2 bytes.  The windows_*_x86 and windows_*_arm
# modules need to interact with specific calling conventions related to D3D12.
#
# TODO(marcos): generic code won't hold for ARM32... If ARM32 support becomes necessary,
# all windows-related runtime modules will have to be wrapped in windows_*_arm.cpp files
# for now, generic Windows 32bit code just assumes x86 (i386)
RUNTIME_TRIPLE_WIN_X86_32 = "i386-unknown-windows-unknown"
RUNTIME_TRIPLE_WIN_X86_64 = "x86_64-unknown-windows-unknown"
RUNTIME_TRIPLE_WIN_ARM_32 = "arm-unknown-windows-unknown"
RUNTIME_TRIPLE_WIN_ARM_64 = "aarch64-unknown-windows-unknown"
RUNTIME_TRIPLE_WIN_GENERIC_64 = "le64-unknown-windows-unknown"

# `-fno-threadsafe-statics` is very important here (note that it allows us to use a 'modern' C++
# standard but still skip threadsafe guards for static initialization in our runtime code)
#
# `-fno-rtti` is necessary to allow us to use classes with virtual functions in the runtime code
RUNTIME_CXX_FLAGS = \
    -O3 \
    -std=c++17 \
    -ffreestanding \
    -fno-blocks \
    -fno-exceptions \
    -fno-unwind-tables \
    -fno-vectorize \
    -fno-threadsafe-statics \
    -fno-rtti \
    -Wall \
    -Wcast-qual \
    -Werror \
    -Wignored-qualifiers \
    -Wno-comment \
    -Wno-psabi \
    -Wno-unknown-warning-option \
    -Wno-unused-function \
    -Wvla \
    -Wsign-compare

$(BUILD_DIR)/initmod.windows_%_x86_32.ll: $(SRC_DIR)/runtime/windows_%_x86.cpp $(BUILD_DIR)/clang_ok
	@mkdir -p $(@D)
	$(CLANG) $(CXX_WARNING_FLAGS) $(RUNTIME_CXX_FLAGS) -m32 -target $(RUNTIME_TRIPLE_WIN_X86_32) -DCOMPILING_HALIDE_RUNTIME -DBITS_32 -emit-llvm -S $(SRC_DIR)/runtime/windows_$*_x86.cpp -o $@ -MMD -MP -MF $(BUILD_DIR)/initmod.windows_$*_x86_32.d

$(BUILD_DIR)/initmod.windows_%_x86_64.ll: $(SRC_DIR)/runtime/windows_%_x86.cpp $(BUILD_DIR)/clang_ok
	@mkdir -p $(@D)
	$(CLANG) $(CXX_WARNING_FLAGS) $(RUNTIME_CXX_FLAGS) -m64 -target $(RUNTIME_TRIPLE_WIN_X86_64) -DCOMPILING_HALIDE_RUNTIME -DBITS_64 -emit-llvm -S $(SRC_DIR)/runtime/windows_$*_x86.cpp -o $@ -MMD -MP -MF $(BUILD_DIR)/initmod.windows_$*_x86_64.d

$(BUILD_DIR)/initmod.windows_%_arm_32.ll: $(SRC_DIR)/runtime/windows_%_arm.cpp $(BUILD_DIR)/clang_ok
	@mkdir -p $(@D)
	$(CLANG) $(CXX_WARNING_FLAGS) $(RUNTIME_CXX_FLAGS) -m32 -target $(RUNTIME_TRIPLE_WIN_ARM_32) -DCOMPILING_HALIDE_RUNTIME -DBITS_32 -emit-llvm -S $(SRC_DIR)/runtime/windows_$*_arm.cpp -o $@ -MMD -MP -MF $(BUILD_DIR)/initmod.windows_$*_arm_32.d

$(BUILD_DIR)/initmod.windows_%_arm_64.ll: $(SRC_DIR)/runtime/windows_%_arm.cpp $(BUILD_DIR)/clang_ok
	@mkdir -p $(@D)
	$(CLANG) $(CXX_WARNING_FLAGS) $(RUNTIME_CXX_FLAGS) -m64 -target $(RUNTIME_TRIPLE_WIN_ARM_64) -DCOMPILING_HALIDE_RUNTIME -DBITS_64 -emit-llvm -S $(SRC_DIR)/runtime/windows_$*_arm.cpp -o $@ -MMD -MP -MF $(BUILD_DIR)/initmod.windows_$*_arm_64.d

$(BUILD_DIR)/initmod.windows_%_32.ll: $(SRC_DIR)/runtime/windows_%.cpp $(BUILD_DIR)/clang_ok
	@mkdir -p $(@D)
	$(CLANG) $(CXX_WARNING_FLAGS) $(RUNTIME_CXX_FLAGS) -m32 -target $(RUNTIME_TRIPLE_WIN_X86_32) -DCOMPILING_HALIDE_RUNTIME -DBITS_32 -emit-llvm -S $(SRC_DIR)/runtime/windows_$*.cpp -o $@ -MMD -MP -MF $(BUILD_DIR)/initmod.windows_$*_32.d

$(BUILD_DIR)/initmod.windows_%_64.ll: $(SRC_DIR)/runtime/windows_%.cpp $(BUILD_DIR)/clang_ok
	@mkdir -p $(@D)
	$(CLANG) $(CXX_WARNING_FLAGS) $(RUNTIME_CXX_FLAGS) -m64 -target $(RUNTIME_TRIPLE_WIN_GENERIC_64) -fshort-wchar -DCOMPILING_HALIDE_RUNTIME -DBITS_64 -emit-llvm -S $(SRC_DIR)/runtime/windows_$*.cpp -o $@ -MMD -MP -MF $(BUILD_DIR)/initmod.windows_$*_64.d

$(BUILD_DIR)/initmod.%_64.ll: $(SRC_DIR)/runtime/%.cpp $(BUILD_DIR)/clang_ok
	@mkdir -p $(@D)
	$(CLANG) $(CXX_WARNING_FLAGS) $(RUNTIME_CXX_FLAGS) -fpic -m64 -target $(RUNTIME_TRIPLE_64) -DCOMPILING_HALIDE_RUNTIME -DBITS_64 -emit-llvm -S $(SRC_DIR)/runtime/$*.cpp -o $@ -MMD -MP -MF $(BUILD_DIR)/initmod.$*_64.d

$(BUILD_DIR)/initmod.%_32.ll: $(SRC_DIR)/runtime/%.cpp $(BUILD_DIR)/clang_ok
	@mkdir -p $(@D)
	$(CLANG) $(CXX_WARNING_FLAGS) $(RUNTIME_CXX_FLAGS) -fpic -m32 -target $(RUNTIME_TRIPLE_32) -DCOMPILING_HALIDE_RUNTIME -DBITS_32 -emit-llvm -S $(SRC_DIR)/runtime/$*.cpp -o $@ -MMD -MP -MF $(BUILD_DIR)/initmod.$*_32.d

$(BUILD_DIR)/initmod.windows_%_x86_32_debug.ll: $(SRC_DIR)/runtime/windows_%_x86.cpp $(BUILD_DIR)/clang_ok
	@mkdir -p $(@D)
	$(CLANG) $(CXX_WARNING_FLAGS) -g -DDEBUG_RUNTIME $(RUNTIME_CXX_FLAGS) -m32 -target $(RUNTIME_TRIPLE_WIN_X86_32) -DCOMPILING_HALIDE_RUNTIME -DBITS_32 -emit-llvm -S $(SRC_DIR)/runtime/windows_$*_x86.cpp -o $@ -MMD -MP -MF $(BUILD_DIR)/initmod.windows_$*_x86_32_debug.d

$(BUILD_DIR)/initmod.windows_%_x86_64_debug.ll: $(SRC_DIR)/runtime/windows_%_x86.cpp $(BUILD_DIR)/clang_ok
	@mkdir -p $(@D)
	$(CLANG) $(CXX_WARNING_FLAGS) -g -DDEBUG_RUNTIME $(RUNTIME_CXX_FLAGS) -m64 -target $(RUNTIME_TRIPLE_WIN_X86_64) -DCOMPILING_HALIDE_RUNTIME -DBITS_64 -emit-llvm -S $(SRC_DIR)/runtime/windows_$*_x86.cpp -o $@ -MMD -MP -MF $(BUILD_DIR)/initmod.windows_$*_x86_64_debug.d

$(BUILD_DIR)/initmod.windows_%_arm_32_debug.ll: $(SRC_DIR)/runtime/windows_%_arm.cpp $(BUILD_DIR)/clang_ok
	@mkdir -p $(@D)
	$(CLANG) $(CXX_WARNING_FLAGS) -g -DDEBUG_RUNTIME $(RUNTIME_CXX_FLAGS) -m32 -target $(RUNTIME_TRIPLE_WIN_ARM_32) -DCOMPILING_HALIDE_RUNTIME -DBITS_32 -emit-llvm -S $(SRC_DIR)/runtime/windows_$*_arm.cpp -o $@ -MMD -MP -MF $(BUILD_DIR)/initmod.windows_$*_arm_32_debug.d

$(BUILD_DIR)/initmod.windows_%_arm_64_debug.ll: $(SRC_DIR)/runtime/windows_%_arm.cpp $(BUILD_DIR)/clang_ok
	@mkdir -p $(@D)
	$(CLANG) $(CXX_WARNING_FLAGS) -g -DDEBUG_RUNTIME $(RUNTIME_CXX_FLAGS) -m64 -target $(RUNTIME_TRIPLE_WIN_ARM_64) -DCOMPILING_HALIDE_RUNTIME -DBITS_64 -emit-llvm -S $(SRC_DIR)/runtime/windows_$*_arm.cpp -o $@ -MMD -MP -MF $(BUILD_DIR)/initmod.windows_$*_arm_64_debug.d

$(BUILD_DIR)/initmod.windows_%_64_debug.ll: $(SRC_DIR)/runtime/windows_%.cpp $(BUILD_DIR)/clang_ok
	@mkdir -p $(@D)
	$(CLANG) $(CXX_WARNING_FLAGS) -g -DDEBUG_RUNTIME $(RUNTIME_CXX_FLAGS) -m64 -target $(RUNTIME_TRIPLE_WIN_GENERIC_64) -DCOMPILING_HALIDE_RUNTIME -DBITS_64 -emit-llvm -S $(SRC_DIR)/runtime/windows_$*.cpp -o $@ -MMD -MP -MF $(BUILD_DIR)/initmod.windows_$*_64_debug.d

$(BUILD_DIR)/initmod.%_64_debug.ll: $(SRC_DIR)/runtime/%.cpp $(BUILD_DIR)/clang_ok
	@mkdir -p $(@D)
	$(CLANG) $(CXX_WARNING_FLAGS) -g -DDEBUG_RUNTIME $(RUNTIME_CXX_FLAGS) -fpic -m64 -target  $(RUNTIME_TRIPLE_64) -DCOMPILING_HALIDE_RUNTIME -DBITS_64 -emit-llvm -S $(SRC_DIR)/runtime/$*.cpp -o $@ -MMD -MP -MF $(BUILD_DIR)/initmod.$*_64_debug.d

$(BUILD_DIR)/initmod.windows_%_32_debug.ll: $(SRC_DIR)/runtime/windows_%.cpp $(BUILD_DIR)/clang_ok
	@mkdir -p $(@D)
	$(CLANG) $(CXX_WARNING_FLAGS) -g -DDEBUG_RUNTIME $(RUNTIME_CXX_FLAGS) -m32 -target $(RUNTIME_TRIPLE_WIN_X86_32) -DCOMPILING_HALIDE_RUNTIME -DBITS_32 -emit-llvm -S $(SRC_DIR)/runtime/windows_$*.cpp -o $@ -MMD -MP -MF $(BUILD_DIR)/initmod.windows_$*_32_debug.d

$(BUILD_DIR)/initmod.%_32_debug.ll: $(SRC_DIR)/runtime/%.cpp $(BUILD_DIR)/clang_ok
	@mkdir -p $(@D)
	$(CLANG) $(CXX_WARNING_FLAGS) -g -DDEBUG_RUNTIME -O3 $(RUNTIME_CXX_FLAGS) -fpic -m32 -target $(RUNTIME_TRIPLE_32) -DCOMPILING_HALIDE_RUNTIME -DBITS_32 -emit-llvm -S $(SRC_DIR)/runtime/$*.cpp -o $@ -MMD -MP -MF $(BUILD_DIR)/initmod.$*_32_debug.d

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
$(BUILD_DIR)/initmod.inlined_c.cpp: $(BIN_DIR)/binary2cpp $(SRC_DIR)/runtime/halide_buffer_t.cpp
	./$(BIN_DIR)/binary2cpp halide_internal_initmod_inlined_c < $(SRC_DIR)/runtime/halide_buffer_t.cpp > $@

$(BUILD_DIR)/initmod_ptx.%_ll.cpp: $(BIN_DIR)/binary2cpp $(SRC_DIR)/runtime/nvidia_libdevice_bitcode/libdevice.%.bc
	./$(BIN_DIR)/binary2cpp halide_internal_initmod_ptx_$(basename $*)_ll < $(SRC_DIR)/runtime/nvidia_libdevice_bitcode/libdevice.$*.bc > $@

$(BUILD_DIR)/c_template.%.cpp: $(BIN_DIR)/binary2cpp $(SRC_DIR)/%.template.cpp
	./$(BIN_DIR)/binary2cpp halide_c_template_$* < $(SRC_DIR)/$*.template.cpp > $@

$(BUILD_DIR)/html_template.%.cpp: $(BIN_DIR)/binary2cpp $(SRC_DIR)/irvisualizer/html_template_%
	./$(BIN_DIR)/binary2cpp halide_html_template_$(subst .,_,$*) < $(SRC_DIR)/irvisualizer/html_template_$* > $@

$(BIN_DIR)/binary2cpp: $(ROOT_DIR)/tools/binary2cpp.cpp
	@mkdir -p $(@D)
	$(CXX) $< -o $@

$(BIN_DIR)/regexp_replace: $(ROOT_DIR)/tools/regexp_replace.cpp
	@mkdir -p $(@D)
	$(CXX) -std=c++11 $< -o $@

$(BUILD_DIR)/initmod_ptx.%_ll.o: $(BUILD_DIR)/initmod_ptx.%_ll.cpp
	$(CXX) -c $< -o $@ -MMD -MP -MF $(BUILD_DIR)/$*.d -MT $(BUILD_DIR)/$*.o

$(BUILD_DIR)/initmod.%.o: $(BUILD_DIR)/initmod.%.cpp
	$(CXX) -c $< -o $@ -MMD -MP -MF $(BUILD_DIR)/$*.d -MT $(BUILD_DIR)/$*.o

$(BUILD_DIR)/c_template.%.o: $(BUILD_DIR)/c_template.%.cpp
	$(CXX) -c $< -o $@ -MMD -MP -MF $(BUILD_DIR)/$*.d -MT $(BUILD_DIR)/$*.o

$(BUILD_DIR)/html_template.%.o: $(BUILD_DIR)/html_template.%.cpp
	$(CXX) -c $< -o $@ -MMD -MP -MF $(BUILD_DIR)/$*.d -MT $(BUILD_DIR)/$*.o

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp $(BUILD_DIR)/llvm_ok
	@mkdir -p $(@D)
	$(CXX) $(CXX_FLAGS) -c $< -o $@ -MMD -MP -MF $(BUILD_DIR)/$*.d -MT $(BUILD_DIR)/$*.o

$(BUILD_DIR)/Simplify_%.o: $(SRC_DIR)/Simplify_%.cpp $(SRC_DIR)/Simplify_Internal.h $(BUILD_DIR)/llvm_ok
	@mkdir -p $(@D)
	$(CXX) $(CXX_FLAGS) -c $< -o $@ -MMD -MP -MF $(BUILD_DIR)/Simplify_$*.d -MT $@

$(BUILD_DIR)/terminate_handler.o: $(ROOT_DIR)/test/common/terminate_handler.cpp
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
	rm -rf $(SHARE_DIR)
	rm -rf $(DISTRIB_DIR)
	rm -rf $(ROOT_DIR)/apps/*/bin

CORRECTNESS_TESTS = $(shell ls $(ROOT_DIR)/test/correctness/*.cpp) $(shell ls $(ROOT_DIR)/test/correctness/*.c)
PERFORMANCE_TESTS = $(shell ls $(ROOT_DIR)/test/performance/*.cpp)
ERROR_TESTS = $(shell ls $(ROOT_DIR)/test/error/*.cpp)
WARNING_TESTS = $(shell ls $(ROOT_DIR)/test/warning/*.cpp)
RUNTIME_TESTS = $(shell ls $(ROOT_DIR)/test/runtime/*.cpp)
GENERATOR_EXTERNAL_TESTS := $(shell ls $(ROOT_DIR)/test/generator/*test.cpp)
GENERATOR_EXTERNAL_TEST_GENERATOR := $(shell ls $(ROOT_DIR)/test/generator/*_generator.cpp)
TUTORIALS = $(filter-out %_generate.cpp, $(shell ls $(ROOT_DIR)/tutorial/*.cpp))
MULLAPUDI2016_TESTS = $(shell ls $(ROOT_DIR)/test/autoschedulers/mullapudi2016/*.cpp)
LI2018_TESTS = $(shell ls $(ROOT_DIR)/test/autoschedulers/li2018/test.cpp)
ADAMS2019_TESTS = $(shell ls $(ROOT_DIR)/test/autoschedulers/adams2019/test.cpp)

test_correctness: $(CORRECTNESS_TESTS:$(ROOT_DIR)/test/correctness/%.cpp=quiet_correctness_%) $(CORRECTNESS_TESTS:$(ROOT_DIR)/test/correctness/%.c=quiet_correctness_%)
test_performance: $(PERFORMANCE_TESTS:$(ROOT_DIR)/test/performance/%.cpp=performance_%)
test_error: $(ERROR_TESTS:$(ROOT_DIR)/test/error/%.cpp=error_%)
test_warning: $(WARNING_TESTS:$(ROOT_DIR)/test/warning/%.cpp=warning_%)
test_runtime: $(RUNTIME_TESTS:$(ROOT_DIR)/test/runtime/%.cpp=runtime_%)
test_tutorial: $(TUTORIALS:$(ROOT_DIR)/tutorial/%.cpp=tutorial_%)
test_valgrind: $(CORRECTNESS_TESTS:$(ROOT_DIR)/test/correctness/%.cpp=valgrind_%)
test_avx512: $(CORRECTNESS_TESTS:$(ROOT_DIR)/test/correctness/%.cpp=avx512_%)
test_autoschedulers: test_mullapudi2016 test_li2018 test_adams2019
test_auto_schedule: test_autoschedulers

.PHONY: test_correctness_multi_gpu
test_correctness_multi_gpu: correctness_gpu_multi_device

# There are 3 types of tests for generators:
# 1) Externally-written aot-based tests
# 2) Externally-written aot-based tests (compiled using C++ backend)
# 3) Externally-written JIT-based tests
GENERATOR_AOT_TESTS = $(GENERATOR_EXTERNAL_TESTS:$(ROOT_DIR)/test/generator/%_aottest.cpp=generator_aot_%)
GENERATOR_AOTCPP_TESTS = $(GENERATOR_EXTERNAL_TESTS:$(ROOT_DIR)/test/generator/%_aottest.cpp=generator_aotcpp_%)
GENERATOR_JIT_TESTS = $(GENERATOR_EXTERNAL_TESTS:$(ROOT_DIR)/test/generator/%_jittest.cpp=generator_jit_%)

# multitarget test doesn't make any sense for the CPP backend; just skip it.
GENERATOR_AOTCPP_TESTS := $(filter-out generator_aotcpp_multitarget,$(GENERATOR_AOTCPP_TESTS))

# Note that many of the AOT-CPP tests are broken right now;
# remove AOT-CPP tests that don't (yet) work for C++ backend
# (each tagged with the *known* blocking issue(s))

# sanitizercoverage relies on LLVM-specific hooks, so it will never work with the C backend
GENERATOR_AOTCPP_TESTS := $(filter-out generator_aotcpp_sanitizercoverage,$(GENERATOR_AOTCPP_TESTS))

# https://github.com/halide/Halide/issues/2084 (only if opencl enabled))
#GENERATOR_AOTCPP_TESTS := $(filter-out generator_aotcpp_cleanup_on_error,$(GENERATOR_AOTCPP_TESTS))

# https://github.com/halide/Halide/issues/7273
GENERATOR_AOTCPP_TESTS := $(filter-out generator_aotcpp_msan,$(GENERATOR_AOTCPP_TESTS))

# https://github.com/halide/Halide/issues/7272
GENERATOR_AOTCPP_TESTS := $(filter-out generator_aotcpp_memory_profiler_mandelbrot,$(GENERATOR_AOTCPP_TESTS))

# https://github.com/halide/Halide/issues/4916
GENERATOR_AOTCPP_TESTS := $(filter-out generator_aotcpp_stubtest,$(GENERATOR_AOTCPP_TESTS))
GENERATOR_AOTCPP_TESTS := $(filter-out generator_aotcpp_stubuser,$(GENERATOR_AOTCPP_TESTS))

# Build requirements are finicky, testing non-C++ backend is good enough here
GENERATOR_AOTCPP_TESTS := $(filter-out generator_aotcpp_gpu_multi_context_threaded,$(GENERATOR_AOTCPP_TESTS))

test_aotcpp_generator: $(GENERATOR_AOTCPP_TESTS)

# This is just a test to ensure than RunGen builds and links for a critical mass of Generators;
# not all will work directly (e.g. due to missing define_externs at link time), so we disable
# those known to be broken for plausible reasons.
GENERATOR_BUILD_RUNGEN_TESTS = $(GENERATOR_EXTERNAL_TEST_GENERATOR:$(ROOT_DIR)/test/generator/%_generator.cpp=$(FILTERS_DIR)/%.rungen)
GENERATOR_BUILD_RUNGEN_TESTS := $(filter-out $(FILTERS_DIR)/async_parallel.rungen,$(GENERATOR_BUILD_RUNGEN_TESTS))
GENERATOR_BUILD_RUNGEN_TESTS := $(filter-out $(FILTERS_DIR)/cxx_mangling_define_extern.rungen,$(GENERATOR_BUILD_RUNGEN_TESTS))
GENERATOR_BUILD_RUNGEN_TESTS := $(filter-out $(FILTERS_DIR)/define_extern_opencl.rungen,$(GENERATOR_BUILD_RUNGEN_TESTS))
GENERATOR_BUILD_RUNGEN_TESTS := $(filter-out $(FILTERS_DIR)/msan.rungen,$(GENERATOR_BUILD_RUNGEN_TESTS))
GENERATOR_BUILD_RUNGEN_TESTS := $(filter-out $(FILTERS_DIR)/sanitizercoverage.rungen,$(GENERATOR_BUILD_RUNGEN_TESTS))
GENERATOR_BUILD_RUNGEN_TESTS := $(filter-out $(FILTERS_DIR)/multitarget.rungen,$(GENERATOR_BUILD_RUNGEN_TESTS))
GENERATOR_BUILD_RUNGEN_TESTS := $(filter-out $(FILTERS_DIR)/nested_externs.rungen,$(GENERATOR_BUILD_RUNGEN_TESTS))
GENERATOR_BUILD_RUNGEN_TESTS := $(filter-out $(FILTERS_DIR)/tiled_blur.rungen,$(GENERATOR_BUILD_RUNGEN_TESTS))
GENERATOR_BUILD_RUNGEN_TESTS := $(filter-out $(FILTERS_DIR)/extern_output.rungen,$(GENERATOR_BUILD_RUNGEN_TESTS))
GENERATOR_BUILD_RUNGEN_TESTS := $(filter-out $(FILTERS_DIR)/gpu_multi_context_threaded.rungen,$(GENERATOR_BUILD_RUNGEN_TESTS))
GENERATOR_BUILD_RUNGEN_TESTS := $(GENERATOR_BUILD_RUNGEN_TESTS) \
	$(FILTERS_DIR)/multi_rungen \
	$(FILTERS_DIR)/multi_rungen2 \
	$(FILTERS_DIR)/rungen_test \
	$(FILTERS_DIR)/registration_test

test_rungen: $(GENERATOR_BUILD_RUNGEN_TESTS)
	$(FILTERS_DIR)/rungen_test
	$(FILTERS_DIR)/registration_test

test_generator: $(GENERATOR_AOT_TESTS) $(GENERATOR_AOTCPP_TESTS) $(GENERATOR_JIT_TESTS) $(GENERATOR_BUILD_RUNGEN_TESTS)
	$(FILTERS_DIR)/rungen_test
	$(FILTERS_DIR)/registration_test

ALL_TESTS = test_internal test_correctness test_error test_tutorial test_warning test_runtime test_generator

# These targets perform timings of each test. For most tests this includes Halide JIT compile times, and run times.
# For generator tests they time the compile time only. The times are recorded in CSV files.
time_compilation_correctness: init_time_compilation_correctness $(CORRECTNESS_TESTS:$(ROOT_DIR)/test/correctness/%.cpp=time_compilation_test_%)
time_compilation_performance: init_time_compilation_performance $(PERFORMANCE_TESTS:$(ROOT_DIR)/test/performance/%.cpp=time_compilation_performance_%)
time_compilation_generator: init_time_compilation_generator $(GENERATOR_TESTS:$(ROOT_DIR)/test/generator/%_aottest.cpp=time_compilation_generator_%)

init_time_compilation_%:
	echo "TEST,User (s),System (s),Real" > $(@:init_time_compilation_%=compile_times_%.csv)

TIME_COMPILATION ?= /usr/bin/time -a -f "$@,%U,%S,%E" -o

run_tests: $(ALL_TESTS)
	make -f $(THIS_MAKEFILE) test_performance test_autoschedulers

.PHONY: build_tests
build_tests: $(CORRECTNESS_TESTS:$(ROOT_DIR)/test/correctness/%.cpp=$(BIN_DIR)/correctness_%) \
	$(PERFORMANCE_TESTS:$(ROOT_DIR)/test/performance/%.cpp=$(BIN_DIR)/performance_%) \
	$(ERROR_TESTS:$(ROOT_DIR)/test/error/%.cpp=$(BIN_DIR)/error_%) \
	$(WARNING_TESTS:$(ROOT_DIR)/test/warning/%.cpp=$(BIN_DIR)/warning_%) \
	$(RUNTIME_TESTS:$(ROOT_DIR)/test/runtime/%.cpp=$(BIN_DIR)/runtime_%) \
	$(GENERATOR_EXTERNAL_TESTS:$(ROOT_DIR)/test/generator/%_aottest.cpp=$(BIN_DIR)/$(TARGET)/generator_aot_%) \
	$(GENERATOR_EXTERNAL_TESTS:$(ROOT_DIR)/test/generator/%_jittest.cpp=$(BIN_DIR)/generator_jit_%) \
	$(MULLAPUDI2016_TESTS:$(ROOT_DIR)/test/autoschedulers/mullapudi2016/%.cpp=$(BIN_DIR)/mullapudi2016_%) \
	$(LI2018_TESTS:$(ROOT_DIR)/test/autoschedulers/li2018/%.cpp=$(BIN_DIR)/li2018_%) \
	$(ADAMS2019_TESTS:$(ROOT_DIR)/test/autoschedulers/adams2019/%.cpp=$(BIN_DIR)/adams2019_%)

clean_generator:
	rm -rf $(BIN_DIR)/*.generator
	rm -rf $(BIN_DIR)/*/runtime.a
	rm -rf $(FILTERS_DIR)
	rm -rf $(BIN_DIR)/*/generator_*
	rm -rf $(BUILD_DIR)/*_generator.o
	rm -f $(BUILD_DIR)/GenGen.o
	rm -f $(BUILD_DIR)/RunGenMain.o

time_compilation_tests: time_compilation_correctness time_compilation_performance time_compilation_generator

# These are just aliases to the autoscheduler plugins to make Generator rules & deps a little terser
BIN_ADAMS2019=$(BIN_DIR)/libautoschedule_adams2019.$(PLUGIN_EXT)
BIN_LI2018=$(BIN_DIR)/libautoschedule_li2018.$(PLUGIN_EXT)
BIN_MULLAPUDI2016=$(BIN_DIR)/libautoschedule_mullapudi2016.$(PLUGIN_EXT)

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

$(BIN_DIR)/test_internal: $(ROOT_DIR)/test/internal.cpp $(TEST_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(TEST_CXX_FLAGS) $< -I$(SRC_DIR) $(TEST_LD_FLAGS) -o $@

ifneq (,$(shell which flatc))
$(BUILD_DIR)/Deserialization.o : $(BUILD_DIR)/halide_ir.fbs.h
$(BUILD_DIR)/Serialization.o : $(BUILD_DIR)/halide_ir.fbs.h
endif

# Generated header for serialization/deserialization
$(BUILD_DIR)/halide_ir.fbs.h: $(SRC_DIR)/halide_ir.fbs
	@mkdir -p $(@D)
	flatc --cpp --cpp-std C++17 --no-union-value-namespacing --keep-prefix --filename-suffix ".fbs" -o $(BUILD_DIR) $^

# Correctness test that link against libHalide
$(BIN_DIR)/correctness_%: $(ROOT_DIR)/test/correctness/%.cpp $(TEST_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(TEST_CXX_FLAGS) -I$(ROOT_DIR)/src/runtime -I$(ROOT_DIR)/test/common $(OPTIMIZE_FOR_BUILD_TIME) $< -I$(INCLUDE_DIR) $(TEST_LD_FLAGS) -o $@

# Correctness tests that do NOT link against libHalide
$(BIN_DIR)/correctness_plain_c_includes: $(ROOT_DIR)/test/correctness/plain_c_includes.c $(RUNTIME_EXPORTED_INCLUDES)
	$(CXX) -x c -Wall -Werror -I$(ROOT_DIR)/src/runtime $(OPTIMIZE_FOR_BUILD_TIME) $< -I$(ROOT_DIR)/src/runtime -o $@

# Note that this test must *not* link in either libHalide, or a Halide runtime;
# this test should be usable without either.
$(BIN_DIR)/correctness_halide_buffer: $(ROOT_DIR)/test/correctness/halide_buffer.cpp $(INCLUDE_DIR)/HalideBuffer.h $(RUNTIME_EXPORTED_INCLUDES)
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE_FOR_BUILD_TIME) $< -I$(INCLUDE_DIR) -o $@

# The image_io test additionally needs to link to libpng and
# libjpeg.
$(BIN_DIR)/correctness_image_io: $(ROOT_DIR)/test/correctness/image_io.cpp $(TEST_DEPS)
	$(CXX) $(TEST_CXX_FLAGS) $(IMAGE_IO_CXX_FLAGS) -I$(ROOT_DIR)/src/runtime -I$(ROOT_DIR)/test/common $(OPTIMIZE_FOR_BUILD_TIME) $< -I$(INCLUDE_DIR) $(TEST_LD_FLAGS) $(IMAGE_IO_LIBS) -o $@

# OpenCL runtime correctness test requires runtime.a to be linked.
$(BIN_DIR)/$(TARGET)/correctness_opencl_runtime: $(ROOT_DIR)/test/correctness/opencl_runtime.cpp $(RUNTIME_EXPORTED_INCLUDES) $(BIN_DIR)/$(TARGET)/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(BIN_DIR)/$(TARGET)/runtime.a $(TEST_CXX_FLAGS) -I$(ROOT_DIR)/src/runtime $(OPTIMIZE_FOR_BUILD_TIME) $< -I$(INCLUDE_DIR) $(TEST_LD_FLAGS) -o $@

$(BIN_DIR)/performance_%: $(ROOT_DIR)/test/performance/%.cpp $(TEST_DEPS)
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE) $< -I$(INCLUDE_DIR) -I$(ROOT_DIR)/src/runtime -I$(ROOT_DIR)/test/common $(TEST_LD_FLAGS) -o $@

# Error tests that link against libHalide
$(BIN_DIR)/error_%: $(ROOT_DIR)/test/error/%.cpp $(TEST_DEPS)
	$(CXX) $(TEST_CXX_FLAGS) -I$(ROOT_DIR)/src/runtime -I$(ROOT_DIR)/test/common $(OPTIMIZE_FOR_BUILD_TIME) $< -I$(INCLUDE_DIR) $(TEST_LD_FLAGS) -o $@

$(BIN_DIR)/warning_%: $(ROOT_DIR)/test/warning/%.cpp $(TEST_DEPS)
	$(CXX) $(TEST_CXX_FLAGS) -I$(ROOT_DIR)/test/common $(OPTIMIZE_FOR_BUILD_TIME) $< -I$(INCLUDE_DIR) $(TEST_LD_FLAGS) -o $@

# Runtime tests that test internals
RUNTIME_TESTS_CXXFLAGS = -fno-rtti -fno-exceptions -fno-threadsafe-statics -Wno-builtin-declaration-mismatch -DCOMPILING_HALIDE_RUNTIME -DCOMPILING_HALIDE_RUNTIME_TESTS

$(BIN_DIR)/runtime_internal_common.o: $(ROOT_DIR)/test/runtime/common.cpp $(ROOT_DIR)/test/runtime/common.h
	@mkdir -p $(@D)
	$(CXX) $(TEST_CXX_FLAGS) $(RUNTIME_TESTS_CXXFLAGS) -I$(ROOT_DIR)/test/runtime -I$(ROOT_DIR)/src/runtime $(OPTIMIZE_FOR_BUILD_TIME) -c $< -o $@

$(BIN_DIR)/runtime_internal_msan_stubs.o: $(ROOT_DIR)/src/runtime/msan_stubs.cpp
	@mkdir -p $(@D)
	$(CXX) $(TEST_CXX_FLAGS) $(RUNTIME_TESTS_CXXFLAGS) -I$(ROOT_DIR)/test/runtime -I$(ROOT_DIR)/src/runtime $(OPTIMIZE_FOR_BUILD_TIME) -c $< -o $@

$(BIN_DIR)/runtime_internal_to_string.o: $(ROOT_DIR)/src/runtime/to_string.cpp
	@mkdir -p $(@D)
	$(CXX) $(TEST_CXX_FLAGS) $(RUNTIME_TESTS_CXXFLAGS) -I$(ROOT_DIR)/test/runtime -I$(ROOT_DIR)/src/runtime $(OPTIMIZE_FOR_BUILD_TIME) -c $< -o $@

$(BIN_DIR)/runtime_common:
	@mkdir -p $(@D)
	touch $@

$(BIN_DIR)/runtime_%: $(ROOT_DIR)/test/runtime/%.cpp $(BIN_DIR)/runtime_internal_common.o $(BIN_DIR)/runtime_internal_msan_stubs.o $(BIN_DIR)/runtime_internal_to_string.o
	@mkdir -p $(@D)
	$(CXX) $(TEST_CXX_FLAGS) $(RUNTIME_TESTS_CXXFLAGS) -I$(ROOT_DIR)/test/runtime -I$(ROOT_DIR)/src/runtime $(OPTIMIZE_FOR_BUILD_TIME) $^ $(COMMON_LD_FLAGS) -o $@

# Auto schedule tests that link against libHalide
$(BIN_DIR)/mullapudi2016_%: $(ROOT_DIR)/test/autoschedulers/mullapudi2016/%.cpp $(TEST_DEPS)
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE_FOR_BUILD_TIME) $< -I$(INCLUDE_DIR) $(TEST_LD_FLAGS) -o $@

$(BIN_DIR)/li2018_%: $(ROOT_DIR)/test/autoschedulers/li2018/%.cpp $(TEST_DEPS)
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE_FOR_BUILD_TIME) $< -I$(INCLUDE_DIR) $(TEST_LD_FLAGS) -o $@

$(BIN_DIR)/adams2019_%: $(ROOT_DIR)/test/autoschedulers/adams2019/%.cpp $(TEST_DEPS)
	$(CXX) $(TEST_CXX_FLAGS) $(OPTIMIZE_FOR_BUILD_TIME) $< -I$(INCLUDE_DIR) $(TEST_LD_FLAGS) -o $@

# TODO(srj): this doesn't auto-delete, why not?
.INTERMEDIATE: $(BIN_DIR)/%.generator

# By default, %.generator is produced by building %_generator.cpp
# Note that the rule includes all _generator.cpp files, so that generator with define_extern
# usage can just add deps later.
$(BUILD_DIR)/%_generator.o: $(ROOT_DIR)/test/generator/%_generator.cpp $(INCLUDE_DIR)/Halide.h
	@mkdir -p $(@D)
	$(CXX) $(TEST_CXX_FLAGS) -I$(INCLUDE_DIR) -I$(CURDIR)/$(FILTERS_DIR) -c $< -o $@

$(BIN_DIR)/%.generator: $(BUILD_DIR)/GenGen.o $(TEST_DEPS) $(BUILD_DIR)/%_generator.o
	@mkdir -p $(@D)
	$(CXX) $(filter %.cpp %.o %.a,$^) $(TEST_LD_FLAGS) -o $@

NAME_MANGLING_TARGET=$(NON_EMPTY_TARGET)-c_plus_plus_name_mangling

GEN_AOT_OUTPUTS=-e static_library,c_header,c_source,registration

# By default, %.a/.h are produced by executing %.generator. Runtimes are not included in these.
# (We explicitly also generate .cpp output here as well, as additional test surface for the C++ backend.)
$(FILTERS_DIR)/%.a: $(BIN_DIR)/%.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g $* $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime

$(FILTERS_DIR)/%.h: $(FILTERS_DIR)/%.a
	@echo $@ produced implicitly by $^

$(FILTERS_DIR)/%.halide_generated.cpp: $(FILTERS_DIR)/%.a
	@echo $@ produced implicitly by $^

$(FILTERS_DIR)/%.registration.cpp: $(FILTERS_DIR)/%.a
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
	$(CURDIR)/$< -g cxx_mangling $(GEN_AOT_OUTPUTS),function_info_header -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime-c_plus_plus_name_mangling -f "HalideTest::AnotherNamespace::cxx_mangling"
	$(ROOT_DIR)/tools/makelib.sh $@ $@ $(FILTERS_DIR)/cxx_mangling_externs.o

ifneq ($(TEST_CUDA), )
# Also build with a gpu target to ensure that the GPU-Host generation
# code handles name mangling properly. (Note that we don't need to
# run this code, just check for link errors.)
$(FILTERS_DIR)/cxx_mangling_gpu.a: $(BIN_DIR)/cxx_mangling.generator $(FILTERS_DIR)/cxx_mangling_externs.o
	@mkdir -p $(@D)
	$(CURDIR)/$< -g cxx_mangling $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime-c_plus_plus_name_mangling-cuda-cuda_capability_30 -f "HalideTest::cxx_mangling_gpu"
	$(ROOT_DIR)/tools/makelib.sh $@ $@ $(FILTERS_DIR)/cxx_mangling_externs.o
endif

$(FILTERS_DIR)/cxx_mangling_define_extern_externs.o: $(ROOT_DIR)/test/generator/cxx_mangling_define_extern_externs.cpp $(FILTERS_DIR)/cxx_mangling.h
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) -c $(filter-out %.h,$^) $(GEN_AOT_INCLUDES) -o $@

$(FILTERS_DIR)/cxx_mangling_define_extern.a: $(BIN_DIR)/cxx_mangling_define_extern.generator $(FILTERS_DIR)/cxx_mangling_define_extern_externs.o
	@mkdir -p $(@D)
	$(CURDIR)/$< -g cxx_mangling_define_extern $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime-c_plus_plus_name_mangling-user_context -f "HalideTest::cxx_mangling_define_extern"
	$(ROOT_DIR)/tools/makelib.sh $@ $@  $(FILTERS_DIR)/cxx_mangling_define_extern_externs.o

# pyramid needs a custom arg.
$(FILTERS_DIR)/pyramid.a: $(BIN_DIR)/pyramid.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g pyramid -f pyramid $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime levels=10

$(FILTERS_DIR)/string_param.a: $(BIN_DIR)/string_param.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g string_param -f string_param  $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime rpn_expr="5 y * x +"

# memory_profiler_mandelbrot need profiler set
$(FILTERS_DIR)/memory_profiler_mandelbrot.a: $(BIN_DIR)/memory_profiler_mandelbrot.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g memory_profiler_mandelbrot -f memory_profiler_mandelbrot $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime-profile

$(FILTERS_DIR)/alias_with_offset_42.a: $(BIN_DIR)/alias.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g alias_with_offset_42 -f alias_with_offset_42 $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime

$(FILTERS_DIR)/alias_Adams2019.a: $(BIN_DIR)/alias.generator autoschedulers
	@mkdir -p $(@D)
	$(CURDIR)/$< -g alias_Adams2019 -f alias_Adams2019 $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime -p $(BIN_ADAMS2019)

$(FILTERS_DIR)/alias_Li2018.a: $(BIN_DIR)/alias.generator autoschedulers
	@mkdir -p $(@D)
	$(CURDIR)/$< -g alias_Li2018 -f alias_Li2018 $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime -p $(BIN_LI2018)

$(FILTERS_DIR)/alias_Mullapudi2016.a: $(BIN_DIR)/alias.generator autoschedulers
	@mkdir -p $(@D)
	$(CURDIR)/$< -g alias_Mullapudi2016 -f alias_Mullapudi2016 $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime -p $(BIN_MULLAPUDI2016)

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
	buffer_f16_untyped.type=float16 \
	untyped_scalar_input.type=uint8 \
	array_outputs.size=2 \
	array_outputs7.size=2 \
	array_outputs8.size=2 \
	array_outputs9.size=2

# metadata_tester is built with and without user-context.
# Also note that metadata_tester (but not metadata_tester_ucon) is built as "multitarget" to verify that
# the metadata names are correctly emitted.
$(FILTERS_DIR)/metadata_tester.a: $(BIN_DIR)/metadata_tester.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g metadata_tester -f metadata_tester -e static_library,c_header,registration,function_info_header -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime,$(TARGET)-no_runtime-no_bounds_query $(METADATA_TESTER_GENERATOR_ARGS)

# c_source output doesn't work properly with multitarget output
$(FILTERS_DIR)/metadata_tester.halide_generated.cpp: $(BIN_DIR)/metadata_tester.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g metadata_tester -f metadata_tester -e c_source -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime $(METADATA_TESTER_GENERATOR_ARGS)

$(FILTERS_DIR)/metadata_tester_ucon.a: $(BIN_DIR)/metadata_tester.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g metadata_tester -f metadata_tester_ucon $(GEN_AOT_OUTPUTS),function_info_header -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-user_context-no_runtime $(METADATA_TESTER_GENERATOR_ARGS)

$(BIN_DIR)/$(TARGET)/generator_aot_metadata_tester: $(FILTERS_DIR)/metadata_tester_ucon.a

$(BIN_DIR)/$(TARGET)/generator_aotcpp_metadata_tester: $(FILTERS_DIR)/metadata_tester_ucon.halide_generated.cpp

$(FILTERS_DIR)/multitarget.a: $(BIN_DIR)/multitarget.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g multitarget -f "HalideTest::multitarget" $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) \
		target=$(TARGET)-no_bounds_query-no_runtime-c_plus_plus_name_mangling,$(TARGET)-no_runtime-c_plus_plus_name_mangling  \
		-e assembly,bitcode,c_source,c_header,stmt_html,static_library,stmt

$(FILTERS_DIR)/msan.a: $(BIN_DIR)/msan.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g msan -f msan $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-msan

$(FILTERS_DIR)/sanitizercoverage.a: $(BIN_DIR)/sanitizercoverage.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g sanitizercoverage -f sanitizercoverage $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-sanitizer_coverage

# user_context needs to be generated with user_context as the first argument to its calls
$(FILTERS_DIR)/user_context.a: $(BIN_DIR)/user_context.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g user_context $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime-user_context

# ditto for user_context_insanity
$(FILTERS_DIR)/user_context_insanity.a: $(BIN_DIR)/user_context_insanity.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g user_context_insanity $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime-user_context

# ditto for async_parallel
$(FILTERS_DIR)/async_parallel.a: $(BIN_DIR)/async_parallel.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g async_parallel $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime-user_context

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

$(BIN_DIR)/$(TARGET)/generator_aotcpp_tiled_blur: $(FILTERS_DIR)/blur2x2.halide_generated.cpp
ifneq ($(TEST_CUDA), )
$(BIN_DIR)/$(TARGET)/generator_aotcpp_cxx_mangling: $(FILTERS_DIR)/cxx_mangling_gpu.halide_generated.cpp
endif
$(BIN_DIR)/$(TARGET)/generator_aotcpp_cxx_mangling: $(FILTERS_DIR)/cxx_mangling_externs.o
$(BIN_DIR)/$(TARGET)/generator_aotcpp_cxx_mangling_define_extern: $(FILTERS_DIR)/cxx_mangling.halide_generated.cpp $(FILTERS_DIR)/cxx_mangling_externs.o $(FILTERS_DIR)/cxx_mangling_define_extern_externs.o

$(BUILD_DIR)/stubuser_generator.o: $(FILTERS_DIR)/stubtest.stub.h $(FILTERS_DIR)/configure.stub.h
$(BIN_DIR)/stubuser.generator: $(BUILD_DIR)/stubtest_generator.o $(BUILD_DIR)/configure_generator.o

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

$(FILTERS_DIR)/stubuser_auto.a: $(BIN_DIR)/stubuser.generator $(BIN_MULLAPUDI2016)
	@mkdir -p $(@D)
	$(CURDIR)/$< -g stubuser $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) -f stubuser_auto target=$(TARGET)-no_runtime autoscheduler=Mullapudi2016 -p $(BIN_MULLAPUDI2016)

$(FILTERS_DIR)/autograd_grad.a: $(BIN_DIR)/autograd.generator $(BIN_MULLAPUDI2016)
	@mkdir -p $(@D)
	$(CURDIR)/$< -g autograd $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) -f autograd_grad target=$(TARGET)-no_runtime autoscheduler=Mullapudi2016 -d 1 -p $(BIN_MULLAPUDI2016)

# Usually, it's considered best practice to have one Generator per
# .cpp file, with the generator-name and filename matching;
# nested_externs_generators.cpp is a counterexample, and thus requires
# some special casing to get right.  First, make a special rule to
# build each of the Generators in nested_externs_generator.cpp (which
# all have the form nested_externs_*).
$(FILTERS_DIR)/nested_externs_%.a: $(BIN_DIR)/nested_externs.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g nested_externs_$* $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime-user_context-c_plus_plus_name_mangling

# Similarly, gpu_multi needs two different kernels to test compilation caching.
# Also requies user-context.
$(FILTERS_DIR)/gpu_multi_context_threaded_%.a: $(BIN_DIR)/gpu_multi_context_threaded.generator
	@mkdir -p $(@D)
	$(CURDIR)/$< -g gpu_multi_context_threaded_$* $(GEN_AOT_OUTPUTS) -o $(CURDIR)/$(FILTERS_DIR) target=$(TARGET)-no_runtime-user_context

GEN_AOT_CXX_FLAGS=$(TEST_CXX_FLAGS) -Wno-unknown-pragmas -Wno-unused-variable
GEN_AOT_INCLUDES=-I$(INCLUDE_DIR) -I$(FILTERS_DIR) -I$(ROOT_DIR)/src/runtime -I$(ROOT_DIR)/test/common -I $(ROOT_DIR)/apps/support -I $(SRC_DIR)/runtime -I$(ROOT_DIR)/tools
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
$(BIN_DIR)/$(TARGET)/generator_aotcpp_%: $(ROOT_DIR)/test/generator/%_aottest.cpp $(FILTERS_DIR)/%.halide_generated.cpp $(FILTERS_DIR)/%.h $(RUNTIME_EXPORTED_INCLUDES) $(BIN_DIR)/$(TARGET)/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) $(OPTIMIZE) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) -o $@

# MSAN test doesn't use the standard runtime
$(BIN_DIR)/$(TARGET)/generator_aot_msan: $(ROOT_DIR)/test/generator/msan_aottest.cpp $(FILTERS_DIR)/msan.a $(FILTERS_DIR)/msan.h $(RUNTIME_EXPORTED_INCLUDES)
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter-out %.h,$^) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) -o $@

# SanitizerCoverage test doesn't use the standard runtime
$(BIN_DIR)/$(TARGET)/generator_aot_sanitizercoverage: $(ROOT_DIR)/test/generator/sanitizercoverage_aottest.cpp $(FILTERS_DIR)/sanitizercoverage.a $(FILTERS_DIR)/sanitizercoverage.h $(RUNTIME_EXPORTED_INCLUDES)
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter-out %.h,$^) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) -o $@

# SanitizerCoverage test will never work with C++ backend
$(BIN_DIR)/$(TARGET)/generator_aotcpp_sanitizercoverage: $(ROOT_DIR)/test/generator/sanitizercoverage_aottest.cpp
	@mkdir -p $(@D)
	echo "SanitizerCoverage test will never work with C++ backend"
	exit 1

# alias has additional deps to link in
$(BIN_DIR)/$(TARGET)/generator_aot_alias: $(ROOT_DIR)/test/generator/alias_aottest.cpp $(FILTERS_DIR)/alias.a $(FILTERS_DIR)/alias_with_offset_42.a $(FILTERS_DIR)/alias_Adams2019.a $(FILTERS_DIR)/alias_Li2018.a $(FILTERS_DIR)/alias_Mullapudi2016.a  $(RUNTIME_EXPORTED_INCLUDES) $(BIN_DIR)/$(TARGET)/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) -o $@

$(BIN_DIR)/$(TARGET)/generator_aotcpp_alias: $(ROOT_DIR)/test/generator/alias_aottest.cpp $(FILTERS_DIR)/alias.halide_generated.cpp $(FILTERS_DIR)/alias_with_offset_42.halide_generated.cpp $(FILTERS_DIR)/alias_Adams2019.halide_generated.cpp $(FILTERS_DIR)/alias_Li2018.halide_generated.cpp $(FILTERS_DIR)/alias_Mullapudi2016.halide_generated.cpp  $(RUNTIME_EXPORTED_INCLUDES) $(BIN_DIR)/$(TARGET)/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) -o $@

# autograd has additional deps to link in
$(BIN_DIR)/$(TARGET)/generator_aot_autograd: $(ROOT_DIR)/test/generator/autograd_aottest.cpp $(FILTERS_DIR)/autograd.a $(FILTERS_DIR)/autograd_grad.a $(RUNTIME_EXPORTED_INCLUDES) $(BIN_DIR)/$(TARGET)/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) -o $@

$(BIN_DIR)/$(TARGET)/generator_aotcpp_autograd: $(ROOT_DIR)/test/generator/autograd_aottest.cpp $(FILTERS_DIR)/autograd.halide_generated.cpp $(FILTERS_DIR)/autograd_grad.halide_generated.cpp $(RUNTIME_EXPORTED_INCLUDES) $(BIN_DIR)/$(TARGET)/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) -o $@

# nested_externs has additional deps to link in
$(BIN_DIR)/$(TARGET)/generator_aot_nested_externs: $(ROOT_DIR)/test/generator/nested_externs_aottest.cpp $(FILTERS_DIR)/nested_externs_root.a $(FILTERS_DIR)/nested_externs_inner.a $(FILTERS_DIR)/nested_externs_combine.a $(FILTERS_DIR)/nested_externs_leaf.a $(RUNTIME_EXPORTED_INCLUDES) $(BIN_DIR)/$(TARGET)/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) -o $@

$(BIN_DIR)/$(TARGET)/generator_aotcpp_nested_externs: $(ROOT_DIR)/test/generator/nested_externs_aottest.cpp $(FILTERS_DIR)/nested_externs_root.halide_generated.cpp $(FILTERS_DIR)/nested_externs_inner.halide_generated.cpp $(FILTERS_DIR)/nested_externs_combine.halide_generated.cpp $(FILTERS_DIR)/nested_externs_leaf.halide_generated.cpp $(RUNTIME_EXPORTED_INCLUDES) $(BIN_DIR)/$(TARGET)/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) -o $@

# The gpu object lifetime test needs the debug runtime
$(BIN_DIR)/$(TARGET)/generator_aot_gpu_object_lifetime: $(ROOT_DIR)/test/generator/gpu_object_lifetime_aottest.cpp $(FILTERS_DIR)/gpu_object_lifetime.a $(FILTERS_DIR)/gpu_object_lifetime.h $(RUNTIME_EXPORTED_INCLUDES) $(BIN_DIR)/$(TARGET)-debug/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) $(TEST_LD_FLAGS) -o $@

# acquire_release explicitly uses CUDA/OpenCL APIs, so link those here.
$(BIN_DIR)/$(TARGET)/generator_aot_acquire_release: $(ROOT_DIR)/test/generator/acquire_release_aottest.cpp $(FILTERS_DIR)/acquire_release.a $(FILTERS_DIR)/acquire_release.h $(RUNTIME_EXPORTED_INCLUDES) $(BIN_DIR)/$(TARGET)/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) $(OPENCL_LD_FLAGS) $(CUDA_LD_FLAGS) -o $@

$(BIN_DIR)/$(TARGET)/generator_aotcpp_acquire_release: $(ROOT_DIR)/test/generator/acquire_release_aottest.cpp $(FILTERS_DIR)/acquire_release.halide_generated.cpp $(FILTERS_DIR)/acquire_release.h $(RUNTIME_EXPORTED_INCLUDES) $(BIN_DIR)/$(TARGET)/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) $(OPENCL_LD_FLAGS) $(CUDA_LD_FLAGS) -o $@

# define_extern_opencl explicitly uses OpenCL APIs, so link those here.
$(BIN_DIR)/$(TARGET)/generator_aot_define_extern_opencl: $(ROOT_DIR)/test/generator/define_extern_opencl_aottest.cpp $(FILTERS_DIR)/define_extern_opencl.a $(FILTERS_DIR)/define_extern_opencl.h $(RUNTIME_EXPORTED_INCLUDES) $(BIN_DIR)/$(TARGET)/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) $(OPENCL_LD_FLAGS) -o $@

$(BIN_DIR)/$(TARGET)/generator_aotcpp_define_extern_opencl: $(ROOT_DIR)/test/generator/define_extern_opencl_aottest.cpp $(FILTERS_DIR)/define_extern_opencl.halide_generated.cpp $(FILTERS_DIR)/define_extern_opencl.h $(RUNTIME_EXPORTED_INCLUDES) $(BIN_DIR)/$(TARGET)/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) $(OPENCL_LD_FLAGS) -o $@

# By default, %_jittest.cpp depends on libHalide, plus the stubs for the Generator. These are external tests that use the JIT.
$(BIN_DIR)/generator_jit_%: $(ROOT_DIR)/test/generator/%_jittest.cpp $(TEST_DEPS) $(FILTERS_DIR)/%.stub.h $(BUILD_DIR)/%_generator.o
	@mkdir -p $(@D)
	$(CXX) -g $(TEST_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) -I$(INCLUDE_DIR) -I$(FILTERS_DIR) -I $(ROOT_DIR)/apps/support $(TEST_LD_FLAGS) -o $@

# stubuser is run with autoscheduling too
$(BIN_DIR)/$(TARGET)/generator_aot_stubuser: $(ROOT_DIR)/test/generator/stubuser_aottest.cpp $(FILTERS_DIR)/stubuser.a $(FILTERS_DIR)/stubuser.h $(FILTERS_DIR)/stubuser_auto.a $(FILTERS_DIR)/stubuser_auto.h $(RUNTIME_EXPORTED_INCLUDES) $(BIN_DIR)/$(TARGET)/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) -o $@

# generator_aot_multitarget is run multiple times, with different env vars.
generator_aot_multitarget: $(BIN_DIR)/$(TARGET)/generator_aot_multitarget
	@mkdir -p $(@D)
	HL_MULTITARGET_TEST_USE_NOBOUNDSQUERY_FEATURE=0 $(CURDIR)/$<
	HL_MULTITARGET_TEST_USE_NOBOUNDSQUERY_FEATURE=1 $(CURDIR)/$<
	@-echo

# gpu_multi_context_threaded has additional deps to link in
$(BIN_DIR)/$(TARGET)/generator_aot_gpu_multi_context_threaded: $(ROOT_DIR)/test/generator/gpu_multi_context_threaded_aottest.cpp \
	                                                       $(FILTERS_DIR)/gpu_multi_context_threaded_add.a \
	                                                       $(FILTERS_DIR)/gpu_multi_context_threaded_mul.a \
	                                                       $(RUNTIME_EXPORTED_INCLUDES) $(BIN_DIR)/$(TARGET)/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) $(OPENCL_LD_FLAGS) $(CUDA_LD_FLAGS) -o $@

$(BIN_DIR)/$(TARGET)/generator_aotcpp_gpu_multi_context_threaded: $(ROOT_DIR)/test/generator/gpu_multi_context_threaded_aottest.cpp \
	                                                          $(FILTERS_DIR)/gpu_multi_context_threaded_add.halide_generated.cpp \
	                                                          $(FILTERS_DIR)/gpu_multi_context_threaded_mul.halide_generated.cpp \
	                                                          $(RUNTIME_EXPORTED_INCLUDES) $(BIN_DIR)/$(TARGET)/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(filter %.cpp %.o %.a,$^) $(GEN_AOT_INCLUDES) $(GEN_AOT_LD_FLAGS) $(OPENCL_LD_FLAGS) $(CUDA_LD_FLAGS) -o $@

# nested externs doesn't actually contain a generator named
# "nested_externs", and has no internal tests in any case.
test_generator_nested_externs:
	@echo "Skipping"

# gpu_multi actually contain a generator named
# "gpu_multi", and has no internal tests in any case.
test_generator_gpu_multi:
	@echo "Skipping"

# gpu_multi_context_threaded actually contain a generator named
# "gpu_multi", and has no internal tests in any case.
test_generator_gpu_multi_context_threaded:
	@echo "Skipping"

$(BUILD_DIR)/RunGenMain.o: $(ROOT_DIR)/tools/RunGenMain.cpp $(RUNTIME_EXPORTED_INCLUDES) $(ROOT_DIR)/tools/RunGen.h
	@mkdir -p $(@D)
	$(CXX) -c $< $(filter-out -g, $(TEST_CXX_FLAGS)) $(OPTIMIZE) -Os $(IMAGE_IO_CXX_FLAGS) -I$(INCLUDE_DIR) -I $(SRC_DIR)/runtime -I$(ROOT_DIR)/tools -o $@

$(FILTERS_DIR)/%.registration.o: $(FILTERS_DIR)/%.registration.cpp
	@mkdir -p $(@D)
	$(CXX) -c $< $(TEST_CXX_FLAGS) -o $@

$(FILTERS_DIR)/%.rungen: $(BUILD_DIR)/RunGenMain.o $(BIN_DIR)/$(TARGET)/runtime.a $(FILTERS_DIR)/%.registration.o $(FILTERS_DIR)/%.a
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -I$(FILTERS_DIR) \
		$(BUILD_DIR)/RunGenMain.o \
		$(BIN_DIR)/$(TARGET)/runtime.a \
		$(call alwayslink,$(FILTERS_DIR)/$*.registration.o) \
		$(FILTERS_DIR)/$*.a \
		$(GEN_AOT_LD_FLAGS) $(IMAGE_IO_LIBS) -o $@

RUNARGS ?=

$(FILTERS_DIR)/%.run: $(FILTERS_DIR)/%.rungen
	$(CURDIR)/$< $(RUNARGS)
	@-echo

$(FILTERS_DIR)/%.registration_extra.o: $(FILTERS_DIR)/%.registration.cpp
	@mkdir -p $(@D)
	$(CXX) -c $< $(TEST_CXX_FLAGS) -DHALIDE_REGISTER_EXTRA_KEY_VALUE_PAIRS_FUNC=halide_register_extra_key_value_pairs_$* -o $@

# Test the registration mechanism, independent of RunGen.
# Note that this depends on the registration_extra.o (rather than registration.o)
# because it compiles with HALIDE_REGISTER_EXTRA_KEY_VALUE_PAIRS_FUNC defined.
$(FILTERS_DIR)/registration_test: $(ROOT_DIR)/test/generator/registration_test.cpp \
														 $(BIN_DIR)/$(TARGET)/runtime.a \
														 $(FILTERS_DIR)/blur2x2.registration_extra.o $(FILTERS_DIR)/blur2x2.a \
														 $(FILTERS_DIR)/cxx_mangling.registration_extra.o $(FILTERS_DIR)/cxx_mangling.a \
														 $(FILTERS_DIR)/pyramid.registration_extra.o $(FILTERS_DIR)/pyramid.a
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(GEN_AOT_INCLUDES) \
			$(ROOT_DIR)/test/generator/registration_test.cpp \
			$(FILTERS_DIR)/blur2x2.registration_extra.o \
			$(FILTERS_DIR)/cxx_mangling.registration_extra.o \
			$(FILTERS_DIR)/pyramid.registration_extra.o \
			$(FILTERS_DIR)/blur2x2.a \
			$(FILTERS_DIR)/cxx_mangling.a \
			$(FILTERS_DIR)/pyramid.a \
      $(BIN_DIR)/$(TARGET)/runtime.a \
			$(GEN_AOT_LD_FLAGS) $(IMAGE_IO_LIBS) -o $@

# Test RunGen itself
$(FILTERS_DIR)/rungen_test: $(ROOT_DIR)/test/generator/rungen_test.cpp \
							$(BIN_DIR)/$(TARGET)/runtime.a \
							$(FILTERS_DIR)/example.registration.o \
							$(FILTERS_DIR)/example.a
	@mkdir -p $(@D)
	$(CXX) $(GEN_AOT_CXX_FLAGS) $(IMAGE_IO_CXX_FLAGS) $(GEN_AOT_INCLUDES) \
			$(ROOT_DIR)/test/generator/rungen_test.cpp \
			$(BIN_DIR)/$(TARGET)/runtime.a \
			$(call alwayslink,$(FILTERS_DIR)/example.registration.o) \
			$(FILTERS_DIR)/example.a \
			$(GEN_AOT_LD_FLAGS) $(IMAGE_IO_LIBS) -o $@

# Test linking multiple filters into a single RunGen instance
$(FILTERS_DIR)/multi_rungen: $(BUILD_DIR)/RunGenMain.o $(BIN_DIR)/$(TARGET)/runtime.a \
														 $(FILTERS_DIR)/blur2x2.registration.o $(FILTERS_DIR)/blur2x2.a \
														 $(FILTERS_DIR)/cxx_mangling.registration.o $(FILTERS_DIR)/cxx_mangling.a \
														 $(FILTERS_DIR)/pyramid.registration.o $(FILTERS_DIR)/pyramid.a
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -I$(FILTERS_DIR) \
			$(BUILD_DIR)/RunGenMain.o \
			$(BIN_DIR)/$(TARGET)/runtime.a \
			$(call alwayslink,$(FILTERS_DIR)/blur2x2.registration.o) \
			$(call alwayslink,$(FILTERS_DIR)/cxx_mangling.registration.o) \
			$(call alwayslink,$(FILTERS_DIR)/pyramid.registration.o) \
			$(FILTERS_DIR)/blur2x2.a \
			$(FILTERS_DIR)/cxx_mangling.a \
			$(FILTERS_DIR)/pyramid.a \
			$(GEN_AOT_LD_FLAGS) $(IMAGE_IO_LIBS) -o $@

# Test concatenating multiple registration files as well, which should also work
$(FILTERS_DIR)/multi_rungen2.registration.cpp: $(FILTERS_DIR)/blur2x2.registration.cpp $(FILTERS_DIR)/cxx_mangling.registration.cpp $(FILTERS_DIR)/pyramid.registration.cpp
	cat $^ > $@

$(FILTERS_DIR)/multi_rungen2: $(BUILD_DIR)/RunGenMain.o $(BIN_DIR)/$(TARGET)/runtime.a \
														 $(FILTERS_DIR)/multi_rungen2.registration.cpp \
														 $(FILTERS_DIR)/blur2x2.a \
														 $(FILTERS_DIR)/cxx_mangling.a \
														 $(FILTERS_DIR)/pyramid.a
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -I$(FILTERS_DIR) $^ $(GEN_AOT_LD_FLAGS) $(IMAGE_IO_LIBS) -o $@

$(BIN_DIR)/tutorial_%: $(ROOT_DIR)/tutorial/%.cpp $(TEST_DEPS)
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

$(BIN_DIR)/tutorial_lesson_15_generators: $(ROOT_DIR)/tutorial/lesson_15_generators.cpp $(TEST_DEPS) $(BUILD_DIR)/GenGen.o
	$(CXX) $(TUTORIAL_CXX_FLAGS) $(IMAGE_IO_CXX_FLAGS) $(OPTIMIZE_FOR_BUILD_TIME) $< $(BUILD_DIR)/GenGen.o \
	-I$(INCLUDE_DIR) $(TEST_LD_FLAGS) $(IMAGE_IO_LIBS) -o $@

tutorial_lesson_15_generators: $(ROOT_DIR)/tutorial/lesson_15_generators_usage.sh $(BIN_DIR)/tutorial_lesson_15_generators
	@-mkdir -p $(TMP_DIR)
	cp $(BIN_DIR)/tutorial_lesson_15_generators $(TMP_DIR)/lesson_15_generate; \
	cd $(TMP_DIR); \
	PATH="$${PATH}:$(CURDIR)/$(BIN_DIR)" source $(ROOT_DIR)/tutorial/lesson_15_generators_usage.sh
	@-echo

$(BIN_DIR)/tutorial_lesson_16_rgb_generate: $(ROOT_DIR)/tutorial/lesson_16_rgb_generate.cpp $(TEST_DEPS) $(BUILD_DIR)/GenGen.o
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

$(BIN_DIR)/tutorial_lesson_21_auto_scheduler_generate: $(ROOT_DIR)/tutorial/lesson_21_auto_scheduler_generate.cpp $(TEST_DEPS) $(BUILD_DIR)/GenGen.o
	$(CXX) $(TUTORIAL_CXX_FLAGS) $(IMAGE_IO_CXX_FLAGS) $(OPTIMIZE_FOR_BUILD_TIME) $< $(BUILD_DIR)/GenGen.o \
	-I$(INCLUDE_DIR) $(TEST_LD_FLAGS) $(IMAGE_IO_LIBS) -o $@

# The values are:
# - the maximum level of parallelism available,
# - the size of the last-level cache (in bytes),
# - the ratio between the cost of a miss at the last level cache and the cost
#   of arithmetic on the target architecture
# ...in that order.
LESSON_21_AUTOSCHEDULER_PARAMS=\
    autoscheduler=Mullapudi2016 \
    autoscheduler.parallelism=32 \
    autoscheduler.last_level_cache_size=16777216 \
    autoscheduler.balance=40

$(BIN_DIR)/tutorial_lesson_21_auto_scheduler_run: $(ROOT_DIR)/tutorial/lesson_21_auto_scheduler_run.cpp $(BIN_DIR)/tutorial_lesson_21_auto_scheduler_generate $(BIN_MULLAPUDI2016)
	@-mkdir -p $(TMP_DIR)
	# Run the generator
	$(BIN_DIR)/tutorial_lesson_21_auto_scheduler_generate -g auto_schedule_gen -o $(TMP_DIR) -e static_library,c_header,schedule -f auto_schedule_false target=host
	$(BIN_DIR)/tutorial_lesson_21_auto_scheduler_generate -g auto_schedule_gen -o $(TMP_DIR) -e static_library,c_header,schedule -f auto_schedule_true  target=host-no_runtime $(LESSON_21_AUTOSCHEDULER_PARAMS) -p $(BIN_MULLAPUDI2016)
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

correctness_opencl_runtime: $(BIN_DIR)/$(TARGET)/correctness_opencl_runtime
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; $(CURDIR)/$<
	@-echo

quiet_correctness_%: $(BIN_DIR)/correctness_%
	@-mkdir -p $(TMP_DIR)
	@cd $(TMP_DIR) ; ( $(CURDIR)/$< 2>stderr_$*.txt > stdout_$*.txt && echo -n . ) || ( echo ; echo FAILED TEST: $* ; cat stdout_$*.txt stderr_$*.txt ; false )

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
	cd $(TMP_DIR) ; $(CURDIR)/$< 2>&1 | egrep --q "terminating with uncaught exception|terminating due to uncaught exception|^terminate called|^Error|Assertion.*failed"
	@-echo

warning_%: $(BIN_DIR)/warning_%
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; $(CURDIR)/$< 2>&1 | egrep --q "^Warning"
	@-echo

runtime_common:
	# nothing

runtime_%: $(BIN_DIR)/runtime_%
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; $(CURDIR)/$<
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

# Skip the serialization tutorial, if we didn't build -DWITH_SERIALIZATION
ifeq (,$(shell which flatc))
tutorial_lesson_23_serialization:
	@echo "Skipping tutorial lesson 23 (serialization not enabled) ..."
endif

test_mullapudi2016: $(MULLAPUDI2016_TESTS:$(ROOT_DIR)/test/autoschedulers/mullapudi2016/%.cpp=mullapudi2016_%)

mullapudi2016_%: $(BIN_DIR)/mullapudi2016_% $(BIN_MULLAPUDI2016)
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; $(CURDIR)/$< $(realpath $(BIN_MULLAPUDI2016))
	@-echo

test_li2018: $(LI2018_TESTS:$(ROOT_DIR)/test/autoschedulers/li2018/%.cpp=li2018_%)

li2018_%: $(BIN_DIR)/li2018_% $(BIN_LI2018)
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; $(CURDIR)/$< $(realpath $(BIN_LI2018))
	@-echo

test_adams2019: $(ADAMS2019_TESTS:$(ROOT_DIR)/test/autoschedulers/adams2019/%.cpp=adams2019_%)

adams2019_test: $(BIN_DIR)/adams2019_test $(BIN_ADAMS2019) $(SRC_DIR)/autoschedulers/adams2019/baseline.weights
	@-mkdir -p $(TMP_DIR)
	cd $(TMP_DIR) ; $(CURDIR)/$< $(realpath $(BIN_ADAMS2019)) $(realpath $(SRC_DIR)/autoschedulers/adams2019/baseline.weights)
	@-echo

time_compilation_test_%: $(BIN_DIR)/test_%
	$(TIME_COMPILATION) compile_times_correctness.csv make -f $(THIS_MAKEFILE) $(@:time_compilation_test_%=test_%)

time_compilation_performance_%: $(BIN_DIR)/performance_%
	$(TIME_COMPILATION) compile_times_performance.csv make -f $(THIS_MAKEFILE) $(@:time_compilation_performance_%=performance_%)

time_compilation_generator_%: $(BIN_DIR)/%.generator
	$(TIME_COMPILATION) compile_times_generator.csv make -f $(THIS_MAKEFILE) $(@:time_compilation_generator_%=$(FILTERS_DIR)/%.a)

TEST_APPS=\
	bilateral_grid \
	bgu \
	blur \
	c_backend \
	camera_pipe \
	conv_layer \
	fft \
	hist \
	interpolate \
	lens_blur \
	linear_algebra \
	local_laplacian \
	max_filter \
	nl_means \
	onnx \
	resize \
	resnet_50 \
	stencil_chain \
	wavelet

TEST_APPS_DEPS=$(TEST_APPS:%=%_test_app)
BUILD_APPS_DEPS=$(TEST_APPS:%=%_build_app)

$(BUILD_APPS_DEPS): distrib
	@echo Building app $(@:%_build_app=%) for ${HL_TARGET}...
	@$(MAKE) -C $(ROOT_DIR)/apps/$(@:%_build_app=%) build \
		HALIDE_DISTRIB_PATH=$(CURDIR)/$(DISTRIB_DIR) \
		BIN_DIR=$(CURDIR)/$(BIN_DIR)/apps/$(@:%_build_app=%)/bin \
		HL_TARGET=$(HL_TARGET) \
		|| exit 1 ; \

$(TEST_APPS_DEPS): distrib
	@echo Testing app $(@:%_test_app=%) for ${HL_TARGET}...
	@$(MAKE) -C $(ROOT_DIR)/apps/$(@:%_test_app=%) test \
		HALIDE_DISTRIB_PATH=$(CURDIR)/$(DISTRIB_DIR) \
		BIN_DIR=$(CURDIR)/$(BIN_DIR)/apps/$(@:%_test_app=%)/bin \
		HL_TARGET=$(HL_TARGET) \
		|| exit 1 ; \

.PHONY: test_apps build_apps $(BUILD_APPS_DEPS)
build_apps: $(BUILD_APPS_DEPS)

test_apps: $(BUILD_APPS_DEPS)
	$(MAKE) -f $(THIS_MAKEFILE) -j1 $(TEST_APPS_DEPS)

build_hannk: distrib
	@echo Building apps/hannk for ${HL_TARGET}...
	@$(MAKE) -C $(ROOT_DIR)/apps/hannk build \
		HALIDE_DISTRIB_PATH=$(CURDIR)/$(DISTRIB_DIR) \
		BIN_DIR=$(CURDIR)/$(BIN_DIR)/apps/hannk/bin \
		HL_TARGET=$(HL_TARGET) \
		|| exit 1 ; \

test_hannk: build_hannk
	@echo Testing apps/hannk for ${HL_TARGET}...
	@$(MAKE) -C $(ROOT_DIR)/apps/hannk test \
		HALIDE_DISTRIB_PATH=$(CURDIR)/$(DISTRIB_DIR) \
		BIN_DIR=$(CURDIR)/$(BIN_DIR)/apps/hannk/bin \
		HL_TARGET=$(HL_TARGET) \
		|| exit 1 ; \

BENCHMARK_APPS=\
	bilateral_grid \
	camera_pipe \
	lens_blur \
	local_laplacian \
	nl_means \
	stencil_chain

$(BENCHMARK_APPS): distrib
	@echo Building $@ for ${HL_TARGET}...
	@$(MAKE) -C $(ROOT_DIR)/apps/$@ \
		$(CURDIR)/$(BIN_DIR)/apps/$@/bin/$(HL_TARGET)/$@.rungen \
		HALIDE_DISTRIB_PATH=$(CURDIR)/$(DISTRIB_DIR) \
		BIN_DIR=$(CURDIR)/$(BIN_DIR)/apps/$@/bin \
		HL_TARGET=$(HL_TARGET) \
		> /dev/null \
		|| exit 1

.PHONY: benchmark_apps $(BENCHMARK_APPS)
benchmark_apps: $(BENCHMARK_APPS)
	@for APP in $(BENCHMARK_APPS); do \
		echo ;\
		echo Benchmarking $${APP} for ${HL_TARGET}... ; \
		make -C $(ROOT_DIR)/apps/$${APP} \
			$${APP}.benchmark \
			HALIDE_DISTRIB_PATH=$(CURDIR)/$(DISTRIB_DIR) \
			BIN_DIR=$(CURDIR)/$(BIN_DIR)/apps/$${APP}/bin \
			HL_TARGET=$(HL_TARGET) \
			|| exit 1 ; \
	done

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

ifneq (,$(findstring clang version 7.1,$(CLANG_VERSION)))
CLANG_OK=yes
endif

ifneq (,$(findstring clang version 8.0,$(CLANG_VERSION)))
CLANG_OK=yes
endif

ifneq (,$(findstring clang version 9.0,$(CLANG_VERSION)))
CLANG_OK=yes
endif

ifneq (,$(findstring clang version 10.0,$(CLANG_VERSION)))
CLANG_OK=yes
endif

ifneq (,$(findstring clang version 11.0,$(CLANG_VERSION)))
CLANG_OK=yes
endif

ifneq (,$(findstring clang version 11.1,$(CLANG_VERSION)))
CLANG_OK=yes
endif

ifneq (,$(findstring clang version 12.0,$(CLANG_VERSION)))
CLANG_OK=yes
endif

ifneq (,$(findstring clang version 13.0,$(CLANG_VERSION)))
CLANG_OK=yes
endif

ifneq (,$(findstring clang version 14.0,$(CLANG_VERSION)))
CLANG_OK=yes
endif

ifneq (,$(findstring clang version 15.0,$(CLANG_VERSION)))
CLANG_OK=yes
endif

ifneq (,$(findstring clang version 16.0,$(CLANG_VERSION)))
CLANG_OK=yes
endif

ifneq (,$(findstring clang version 17.0,$(CLANG_VERSION)))
CLANG_OK=yes
endif

ifneq (,$(findstring clang version 18.0,$(CLANG_VERSION)))
CLANG_OK=yes
endif

ifneq (,$(findstring clang version 19.0,$(CLANG_VERSION)))
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

ifneq (,$(findstring $(LLVM_VERSION_TIMES_10), 160 170 180 190))
LLVM_OK=yes
endif

ifneq ($(LLVM_OK), )
$(BUILD_DIR)/llvm_ok: $(BUILD_DIR)/rtti_ok
	@echo "Found a new enough version of llvm"
	mkdir -p $(BUILD_DIR)
	touch $(BUILD_DIR)/llvm_ok
else
$(BUILD_DIR)/llvm_ok:
	@echo "Can't find llvm or version of llvm too old (we need 9.0 or greater):"
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
	cp $(ROOT_DIR)/tools/GenGen.cpp $(PREFIX)/share/halide/tools
	cp $(ROOT_DIR)/tools/RunGen.h $(PREFIX)/share/halide/tools
	cp $(ROOT_DIR)/tools/RunGenMain.cpp $(PREFIX)/share/halide/tools
	cp $(ROOT_DIR)/tools/halide_image.h $(PREFIX)/share/halide/tools
	cp $(ROOT_DIR)/tools/halide_image_io.h $(PREFIX)/share/halide/tools
	cp $(ROOT_DIR)/tools/halide_image_info.h $(PREFIX)/share/halide/tools
	cp $(ROOT_DIR)/tools/halide_malloc_trace.h $(PREFIX)/share/halide/tools
	cp $(ROOT_DIR)/tools/halide_thread_pool.h $(PREFIX)/share/halide/tools
ifeq ($(UNAME), Darwin)
	install_name_tool -id $(PREFIX)/lib/libHalide.$(SHARED_EXT) $(PREFIX)/lib/libHalide.$(SHARED_EXT)
endif

# This is a specialized 'install' for users who need Hexagon support libraries as well.
install_qc: install $(HEXAGON_RUNTIME_LIBS)
	mkdir -p $(PREFIX)/bin $(PREFIX)/tools $(PREFIX)/support
	mkdir -p $(PREFIX)/lib/arm-32-android
	mkdir -p $(PREFIX)/lib/arm-64-android
	mkdir -p $(PREFIX)/lib/adsp/arm-32-android
	mkdir -p $(PREFIX)/lib/adsp/arm-64-android
	mkdir -p $(PREFIX)/lib/cdsp/arm-32-android
	mkdir -p $(PREFIX)/lib/cdsp/arm-64-android
	mkdir -p $(PREFIX)/lib/host
	mkdir -p $(PREFIX)/lib/v65
	cp $(HEXAGON_RUNTIME_LIBS_DIR)/arm-32-android/* $(PREFIX)/lib/arm-32-android
	cp $(HEXAGON_RUNTIME_LIBS_DIR)/arm-64-android/* $(PREFIX)/lib/arm-64-android
	cp $(HEXAGON_RUNTIME_LIBS_DIR)/cdsp/arm-32-android/* $(PREFIX)/lib/cdsp/arm-32-android
	cp $(HEXAGON_RUNTIME_LIBS_DIR)/cdsp/arm-64-android/* $(PREFIX)/lib/cdsp/arm-64-android
	cp $(HEXAGON_RUNTIME_LIBS_DIR)/adsp/arm-32-android/* $(PREFIX)/lib/adsp/arm-32-android
	cp $(HEXAGON_RUNTIME_LIBS_DIR)/adsp/arm-64-android/* $(PREFIX)/lib/adsp/arm-64-android
	cp $(HEXAGON_RUNTIME_LIBS_DIR)/host/* $(PREFIX)/lib/host
	cp -r $(HEXAGON_RUNTIME_LIBS_DIR)/v65/* $(PREFIX)/lib/v65
	ln -sf ../share/halide/tools/GenGen.cpp $(PREFIX)/tools/GenGen.cpp
	ln -sf ../lib/v65/hexagon_sim_remote $(PREFIX)/bin/hexagon_sim_remote
	ln -sf v65/libsim_qurt.a $(PREFIX)/lib/libsim_qurt.a
	ln -sf v65/libhalide_hexagon_remote_skel.so $(PREFIX)/lib/libhalide_hexagon_remote_skel.so

# We need to capture the system libraries that we'll need to link
# against, so that downstream consumers of our build rules don't
# have to guess what's necessary on their system; call
# llvm-config and capture the result in config files that
# we include in our distribution.
HALIDE_RTTI_RAW=$(if $(WITH_RTTI),1,0)

$(BUILD_DIR)/halide_config.%: $(ROOT_DIR)/tools/halide_config.%.tpl
	@mkdir -p $(@D)
	cat $< | sed -e 's/@HALIDE_SYSTEM_LIBS_RAW@/${LLVM_SYSTEM_LIBS}/g' \
	       | sed -e 's/@HALIDE_RTTI_RAW@/${HALIDE_RTTI_RAW}/g' \
	       | sed -e 's;@HALIDE_LLVM_CXX_FLAGS_RAW@;${LLVM_CXX_FLAGS};g' > $@


$(DISTRIB_DIR)/lib/libHalide.$(SHARED_EXT): \
			   $(LIB_DIR)/libHalide.a \
	       		   $(BIN_DIR)/libHalide.$(SHARED_EXT) \
                           $(INCLUDE_DIR)/Halide.h \
                           $(RUNTIME_EXPORTED_INCLUDES) \
                           $(ROOT_DIR)/README*.md \
                           $(BUILD_DIR)/halide_config.make
	rm -rf $(DISTRIB_DIR)
	mkdir -p $(DISTRIB_DIR)/include \
	         $(DISTRIB_DIR)/bin \
	         $(DISTRIB_DIR)/lib \
	         $(DISTRIB_DIR)/tutorial \
	         $(DISTRIB_DIR)/tutorial/images \
	         $(DISTRIB_DIR)/tools \
	         $(DISTRIB_DIR)/tutorial/figures
	cp $(BIN_DIR)/libHalide.$(SHARED_EXT) $(DISTRIB_DIR)/lib
	cp $(LIB_DIR)/libHalide.a $(DISTRIB_DIR)/lib
	cp $(INCLUDE_DIR)/Halide.h $(DISTRIB_DIR)/include
	cp $(INCLUDE_DIR)/HalideBuffer.h $(DISTRIB_DIR)/include
	cp $(INCLUDE_DIR)/HalideRuntim*.h $(DISTRIB_DIR)/include
	cp $(INCLUDE_DIR)/HalidePyTorch*.h $(DISTRIB_DIR)/include
	cp $(ROOT_DIR)/tutorial/images/*.png $(DISTRIB_DIR)/tutorial/images
	cp $(ROOT_DIR)/tutorial/figures/*.gif $(DISTRIB_DIR)/tutorial/figures
	cp $(ROOT_DIR)/tutorial/figures/*.jpg $(DISTRIB_DIR)/tutorial/figures
	cp $(ROOT_DIR)/tutorial/figures/*.mp4 $(DISTRIB_DIR)/tutorial/figures
	cp $(ROOT_DIR)/tutorial/*.cpp $(DISTRIB_DIR)/tutorial
	cp $(ROOT_DIR)/tutorial/*.h $(DISTRIB_DIR)/tutorial
	cp $(ROOT_DIR)/tutorial/*.sh $(DISTRIB_DIR)/tutorial
	cp $(ROOT_DIR)/tools/GenGen.cpp $(DISTRIB_DIR)/tools
	cp $(ROOT_DIR)/tools/RunGen.h $(DISTRIB_DIR)/tools
	cp $(ROOT_DIR)/tools/RunGenMain.cpp $(DISTRIB_DIR)/tools
	cp $(ROOT_DIR)/tools/halide_benchmark.h $(DISTRIB_DIR)/tools
	cp $(ROOT_DIR)/tools/halide_image.h $(DISTRIB_DIR)/tools
	cp $(ROOT_DIR)/tools/halide_image_io.h $(DISTRIB_DIR)/tools
	cp $(ROOT_DIR)/tools/halide_image_info.h $(DISTRIB_DIR)/tools
	cp $(ROOT_DIR)/tools/halide_malloc_trace.h $(DISTRIB_DIR)/tools
	cp $(ROOT_DIR)/tools/halide_thread_pool.h $(DISTRIB_DIR)/tools
	cp $(ROOT_DIR)/tools/halide_trace_config.h $(DISTRIB_DIR)/tools
	cp $(ROOT_DIR)/README*.md $(DISTRIB_DIR)
	cp $(BUILD_DIR)/halide_config.* $(DISTRIB_DIR)
ifeq ($(UNAME), Darwin)
	install_name_tool -id @rpath/libHalide.$(SHARED_EXT) $(DISTRIB_DIR)/lib/libHalide.$(SHARED_EXT)
endif

$(BIN_DIR)/libautoschedule_%.$(PLUGIN_EXT): $(DISTRIB_DIR)/lib/libHalide.$(SHARED_EXT)
	$(MAKE) -f $(SRC_DIR)/autoschedulers/$*/Makefile $@ HALIDE_DISTRIB_PATH=$(CURDIR)/$(DISTRIB_DIR)
ifeq ($(UNAME), Darwin)
	install_name_tool -id @rpath/$(@F) $(CURDIR)/$@
endif


$(DISTRIB_DIR)/lib/libautoschedule_%.$(PLUGIN_EXT): $(BIN_DIR)/libautoschedule_%.$(PLUGIN_EXT)
	@mkdir -p $(@D)
	cp $< $(DISTRIB_DIR)/lib
ifeq ($(UNAME), Darwin)
	install_name_tool -id @rpath/$(@F) $(CURDIR)/$@
endif

# Build some common tools
$(DISTRIB_DIR)/bin/featurization_to_sample $(DISTRIB_DIR)/bin/get_host_target: $(DISTRIB_DIR)/lib/libHalide.$(SHARED_EXT)
	@mkdir -p $(@D)
	$(MAKE) -f $(SRC_DIR)/autoschedulers/common/Makefile $(BIN_DIR)/featurization_to_sample $(BIN_DIR)/get_host_target HALIDE_DISTRIB_PATH=$(CURDIR)/$(DISTRIB_DIR)
	for TOOL in featurization_to_sample get_host_target; do \
		cp $(BIN_DIR)/$${TOOL} $(DISTRIB_DIR)/bin/;  \
	done

# Adams2019 also includes autotuning tools
$(DISTRIB_DIR)/lib/libautoschedule_adams2019.$(PLUGIN_EXT): $(BIN_DIR)/libautoschedule_adams2019.$(PLUGIN_EXT)
	@mkdir -p $(@D)
	$(MAKE) -f $(SRC_DIR)/autoschedulers/adams2019/Makefile $(BIN_DIR)/adams2019_retrain_cost_model $(BIN_DIR)/adams2019_weightsdir_to_weightsfile HALIDE_DISTRIB_PATH=$(CURDIR)/$(DISTRIB_DIR)
	cp $< $(DISTRIB_DIR)/lib/
	for TOOL in adams2019_retrain_cost_model adams2019_weightsdir_to_weightsfile; do \
		cp $(BIN_DIR)/$${TOOL} $(DISTRIB_DIR)/bin/;  \
	done
	cp $(SRC_DIR)/autoschedulers/adams2019/adams2019_autotune_loop.sh $(DISTRIB_DIR)/tools/
ifeq ($(UNAME), Darwin)
	install_name_tool -id @rpath/$(@F) $(CURDIR)/$@
endif

autoschedulers: \
$(DISTRIB_DIR)/lib/libautoschedule_mullapudi2016.$(PLUGIN_EXT) \
$(DISTRIB_DIR)/lib/libautoschedule_li2018.$(PLUGIN_EXT) \
$(DISTRIB_DIR)/lib/libautoschedule_adams2019.$(PLUGIN_EXT) \
$(DISTRIB_DIR)/bin/featurization_to_sample \
$(DISTRIB_DIR)/bin/get_host_target

.PHONY: distrib
distrib: $(DISTRIB_DIR)/lib/libHalide.$(SHARED_EXT) autoschedulers

$(DISTRIB_DIR)/halide.tgz: distrib
	ln -sf $(DISTRIB_DIR) halide
	tar -czf $(BUILD_DIR)/halide.tgz \
		halide/bin \
		halide/lib \
		halide/include \
		halide/tools \
		halide/tutorial \
		halide/README*.md \
		halide/halide_config.*
	rm -rf halide
	mv $(BUILD_DIR)/halide.tgz $(DISTRIB_DIR)/halide.tgz

$(BIN_DIR)/HalideTraceViz: $(ROOT_DIR)/util/HalideTraceViz.cpp $(INCLUDE_DIR)/HalideRuntime.h $(ROOT_DIR)/tools/halide_image_io.h $(ROOT_DIR)/tools/halide_trace_config.h
	$(CXX) $(OPTIMIZE) -std=c++17 $(filter %.cpp,$^) -I$(INCLUDE_DIR) -I$(ROOT_DIR)/tools -L$(BIN_DIR) -o $@

$(BIN_DIR)/HalideTraceDump: $(ROOT_DIR)/util/HalideTraceDump.cpp $(ROOT_DIR)/util/HalideTraceUtils.cpp $(INCLUDE_DIR)/HalideRuntime.h $(ROOT_DIR)/tools/halide_image_io.h
	$(CXX) $(OPTIMIZE) -std=c++17 $(filter %.cpp,$^) -I$(INCLUDE_DIR) -I$(ROOT_DIR)/tools -I$(ROOT_DIR)/src/runtime -L$(BIN_DIR) $(IMAGE_IO_CXX_FLAGS) $(IMAGE_IO_LIBS) -o $@

# Note: you must have CLANG_FORMAT_LLVM_INSTALL_DIR set for this rule to work.
# Let's default to the Ubuntu install location.
CLANG_FORMAT_LLVM_INSTALL_DIR ?= /usr/lib/llvm-12

.PHONY: format
format:
	@CLANG_FORMAT_LLVM_INSTALL_DIR=$(CLANG_FORMAT_LLVM_INSTALL_DIR) ${ROOT_DIR}/run-clang-format.sh

# Note: you must have CLANG_TIDY_LLVM_INSTALL_DIR set for these rules to work.
# Let's default to the Ubuntu install location.
CLANG_TIDY_LLVM_INSTALL_DIR ?= /usr/lib/llvm-12

.PHONY: clang-tidy
clang-tidy:
	@CLANG_TIDY_LLVM_INSTALL_DIR=$(CLANG_TIDY_LLVM_INSTALL_DIR) ${ROOT_DIR}/run-clang-tidy.sh

.PHONY: clang-tidy-fix
clang-tidy-fix:
	@CLANG_TIDY_LLVM_INSTALL_DIR=$(CLANG_TIDY_LLVM_INSTALL_DIR) ${ROOT_DIR}/run-clang-tidy.sh -fix

# Build the documentation. Be sure to keep this synchronized with doc/CMakeLists.txt
# if you choose to edit it.

# Copy ROOT_DIR to keep the following Doxyfile closer to CMake
Halide_SOURCE_DIR=${ROOT_DIR}

define Doxyfile
# Keep the following in sync with doc/CMakeLists.txt
ALPHABETICAL_INDEX     = NO
BUILTIN_STL_SUPPORT    = YES
CASE_SENSE_NAMES       = NO
CLASS_DIAGRAMS         = NO
DISTRIBUTE_GROUP_DOC   = YES
EXAMPLE_PATH           = "${Halide_SOURCE_DIR}/tutorial"
EXCLUDE                = bin
EXTRACT_ALL            = YES
EXTRACT_LOCAL_CLASSES  = NO
FILE_PATTERNS          = *.h
GENERATE_TREEVIEW      = YES
HIDE_FRIEND_COMPOUNDS  = YES
HIDE_IN_BODY_DOCS      = YES
HIDE_UNDOC_CLASSES     = YES
HIDE_UNDOC_MEMBERS     = YES
JAVADOC_AUTOBRIEF      = YES
QT_AUTOBRIEF           = YES
QUIET                  = YES
RECURSIVE              = YES
REFERENCED_BY_RELATION = YES
REFERENCES_RELATION    = YES
SORT_BY_SCOPE_NAME     = YES
SORT_MEMBER_DOCS       = NO
SOURCE_BROWSER         = YES
STRIP_CODE_COMMENTS    = NO

# Makefile-specific options
GENERATE_LATEX         = NO
HAVE_DOT               = NO
HTML_OUTPUT            = .
INPUT                  = "${Halide_SOURCE_DIR}/src" "${Halide_SOURCE_DIR}/test"
OUTPUT_DIRECTORY       = ${DOC_DIR}
PROJECT_NAME           = Halide
endef

# Make the above Doxyfile variable available to the doc target.
export Doxyfile

.PHONY: doc
doc:
	@-mkdir -p $(TMP_DIR)
	echo "$$Doxyfile" > $(TMP_DIR)/Doxyfile
	@-mkdir -p ${DOC_DIR}
	doxygen $(TMP_DIR)/Doxyfile
