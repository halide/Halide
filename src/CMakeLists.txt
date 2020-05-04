include(CheckCXXCompilerFlag)

if (MSVC)
  # -g produces dwarf debugging info, which is not useful on windows
  #  (and fails to compile due to llvm bug 15393)
  set(RUNTIME_DEBUG_FLAG "")
else()
  set(RUNTIME_DEBUG_FLAG "-g")
endif()

set(RUNTIME_CPP
  aarch64_cpu_features
  alignment_128
  alignment_32
  allocation_cache
  alignment_64
  android_clock
  android_host_cpu_count
  android_io
  arm_cpu_features
  halide_buffer_t
  cache
  can_use_target
  cuda
  d3d12compute
  destructors
  device_interface
  errors
  fake_get_symbol
  fake_thread_pool
  float16_t
  fuchsia_clock
  fuchsia_host_cpu_count
  fuchsia_yield
  gpu_device_selection
  hexagon_cache_allocator
  hexagon_cpu_features
  hexagon_dma
  hexagon_dma_pool
  hexagon_host
  ios_io
  linux_clock
  linux_host_cpu_count
  linux_yield
  matlab
  metadata
  metal
  metal_objc_arm
  metal_objc_x86
  mips_cpu_features
  module_aot_ref_count
  module_jit_ref_count
  msan
  msan_stubs
  opencl
  opengl
  openglcompute
  opengl_egl_context
  opengl_glx_context
  osx_clock
  osx_get_symbol
  osx_host_cpu_count
  osx_opengl_context
  osx_yield
  posix_abort
  posix_allocator
  posix_clock
  posix_error_handler
  posix_get_symbol
  posix_io
  posix_print
  posix_threads
  posix_threads_tsan
  powerpc_cpu_features
  prefetch
  profiler
  profiler_inlined
  pseudostack
  qurt_allocator
  qurt_hvx
  qurt_hvx_vtcm
  qurt_init_fini
  qurt_threads
  qurt_threads_tsan
  qurt_yield
  riscv_cpu_features
  runtime_api
  ssp
  to_string
  trace_helper
  tracing
  wasm_cpu_features
  windows_abort
  windows_clock
  windows_cuda
  windows_get_symbol
  windows_io
  windows_opencl
  windows_profiler
  windows_threads
  windows_threads_tsan
  windows_yield
  write_debug_image
  x86_cpu_features
)

set (RUNTIME_LL
  aarch64
  arm
  arm_no_neon
  hvx_64
  hvx_128
  mips
  posix_math
  powerpc
  ptx_dev
  wasm_math
  win32_math
  x86
  x86_avx
  x86_avx2
  x86_sse41
  d3d12_abi_patch_64
)

set (RUNTIME_BC
  compute_20
  compute_30
  compute_35
)

