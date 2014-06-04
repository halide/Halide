add_executable(build_halide_h ../tools/build_halide_h.cpp)
add_executable(bitcode2cpp ../tools/bitcode2cpp.cpp)


if (MSVC)
  # Suppress some warnings
  add_definitions("-D_CRT_SECURE_NO_WARNINGS")
  add_definitions("-D_SCL_SECURE_NO_WARNINGS")
  add_definitions("/wd4244 /wd4267 /wd4800 /wd4996")
else()
  add_definitions("-D__STDC_LIMIT_MACROS")
  add_definitions("-D__STDC_CONSTANT_MACROS")
  add_definitions("-std=c++11")
endif()

if (HALIDE_SHARED_LIBRARY)
  add_definitions("-DHalide_SHARED")
  set(HALIDE_LIBRARY_TYPE SHARED)
else()
  set(HALIDE_LIBRARY_TYPE STATIC)
endif()

include_directories ( "${LLVM_INCLUDE}")
link_directories ("${LLVM_LIB}")

if(WIN32)
  file(GLOB LIBS RELATIVE "${LLVM_LIB}" "${LLVM_LIB}/*.lib")
else()
  execute_process(COMMAND "${LLVM_CONFIG}" --ldflags --libs OUTPUT_VARIABLE LIBS)
endif()

if (TARGET_NATIVE_CLIENT)
  add_definitions("-DWITH_NATIVE_CLIENT=1")
endif()

if (TARGET_X86)
  add_definitions("-DWITH_X86=1")
endif()

if (TARGET_ARM)
  add_definitions("-DWITH_ARM=1")
endif()

if (TARGET_PTX)
  add_definitions("-DWITH_PTX=1")
endif()

if (TARGET_OPENCL)
  add_definitions("-DWITH_OPENCL=1")
endif()

if (TARGET_OPENGL)
  add_definitions("-DWITH_OPENGL=1")
endif()

if (TARGET_SPIR)
  add_definitions("-DWITH_SPIR=1")
endif()

if (TARGET_ARM64)
  add_definitions("-DWITH_ARM64=1")
endif()

add_definitions("-DLLVM_VERSION=${LLVM_VERSION}")

set(RUNTIME_CPP
  android_io
  cuda
  fake_thread_pool
  gcd_thread_pool
  ios_io
  android_clock
  linux_clock
  nogpu
  opencl
  opengl
  osx_opengl_context
  linux_opengl_context
  posix_allocator
  posix_clock
  osx_clock
  windows_clock
  posix_error_handler
  posix_io
  nacl_io
  ssp
  osx_io
  posix_math
  posix_thread_pool
  windows_thread_pool
  android_host_cpu_count
  linux_host_cpu_count
  osx_host_cpu_count
  tracing
  write_debug_image
  cuda_debug
  opencl_debug
  opengl_debug
  windows_io)
set (RUNTIME_LL
  arm
  posix_math
  pnacl_math
  ptx_dev
  spir_dev
  spir64_dev
  spir_common_dev
  x86_avx
  x86
  x86_sse41
  pnacl_math)
set (RUNTIME_BC
  compute_20
  compute_30
  compute_35)

