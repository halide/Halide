##
# Lists of source files. Keep ALL lists sorted in alphabetical order.
##

# The externally-visible header files that go into making Halide.h.
# Don't include anything here that includes llvm headers.
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
    AutoScheduleUtils.h
    BoundaryConditions.h
    Bounds.h
    BoundsInference.h
    BoundSmallAllocations.h
    Buffer.h
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
    CodeGen_OpenGLCompute_Dev.h
    CodeGen_Posix.h
    CodeGen_PTX_Dev.h
    CodeGen_PyTorch.h
    CodeGen_Targets.h
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
    LowerParallelTasks.h
    LowerWarpShuffles.h
    MainPage.h
    Memoization.h
    Module.h
    ModulusRemainder.h
    Monotonic.h
    ObjectInstanceRegistry.h
    OffloadGPULoops.h
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
    VectorizeLoops.h
    WasmExecutor.h
    WrapCalls.h
    )

set(SOURCE_FILES
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
    AutoScheduleUtils.cpp
    BoundaryConditions.cpp
    Bounds.cpp
    BoundsInference.cpp
    BoundSmallAllocations.cpp
    Buffer.cpp
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
    CodeGen_MIPS.cpp
    CodeGen_OpenCL_Dev.cpp
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
    Introspection.cpp
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
    RebaseLoopsToZero.cpp
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
    VectorizeLoops.cpp
    WasmExecutor.cpp
    WrapCalls.cpp
    )

##
# Build and import the runtime.
##

add_subdirectory(runtime)

##
# Build the Halide mono-header.
##

set(HALIDE_H "${Halide_BINARY_DIR}/include/Halide.h")
set(LICENSE_PATH "${Halide_SOURCE_DIR}/LICENSE.txt")
add_custom_command(OUTPUT "${Halide_BINARY_DIR}/include/Halide.h"
                   COMMAND ${CMAKE_COMMAND} -E make_directory "$<SHELL_PATH:${Halide_BINARY_DIR}/include>"
                   COMMAND build_halide_h "$<SHELL_PATH:${LICENSE_PATH}>" ${HEADER_FILES} > "$<SHELL_PATH:${HALIDE_H}>"
                   DEPENDS build_halide_h "${LICENSE_PATH}" ${HEADER_FILES}
                   WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
                   VERBATIM)
add_custom_target(HalideIncludes ALL DEPENDS "${Halide_BINARY_DIR}/include/Halide.h")

##
# Define the Halide library target.
##

add_library(Halide
            ${SOURCE_FILES}
            ${HEADER_FILES}
            # Including these as sources works around the need to "install" Halide_initmod
            $<TARGET_OBJECTS:Halide_initmod>)
add_library(Halide::Halide ALIAS Halide)

target_link_libraries(Halide PUBLIC Threads::Threads)
target_link_libraries(Halide PRIVATE Halide::LLVM)
target_link_libraries(Halide PUBLIC Halide::LanguageOptions)
target_compile_definitions(Halide PRIVATE $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,STATIC_LIBRARY>:Halide_STATIC_DEFINE>)
target_compile_features(Halide PUBLIC cxx_std_17)

include(TargetExportScript)
## TODO: implement something similar for Windows/link.exe
# https://github.com/halide/Halide/issues/4651
target_export_script(Halide
                     APPLE_LD "${CMAKE_CURRENT_LIST_DIR}/exported_symbols.osx"
                     GNU_LD "${CMAKE_CURRENT_LIST_DIR}/exported_symbols.ldscript")

set(Halide_SOVERSION_OVERRIDE "${Halide_VERSION_MAJOR}"
    CACHE STRING "SOVERSION to set for custom Halide packaging")
mark_as_advanced(Halide_SOVERSION_OVERRIDE)

set_target_properties(Halide PROPERTIES
                      POSITION_INDEPENDENT_CODE ON
                      VERSION ${Halide_VERSION}
                      SOVERSION ${Halide_SOVERSION_OVERRIDE})

target_include_directories(Halide INTERFACE "$<BUILD_INTERFACE:${Halide_BINARY_DIR}/include>")
add_dependencies(Halide HalideIncludes)