set(RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/runtime")
file(TO_NATIVE_PATH "${RUNTIME_DIR}/" NATIVE_RUNTIME_DIR)
file(TO_NATIVE_PATH "${CMAKE_CFG_INTDIR}/" NATIVE_INT_DIR)

# ./ seems to confuse cmake on linux
if("${NATIVE_INT_DIR}" STREQUAL "./")
  set(NATIVE_INT_DIR "")
endif()

# Commands to build initial module objects.
file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.build")
set (CXX_WARNING_FLAGS -Wall -Werror -Wno-unused-function -Wcast-qual)
set (RUNTIME_CXX_FLAGS -O3 -fno-vectorize -ffreestanding -fno-blocks -fno-exceptions -fno-unwind-tables -fpic -std=gnu++98)
set (INITMOD_PREFIX "_initmod_")
set (ARCHS 32 64)
set(INITIAL_MODULES )

function(add_runtime_modules TARGET_RUNTIME_FILES TARGET_RUNTIME_DIR)
  foreach (i ${TARGET_RUNTIME_FILES} )
    foreach (j ${ARCHS} )
      IF (${j} EQUAL 32)
        IF (${i} MATCHES "windows_.*")
          # win32 uses the stdcall calling convention, which is x86-specific
          set(TARGET "i386-unknown-unknown-unknown")
        ELSE()
          # (The 'nacl' is a red herring. This is just a generic 32-bit little-endian target.)
          set(TARGET "le32-unknown-nacl-unknown")
        ENDIF()
      ELSE()
        # generic 64-bit code
        set(TARGET "le64-unknown-unknown-unknown")
      ENDIF()
      set(SOURCE "${TARGET_RUNTIME_DIR}${i}.cpp")
      set(LL_D "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.build/${NATIVE_INT_DIR}initmod.${i}_${j}_debug.ll")
      set(LL "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.build/${NATIVE_INT_DIR}initmod.${i}_${j}.ll")
      set(BC_D "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.build/${NATIVE_INT_DIR}initmod.${i}_${j}_debug.bc")
      set(BC "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.build/${NATIVE_INT_DIR}initmod.${i}_${j}.bc")
      set(INITMOD_D "${INITMOD_PREFIX}${i}_${j}_debug.cpp")
      set(INITMOD "${INITMOD_PREFIX}${i}_${j}.cpp")

      # -std=gnu++98 is deliberate; we do NOT want c++11 here,
      # as we don't want static locals to get thread synchronization stuff.
      add_custom_command(OUTPUT "${LL_D}"
                         # COMMENT "${SOURCE} -> ${LL_D}"
                         DEPENDS "${SOURCE}"
                         VERBATIM
                         COMMAND ${CLANG} ${CXX_WARNING_FLAGS} ${RUNTIME_DEBUG_FLAG} -DDEBUG_RUNTIME ${RUNTIME_CXX_FLAGS} -m${j} -target ${TARGET} -I${TARGET_RUNTIME_DIR} -DCOMPILING_HALIDE_RUNTIME -DLLVM_VERSION=${LLVM_VERSION} -DBITS_${j} -emit-llvm -S ${SOURCE} -o ${LL_D}
                         # Make sure that the output of this command also depends
                         # on the header files that ${SOURCE} uses
                         # FIXME: Only works for makefile generator
                         IMPLICIT_DEPENDS CXX "${SOURCE}"
                        )
      add_custom_command(OUTPUT "${LL}"
                         # COMMENT "${SOURCE} -> ${LL}"
                         DEPENDS "${SOURCE}"
                         VERBATIM
                         COMMAND ${CLANG} ${CXX_WARNING_FLAGS} ${RUNTIME_CXX_FLAGS} -m${j} -target ${TARGET} -I${TARGET_RUNTIME_DIR} -DCOMPILING_HALIDE_RUNTIME -DLLVM_VERSION=${LLVM_VERSION} -DBITS_${j} -emit-llvm -S ${SOURCE} -o ${LL}
                         )

      add_custom_command(OUTPUT "${BC_D}"
                         # COMMENT "${LL_D} -> ${BC_D}"
                         DEPENDS "${LL_D}"
                         COMMAND "${LLVM_AS}" "${LL_D}" -o "${BC_D}"
                         )
      add_custom_command(OUTPUT "${BC}"
                         # COMMENT "${LL} -> ${BC}"
                         DEPENDS "${LL}"
                         COMMAND "${LLVM_AS}" "${LL}" -o "${BC}"
                         )

      add_custom_command(OUTPUT "${INITMOD_D}"
                         # COMMENT "${BC_D} -> ${INITMOD_D}"
                         DEPENDS "${BC_D}" binary2cpp
                         COMMAND $<TARGET_FILE:binary2cpp> "halide_internal_initmod_${i}_${j}_debug" < "${BC_D}" > "${INITMOD_D}"
                         )
      add_custom_command(OUTPUT "${INITMOD}"
                         # COMMENT "${BC} -> ${INITMOD}"
                         DEPENDS "${BC}" binary2cpp
                         COMMAND $<TARGET_FILE:binary2cpp> "halide_internal_initmod_${i}_${j}" < "${BC}" > "${INITMOD}"
                         )
      list(APPEND INITIAL_MODULES ${INITMOD})
      list(APPEND INITIAL_MODULES ${INITMOD_D})
    endforeach()
  endforeach()
  # This seems to be how you return values from functions in cmake.
  # I just threw up in my mouth a little.
  set(INITIAL_MODULES "${INITIAL_MODULES}" PARENT_SCOPE)
endfunction (add_runtime_modules)

add_runtime_modules("${RUNTIME_CPP}" "${NATIVE_RUNTIME_DIR}")

foreach (i ${RUNTIME_LL} )
  set(LL "${NATIVE_RUNTIME_DIR}${i}.ll")
  set(BC "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.build/${NATIVE_INT_DIR}initmod.${i}.bc")
  set(INITMOD "${INITMOD_PREFIX}${i}.cpp")
  add_custom_command(OUTPUT "${BC}"
                     # COMMENT "${LL} -> ${BC}"
                     DEPENDS "${LL}"
                     COMMAND "${LLVM_AS}" "${LL}" -o "${BC}"
                     )
  add_custom_command(OUTPUT "${INITMOD}"
                     # COMMENT "${BC} -> ${INITMOD}"
                     DEPENDS "${BC}" binary2cpp
                     COMMAND $<TARGET_FILE:binary2cpp> "halide_internal_initmod_${i}_ll" < "${BC}" > "${INITMOD}"
                     )
  list(APPEND INITIAL_MODULES "${INITMOD}")
endforeach()
foreach (i ${RUNTIME_BC} )
  set(INITMOD "${INITMOD_PREFIX}ptx_${i}.cpp")
  add_custom_command(OUTPUT "${INITMOD}"
                     # COMMENT "Building initial module ptx_${i}..."
                     DEPENDS binary2cpp
                     COMMAND $<TARGET_FILE:binary2cpp> "halide_internal_initmod_ptx_${i}_ll" < "${NATIVE_RUNTIME_DIR}nvidia_libdevice_bitcode/libdevice.${i}.10.bc" > "${INITMOD}"
                     VERBATIM)
  list(APPEND INITIAL_MODULES "${INITMOD}")
endforeach()

add_custom_command(OUTPUT "${INITMOD_PREFIX}inlined_c.cpp"
  # COMMENT "halide_buffer_t.cpp -> ${INITMOD_PREFIX}inlined_c.cpp"
  DEPENDS "${NATIVE_RUNTIME_DIR}halide_buffer_t.cpp" binary2cpp
  COMMAND $<TARGET_FILE:binary2cpp> "halide_internal_initmod_inlined_c" < "${NATIVE_RUNTIME_DIR}halide_buffer_t.cpp" > "${INITMOD_PREFIX}inlined_c.cpp"
  )
list(APPEND INITIAL_MODULES "${INITMOD_PREFIX}inlined_c.cpp")

set(RUNTIME_HEADER_FILES
  HalideRuntime.h
  HalideRuntimeCuda.h
  HalideRuntimeHexagonDma.h
  HalideRuntimeHexagonHost.h
  HalideRuntimeOpenCL.h
  HalideRuntimeMetal.h
  HalideRuntimeOpenGL.h
  HalideRuntimeOpenGLCompute.h
  HalideRuntimeD3D12Compute.h
  HalideRuntimeQurt.h
  HalideBuffer.h
  HalidePyTorchHelpers.h
  HalidePyTorchCudaHelpers.h
)

foreach (i ${RUNTIME_HEADER_FILES})
  string(REPLACE "." "_" SYM_NAME "${i}")
  add_custom_command(OUTPUT "${INITMOD_PREFIX}${SYM_NAME}.cpp"
    # COMMENT "${i} -> ${INITMOD_PREFIX}${SYM_NAME}.cpp"
    DEPENDS "${NATIVE_RUNTIME_DIR}${i}" binary2cpp
    COMMAND $<TARGET_FILE:binary2cpp> "halide_internal_runtime_header_${SYM_NAME}" < "${NATIVE_RUNTIME_DIR}${i}" > "${INITMOD_PREFIX}${SYM_NAME}.cpp"
    )
  list(APPEND INITIAL_MODULES "${INITMOD_PREFIX}${SYM_NAME}.cpp")
endforeach()

# The externally-visible header files that go into making Halide.h.
# Don't include anything here that includes llvm headers.
# Keep this list sorted in alphabetical order.
set(HEADER_FILES
  AddAtomicMutex.h
  AddImageChecks.h
  AddParameterChecks.h
  AlignLoads.h
  AllocationBoundsInference.h
  ApplySplit.h
  Argument.h
  AssociativeOpsTable.h
  Associativity.h
  AsyncProducers.h
  AutoSchedule.h
  AutoScheduleUtils.h
  BoundaryConditions.h
  Bounds.h
  BoundsInference.h
  BoundSmallAllocations.h
  Buffer.h
  CanonicalizeGPUVars.h
  Closure.h
  CodeGen_ARM.h
  CodeGen_C.h
  CodeGen_D3D12Compute_Dev.h
  CodeGen_GPU_Dev.h
  CodeGen_GPU_Host.h
  CodeGen_Internal.h
  CodeGen_LLVM.h
  CodeGen_Metal_Dev.h
  CodeGen_MIPS.h
  CodeGen_OpenCL_Dev.h
  CodeGen_OpenGL_Dev.h
  CodeGen_OpenGLCompute_Dev.h
  CodeGen_Posix.h
  CodeGen_PowerPC.h
  CodeGen_PTX_Dev.h
  CodeGen_PyTorch.h
  CodeGen_RISCV.h
  CodeGen_WebAssembly.h
  CodeGen_X86.h
  CompilerLogger.h
  ConciseCasts.h
  CPlusPlusMangle.h
  CSE.h
  Debug.h
  DebugArguments.h
  DebugToFile.h
  Definition.h
  Deinterleave.h
  Derivative.h
  DerivativeUtils.h
  DeviceAPI.h
  DeviceArgument.h
  DeviceInterface.h
  Dimension.h
  EarlyFree.h
  Elf.h
  EliminateBoolVectors.h
  EmulateFloat16Math.h
  Error.h
  Expr.h
  ExprUsesVar.h
  Extern.h
  ExternFuncArgument.h
  FastIntegerDivide.h
  FindCalls.h
  Float16.h
  Func.h
  Function.h
  FunctionPtr.h
  FuseGPUThreadLoops.h
  FuzzFloatStores.h
  Generator.h
  HexagonOffload.h
  HexagonOptimize.h
  ImageParam.h
  InferArguments.h
  InjectHostDevBufferCopies.h
  InjectOpenGLIntrinsics.h
  Inline.h
  InlineReductions.h
  IntegerDivisionTable.h
  Interval.h
  Introspection.h
  IntrusivePtr.h
  IR.h
  IREquality.h
  IRMatch.h
  IRMutator.h
  IROperator.h
  IRPrinter.h
  IRVisitor.h
  JITModule.h
  Lambda.h
  Lerp.h
  LICM.h
  LLVM_Output.h
  LLVM_Runtime_Linker.h
  LoopCarry.h
  Lower.h
  LowerWarpShuffles.h
  MainPage.h
  MatlabWrapper.h
  Memoization.h
  Module.h
  ModulusRemainder.h
  Monotonic.h
  ObjectInstanceRegistry.h
  OutputImageParam.h
  ParallelRVar.h
  Param.h
  Parameter.h
  ParamMap.h
  PartitionLoops.h
  Pipeline.h
  Prefetch.h
  Profiling.h
  PurifyIndexMath.h
  PythonExtensionGen.h
  Qualify.h
  Random.h
  RDom.h
  Realization.h
  RealizationOrder.h
  Reduction.h
  RegionCosts.h
  RemoveDeadAllocations.h
  RemoveExternLoops.h
  RemoveUndef.h
  runtime/HalideBuffer.h
  runtime/HalideRuntime.h
  Schedule.h
  ScheduleFunctions.h
  Scope.h
  SelectGPUAPI.h
  Simplify.h
  SimplifyCorrelatedDifferences.h
  SimplifySpecializations.h
  SkipStages.h
  SlidingWindow.h
  Solve.h
  SplitTuples.h
  StmtToHtml.h
  StorageFlattening.h
  StorageFolding.h
  StrictifyFloat.h
  Substitute.h
  Target.h
  ThreadPool.h
  Tracing.h
  TrimNoOps.h
  Tuple.h
  Type.h
  UnifyDuplicateLets.h
  UniquifyVariableNames.h
  UnpackBuffers.h
  UnrollLoops.h
  UnsafePromises.h
  Util.h
  Var.h
  VaryingAttributes.h
  VectorizeLoops.h
  WasmExecutor.h
  WrapCalls.h
)

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/include")
file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}/include/" NATIVE_INCLUDE_PATH)
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/include/Halide.h"
  COMMAND $<TARGET_FILE:build_halide_h> "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE.txt" ${HEADER_FILES} > "${NATIVE_INCLUDE_PATH}Halide.h"
  WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
  DEPENDS build_halide_h "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE.txt" ${HEADER_FILES})