set(RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/runtime")
file(TO_NATIVE_PATH "${RUNTIME_DIR}/" NATIVE_RUNTIME_DIR)
file(TO_NATIVE_PATH "${CMAKE_CFG_INTDIR}/" NATIVE_INT_DIR)

# ./ seems to confuse cmake on linux
if("${NATIVE_INT_DIR}" STREQUAL "./")
  set(NATIVE_INT_DIR "")
endif()

# Gather dependencies for runtime objects.
file(GLOB RUNTIME_CL_DEPENDS "${RUNTIME_DIR}/CL/*.h")
file(GLOB RUNTIME_CPP_DEPENDS "${RUNTIME_DIR}/*.cpp")
file(GLOB RUNTIME_H_DEPENDS "${RUNTIME_DIR}/*.h")
file(GLOB RUNTIME_LL_DEPENDS "${RUNTIME_DIR}/*.ll")
file(GLOB RUNTIME_BC_DEPENDS "${RUNTIME_DIR}/nvidia_libdevice_bitcode/*.bc")
set (RUNTIME_DEPENDS
  ${RUNTIME_CL_DEPENDS}
  ${RUNTIME_CPP_DEPENDS}
  ${RUNTIME_H_DEPENDS}
  ${RUNTIME_LL_DEPENDS}
  ${RUNTIME_BC_DEPENDS})

# Commands to build initial module objects.
set (INITMOD_PREFIX "_initmod_")
set (ARCHS 32 64)
set(INITIAL_MODULES )
foreach (i ${RUNTIME_CPP} )
  foreach (j ${ARCHS} )
    set(INITMOD "${INITMOD_PREFIX}${i}_${j}.cpp")
    add_custom_command(OUTPUT "${INITMOD}"
                       COMMAND ${CLANG} -fno-blocks -m${j} -DCOMPILING_HALIDE -DBITS_${j} -emit-llvm -O3 -S "${NATIVE_RUNTIME_DIR}${i}.cpp" -o "${NATIVE_INT_DIR}initmod.${i}_${j}.ll"
                       COMMAND "${LLVM_AS}" "${NATIVE_INT_DIR}initmod.${i}_${j}.ll" -o "${NATIVE_INT_DIR}initmod.${i}_${j}.bc"
                       COMMAND bitcode2cpp "${i}_${j}" < "${NATIVE_INT_DIR}initmod.${i}_${j}.bc" > "${INITMOD}"
                       COMMENT "Building initial module ${i}_${j}..."
                       VERBATIM)
    list(APPEND INITIAL_MODULES ${INITMOD})
  endforeach()
endforeach()
foreach (i ${RUNTIME_LL} )
  set(INITMOD "${INITMOD_PREFIX}${i}.cpp")
  add_custom_command(OUTPUT "${INITMOD}"
                     COMMAND "${LLVM_AS}" "${NATIVE_RUNTIME_DIR}${i}.ll" -o "${NATIVE_INT_DIR}initmod.${i}.bc"
                     COMMAND bitcode2cpp ${i}_ll < "${NATIVE_INT_DIR}initmod.${i}.bc" > "${INITMOD}"
                     COMMENT "Building initial module ${i}..."
                     VERBATIM)
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

add_library(InitialModules STATIC
  ${INITIAL_MODULES})

add_dependencies(InitialModules
  bitcode2cpp)

set(HEADER_FILES
  Util.h
  Type.h
  Argument.h
  Bounds.h
  BoundsInference.h
  Buffer.h
  buffer_t.h
  CodeGen_C.h
  CodeGen.h
  CodeGen_X86.h
  CodeGen_GPU_Host.h
  CodeGen_PTX_Dev.h
  CodeGen_OpenCL_Dev.h
  CodeGen_OpenGL_Dev.h
  CodeGen_GPU_Dev.h
  CodeGen_SPIR_Dev.h
  CodeGen_PNaCl.h
  Deinterleave.h
  Derivative.h
  OneToOne.h
  Extern.h
  FindCalls.h
  Func.h
  Function.h
  Image.h
  InlineReductions.h
  IntegerDivisionTable.h
  Introspection.h
  IntrusivePtr.h
  IREquality.h
  IR.h
  IRMatch.h
  IRMutator.h
  IROperator.h
  IRPrinter.h
  IRVisitor.h
  InjectOpenGLIntrinsics.h
  InjectHostDevBufferCopies.h
  JITCompiledModule.h
  Lambda.h
  Debug.h
  Lower.h
  MainPage.h
  ModulusRemainder.h
  Parameter.h
  Param.h
  RDom.h
  Reduction.h
  RemoveTrivialForLoops.h
  Schedule.h
  Scope.h
  Simplify.h
  SlidingWindow.h
  StmtCompiler.h
  StorageFlattening.h
  StorageFolding.h
  Substitute.h
  Profiling.h
  Tracing.h
  UnrollLoops.h
  Var.h
  VectorizeLoops.h
  CodeGen_Posix.h
  CodeGen_ARM.h
  DebugToFile.h
  EarlyFree.h
  UniquifyVariableNames.h
  CSE.h
  Tuple.h
  Lerp.h
  Target.h
  SkipStages.h
  RemoveUndef.h
  SpecializeClampedRamps.h
  FastIntegerDivide.h
  AllocationBoundsInference.h
  Error.h
  Inline.h
  Qualify.h
  ExprUsesVar.h
  Random.h
  UnifyDuplicateLets.h)

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/include")
file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}/include/" NATIVE_INCLUDE_PATH)
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/include/Halide.h"
  COMMAND build_halide_h ${HEADER_FILES} > "${NATIVE_INCLUDE_PATH}Halide.h"
  WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
  DEPENDS build_halide_h ${HEADER_FILES})

