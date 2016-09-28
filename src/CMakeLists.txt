include(CheckCXXCompilerFlag)

add_executable(build_halide_h ../tools/build_halide_h.cpp)
add_executable(bitcode2cpp ../tools/bitcode2cpp.cpp)

if (MSVC)
  # -g produces dwarf debugging info, which is not useful on windows
  #  (and fails to compile due to llvm bug 15393)
  set(RUNTIME_DEBUG_FLAG "")
else()
  set(RUNTIME_DEBUG_FLAG "-g")
endif()

if (HALIDE_SHARED_LIBRARY)
  add_definitions("-DHalide_SHARED")
  set(HALIDE_LIBRARY_TYPE SHARED)
else()
  set(HALIDE_LIBRARY_TYPE STATIC)
endif()

set(RUNTIME_CPP
  aarch64_cpu_features
  android_clock
  android_host_cpu_count
  android_io
  android_opengl_context
  android_tempfile
  arm_cpu_features
  cache
  can_use_target
  cuda
  destructors
  device_interface
  errors
  fake_thread_pool
  float16_t
  gcd_thread_pool
  gpu_device_selection
  hexagon_host
  ios_io
  linux_clock
  linux_host_cpu_count
  linux_opengl_context
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
  nacl_host_cpu_count
  noos
  opencl
  opengl
  openglcompute
  osx_clock
  osx_get_symbol
  osx_host_cpu_count
  osx_opengl_context
  posix_allocator
  posix_clock
  posix_error_handler
  posix_get_symbol
  posix_io
  posix_print
  posix_tempfile
  posix_threads
  powerpc_cpu_features
  profiler
  profiler_inlined
  qurt_allocator
  qurt_hvx
  renderscript
  runtime_api
  ssp
  thread_pool
  to_string
  tracing
  windows_clock
  windows_cuda
  windows_get_symbol
  windows_io
  windows_opencl
  windows_tempfile
  windows_threads
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
  pnacl_math
  posix_math
  ptx_dev
  renderscript_dev
  win32_math
  x86
  x86_avx
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
foreach (i ${RUNTIME_CPP} )
  foreach (j ${ARCHS} )
    IF (${j} EQUAL 32)
      IF (${i} MATCHES "windows_.*")
        # win32 uses the stdcall calling convention, which is x86-specific
        set(TARGET "i386-unknown-unknown-unknown")
      ELSE()
        set(TARGET "le32-unknown-nacl-unknown")
      ENDIF()
    ELSE()
      # generic 64-bit code
      set(TARGET "le64-unknown-unknown-unknown")
    ENDIF()
    set(SOURCE "${NATIVE_RUNTIME_DIR}${i}.cpp")
    set(LL_D "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.build/${NATIVE_INT_DIR}initmod.${i}_${j}_debug.ll")
    set(LL "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.build/${NATIVE_INT_DIR}initmod.${i}_${j}.ll")
    set(BC_D "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.build/${NATIVE_INT_DIR}initmod.${i}_${j}_debug.bc")
    set(BC "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.build/${NATIVE_INT_DIR}initmod.${i}_${j}.bc")
    set(INITMOD_D "${INITMOD_PREFIX}${i}_${j}_debug.cpp")
    set(INITMOD "${INITMOD_PREFIX}${i}_${j}.cpp")

    add_custom_command(OUTPUT "${LL_D}"
                       DEPENDS "${SOURCE}"
                       COMMAND ${CLANG} ${CXX_WARNING_FLAGS} ${RUNTIME_DEBUG_FLAG} -DDEBUG_RUNTIME -O3 -fno-ms-compatibility -ffreestanding -fno-blocks -fno-exceptions -fno-unwind-tables -m${j} -target "${TARGET}" "-I${NATIVE_RUNTIME_DIR}" -DCOMPILING_HALIDE_RUNTIME "-DLLVM_VERSION=${LLVM_VERSION}" -DBITS_${j} -emit-llvm -S "${SOURCE}" -o "${LL_D}"
                       COMMENT "${SOURCE} -> ${LL_D}"
                       # Make sure that the output of this command also depends
                       # on the header files that ${SOURCE} uses
                       # FIXME: Only works for makefile generator
                       IMPLICIT_DEPENDS CXX "${SOURCE}"
                      )
    add_custom_command(OUTPUT "${LL}"
                       DEPENDS "${SOURCE}"
                       COMMAND ${CLANG} ${CXX_WARNING_FLAGS} -O3 -fno-ms-compatibility -ffreestanding -fno-blocks -fno-exceptions -fno-unwind-tables -m${j} -target "${TARGET}" "-I${NATIVE_RUNTIME_DIR}" -DCOMPILING_HALIDE_RUNTIME "-DLLVM_VERSION=${LLVM_VERSION}" -DBITS_${j} -emit-llvm -S "${SOURCE}" -o "${LL}"
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
                       COMMAND bitcode2cpp "${i}_${j}_debug" < "${BC_D}" > "${INITMOD_D}"
                       COMMENT "${BC_D} -> ${INITMOD_D}")
    add_custom_command(OUTPUT "${INITMOD}"
                       DEPENDS "${BC}"
                       COMMAND bitcode2cpp "${i}_${j}" < "${BC}" > "${INITMOD}"
                       COMMENT "${BC} -> ${INITMOD}")
    list(APPEND INITIAL_MODULES ${INITMOD})
    list(APPEND INITIAL_MODULES ${INITMOD_D})
  endforeach()
endforeach()
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
                     COMMAND bitcode2cpp ${i}_ll < "${BC}" > "${INITMOD}"
                     COMMENT "${BC} -> ${INITMOD}")
  list(APPEND INITIAL_MODULES "${INITMOD}")