add_custom_target(
    HalideIncludes
    ALL
    DEPENDS "${CMAKE_BINARY_DIR}/include/Halide.h")

foreach (i ${RUNTIME_HEADER_FILES})
  configure_file(runtime/${i} "${CMAKE_BINARY_DIR}/include" COPYONLY)
endforeach()

# Keep this list sorted in alphabetical order.
add_library(Halide ${HALIDE_LIBRARY_TYPE}
  AddAtomicMutex.cpp
  AddImageChecks.cpp
  AddParameterChecks.cpp
  AlignLoads.cpp
  AllocationBoundsInference.cpp
  ApplySplit.cpp
  Argument.cpp
  AssociativeOpsTable.cpp
  Associativity.cpp
  AsyncProducers.cpp
  AutoSchedule.cpp
  AutoScheduleUtils.cpp
  BoundaryConditions.cpp
  Bounds.cpp
  BoundsInference.cpp
  BoundSmallAllocations.cpp
  Buffer.cpp
  CanonicalizeGPUVars.cpp
  Closure.cpp
  CodeGen_ARM.cpp
  CodeGen_C.cpp
  CodeGen_D3D12Compute_Dev.cpp
  CodeGen_GPU_Dev.cpp
  CodeGen_GPU_Host.cpp
  CodeGen_Hexagon.cpp
  CodeGen_Internal.cpp
  CodeGen_LLVM.cpp
  CodeGen_Metal_Dev.cpp
  CodeGen_MIPS.cpp
  CodeGen_OpenCL_Dev.cpp
  CodeGen_OpenGL_Dev.cpp
  CodeGen_OpenGLCompute_Dev.cpp
  CodeGen_Posix.cpp
  CodeGen_PowerPC.cpp
  CodeGen_PTX_Dev.cpp
  CodeGen_PyTorch.cpp
  CodeGen_RISCV.cpp
  CodeGen_WebAssembly.cpp
  CodeGen_X86.cpp
  CompilerLogger.cpp
  CPlusPlusMangle.cpp
  CSE.cpp
  Debug.cpp
  DebugArguments.cpp
  DebugToFile.cpp
  Definition.cpp
  Deinterleave.cpp
  Derivative.cpp
  DerivativeUtils.cpp
  DeviceArgument.cpp
  DeviceInterface.cpp
  Dimension.cpp
  EarlyFree.cpp
  Elf.cpp
  EliminateBoolVectors.cpp
  EmulateFloat16Math.cpp
  Error.cpp
  Expr.cpp
  FastIntegerDivide.cpp
  FindCalls.cpp
  Float16.cpp
  Func.cpp
  Function.cpp
  FuseGPUThreadLoops.cpp
  FuzzFloatStores.cpp
  Generator.cpp
  HexagonOffload.cpp
  HexagonOptimize.cpp
  ImageParam.cpp
  InferArguments.cpp
  InjectHostDevBufferCopies.cpp
  InjectOpenGLIntrinsics.cpp
  Inline.cpp
  InlineReductions.cpp
  IntegerDivisionTable.cpp
  Interval.cpp
  Introspection.cpp
  IR.cpp
  IREquality.cpp
  IRMatch.cpp
  IRMutator.cpp
  IROperator.cpp
  IRPrinter.cpp
  IRVisitor.cpp
  JITModule.cpp
  Lerp.cpp
  LICM.cpp
  LLVM_Output.cpp
  LLVM_Runtime_Linker.cpp
  LoopCarry.cpp
  Lower.cpp
  LowerWarpShuffles.cpp
  MatlabWrapper.cpp
  Memoization.cpp
  Module.cpp
  ModulusRemainder.cpp
  Monotonic.cpp
  ObjectInstanceRegistry.cpp
  OutputImageParam.cpp
  ParallelRVar.cpp
  Parameter.cpp
  ParamMap.cpp
  PartitionLoops.cpp
  Pipeline.cpp
  Prefetch.cpp
  PrintLoopNest.cpp
  Profiling.cpp
  PurifyIndexMath.cpp
  PythonExtensionGen.cpp
  Qualify.cpp
  Random.cpp
  RDom.cpp
  Realization.cpp
  RealizationOrder.cpp
  Reduction.cpp
  RegionCosts.cpp
  RemoveDeadAllocations.cpp
  RemoveExternLoops.cpp
  RemoveUndef.cpp
  Schedule.cpp
  ScheduleFunctions.cpp
  SelectGPUAPI.cpp
  Simplify.cpp
  Simplify_Add.cpp
  Simplify_And.cpp
  Simplify_Call.cpp
  Simplify_Cast.cpp
  Simplify_Div.cpp
  Simplify_EQ.cpp
  Simplify_Exprs.cpp
  Simplify_Let.cpp
  Simplify_LT.cpp
  Simplify_Max.cpp
  Simplify_Min.cpp
  Simplify_Mod.cpp
  Simplify_Mul.cpp
  Simplify_Not.cpp
  Simplify_Or.cpp
  Simplify_Select.cpp
  Simplify_Shuffle.cpp
  Simplify_Stmts.cpp
  Simplify_Sub.cpp
  SimplifyCorrelatedDifferences.cpp
  SimplifySpecializations.cpp
  SkipStages.cpp
  SlidingWindow.cpp
  Solve.cpp
  SplitTuples.cpp
  StmtToHtml.cpp
  StorageFlattening.cpp
  StorageFolding.cpp
  StrictifyFloat.cpp
  Substitute.cpp
  Target.cpp
  Tracing.cpp
  TrimNoOps.cpp
  Tuple.cpp
  Type.cpp
  UnifyDuplicateLets.cpp
  UniquifyVariableNames.cpp
  UnpackBuffers.cpp
  UnrollLoops.cpp
  UnsafePromises.cpp
  Util.cpp
  Var.cpp
  VaryingAttributes.cpp
  VectorizeLoops.cpp
  WasmExecutor.cpp
  WrapCalls.cpp
  ${HEADER_FILES}
  ${INITIAL_MODULES}
)

