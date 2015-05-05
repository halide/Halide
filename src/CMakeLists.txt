add_executable(build_halide_h ../tools/build_halide_h.cpp)
add_executable(bitcode2cpp ../tools/bitcode2cpp.cpp)


if (MSVC)
  # Suppress some warnings
  add_definitions("-D_CRT_SECURE_NO_WARNINGS")
  add_definitions("-D_SCL_SECURE_NO_WARNINGS")
  add_definitions("/wd4244 /wd4267 /wd4800 /wd4996")
  # -g produces dwarf debugging info, which is not useful on windows
  #  (and fails to compile due to llvm bug 15393)
  set(RUNTIME_DEBUG_FLAG "")
else()
  add_definitions("-D__STDC_LIMIT_MACROS")
  add_definitions("-D__STDC_CONSTANT_MACROS")
  add_definitions("-std=c++11")
  add_definitions("-fno-rtti")
  set(RUNTIME_DEBUG_FLAG "-g")
endif()

if (HALIDE_SHARED_LIBRARY)
  add_definitions("-DHalide_SHARED")
  set(HALIDE_LIBRARY_TYPE SHARED)
else()
  set(HALIDE_LIBRARY_TYPE STATIC)
endif()

include_directories ( "${LLVM_INCLUDE}")
link_directories ("${LLVM_LIB}")

set(LLVM_CONFIG ${LLVM_BIN}/llvm-config)
if(WIN32)
  file(GLOB LIBS RELATIVE "${LLVM_LIB}" "${LLVM_LIB}/*.lib")
else()
  #file(GLOB LIBS RELATIVE "${LLVM_LIB}" "${LLVM_LIB}/libLLVM*.a")
  execute_process(COMMAND "${LLVM_CONFIG}" --libfiles OUTPUT_VARIABLE LIBS_UNSTRIPPED)
  string(STRIP "${LIBS_UNSTRIPPED}" LIBS_SPACES)
  string(REPLACE " " ";" LIBS "${LIBS_SPACES}")
endif()

if (TARGET_NATIVE_CLIENT)
  add_definitions("-DWITH_NATIVE_CLIENT")
endif()

if (TARGET_X86)
  add_definitions("-DWITH_X86")
endif()

if (TARGET_ARM)
  add_definitions("-DWITH_ARM")
endif()

if (TARGET_AARCH64)
  add_definitions("-DWITH_AARCH64")
endif()

if (TARGET_PTX)
  add_definitions("-DWITH_PTX")
endif()

if (TARGET_OPENCL)
  add_definitions("-DWITH_OPENCL")
endif()

if (TARGET_OPENGL)
  add_definitions("-DWITH_OPENGL")
endif()

if (TARGET_MIPS)
  add_definitions("-DWITH_MIPS")
endif()

add_definitions("-DLLVM_VERSION=${LLVM_VERSION}")

set(RUNTIME_CPP
  android_clock
  android_host_cpu_count
  android_io
  android_opengl_context
  cache
  cuda
  destructors
  device_interface
  fake_thread_pool
  gcd_thread_pool
  gpu_device_selection
  ios_io
  linux_clock
  linux_host_cpu_count
  linux_opengl_context
  matlab
  metadata
  module_aot_ref_count
  module_jit_ref_count
  nacl_host_cpu_count
  opencl
  opengl
  osx_clock
  osx_get_symbol
  osx_host_cpu_count
  osx_opengl_context
  posix_allocator
  posix_clock
  posix_error_handler
  posix_get_symbol
  posix_io
  posix_math
  posix_print
  posix_thread_pool
  ssp
  to_string
  tracing
  windows_clock
  windows_cuda
  windows_get_symbol
  windows_io
  windows_opencl
  windows_thread_pool
  write_debug_image
)

set (RUNTIME_LL
  aarch64
  arm
  arm_no_neon
  mips
  pnacl_math
  posix_math
  ptx_dev
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
    IF (LLVM_VERSION LESS 36)
      IF (ARCHS EQUAL 32)
        set(TARGET "i386-unknown-unknown-unknown")
      ELSE()
        set(TARGET "x86_64-unknown-unknown-unknown")
      ENDIF()
    ELSE()
      IF (ARCHS EQUAL 32)
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
                       COMMENT "${SOURCE} -> ${LL_D}")
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

