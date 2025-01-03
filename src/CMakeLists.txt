##
# Declare the Halide library target.
##

add_library(Halide)
add_library(Halide::Halide ALIAS Halide)

# Language standard
target_compile_features(Halide PUBLIC cxx_std_17)

# Inform the sources if we're building a static or shared library
if (NOT BUILD_SHARED_LIBS)
    target_compile_definitions(Halide PRIVATE Halide_STATIC_DEFINE)
endif ()

# Set the (shared) library version
set(Halide_VERSION_OVERRIDE "${Halide_VERSION}"
    CACHE STRING "VERSION to set for custom Halide packaging")
mark_as_advanced(Halide_VERSION_OVERRIDE)

if (NOT Halide_VERSION_OVERRIDE STREQUAL "")
    # CMake treats an empty VERSION property differently from leaving it unset.
    # We also can't check the boolean-ness of Halide_VERSION_OVERRIDE because
    # VERSION 0 is valid. See: https://github.com/halide/Halide/issues/8522
    set_target_properties(Halide PROPERTIES VERSION "${Halide_VERSION_OVERRIDE}")
endif ()

set(Halide_SOVERSION_OVERRIDE "${Halide_VERSION_MAJOR}"
    CACHE STRING "SOVERSION to set for custom Halide packaging")
mark_as_advanced(Halide_SOVERSION_OVERRIDE)

if (NOT Halide_SOVERSION_OVERRIDE STREQUAL "")
    set_target_properties(Halide PROPERTIES SOVERSION "${Halide_SOVERSION_OVERRIDE}")
endif ()

# Always build with PIC, even when static
set_target_properties(Halide PROPERTIES POSITION_INDEPENDENT_CODE ON)

##
# Lists of source files. Keep ALL lists sorted in alphabetical order.
##

# The externally-visible header files that go into making Halide.h.
# Don't include anything here that includes llvm headers.
# Also *don't* include anything that's only used internally (eg SpirvIR.h).
target_sources(
    Halide
    PRIVATE
    FILE_SET private_headers
    TYPE HEADERS
    FILES
    AbstractGenerator.h
    AddAtomicMutex.h
    AddImageChecks.h
    AddParameterChecks.h
    AddSplitFactorChecks.h
    AlignLoads.h
    AllocationBoundsInference.h
    ApplySplit.h
    Argument.h
    AssociativeOpsTable.h
    Associativity.h
    AsyncProducers.h
    AutoScheduleUtils.h
    BoundaryConditions.h
    Bounds.h
    BoundsInference.h
    BoundConstantExtentLoops.h
    BoundSmallAllocations.h
    Buffer.h
    Callable.h
    CanonicalizeGPUVars.h
    ClampUnsafeAccesses.h
    Closure.h
    CodeGen_C.h
    CodeGen_D3D12Compute_Dev.h
    CodeGen_GPU_Dev.h
    CodeGen_Internal.h
    CodeGen_LLVM.h
    CodeGen_Metal_Dev.h
    CodeGen_OpenCL_Dev.h
    CodeGen_Posix.h
    CodeGen_PTX_Dev.h
    CodeGen_PyTorch.h
    CodeGen_Targets.h
    CodeGen_Vulkan_Dev.h
    CodeGen_WebGPU_Dev.h
    CompilerLogger.h
    ConciseCasts.h
    CPlusPlusMangle.h
    ConstantBounds.h
    ConstantInterval.h
    CSE.h
    Debug.h
    DebugArguments.h
    DebugToFile.h
    Definition.h
    Deinterleave.h
    Derivative.h
    DerivativeUtils.h
    Deserialization.h
    DeviceAPI.h
    DeviceArgument.h
    DeviceInterface.h
    Dimension.h
    DistributeShifts.h
    EarlyFree.h
    Elf.h
    EliminateBoolVectors.h
    EmulateFloat16Math.h
    Error.h
    Expr.h
    ExprUsesVar.h
    Extern.h
    ExternFuncArgument.h
    ExtractTileOperations.h
    FastIntegerDivide.h
    FindCalls.h
    FindIntrinsics.h
    FlattenNestedRamps.h
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
    Inline.h
    InlineReductions.h
    IntegerDivisionTable.h
    Interval.h
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
    LowerParallelTasks.h
    LowerWarpShuffles.h
    MainPage.h
    Memoization.h
    Module.h
    ModulusRemainder.h
    Monotonic.h
    ObjectInstanceRegistry.h
    OffloadGPULoops.h
    OptimizeShuffles.h
    OutputImageParam.h
    ParallelRVar.h
    Param.h
    Parameter.h
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
    RebaseLoopsToZero.h
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
    Serialization.h
    Simplify.h
    SimplifyCorrelatedDifferences.h
    SimplifySpecializations.h
    SkipStages.h
    SlidingWindow.h
    Solve.h
    SplitTuples.h
    StageStridedLoads.h
    StmtToHTML.h
    StorageFlattening.h
    StorageFolding.h
    StrictifyFloat.h
    StripAsserts.h
    Substitute.h
    Target.h
    TargetQueryOps.h
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
    VectorizeLoops.h
    WasmExecutor.h
    WrapCalls.h
)

