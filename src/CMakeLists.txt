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
  android_clock
  android_host_cpu_count
  android_io
  android_opengl_context
  android_tempfile
  arm_cpu_features
  buffer_t
  cache
  can_use_target
  cuda
  destructors
  device_interface
  errors
  fake_thread_pool
  float16_t
  gpu_device_selection
  hexagon_cpu_features
  hexagon_host
  ios_io
  linux_clock
  linux_host_cpu_count
  linux_opengl_context
  linux_yield
  matlab
  metadata
  metal
  metal_objc_arm
  metal_objc_x86
  mingw_math
  mips_cpu_features
  module_aot_ref_count
  module_jit_ref_count
  msan
  msan_stubs
  old_buffer_t
  opencl
  opengl
  openglcompute
  osx_clock
  osx_get_symbol
  osx_host_cpu_count
  osx_opengl_context
  osx_yield
  posix_allocator
  posix_clock
  posix_error_handler
  posix_get_symbol
  posix_io
  posix_print
  posix_tempfile
  posix_threads
  posix_threads_tsan
  powerpc_cpu_features
  prefetch
  profiler
  profiler_inlined
  qurt_allocator
  qurt_hvx
  qurt_init_fini
  qurt_threads
  qurt_threads_tsan
  qurt_yield
  runtime_api
  ssp
  to_string
  tracing
  windows_clock
  windows_cuda
  windows_get_symbol
  windows_io
  windows_opencl
  windows_profiler
  windows_tempfile
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
  win32_math
  x86
  x86_avx
  x86_avx2
  x86_sse41
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

      add_custom_command(OUTPUT "${LL_D}"
                         DEPENDS "${SOURCE}"
                         COMMAND ${CLANG} ${CXX_WARNING_FLAGS} ${RUNTIME_DEBUG_FLAG} -DDEBUG_RUNTIME -O3 -fno-ms-compatibility -ffreestanding -fno-blocks -fno-exceptions -fno-unwind-tables -m${j} -target "${TARGET}" "-I${TARGET_RUNTIME_DIR}" -DCOMPILING_HALIDE_RUNTIME "-DLLVM_VERSION=${LLVM_VERSION}" -DBITS_${j} -emit-llvm -S "${SOURCE}" -o "${LL_D}"
                         COMMENT "${SOURCE} -> ${LL_D}"
                         # Make sure that the output of this command also depends
                         # on the header files that ${SOURCE} uses
                         # FIXME: Only works for makefile generator
                         IMPLICIT_DEPENDS CXX "${SOURCE}"
                        )
      add_custom_command(OUTPUT "${LL}"
                         DEPENDS "${SOURCE}"
                         COMMAND ${CLANG} ${CXX_WARNING_FLAGS} -O3 -fno-ms-compatibility -ffreestanding -fno-blocks -fno-exceptions -fno-unwind-tables -m${j} -target "${TARGET}" "-I${TARGET_RUNTIME_DIR}" -DCOMPILING_HALIDE_RUNTIME "-DLLVM_VERSION=${LLVM_VERSION}" -DBITS_${j} -emit-llvm -S "${SOURCE}" -o "${LL}"
                         COMMENT "${SOURCE} -> ${LL}")

      add_custom_command(OUTPUT "${BC_D}"
                         DEPENDS "${LL_D}"
                         COMMAND "${LLVM_AS}" "${LL_D}" -o "${BC_D}"
                         COMMENT "${LL_D} -> ${BC_D}")
      add_custom_command(OUTPUT "${BC}"
                         DEPENDS "${LL}"
                         COMMAND "${LLVM_AS}" "${LL}" -o "${BC}"
                         COMMENT "${LL} -> ${BC}")

      add_custom_command(OUTPUT "${INITMOD_D}"
                         DEPENDS "${BC_D}"
                         COMMAND binary2cpp "halide_internal_initmod_${i}_${j}_debug" < "${BC_D}" > "${INITMOD_D}"
                         COMMENT "${BC_D} -> ${INITMOD_D}")
      add_custom_command(OUTPUT "${INITMOD}"
                         DEPENDS "${BC}"
                         COMMAND binary2cpp "halide_internal_initmod_${i}_${j}" < "${BC}" > "${INITMOD}"
                         COMMENT "${BC} -> ${INITMOD}")
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
                     DEPENDS "${LL}"
                     COMMAND "${LLVM_AS}" "${LL}" -o "${BC}"
                     COMMENT "${LL} -> ${BC}")
  add_custom_command(OUTPUT "${INITMOD}"
                     DEPENDS "${BC}"
                     COMMAND binary2cpp "halide_internal_initmod_${i}_ll" < "${BC}" > "${INITMOD}"
                     COMMENT "${BC} -> ${INITMOD}")
  list(APPEND INITIAL_MODULES "${INITMOD}")