# We could expose the /MP flag to all targets, but that might end up saturating the build
# since multiple MSBuild projects might get built in parallel, each of which compiling their
# source files in parallel; the Halide library itself is a "knot" point of the build graph,
# so compiling its files in parallel should not oversubscribe the system
target_compile_options(Halide PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/MP>)

# Define Halide_SHARED or Halide_STATIC depending on library type
target_compile_definitions(Halide PRIVATE "-DHalide_${HALIDE_LIBRARY_TYPE}")
add_dependencies(Halide HalideIncludes)

# List of LLVM Components required
# This list will be appended to depending on the targets we need to support
# See the output of ``llvm-config --components`` for a list of possible components
set(LLVM_COMPONENTS mcjit;bitwriter;linker)
list(APPEND LLVM_COMPONENTS passes)

# Set definitions and compiler flags

# Note when PUBLIC or INTERFACE scope is used in target_compile_* then targets
# that link against the Halide library inherit those options and definitions
target_include_directories(Halide PRIVATE ${LLVM_INCLUDE_DIRS})
target_include_directories(Halide INTERFACE "${CMAKE_BINARY_DIR}/include")

# TODO: For targets we can link against even fewer libraries by specifying
# only the components we **REALLY** need (e.g. x86asmprinter;x86codegen rather than x86)
if (TARGET_X86)
  target_compile_definitions(Halide PRIVATE "-DWITH_X86")
  list(APPEND LLVM_COMPONENTS X86)