# The sources that go into libHalide.
target_sources(
    Halide
    PRIVATE
    AbstractGenerator.cpp
    AddAtomicMutex.cpp
    AddImageChecks.cpp
    AddParameterChecks.cpp
    AddSplitFactorChecks.cpp
    AlignLoads.cpp
    AllocationBoundsInference.cpp
    ApplySplit.cpp
    ApproximationTables.cpp
    Argument.cpp
    AssociativeOpsTable.cpp
    Associativity.cpp
    AsyncProducers.cpp
    AutoScheduleUtils.cpp
    BoundaryConditions.cpp
    Bounds.cpp
    BoundsInference.cpp
    BoundConstantExtentLoops.cpp
    BoundSmallAllocations.cpp
    Buffer.cpp
    Callable.cpp
    CanonicalizeGPUVars.cpp
    ClampUnsafeAccesses.cpp
    Closure.cpp
    CodeGen_ARM.cpp
    CodeGen_C.cpp
    CodeGen_D3D12Compute_Dev.cpp
    CodeGen_GPU_Dev.cpp
    CodeGen_Hexagon.cpp
    CodeGen_Internal.cpp
    CodeGen_LLVM.cpp
    CodeGen_Metal_Dev.cpp
    CodeGen_OpenCL_Dev.cpp
    CodeGen_Posix.cpp
    CodeGen_PowerPC.cpp
    CodeGen_PTX_Dev.cpp
    CodeGen_PyTorch.cpp
    CodeGen_RISCV.cpp
    CodeGen_Vulkan_Dev.cpp
    CodeGen_WebAssembly.cpp
    CodeGen_WebGPU_Dev.cpp
    CodeGen_X86.cpp
    CompilerLogger.cpp
    CPlusPlusMangle.cpp
    ConstantBounds.cpp
    ConstantInterval.cpp
    CSE.cpp
    Debug.cpp
    DebugArguments.cpp
    DebugToFile.cpp
    Definition.cpp
    Deinterleave.cpp
    Derivative.cpp
    DerivativeUtils.cpp
    Deserialization.cpp
    DeviceArgument.cpp
    DeviceInterface.cpp
    Dimension.cpp
    DistributeShifts.cpp
    EarlyFree.cpp
    Elf.cpp
    EliminateBoolVectors.cpp
    EmulateFloat16Math.cpp
    Error.cpp
    Expr.cpp
    ExtractTileOperations.cpp
    FastIntegerDivide.cpp
    FindCalls.cpp
    FindIntrinsics.cpp
    FlattenNestedRamps.cpp
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
    Inline.cpp
    InlineReductions.cpp
    IntegerDivisionTable.cpp
    Interval.cpp
    IR.cpp
    IREquality.cpp
    IRMatch.cpp
    IRMutator.cpp
    IROperator.cpp
    IRPrinter.cpp
    IRVisitor.cpp
    JITModule.cpp
    Lambda.cpp
    Lerp.cpp
    LICM.cpp
    LLVM_Output.cpp
    LLVM_Runtime_Linker.cpp
    LoopCarry.cpp
    Lower.cpp
    LowerParallelTasks.cpp
    LowerWarpShuffles.cpp
    Memoization.cpp
    Module.cpp
    ModulusRemainder.cpp
    Monotonic.cpp
    ObjectInstanceRegistry.cpp
    OffloadGPULoops.cpp
    OptimizeShuffles.cpp
    OutputImageParam.cpp
    ParallelRVar.cpp
    Parameter.cpp
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
    RebaseLoopsToZero.cpp
    Reduction.cpp
    RegionCosts.cpp
    RemoveDeadAllocations.cpp
    RemoveExternLoops.cpp
    RemoveUndef.cpp
    Schedule.cpp
    ScheduleFunctions.cpp
    SelectGPUAPI.cpp
    Serialization.cpp
    Simplify.cpp
    Simplify_Add.cpp
    Simplify_And.cpp
    Simplify_Call.cpp
    Simplify_Reinterpret.cpp
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
    SpirvIR.cpp
    SpirvIR.h
    SplitTuples.cpp
    StageStridedLoads.cpp
    StmtToHTML.cpp
    StorageFlattening.cpp
    StorageFolding.cpp
    StrictifyFloat.cpp
    StripAsserts.cpp
    Substitute.cpp
    Target.cpp
    TargetQueryOps.cpp
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
    VectorizeLoops.cpp
    WasmExecutor.cpp
    WrapCalls.cpp
)