add_library(InitialModules STATIC
  ${INITIAL_MODULES})

add_dependencies(InitialModules
  bitcode2cpp)

set(HEADER_FILES
  AddImageChecks.h
  AddParameterChecks.h
  AllocationBoundsInference.h
  Argument.h
  BlockFlattening.h
  BoundaryConditions.h
  Bounds.h
  BoundsInference.h
  Buffer.h
  CSE.h
  CodeGen_ARM.h
  CodeGen_C.h
  CodeGen_GPU_Dev.h
  CodeGen_GPU_Host.h
  CodeGen_LLVM.h
  CodeGen_MIPS.h
  CodeGen_OpenCL_Dev.h
  CodeGen_OpenGL_Dev.h
  CodeGen_PNaCl.h
  CodeGen_PTX_Dev.h
  CodeGen_Posix.h
  CodeGen_X86.h
  Debug.h
  DebugToFile.h
  Deinterleave.h
  Derivative.h
  DeviceInterface.h
  EarlyFree.h
  Error.h
  Expr.h
  ExprUsesVar.h
  Extern.h
  FastIntegerDivide.h
  FindCalls.h
  Func.h
  Function.h
  Generator.h
  HumanReadableStmt.h
  IR.h
  IREquality.h
  IRMatch.h
  IRMutator.h
  IROperator.h
  IRPrinter.h
  IRVisitor.h
  Image.h
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
  Lower.h
  MainPage.h
  MatlabWrapper.h
  Memoization.h
  Module.h
  ModulusRemainder.h
  ObjectInstanceRegistry.h
  OneToOne.h
  Output.h
  ParallelRVar.h
  Param.h
  Parameter.h
  PartitionLoops.h
  Pipeline.h
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
  Simplify.h
  SkipStages.h
  SlidingWindow.h
  Solve.h
  StmtToHtml.h
  StorageFlattening.h
  StorageFolding.h
  Substitute.h
  Target.h
  Tracing.h
  Tuple.h
  Type.h
  UnifyDuplicateLets.h
  UniquifyVariableNames.h
  UnrollLoops.h
  Util.h
  Var.h
  VaryingAttributes.h
  VectorizeLoops.h
  runtime/HalideRuntime.h
)

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/include")
file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}/include/" NATIVE_INCLUDE_PATH)
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/include/Halide.h"
  COMMAND build_halide_h ${HEADER_FILES} > "${NATIVE_INCLUDE_PATH}Halide.h"
  WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
  DEPENDS build_halide_h ${HEADER_FILES})

file(COPY runtime/HalideRuntime.h DESTINATION "${CMAKE_BINARY_DIR}/include")
file(COPY runtime/HalideRuntimeCuda.h DESTINATION "${CMAKE_BINARY_DIR}/include")
file(COPY runtime/HalideRuntimeOpenCL.h DESTINATION "${CMAKE_BINARY_DIR}/include")
file(COPY runtime/HalideRuntimeOpenGL.h DESTINATION "${CMAKE_BINARY_DIR}/include")

add_definitions("-DCOMPILING_HALIDE")