endif()

if (TARGET_ARM)
  target_compile_definitions(Halide PRIVATE "-DWITH_ARM")
  list(APPEND LLVM_COMPONENTS ARM)
endif()

if (TARGET_AARCH64)
  target_compile_definitions(Halide PRIVATE "-DWITH_AARCH64")
  list(APPEND LLVM_COMPONENTS AArch64)
endif()

if (TARGET_HEXAGON)
  target_compile_definitions(Halide PRIVATE "-DWITH_HEXAGON")
  list(APPEND LLVM_COMPONENTS Hexagon)
endif()

if (TARGET_MIPS)
  target_compile_definitions(Halide PRIVATE "-DWITH_MIPS")
  list(APPEND LLVM_COMPONENTS Mips)
endif()

if (TARGET_POWERPC)
  target_compile_definitions(Halide PRIVATE "-DWITH_POWERPC")
  list(APPEND LLVM_COMPONENTS PowerPC)
endif()

if (TARGET_WEBASSEMBLY)
  target_compile_definitions(Halide PRIVATE "-DWITH_WEBASSEMBLY")
  list(APPEND LLVM_COMPONENTS WebAssembly)
endif()

if (TARGET_PTX)
  target_compile_definitions(Halide PRIVATE "-DWITH_PTX")
  list(APPEND LLVM_COMPONENTS NVPTX)