endforeach()
foreach (i ${RUNTIME_BC} )
  set(INITMOD "${INITMOD_PREFIX}ptx_${i}.cpp")
  add_custom_command(OUTPUT "${INITMOD}"
                     COMMAND bitcode2cpp "ptx_${i}_ll" < "${NATIVE_RUNTIME_DIR}nvidia_libdevice_bitcode/libdevice.${i}.10.bc" > "${INITMOD}"
                     COMMENT "Building initial module ptx_${i}..."
                     VERBATIM)
  list(APPEND INITIAL_MODULES "${INITMOD}")
endforeach()


set(HEADER_FILES
  AddImageChecks.h
  AddParameterChecks.h
  AllocationBoundsInference.h
  Argument.h
  Associativity.h
  BoundaryConditions.h
  Bounds.h
  BoundsInference.h
  BufferPtr.h
  CSE.h
  Closure.h
  CodeGen_ARM.h
  CodeGen_C.h
  CodeGen_GPU_Dev.h
  CodeGen_GPU_Host.h
  CodeGen_Hexagon.h
  CodeGen_LLVM.h
  CodeGen_MIPS.h
  CodeGen_OpenCL_Dev.h
  CodeGen_Metal_Dev.h
  CodeGen_OpenGL_Dev.h
  CodeGen_OpenGLCompute_Dev.h
  CodeGen_PNaCl.h
  CodeGen_PowerPC.h
  CodeGen_PTX_Dev.h
  CodeGen_Posix.h
  CodeGen_Renderscript_Dev.h
  CodeGen_X86.h
  ConciseCasts.h
  CPlusPlusMangle.h
  Debug.h
  DebugToFile.h
  DeepCopy.h
  Definition.h
  Deinterleave.h
  DeviceArgument.h
  DeviceInterface.h
  EarlyFree.h
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
  FuseGPUThreadLoops.h
  FuzzFloatStores.h
  Generator.h
  HexagonOffload.h
  HexagonOptimize.h
  IR.h
  IREquality.h
  IRMatch.h
  IRMutator.h
  IROperator.h
  IRPrinter.h
  IRVisitor.h
  ImageParam.h
  Interval.h
  InjectHostDevBufferCopies.h
  InjectImageIntrinsics.h
  InjectOpenGLIntrinsics.h
  Inline.h
  InlineReductions.h
  IntegerDivisionTable.h
  Introspection.h
  IntrusivePtr.h
  JITModule.h
  LLVM_Output.h
  LLVM_Runtime_Linker.h
  Lambda.h
  Lerp.h
  LoopCarry.h
  Lower.h
  MainPage.h
  MatlabWrapper.h
  Memoization.h
  Module.h
  ModulusRemainder.h
  Monotonic.h
  ObjectInstanceRegistry.h
  OutputImageParam.h
  Outputs.h
  ParallelRVar.h
  Param.h
  Parameter.h
  PartitionLoops.h
  Pipeline.h
  Prefetch.h
  Profiling.h
  Qualify.h
  RDom.h
  Random.h
  RealizationOrder.h
  Reduction.h
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
  StmtToHtml.h
  StorageFlattening.h
  StorageFolding.h
  Substitute.h
  Target.h
  Tracing.h
  TrimNoOps.h
  Tuple.h
  Type.h
  UnifyDuplicateLets.h
  UniquifyVariableNames.h
  UnrollLoops.h
  Util.h
  Var.h
  VaryingAttributes.h
  VectorizeLoops.h
  WrapCalls.h
  runtime/HalideRuntime.h
  runtime/HalideBuffer.h
)

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/include")
file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}/include/" NATIVE_INCLUDE_PATH)
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/include/Halide.h"
  COMMAND build_halide_h ${HEADER_FILES} > "${NATIVE_INCLUDE_PATH}Halide.h"
  WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
  DEPENDS build_halide_h ${HEADER_FILES})