set(C_TEMPLATE_FILES
    CodeGen_C_prologue
    CodeGen_C_vectors
)

set(HTML_TEMPLATE_FILES
    StmtToHTML_dependencies.html
    StmtToHTML.js
    StmtToHTML.css
)

##
# Build and import the runtime.
##

add_subdirectory(runtime)
target_link_libraries(Halide PRIVATE "$<BUILD_LOCAL_INTERFACE:Halide::initmod>")
target_link_libraries(Halide INTERFACE Halide::Runtime)

##
# Build the template files via binary2cpp.
##

foreach (f IN LISTS C_TEMPLATE_FILES)
    set(SRC "$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/${f}.template.cpp>")
    set(DST "c_template.${f}.template.cpp")

    add_custom_command(OUTPUT "${DST}"
                       COMMAND binary2cpp "halide_c_template_${f}" < "${SRC}" > "${DST}"
                       DEPENDS "${SRC}" binary2cpp
                       VERBATIM)
    target_sources(Halide PRIVATE ${DST})
endforeach ()

foreach (f IN LISTS HTML_TEMPLATE_FILES)
    set(SRC "$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/irvisualizer/html_template_${f}>")
    string(REPLACE "." "_" VARNAME "halide_html_template_${f}")
    set(DST "html_template.${f}.cpp")

    add_custom_command(OUTPUT "${DST}"
                       COMMAND binary2cpp "${VARNAME}" < "${SRC}" > "${DST}"
                       DEPENDS "${SRC}" binary2cpp
                       VERBATIM)
    target_sources(Halide PRIVATE ${DST})
endforeach ()

##
# Build the Halide mono-header.
##

set(HALIDE_H "${Halide_BINARY_DIR}/include/Halide.h")
set(LICENSE_PATH "${Halide_SOURCE_DIR}/LICENSE.txt")
set(headers "$<TARGET_PROPERTY:Halide,HEADER_SET_private_headers>")
add_custom_command(OUTPUT "${HALIDE_H}"
                   COMMAND ${CMAKE_COMMAND} -E make_directory "$<SHELL_PATH:${Halide_BINARY_DIR}/include>"
                   COMMAND build_halide_h "$<SHELL_PATH:${LICENSE_PATH}>" "${headers}" > "$<SHELL_PATH:${HALIDE_H}>"
                   DEPENDS build_halide_h "${LICENSE_PATH}" "${headers}"
                   WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
                   COMMAND_EXPAND_LISTS
                   VERBATIM)
add_custom_target(HalideIncludes DEPENDS "${HALIDE_H}")
add_dependencies(Halide HalideIncludes)

target_sources(
    Halide
    INTERFACE
    FILE_SET HEADERS
    BASE_DIRS "${Halide_BINARY_DIR}/include"
    FILES "${Halide_BINARY_DIR}/include/Halide.h"
)

if (Halide_BUNDLE_STATIC)
    bundle_static(Halide)
endif ()

##
# CodeGen backends
##

# LLVM backends
foreach (backend IN LISTS Halide_LLVM_COMPONENTS)
    string(TOUPPER "WITH_${backend}" definition)
    target_compile_definitions(Halide PRIVATE "${definition}")
    if (Halide_BUNDLE_STATIC AND NOT Halide_LLVM_SHARED_LIBS)
        target_link_libraries(Halide PRIVATE "$<BUILD_LOCAL_INTERFACE:Halide_LLVM::${backend}>")
    else ()
        target_link_libraries(Halide PRIVATE Halide_LLVM::${backend})
    endif ()