endif()

if (TARGET_AMDGPU)
  target_compile_definitions(Halide PRIVATE "-DWITH_AMDGPU")
  list(APPEND LLVM_COMPONENTS AMDGPU)
endif()

if (TARGET_RISCV)
  target_compile_definitions(Halide PRIVATE "-DWITH_RISCV")
  list(APPEND LLVM_COMPONENTS RISCV)
endif()

if (TARGET_OPENCL)
  target_compile_definitions(Halide PRIVATE "-DWITH_OPENCL")
endif()

if (TARGET_OPENGL)
  target_compile_definitions(Halide PRIVATE "-DWITH_OPENGL")
endif()

if (TARGET_METAL)
  target_compile_definitions(Halide PRIVATE "-DWITH_METAL")
endif()

if (TARGET_D3D12COMPUTE)
  target_compile_definitions(Halide PRIVATE "-DWITH_D3D12")
endif()

target_compile_definitions(Halide PRIVATE "-DLLVM_VERSION=${LLVM_VERSION}")
target_compile_definitions(Halide PRIVATE "-DCOMPILING_HALIDE")
target_compile_definitions(Halide PRIVATE ${LLVM_DEFINITIONS})
if (NOT LLVM_ENABLE_ASSERTIONS)
  target_compile_definitions(Halide PRIVATE NDEBUG)
