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
  profiler
  profiler_inlined
  renderscript
  runtime_api
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
  CodeGen_Renderscript_Dev.h
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
file(COPY runtime/HalideRuntimeRenderscript.h DESTINATION "${CMAKE_BINARY_DIR}/include")


IF ((LLVM_VERSION LESS 36) OR (TARGET_NATIVE_CLIENT))
  set(BITWRITER_FILES
    BitWriter_3_2.35/BitcodeWriter.cpp
    BitWriter_3_2.35/BitcodeWriterPass.cpp
    BitWriter_3_2.35/ValueEnumerator.cpp
  )
ELSE()
  set(BITWRITER_FILES
    BitWriter_3_2/BitcodeWriter.cpp
    BitWriter_3_2/BitcodeWriterPass.cpp
    BitWriter_3_2/ValueEnumerator.cpp
  )
ENDIF()

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
  CodeGen_Renderscript_Dev.cpp
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
  PrintLoopNest.cpp
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
  ${BITWRITER_FILES}
  "${CMAKE_BINARY_DIR}/include/Halide.h"
  ${HEADER_FILES}
  ${INITIAL_MODULES}
)

# List of LLVM Components required
# This list will be appended to depending on the targets we need to support
# See the output of ``llvm-config --components`` for a list of possible components
set(LLVM_COMPONENTS mcjit;bitwriter;linker)
if (LLVM_VERSION GREATER 36)
  list(APPEND LLVM_COMPONENTS passes)
else()
  list(APPEND LLVM_COMPONENTS ipo)
endif()

# Set definitions and compiler flags

# Note when PUBLIC or INTERFACE scope is used in target_compile_* then targets
# that link against the Halide library inherit those options and definitions
target_include_directories(Halide PRIVATE ${LLVM_INCLUDE})
target_include_directories(Halide INTERFACE "${CMAKE_BINARY_DIR}/include")

if (TARGET_NATIVE_CLIENT)
  target_compile_definitions(Halide PRIVATE "-DWITH_NATIVE_CLIENT")
endif()

# TODO: For targets we can link against even fewer libraries by specifying
# only the components we **REALLY** need (e.g. x86asmprinter;x86codegen rather than x86)
if (TARGET_X86)
  target_compile_definitions(Halide PRIVATE "-DWITH_X86")
  list(APPEND LLVM_COMPONENTS x86)
endif()

if (TARGET_ARM)
  target_compile_definitions(Halide PRIVATE "-DWITH_ARM")
  list(APPEND LLVM_COMPONENTS arm)
endif()

if (TARGET_AARCH64)
  target_compile_definitions(Halide PRIVATE "-DWITH_AARCH64")
  list(APPEND LLVM_COMPONENTS aarch64)
endif()

if (TARGET_PTX)
  target_compile_definitions(Halide PRIVATE "-DWITH_PTX")
  list(APPEND LLVM_COMPONENTS nvptx)
endif()

if (TARGET_OPENCL)
  target_compile_definitions(Halide PRIVATE "-DWITH_OPENCL")
endif()

if (TARGET_OPENGL)
  target_compile_definitions(Halide PRIVATE "-DWITH_OPENGL")
endif()

if (TARGET_MIPS)
  target_compile_definitions(Halide PRIVATE "-DWITH_MIPS")
  list(APPEND LLVM_COMPONENTS mips)
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
  target_compile_options(Halide PUBLIC "-fno-rtti")
endif()

# Get the LLVM libraries we need
set(LLVM_CONFIG ${LLVM_BIN}/llvm-config)
if(WIN32)
  file(GLOB LIBS "${LLVM_LIB}/LLVM*.lib")
else()
  execute_process(COMMAND "${LLVM_CONFIG}" --libfiles ${LLVM_COMPONENTS} OUTPUT_VARIABLE LIBS_UNSTRIPPED)
  string(STRIP "${LIBS_UNSTRIPPED}" LIBS_SPACES)
  string(REPLACE " " ";" LIBS "${LIBS_SPACES}")
endif()

# When building a shared library the LLVM libraries will be
# embedded in the Halide library. When building a static library
# LLVM is not embedded but CMake knows that when building an executable
# against the Halide static library that it needs to link LLVM too so
# PRIVATE scope is the correct choice here.
target_link_libraries(Halide PRIVATE ${LIBS})

if (NOT WIN32)
  if (${LLVM_VERSION} GREATER 34)
    execute_process(COMMAND "${LLVM_CONFIG}" --system-libs ${LLVM_COMPONENTS} OUTPUT_VARIABLE EXTRA_LIBS)
  else()
    execute_process(COMMAND "${LLVM_CONFIG}" --ldflags ${LLVM_COMPONENTS} OUTPUT_VARIABLE EXTRA_LIBS)
  endif()
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