add_definitions("-DCOMPILING_HALIDE")

add_library(Halide ${HALIDE_LIBRARY_TYPE}
  CodeGen.cpp
  CodeGen_Internal.cpp
  CodeGen_X86.cpp
  CodeGen_GPU_Host.cpp
  CodeGen_PTX_Dev.cpp
  CodeGen_OpenCL_Dev.cpp
  CodeGen_OpenGL_Dev.cpp
  CodeGen_SPIR_Dev.cpp
  CodeGen_GPU_Dev.cpp
  CodeGen_Posix.cpp
  CodeGen_PNaCl.cpp
  CodeGen_ARM.cpp
  IR.cpp
  IRMutator.cpp
  IRPrinter.cpp
  IRVisitor.cpp
  CodeGen_C.cpp
  Substitute.cpp
  ModulusRemainder.cpp
  Bounds.cpp
  Derivative.cpp
  OneToOne.cpp
  FindCalls.cpp
  Func.cpp
  Simplify.cpp
  IREquality.cpp
  Util.cpp
  Function.cpp
  IROperator.cpp
  Lower.cpp
  Debug.cpp
  Parameter.cpp
  Reduction.cpp
  Random.cpp
  RDom.cpp
  Profiling.cpp
  Tracing.cpp
  StorageFlattening.cpp
  VectorizeLoops.cpp
  UnrollLoops.cpp
  BoundsInference.cpp
  IRMatch.cpp
  StmtCompiler.cpp
  IntegerDivisionTable.cpp
  SlidingWindow.cpp
  StorageFolding.cpp
  InlineReductions.cpp
  RemoveTrivialForLoops.cpp
  Deinterleave.cpp
  DebugToFile.cpp
  Type.cpp
  JITCompiledModule.cpp
  EarlyFree.cpp
  UniquifyVariableNames.cpp
  CSE.cpp
  Tuple.cpp
  Lerp.cpp
  Target.cpp
  SkipStages.cpp
  RemoveUndef.cpp
  SpecializeClampedRamps.cpp
  FastIntegerDivide.cpp
  AllocationBoundsInference.cpp
  Inline.cpp
  Qualify.cpp
  UnifyDuplicateLets.cpp
  ExprUsesVar.cpp
  Introspection.cpp
  Buffer.cpp
  Param.cpp
  Error.cpp
  Image.cpp
  Debug.cpp
  InjectOpenGLIntrinsics.cpp
  InjectHostDevBufferCopies.cpp
  FuseGPUThreadLoops.cpp
  "${CMAKE_BINARY_DIR}/include/Halide.h"
  ${HEADER_FILES})

target_link_libraries(Halide InitialModules ${LIBS})
# if this is a DLL, don't link dependencies to this set of libs.
if (HALIDE_SHARED_LIBRARY)
  set_target_properties(Halide PROPERTIES LINK_INTERFACE_LIBRARIES "")
endif()

add_dependencies(Halide
  InitialModules
  build_halide_h)