configure_file(runtime/HalideRuntime.h "${CMAKE_BINARY_DIR}/include" COPYONLY)
configure_file(runtime/HalideRuntimeCuda.h "${CMAKE_BINARY_DIR}/include" COPYONLY)
configure_file(runtime/HalideRuntimeHexagonHost.h "${CMAKE_BINARY_DIR}/include" COPYONLY)
configure_file(runtime/HalideRuntimeOpenCL.h "${CMAKE_BINARY_DIR}/include" COPYONLY)
configure_file(runtime/HalideRuntimeMetal.h "${CMAKE_BINARY_DIR}/include" COPYONLY)
configure_file(runtime/HalideRuntimeOpenGL.h "${CMAKE_BINARY_DIR}/include" COPYONLY)
configure_file(runtime/HalideRuntimeOpenGLCompute.h "${CMAKE_BINARY_DIR}/include" COPYONLY)
configure_file(runtime/HalideRuntimeQurt.h "${CMAKE_BINARY_DIR}/include" COPYONLY)
configure_file(runtime/HalideRuntimeRenderscript.h "${CMAKE_BINARY_DIR}/include" COPYONLY)
configure_file(runtime/HalideBuffer.h "${CMAKE_BINARY_DIR}/include" COPYONLY)


set(BITWRITER_FILES
  BitWriter_3_2/BitcodeWriter.cpp
  BitWriter_3_2/BitcodeWriterPass.cpp
  BitWriter_3_2/ValueEnumerator.cpp
)

add_library(Halide ${HALIDE_LIBRARY_TYPE}
  AddImageChecks.cpp
  AddParameterChecks.cpp
  AlignLoads.cpp
  AllocationBoundsInference.cpp
  Associativity.cpp
  BoundaryConditions.cpp
  Bounds.cpp
  BoundsInference.cpp
  BufferPtr.cpp
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
  CodeGen_PNaCl.cpp
  CodeGen_PowerPC.cpp
  CodeGen_PTX_Dev.cpp
  CodeGen_Posix.cpp
  CodeGen_Renderscript_Dev.cpp
  CodeGen_X86.cpp
  CPlusPlusMangle.cpp
  CSE.cpp
  Debug.cpp
  Debug.cpp
  DebugToFile.cpp
  DeepCopy.cpp
  Definition.cpp
  Deinterleave.cpp
  DeviceArgument.cpp
  DeviceInterface.cpp
  EarlyFree.cpp
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
  Interval.cpp
  InjectHostDevBufferCopies.cpp
  InjectImageIntrinsics.cpp
  InjectOpenGLIntrinsics.cpp
  Inline.cpp
  InlineReductions.cpp
  IntegerDivisionTable.cpp
  Introspection.cpp
  JITModule.cpp
  LLVM_Output.cpp
  LLVM_Runtime_Linker.cpp
  Lerp.cpp
  LoopCarry.cpp
  Lower.cpp
  MatlabWrapper.cpp
  Memoization.cpp
  Module.cpp
  ModulusRemainder.cpp
  Monotonic.cpp
  ObjectInstanceRegistry.cpp
  OutputImageParam.cpp
  ParallelRVar.cpp
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
  StmtToHtml.cpp
  StorageFlattening.cpp
  StorageFolding.cpp
  Substitute.cpp
  Target.cpp
  Tracing.cpp
  TrimNoOps.cpp
  Tuple.cpp
  Type.cpp
  UnifyDuplicateLets.cpp
  UniquifyVariableNames.cpp
  UnrollLoops.cpp
  Util.cpp
  Var.cpp
  VaryingAttributes.cpp
  VectorizeLoops.cpp
  WrapCalls.cpp
  ${BITWRITER_FILES}
  "${CMAKE_BINARY_DIR}/include/Halide.h"
  ${HEADER_FILES}
  ${INITIAL_MODULES}
)

   if (WIN32)
     if (MSVC)
           # empty
     else()
       set_target_properties(Halide PROPERTIES LINK_FLAGS "-Wl,--export-all-symbols")
     endif()
   endif()

# List of LLVM Components required
# This list will be appended to depending on the targets we need to support
# See the output of ``llvm-config --components`` for a list of possible components
set(LLVM_COMPONENTS mcjit;bitwriter;linker)
list(APPEND LLVM_COMPONENTS passes)

# Set definitions and compiler flags

# Note when PUBLIC or INTERFACE scope is used in target_compile_* then targets
# that link against the Halide library inherit those options and definitions
target_include_directories(Halide PRIVATE ${LLVM_INCLUDE})
target_include_directories(Halide INTERFACE "${CMAKE_BINARY_DIR}/include")