endforeach()
foreach (i ${RUNTIME_BC} )
  set(INITMOD "${INITMOD_PREFIX}ptx_${i}.cpp")
  add_custom_command(OUTPUT "${INITMOD}"
                     COMMAND binary2cpp "halide_internal_initmod_ptx_${i}_ll" < "${NATIVE_RUNTIME_DIR}nvidia_libdevice_bitcode/libdevice.${i}.10.bc" > "${INITMOD}"
                     COMMENT "Building initial module ptx_${i}..."
                     VERBATIM)
  list(APPEND INITIAL_MODULES "${INITMOD}")
endforeach()

add_custom_command(OUTPUT "${INITMOD_PREFIX}inlined_c.cpp"
  DEPENDS "${NATIVE_RUNTIME_DIR}buffer_t.cpp"
  COMMAND binary2cpp "halide_internal_initmod_inlined_c" < "${NATIVE_RUNTIME_DIR}buffer_t.cpp" > "${INITMOD_PREFIX}inlined_c.cpp"
  COMMENT "buffer_t.cpp -> ${INITMOD_PREFIX}inlined_c.cpp")
list(APPEND INITIAL_MODULES "${INITMOD_PREFIX}inlined_c.cpp")

set(RUNTIME_HEADER_FILES
  HalideRuntime.h
  HalideRuntimeCuda.h
  HalideRuntimeHexagonHost.h
  HalideRuntimeOpenCL.h
  HalideRuntimeMetal.h
  HalideRuntimeOpenGL.h
  HalideRuntimeOpenGLCompute.h
  HalideRuntimeQurt.h
  HalideBuffer.h
)

foreach (i ${RUNTIME_HEADER_FILES})
  string(REPLACE "." "_" SYM_NAME "${i}")
  add_custom_command(OUTPUT "${INITMOD_PREFIX}${SYM_NAME}.cpp"
    DEPENDS "${NATIVE_RUNTIME_DIR}${i}"
    COMMAND binary2cpp "halide_internal_runtime_header_${SYM_NAME}" < "${NATIVE_RUNTIME_DIR}${i}" > "${INITMOD_PREFIX}${SYM_NAME}.cpp"
    COMMENT "${i} -> ${INITMOD_PREFIX}${SYM_NAME}.cpp")
  list(APPEND INITIAL_MODULES "${INITMOD_PREFIX}${SYM_NAME}.cpp")
endforeach()

# The externally-visible header files that go into making Halide.h.
# Don't include anything here that includes llvm headers.
set(HEADER_FILES
  AddImageChecks.h
  AddParameterChecks.h
  AlignLoads.h
  AllocationBoundsInference.h
  ApplySplit.h
  Argument.h
  AssociativeOpsTable.h
  Associativity.h
  AutoSchedule.h
  AutoScheduleUtils.h
  BoundaryConditions.h
  Bounds.h
  BoundsInference.h
  BoundSmallAllocations.h
  Buffer.h
  Closure.h
  CodeGen_ARM.h
  CodeGen_C.h
  CodeGen_GPU_Dev.h
  CodeGen_GPU_Host.h
  CodeGen_Internal.h
  CodeGen_LLVM.h
  CodeGen_MIPS.h
  CodeGen_OpenCL_Dev.h
  CodeGen_Metal_Dev.h
  CodeGen_OpenGL_Dev.h
  CodeGen_OpenGLCompute_Dev.h
  CodeGen_Posix.h
  CodeGen_PowerPC.h
  CodeGen_PTX_Dev.h
  CodeGen_X86.h
  ConciseCasts.h
  CPlusPlusMangle.h
  CSE.h
  CanonicalizeGPUVars.h
  CopyElision.h
  Debug.h
  DebugArguments.h
  DebugToFile.h
  Definition.h
  Deinterleave.h
  DeviceArgument.h
  DeviceInterface.h
  Dimension.h
  EarlyFree.h
  Elf.h
  EliminateBoolVectors.h
  Error.h
  Expr.h
  ExprUsesVar.h
  Extern.h
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
  runtime/HalideRuntime.h
  runtime/HalideBuffer.h
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
  IREquality.h
  IR.h
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
  Outputs.h
  OutputImageParam.h
  ParallelRVar.h
  Param.h
  ParamMap.h
  Parameter.h
  PartitionLoops.h
  Pipeline.h
  Prefetch.h
  Profiling.h
  Qualify.h
  Random.h
  RealizationOrder.h
  RDom.h
  Reduction.h
  RegionCosts.h
  RemoveDeadAllocations.h
  RemoveTrivialForLoops.h
  RemoveUndef.h
  Schedule.h
  ScheduleFunctions.h
  Scope.h
  SelectGPUAPI.h
  Simplify.h
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
  Util.h
  Var.h
  VaryingAttributes.h
  VectorizeLoops.h
  WrapCalls.h
  WrapExternStages.h
)

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/include")
file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}/include/" NATIVE_INCLUDE_PATH)
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/include/Halide.h"
  COMMAND build_halide_h "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE.txt" ${HEADER_FILES} > "${NATIVE_INCLUDE_PATH}Halide.h"
  WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
  DEPENDS build_halide_h "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE.txt" ${HEADER_FILES})