option(Halide_WITH_INTROSPECTION "Enable use of debugging symbols for default Func, Var, etc. names" ON)
if (Halide_WITH_INTROSPECTION)
    target_compile_definitions(Halide PRIVATE WITH_INTROSPECTION)
endif ()

if (TARGET Halide_wabt)
    target_link_libraries(Halide PRIVATE Halide_wabt)
    target_compile_definitions(Halide PRIVATE WITH_WABT)
endif ()

if (TARGET Halide_V8)
    target_link_libraries(Halide PRIVATE Halide_V8)
    target_compile_definitions(Halide PRIVATE WITH_V8)
endif ()

##
# Set compiler options for libHalide
##

target_compile_options(
        Halide
        PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall>

        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wcast-qual>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wignored-qualifiers>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Woverloaded-virtual>

        $<$<CXX_COMPILER_ID:GNU>:-Wsuggest-override>

        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Winconsistent-missing-destructor-override>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Winconsistent-missing-override>

        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-deprecated-declarations>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-double-promotion>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-float-conversion>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-float-equal>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-missing-field-initializers>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-old-style-cast>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-shadow>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-sign-conversion>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-switch-enum>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-undef>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-unused-function>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-unused-macros>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-unused-parameter>

        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-c++98-compat-pedantic>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-c++98-compat>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-cast-align>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-comma>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-covered-switch-default>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-documentation-unknown-command>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-documentation>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-exit-time-destructors>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-global-constructors>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-implicit-float-conversion>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-implicit-int-conversion>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-implicit-int-float-conversion>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-missing-prototypes>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-nonportable-system-include-path>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-reserved-id-macro>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-shadow-field-in-constructor>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-shadow-field>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-shorten-64-to-32>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-undefined-func-template>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-unused-member-function>
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-unused-template>

        # This warning was removed in Clang 13
        $<$<AND:$<CXX_COMPILER_ID:Clang,AppleClang>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,13.0>>:-Wno-return-std-move-in-c++11>

        $<$<CXX_COMPILER_ID:MSVC>:/W3>
        $<$<CXX_COMPILER_ID:MSVC>:/wd4018>  # 4018: disable "signed/unsigned mismatch"
        $<$<CXX_COMPILER_ID:MSVC>:/wd4141>  # 4141: 'inline' used more than once
        $<$<CXX_COMPILER_ID:MSVC>:/wd4146>  # 4146: unary minus applied to unsigned type
        $<$<CXX_COMPILER_ID:MSVC>:/wd4244>  # 4244: conversion, possible loss of data
        $<$<CXX_COMPILER_ID:MSVC>:/wd4267>  # 4267: conversion from 'size_t' to 'int', possible loss of data
        $<$<CXX_COMPILER_ID:MSVC>:/wd4291>  # 4291: No matching operator delete found
        $<$<CXX_COMPILER_ID:MSVC>:/wd4503>  # 4503: disable "decorated name length exceeded, name was truncated"
        $<$<CXX_COMPILER_ID:MSVC>:/wd4800>  # 4800: forcing value to bool 'true' or 'false' (performance warning)
        $<$<CXX_COMPILER_ID:MSVC>:/wd4996>  # 4996: compiler encountered deprecated declaration
)

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
# Set up additional backend options for Halide
##

option(TARGET_OPENCL "Include OpenCL-C target" ON)
if (TARGET_OPENCL)
    target_compile_definitions(Halide PRIVATE WITH_OPENCL)
endif ()

option(TARGET_METAL "Include Metal target" ON)
if (TARGET_METAL)
    target_compile_definitions(Halide PRIVATE WITH_METAL)
endif ()

option(TARGET_D3D12COMPUTE "Include Direct3D 12 Compute target" ON)
if (TARGET_D3D12COMPUTE)
    target_compile_definitions(Halide PRIVATE WITH_D3D12)
endif ()

option(TARGET_OPENGLCOMPUTE "Include OpenGLCompute target" ON)
if (TARGET_OPENGLCOMPUTE)
    target_compile_definitions(Halide PRIVATE WITH_OPENGLCOMPUTE)
endif ()

##
# Add autoschedulers to the build.
##

if (BUILD_SHARED_LIBS)
    message(STATUS "Building autoschedulers enabled")
    add_subdirectory(autoschedulers)
else ()
    message(STATUS "Building autoschedulers disabled (static Halide)")
endif ()