endforeach ()

# GPU backends
find_package(
    SPIRV-Headers 1.5.5 REQUIRED
    HINTS "${Halide_SOURCE_DIR}/dependencies/spirv"
)

target_link_libraries(
    Halide PRIVATE "$<BUILD_LOCAL_INTERFACE:SPIRV-Headers::SPIRV-Headers>"
)

target_compile_definitions(Halide PRIVATE WITH_D3D12)
target_compile_definitions(Halide PRIVATE WITH_METAL)
target_compile_definitions(Halide PRIVATE WITH_OPENCL)
target_compile_definitions(Halide PRIVATE WITH_SPIRV)
target_compile_definitions(Halide PRIVATE WITH_VULKAN)
target_compile_definitions(Halide PRIVATE WITH_WEBGPU)

##
# Flatbuffers and Serialization dependencies.
##

# Build serialization, enabled by default
if (WITH_SERIALIZATION)
    # Sadly, there seem to be at least three variations of the Flatbuffer
    # package in terms of the case of the relevant CMake files. Fortunately,
    # the IMPORTED targets appear to be consistently named `flatbuffers`.
    find_package(
        flatbuffers 23.5.26 REQUIRED
        NAMES flatbuffers Flatbuffers FlatBuffers
    )
    _Halide_pkgdep(flatbuffers)

    if (Halide_BUNDLE_STATIC)
        target_link_libraries(Halide PRIVATE "$<BUILD_LOCAL_INTERFACE:flatbuffers::flatbuffers>")
    elseif (Halide_USE_FETCHCONTENT AND NOT BUILD_SHARED_LIBS)
        target_sources(Halide PRIVATE "$<TARGET_OBJECTS:flatbuffers::flatbuffers>")
        target_link_libraries(Halide PRIVATE "$<BUILD_LOCAL_INTERFACE:$<COMPILE_ONLY:flatbuffers::flatbuffers>>")
    else ()
        target_link_libraries(Halide PRIVATE flatbuffers::flatbuffers)
    endif ()

    set(fb_def "${CMAKE_CURRENT_SOURCE_DIR}/halide_ir.fbs")
    set(fb_dir "${Halide_BINARY_DIR}/include/flatc")
    set(fb_header "${fb_dir}/halide_ir.fbs.h")
    add_custom_command(
        OUTPUT "${fb_header}"
        COMMAND flatbuffers::flatc --cpp --cpp-std C++17 --no-union-value-namespacing --keep-prefix --filename-suffix ".fbs" -o "${fb_dir}" "${fb_def}"
        DEPENDS "${fb_def}"
        VERBATIM
    )
    add_custom_target(generate_fb_header DEPENDS "${fb_header}")

    target_sources(
        Halide
        PRIVATE
        FILE_SET fb_headers
        TYPE HEADERS
        BASE_DIRS "${fb_dir}"
        FILES "${fb_header}"
    )
    target_compile_definitions(Halide PRIVATE WITH_SERIALIZATION)
endif ()

# Enable serialization testing by intercepting JIT compilation with a serialization roundtrip;
# This is used only for special builds made specifically for testing, and must be disabled by default.
if (WITH_SERIALIZATION_JIT_ROUNDTRIP_TESTING)
    target_compile_definitions(Halide PRIVATE WITH_SERIALIZATION_JIT_ROUNDTRIP_TESTING)
endif ()

# Note that we (deliberately) redeclare these versions here, even though the macros
# with identical versions are expected to be defined in source; this allows us to
# ensure that the versions defined between all build systems are identical.
target_compile_definitions(Halide PUBLIC
                           HALIDE_VERSION_MAJOR=${Halide_VERSION_MAJOR}
                           HALIDE_VERSION_MINOR=${Halide_VERSION_MINOR}
                           HALIDE_VERSION_PATCH=${Halide_VERSION_PATCH})

##
# WasmExecutor backend selection
##

set(Halide_WASM_BACKEND "wabt"
    CACHE STRING "Which backend to use for Halide's WASM testing.")