endif()

if (MSVC)
  # Suppress some warnings
  # 4244: conversion, possible loss of data
  # 4267: conversion, possible loss of data
  # 4800: BOOL -> true or false
  # 4996: compiler encountered deprecated declaration
  target_compile_options(Halide PUBLIC /wd4244 /wd4267 /wd4800 /wd4996)
  # Injected from recent LLVM:
  target_compile_options(Halide PUBLIC /wd4141)  # 'inline' used more than once
  target_compile_options(Halide PUBLIC /wd4146)  # unary minus applied to unsigned type
  target_compile_options(Halide PUBLIC /wd4291)  # No matching operator delete found

  target_compile_definitions(Halide PUBLIC "-D_CRT_SECURE_NO_WARNINGS" "-D_SCL_SECURE_NO_WARNINGS")
  # To compile LLVM headers following was taken from LLVM CMake files:
  # Disable sized deallocation if the flag is supported. MSVC fails to compile
  # the operator new overload in LLVM/IR/Function.h and Instruction.h otherwise.
  # See LLVM PR: 23513 (https://llvm.org/bugs/show_bug.cgi?id=23513)
  check_cxx_compiler_flag("/WX /Zc:sizedDealloc-" SUPPORTS_SIZED_DEALLOC)
  if (SUPPORTS_SIZED_DEALLOC)
    target_compile_options(Halide PRIVATE "/Zc:sizedDealloc-")
  endif()
else()
  if (NOT HALIDE_ENABLE_RTTI)
    if (NOT MSVC)
      target_compile_options(Halide PUBLIC $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
    else()
      target_compile_options(Halide PUBLIC $<$<COMPILE_LANGUAGE:CXX>:/GR- >)
    endif()
  endif()
endif()

# When building a shared library the LLVM libraries will either be
# embedded or linked into the Halide library. When building a static library
# LLVM is not embedded/linked but CMake knows that when building an executable
# against Halide static library that it needs to link LLVM too, IFF the
# halide is being built as a subproject. (Which means, statically-built Halide
# doesn't work out-of-installation directory.)
# FIXME: migrate to proper cmake config magic.
if(LLVM_USE_SHARED_LLVM_LIBRARY)
  set(LLVM_USE_SHARED "USE_SHARED")
endif()
llvm_config(Halide ${LLVM_USE_SHARED} ${LLVM_COMPONENTS})

if (NOT MSVC AND NOT LLVM_USE_SHARED_LLVM_LIBRARY)
  set(LLVM_CONFIG ${LLVM_TOOLS_BINARY_DIR}/llvm-config)
  execute_process(COMMAND "${LLVM_CONFIG}" --system-libs ${LLVM_COMPONENTS} OUTPUT_VARIABLE EXTRA_LIBS)
  string(STRIP EXTRA_LIBS "${EXTRA_LIBS}")   # strip whitespace from start & end
  string(REPLACE "\n" "" EXTRA_LIBS "${EXTRA_LIBS}")
  string(REPLACE " " ";" EXTRA_LIBS "${EXTRA_LIBS}")  # convert into a list
  target_link_libraries(Halide PUBLIC ${EXTRA_LIBS})
endif()

install(TARGETS Halide
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION bin
        ARCHIVE DESTINATION lib)