if (TARGET_NATIVE_CLIENT)
  target_compile_definitions(Halide PRIVATE "-DWITH_NATIVE_CLIENT=1")
endif()

# TODO: For targets we can link against even fewer libraries by specifying
# only the components we **REALLY** need (e.g. x86asmprinter;x86codegen rather than x86)
if (TARGET_X86)
  target_compile_definitions(Halide PRIVATE "-DWITH_X86=1")
  list(APPEND LLVM_COMPONENTS x86)
endif()

if (TARGET_ARM)
  target_compile_definitions(Halide PRIVATE "-DWITH_ARM=1")
  list(APPEND LLVM_COMPONENTS arm)
endif()

if (TARGET_AARCH64)
  target_compile_definitions(Halide PRIVATE "-DWITH_AARCH64=1")
  list(APPEND LLVM_COMPONENTS aarch64)
endif()

if (TARGET_HEXAGON)
  target_compile_definitions(Halide PRIVATE "-DWITH_HEXAGON=1")
  list(APPEND LLVM_COMPONENTS hexagon)
endif()

if (TARGET_PTX)
  target_compile_definitions(Halide PRIVATE "-DWITH_PTX=1")
  list(APPEND LLVM_COMPONENTS nvptx)
endif()

if (TARGET_OPENCL)
  target_compile_definitions(Halide PRIVATE "-DWITH_OPENCL=1")
endif()

if (TARGET_OPENGL)
  target_compile_definitions(Halide PRIVATE "-DWITH_OPENGL=1")
endif()

if (TARGET_MIPS)
  target_compile_definitions(Halide PRIVATE "-DWITH_MIPS=1")
  list(APPEND LLVM_COMPONENTS mips)
endif()

if (TARGET_METAL)
  target_compile_definitions(Halide PRIVATE "-DWITH_METAL=1")
endif()

target_compile_definitions(Halide PRIVATE "-DLLVM_VERSION=${LLVM_VERSION}")
target_compile_definitions(Halide PRIVATE "-DCOMPILING_HALIDE")

if (MSVC)
  # Suppress some warnings
  target_compile_options(Halide PUBLIC /wd4244 /wd4267 /wd4800 /wd4996)
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
  # I'm not sure if we want targets that link against Halide to inherit
  # the LLVM definitions so make the scope PRIVATE for now.
  # FIXME: These defines should come from ``llvm-config --cppflags``
  target_compile_definitions(Halide PRIVATE "-D__STDC_LIMIT_MACROS" "-D__STDC_CONSTANT_MACROS" "-D__STDC_FORMAT_MACROS")
  target_compile_options(Halide PUBLIC "-std=c++11") # Halide and its clients need to use C++11
  if(NOT HALIDE_ENABLE_RTTI)
    target_compile_options(Halide PUBLIC "-fno-rtti")
  endif()
endif()

# Get the LLVM libraries we need
set(LLVM_CONFIG ${LLVM_BIN}/llvm-config)
if(MSVC)
  file(GLOB LIBS "${LLVM_LIB}/LLVM*.lib")
else()
  execute_process(COMMAND "${LLVM_CONFIG}" --libfiles ${LLVM_COMPONENTS} 
                  RESULT_VARIABLE LLVM_CONFIG_RESULT
                  OUTPUT_VARIABLE LIBS_UNSTRIPPED
                  ERROR_VARIABLE LLVM_CONFIG_ERROR)
  if (${LLVM_CONFIG_RESULT})
    message(FATAL_ERROR "llvm-config failed: ${LLVM_CONFIG_ERROR}")
  endif()
  string(STRIP "${LIBS_UNSTRIPPED}" LIBS_SPACES)
  string(REPLACE " " ";" LIBS "${LIBS_SPACES}")
endif()

# When building a shared library the LLVM libraries will be
# embedded in the Halide library. When building a static library
# LLVM is not embedded but CMake knows that when building an executable
# against the Halide static library that it needs to link LLVM too so
# PRIVATE scope is the correct choice here.
target_link_libraries(Halide PRIVATE ${LIBS})

if (NOT MSVC)
  execute_process(COMMAND "${LLVM_CONFIG}" --system-libs ${LLVM_COMPONENTS} OUTPUT_VARIABLE EXTRA_LIBS)
  string(STRIP EXTRA_LIBS "${EXTRA_LIBS}")
  string(REPLACE "-l" ";" EXTRA_LIBS "${EXTRA_LIBS}")
  string(REPLACE "\n" "" EXTRA_LIBS "${EXTRA_LIBS}")
  string(REPLACE " " "" EXTRA_LIBS "${EXTRA_LIBS}")
  target_link_libraries(Halide PUBLIC ${EXTRA_LIBS})
endif()

add_dependencies(Halide
  bitcode2cpp
  build_halide_h
)