add_library(Halide ${HALIDE_LIBRARY_TYPE}
  AddImageChecks.cpp
  AddParameterChecks.cpp
  AllocationBoundsInference.cpp
  BlockFlattening.cpp
  BoundaryConditions.cpp
  Bounds.cpp
  BoundsInference.cpp
  Buffer.cpp
  CSE.cpp
  CodeGen_ARM.cpp
  CodeGen_C.cpp
  CodeGen_GPU_Dev.cpp
  CodeGen_GPU_Host.cpp
  CodeGen_Internal.cpp
  CodeGen_LLVM.cpp
  CodeGen_MIPS.cpp
  CodeGen_OpenCL_Dev.cpp
  CodeGen_OpenGL_Dev.cpp
  CodeGen_PNaCl.cpp
  CodeGen_PTX_Dev.cpp
  CodeGen_Posix.cpp
  CodeGen_X86.cpp
  Debug.cpp
  Debug.cpp
  DebugToFile.cpp
  Deinterleave.cpp
  Derivative.cpp
  DeviceInterface.cpp
  EarlyFree.cpp
  Error.cpp
  ExprUsesVar.cpp
  FastIntegerDivide.cpp
  FindCalls.cpp
  Func.cpp
  Function.cpp
  FuseGPUThreadLoops.cpp
  Generator.cpp
  HumanReadableStmt.cpp
  IR.cpp
  IREquality.cpp
  IRMatch.cpp
  IRMutator.cpp
  IROperator.cpp
  IRPrinter.cpp
  IRVisitor.cpp
  Image.cpp
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
  Lower.cpp
  MatlabWrapper.cpp
  Memoization.cpp
  Module.cpp
  ModulusRemainder.cpp
  ObjectInstanceRegistry.cpp
  OneToOne.cpp
  Output.cpp
  ParallelRVar.cpp
  Param.cpp
  Parameter.cpp
  PartitionLoops.cpp
  Pipeline.cpp
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
  Simplify.cpp
  SkipStages.cpp
  SlidingWindow.cpp
  Solve.cpp
  StmtToHtml.cpp
  StorageFlattening.cpp
  StorageFolding.cpp
  Substitute.cpp
  Target.cpp
  Tracing.cpp
  Tuple.cpp
  Type.cpp
  UnifyDuplicateLets.cpp
  UniquifyVariableNames.cpp
  UnrollLoops.cpp
  Util.cpp
  Var.cpp
  VaryingAttributes.cpp
  VectorizeLoops.cpp
  "${CMAKE_BINARY_DIR}/include/Halide.h"
  ${HEADER_FILES}
)

# CMake's Xcode generator will run otool on all of the ${LIBS} if we pass them
# to target_link_libraries. This makes the CMake generation step take a very
# long time. Instead, since we add the libs to the extra.LinkFileList passed to
# Xcode below it is not necessary to pass them to target_link_libraries as well.
if (XCODE)
  target_link_libraries(Halide InitialModules)
else()
  target_link_libraries(Halide InitialModules ${LIBS})
endif()

if (NOT WIN32)
  if (${LLVM_VERSION} GREATER 34)
    execute_process(COMMAND "${LLVM_CONFIG}" --system-libs OUTPUT_VARIABLE EXTRA_LIBS)
  else()
    execute_process(COMMAND "${LLVM_CONFIG}" --ldflags OUTPUT_VARIABLE EXTRA_LIBS)
  endif()
  string(STRIP EXTRA_LIBS "${EXTRA_LIBS}")
  string(REPLACE "-l" ";" EXTRA_LIBS "${EXTRA_LIBS}")
  string(REPLACE "\n" "" EXTRA_LIBS "${EXTRA_LIBS}")
  string(REPLACE " " "" EXTRA_LIBS "${EXTRA_LIBS}")
  target_link_libraries(Halide ${EXTRA_LIBS})
endif()

if (NOT HALIDE_SHARED_LIBRARY)

# For the Xcode build, we want the static library version of libHalide.a
# to include all of its dependencies so that other projects can link to the
# library without having to worry about which version of LLVM or the initial
# modules it was built with. This is accomplished by adding an additional file
# list to the link line containing all of these dependencies

if (XCODE)

# Create a link file list
set(EXTRA_LINKFILELIST "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.build/extra.LinkFileList")

# Determine the location of libInitialModules.a so we can include the path to it
# in the link file list
IF (CMAKE_BUILD_TYPE)
  set(InitialModulesPath ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}/${CMAKE_BUILD_TYPE}/libInitialModules.a)
ELSE()
  set(InitialModulesPath ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}/Debug/libInitialModules.a)
ENDIF()

# We need a newline delimited list of the lib files
execute_process(COMMAND "${LLVM_CONFIG}" --libfiles OUTPUT_VARIABLE LIBS)
string(REPLACE " " "\n" LIBS ${LIBS})

# Write the archive file dependencies to a new LinkFileList
file(WRITE ${EXTRA_LINKFILELIST} "${LIBS}${InitialModulesPath}\n")

set_target_properties(Halide PROPERTIES XCODE_ATTRIBUTE_OTHER_LIBTOOLFLAGS "-filelist ${EXTRA_LINKFILELIST}")

endif(XCODE)

endif (NOT HALIDE_SHARED_LIBRARY)

# if this is a DLL, don't link dependencies to this set of libs.
#if (HALIDE_SHARED_LIBRARY)
#  set_target_properties(Halide PROPERTIES LINK_INTERFACE_LIBRARIES "")
#endif()

add_dependencies(Halide
  InitialModules
  build_halide_h
)