add_custom_target(
    HalideIncludes
    ALL
    DEPENDS "${CMAKE_BINARY_DIR}/include/Halide.h")

foreach (i ${RUNTIME_HEADER_FILES})
  configure_file(runtime/${i} "${CMAKE_BINARY_DIR}/include" COPYONLY)
endforeach()

add_library(Halide ${HALIDE_LIBRARY_TYPE}
  AddImageChecks.cpp
  AddParameterChecks.cpp
  AlignLoads.cpp
  AllocationBoundsInference.cpp
  ApplySplit.cpp
  AssociativeOpsTable.cpp
  Associativity.cpp
  AutoSchedule.cpp
  AutoScheduleUtils.cpp
  BoundaryConditions.cpp
  Bounds.cpp
  BoundsInference.cpp
  BoundSmallAllocations.cpp
  Buffer.cpp
  Closure.cpp
  CodeGen_ARM.cpp
  CodeGen_C.cpp
  CodeGen_GPU_Dev.cpp
  CodeGen_GPU_Host.cpp
  CodeGen_Hexagon.cpp
  CodeGen_Internal.cpp
  CodeGen_LLVM.cpp
  CodeGen_MIPS.cpp
  CodeGen_OpenCL_Dev.cpp
  CodeGen_Metal_Dev.cpp
  CodeGen_OpenGL_Dev.cpp
  CodeGen_OpenGLCompute_Dev.cpp
  CodeGen_PowerPC.cpp
  CodeGen_PTX_Dev.cpp
  CodeGen_Posix.cpp
  CodeGen_X86.cpp
  CPlusPlusMangle.cpp
  CSE.cpp
  CanonicalizeGPUVars.cpp
  CopyElision.cpp
  Debug.cpp
  DebugArguments.cpp
  DebugToFile.cpp
  Definition.cpp
  Deinterleave.cpp
  DeviceArgument.cpp
  DeviceInterface.cpp
  Dimension.cpp
  EarlyFree.cpp
  Elf.cpp
  EliminateBoolVectors.cpp
  Error.cpp
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
  IR.cpp
  IREquality.cpp
  IRMatch.cpp
  IRMutator.cpp
  IROperator.cpp
  IRPrinter.cpp
  IRVisitor.cpp
  ImageParam.cpp
  InferArguments.cpp
  Interval.cpp
  InjectHostDevBufferCopies.cpp
  InjectOpenGLIntrinsics.cpp
  Inline.cpp
  InlineReductions.cpp
  IntegerDivisionTable.cpp
  Introspection.cpp
  JITModule.cpp
  LLVM_Output.cpp
  LLVM_Runtime_Linker.cpp
  Lerp.cpp
  LICM.cpp
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
  ParamMap.cpp
  Parameter.cpp
  PartitionLoops.cpp
  Pipeline.cpp
  PrintLoopNest.cpp
  Prefetch.cpp
  Profiling.cpp
  Qualify.cpp
  RDom.cpp
  Random.cpp
  RealizationOrder.cpp
  Reduction.cpp
  RegionCosts.cpp
  RemoveDeadAllocations.cpp
  RemoveTrivialForLoops.cpp
  RemoveUndef.cpp
  Schedule.cpp
  ScheduleFunctions.cpp
  SelectGPUAPI.cpp
  Simplify.cpp
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
  Util.cpp
  Var.cpp
  VaryingAttributes.cpp
  VectorizeLoops.cpp
  WrapCalls.cpp
  WrapExternStages.cpp
  ${HEADER_FILES}
  ${INITIAL_MODULES}
)
# Define Halide_SHARED or Halide_STATIC depending on library type
target_compile_definitions(Halide PRIVATE "-DHalide_${HALIDE_LIBRARY_TYPE}")
# Ensure that these tools are build first
add_dependencies(Halide
  binary2cpp
  build_halide_h
  HalideIncludes
)

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
  target_compile_definitions(Halide PRIVATE "-DWITH_X86=1")
  list(APPEND LLVM_COMPONENTS X86)