set_property(CACHE Halide_WASM_BACKEND PROPERTY STRINGS "wabt;V8;OFF")

if (MSVC AND Halide_WASM_BACKEND STREQUAL "wabt")
    message(WARNING "wabt is not yet supported on Windows")
    set(Halide_WASM_BACKEND "OFF")
endif ()

if (Halide_WASM_BACKEND STREQUAL "wabt")
    find_package(wabt 1.0.36 REQUIRED)
    _Halide_pkgdep(wabt)

    if (Halide_BUNDLE_STATIC)
        target_link_libraries(Halide PRIVATE "$<BUILD_LOCAL_INTERFACE:wabt::wabt>")
    elseif (Halide_USE_FETCHCONTENT AND NOT BUILD_SHARED_LIBS)
        target_sources(Halide PRIVATE "$<TARGET_OBJECTS:wabt::wabt>")
        target_link_libraries(Halide PRIVATE "$<BUILD_LOCAL_INTERFACE:$<COMPILE_ONLY:wabt::wabt>>")
    else ()
        target_link_libraries(Halide PRIVATE wabt::wabt)
    endif ()

    target_compile_definitions(Halide PRIVATE WITH_WABT)
elseif (Halide_WASM_BACKEND STREQUAL "V8")
    find_package(V8 REQUIRED)
    _Halide_pkgdep(V8)
    target_compile_definitions(Halide PRIVATE WITH_V8)

    get_property(type TARGET V8::V8 PROPERTY TYPE)
    if (Halide_BUNDLE_STATIC AND type STREQUAL "STATIC_LIBRARY")
        target_link_libraries(Halide PRIVATE "$<BUILD_LOCAL_INTERFACE:V8::V8>")
    else ()
        target_link_libraries(Halide PRIVATE V8::V8)
    endif ()
elseif (Halide_WASM_BACKEND)
    message(FATAL_ERROR "Unknown Halide_WASM_BACKEND `${Halide_WASM_BACKEND}`")
endif ()

##
# Attach symbol export scripts
##

## TODO: implement something similar for Windows/link.exe
# https://github.com/halide/Halide/issues/4651
include(TargetExportScript)
target_export_script(Halide
                     APPLE_LD "${CMAKE_CURRENT_LIST_DIR}/exported_symbols.osx"
                     GNU_LD "${CMAKE_CURRENT_LIST_DIR}/exported_symbols.ldscript")

##
# Set compiler options for libHalide
##

set_halide_compiler_warnings(Halide)

if (CMAKE_GENERATOR MATCHES "Visual Studio")
    # We could expose the /MP flag to all targets, but that might end up saturating the build
    # since multiple MSBuild projects might get built in parallel, each of which compiling their
    # source files in parallel; the Halide library itself is a "knot" point of the build graph,
    # so compiling its files in parallel should not oversubscribe the system
    target_compile_options(Halide PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/MP>)
endif ()

target_compile_definitions(Halide
                           PRIVATE
                           # Disable warnings about standard C functions that have more secure replacements
                           # in the Windows API.
                           $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
                           $<$<CXX_COMPILER_ID:MSVC>:_SCL_SECURE_NO_WARNINGS>
)

##
# RTTI and exceptions settings
##

# RTTI
set_property(TARGET Halide PROPERTY CXX_RTTI "${Halide_ENABLE_RTTI}")
set_property(TARGET Halide APPEND PROPERTY COMPATIBLE_INTERFACE_BOOL CXX_RTTI)

if (Halide_ENABLE_RTTI)
    target_compile_definitions(Halide PUBLIC HALIDE_ENABLE_RTTI)
else ()
    target_compile_options(
        Halide
        PUBLIC
        "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/GR->"
        "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-fno-rtti>"
    )
endif ()

# Exceptions
if (Halide_ENABLE_EXCEPTIONS)
    target_compile_definitions(Halide PUBLIC HALIDE_WITH_EXCEPTIONS)
else ()
    target_compile_options(
        Halide
        PUBLIC
        "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/EHs-c->"
        "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-fno-exceptions>"
    )
    target_compile_definitions(
        Halide PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:_HAS_EXCEPTIONS=0>"
    )
endif ()

##
# Add autoschedulers to the build.
##

if (WITH_AUTOSCHEDULERS)
    add_subdirectory(autoschedulers)
endif ()