endif()

if (TARGET_ARM)
  target_compile_definitions(Halide PRIVATE "-DWITH_ARM=1")
  list(APPEND LLVM_COMPONENTS ARM)
endif()

if (TARGET_AARCH64)
  target_compile_definitions(Halide PRIVATE "-DWITH_AARCH64=1")
  list(APPEND LLVM_COMPONENTS AArch64)
endif()

if (TARGET_HEXAGON)
  target_compile_definitions(Halide PRIVATE "-DWITH_HEXAGON=1")
  list(APPEND LLVM_COMPONENTS Hexagon)
endif()

if (TARGET_MIPS)
  target_compile_definitions(Halide PRIVATE "-DWITH_MIPS=1")
  list(APPEND LLVM_COMPONENTS Mips)
endif()

if (TARGET_POWERPC)
  target_compile_definitions(Halide PRIVATE "-DWITH_POWERPC=1")
  list(APPEND LLVM_COMPONENTS PowerPC)
endif()

if (TARGET_PTX)
  target_compile_definitions(Halide PRIVATE "-DWITH_PTX=1")
  list(APPEND LLVM_COMPONENTS NVPTX)
endif()

if (TARGET_AMDGPU)
  target_compile_definitions(Halide PRIVATE "-DWITH_AMDGPU=1")
  list(APPEND LLVM_COMPONENTS AMDGPU)
endif()

if (TARGET_OPENCL)
  target_compile_definitions(Halide PRIVATE "-DWITH_OPENCL=1")
endif()

if (TARGET_OPENGL)
  target_compile_definitions(Halide PRIVATE "-DWITH_OPENGL=1")
endif()

if (TARGET_METAL)
  target_compile_definitions(Halide PRIVATE "-DWITH_METAL=1")
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
      target_compile_options(Halide PUBLIC "-fno-rtti")
    else()
      target_compile_options(Halide PUBLIC "/GR-")
    endif()
  endif()
endif()

# Get the LLVM libraries we need
llvm_map_components_to_libnames(LIBS ${LLVM_COMPONENTS})

# When building a shared library the LLVM libraries will be
# embedded in the Halide library. When building a static library
# LLVM is not embedded but CMake knows that when building an executable
# against the Halide static library that it needs to link LLVM too so
# PRIVATE scope is the correct choice here.
target_link_libraries(Halide PRIVATE ${LIBS})

if (NOT MSVC)
  set(LLVM_CONFIG ${LLVM_TOOLS_BINARY_DIR}/llvm-config)
  execute_process(COMMAND "${LLVM_CONFIG}" --system-libs ${LLVM_COMPONENTS} OUTPUT_VARIABLE EXTRA_LIBS)
  string(STRIP EXTRA_LIBS "${EXTRA_LIBS}")
  string(REPLACE "-l" ";" EXTRA_LIBS "${EXTRA_LIBS}")
  string(REPLACE "\n" "" EXTRA_LIBS "${EXTRA_LIBS}")
  string(REPLACE " " "" EXTRA_LIBS "${EXTRA_LIBS}")
  target_link_libraries(Halide PUBLIC ${EXTRA_LIBS})
endif()

install(TARGETS Halide
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION bin
        ARCHIVE DESTINATION lib)
if (APPLE AND "${HALIDE_LIBRARY_TYPE}" STREQUAL "SHARED")
  install(CODE "execute_process(COMMAND install_name_tool $<INSTALL_PREFIX>/bin/$<TARGET_FILE_NAME:Halide> $<INSTALL_PREFIX>/bin/$<TARGET_FILE_NAME:Halide>)")
endif()
