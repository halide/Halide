##
# Find LLVM and check the version.
##

find_package(LLVM ${HALIDE_REQUIRE_LLVM_VERSION} REQUIRED)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

if ("${LLVM_PACKAGE_VERSION}" VERSION_LESS 9.0)
    message(FATAL_ERROR "LLVM version must be 9.0 or newer")
endif ()

if ("${LLVM_PACKAGE_VERSION}" VERSION_GREATER 11.0)
    message(WARNING "Halide is not tested on LLVM versions beyond 11.0")
endif ()

##
# Promote executable targets from LLVM
##

set_target_properties(llvm-as PROPERTIES IMPORTED_GLOBAL TRUE)
set_target_properties(llvm-nm PROPERTIES IMPORTED_GLOBAL TRUE)
set_target_properties(llvm-config PROPERTIES IMPORTED_GLOBAL TRUE)

##
# Create target for holding LLVM properties
##

add_library(Halide_LLVM INTERFACE)
add_library(Halide::LLVM ALIAS Halide_LLVM)

set_target_properties(Halide_LLVM PROPERTIES EXPORT_NAME LLVM)

target_include_directories(Halide_LLVM INTERFACE $<BUILD_INTERFACE:${LLVM_INCLUDE_DIRS}>)
target_compile_definitions(Halide_LLVM INTERFACE
                           "LLVM_VERSION=${LLVM_VERSION_MAJOR}${LLVM_VERSION_MINOR}"
                           ${LLVM_DEFINITIONS})

##
# Detect supported llvm targets
##

function(check_llvm_target TARGET HAS_TARGET)
    if ("${TARGET}" IN_LIST LLVM_TARGETS_TO_BUILD)
        set(${HAS_TARGET} ON PARENT_SCOPE)
    else ()
        set(${HAS_TARGET} OFF PARENT_SCOPE)
    endif ()
endfunction()

check_llvm_target(AArch64 WITH_AARCH64)
check_llvm_target(AMDGPU WITH_AMDGPU)
check_llvm_target(ARM WITH_ARM)
check_llvm_target(Hexagon WITH_HEXAGON)
check_llvm_target(Mips WITH_MIPS)
check_llvm_target(NVPTX WITH_NVPTX)
check_llvm_target(PowerPC WITH_POWERPC)
check_llvm_target(RISCV WITH_RISCV)
check_llvm_target(WebAssembly WITH_WEBASSEMBLY)
check_llvm_target(X86 WITH_X86)

##
# Create options that are initialized based on LLVM's config
##

set(LLVM_COMPONENTS mcjit bitwriter linker passes)

option(TARGET_AARCH64 "Include AARCH64 (arm64) target" ${WITH_AARCH64})
if (TARGET_AARCH64)
    target_compile_definitions(Halide_LLVM INTERFACE WITH_AARCH64)
    list(APPEND LLVM_COMPONENTS AArch64)
endif ()

option(TARGET_AMDGPU "Include AMDGPU target" ${WITH_AMDGPU})
if (TARGET_AMDGPU)
    target_compile_definitions(Halide_LLVM INTERFACE WITH_AMDGPU)
    list(APPEND LLVM_COMPONENTS AMDGPU)
endif ()

option(TARGET_ARM "Include ARM target" ${WITH_ARM})
if (TARGET_ARM)
    target_compile_definitions(Halide_LLVM INTERFACE WITH_ARM)
    list(APPEND LLVM_COMPONENTS ARM)
endif ()

option(TARGET_HEXAGON "Include Hexagon target" ${WITH_HEXAGON})
if (TARGET_HEXAGON)
    target_compile_definitions(Halide_LLVM INTERFACE WITH_HEXAGON)
    list(APPEND LLVM_COMPONENTS Hexagon)
endif ()

option(TARGET_MIPS "Include MIPS target" ${WITH_MIPS})
if (TARGET_MIPS)
    target_compile_definitions(Halide_LLVM INTERFACE WITH_MIPS)
    list(APPEND LLVM_COMPONENTS Mips)
endif ()

option(TARGET_PTX "Include PTX target" ${WITH_NVPTX})
if (TARGET_PTX)
    target_compile_definitions(Halide_LLVM INTERFACE WITH_PTX)
    list(APPEND LLVM_COMPONENTS NVPTX)
endif ()

option(TARGET_POWERPC "Include POWERPC target" ${WITH_POWERPC})
if (TARGET_POWERPC)
    target_compile_definitions(Halide_LLVM INTERFACE WITH_POWERPC)
    list(APPEND LLVM_COMPONENTS PowerPC)
endif ()

option(TARGET_RISCV "Include RISCV target" ${WITH_RISCV})
if (TARGET_RISCV)
    target_compile_definitions(Halide_LLVM INTERFACE WITH_RISCV)
    list(APPEND LLVM_COMPONENTS RISCV)
endif ()

option(TARGET_WEBASSEMBLY "Include WebAssembly target" ${WITH_WEBASSEMBLY})
if (TARGET_WEBASSEMBLY)
    target_compile_definitions(Halide_LLVM INTERFACE WITH_WEBASSEMBLY)
    list(APPEND LLVM_COMPONENTS WebAssembly)
endif ()

option(TARGET_X86 "Include x86 target" ${WITH_X86})
if (TARGET_X86)
    target_compile_definitions(Halide_LLVM INTERFACE WITH_X86)
    list(APPEND LLVM_COMPONENTS X86)
endif ()

##
# Finish setting up llvm library
##

if (LLVM_USE_SHARED_LLVM_LIBRARY)
    set(LLVM_USE_SHARED "USE_SHARED")
endif ()

llvm_map_components_to_libnames(LLVM_LIBNAMES ${LLVM_COMPONENTS})
target_link_libraries(Halide_LLVM INTERFACE ${LLVM_LIBNAMES})

##
# Language options interface library
##

add_library(Halide_LanguageOptions INTERFACE)
add_library(Halide::LanguageOptions ALIAS Halide_LanguageOptions)

set_target_properties(Halide_LanguageOptions PROPERTIES EXPORT_NAME LanguageOptions)

option(HALIDE_ENABLE_RTTI "Enable RTTI" ${LLVM_ENABLE_RTTI})
if (HALIDE_ENABLE_RTTI AND NOT LLVM_ENABLE_RTTI)
    message(FATAL_ERROR "Can't enable RTTI. LLVM was compiled without it")
endif ()

if (HALIDE_ENABLE_RTTI)
    message(STATUS "Compiling Halide WITH RTTI.")
    target_compile_definitions(Halide_LanguageOptions INTERFACE HALIDE_ENABLE_RTTI)
else ()
    message(STATUS "Compiling Halide WITHOUT RTTI.")
    target_compile_options(Halide_LanguageOptions INTERFACE
                           $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/GR->
                           $<$<COMPILE_LANG_AND_ID:CXX,GCC,Clang,AppleClang>:-fno-rtti>)
endif ()

option(HALIDE_ENABLE_EXCEPTIONS "Enable exceptions" YES)
if (HALIDE_ENABLE_EXCEPTIONS)
    message(STATUS "Compiling Halide WITH exceptions.")
    target_compile_definitions(Halide_LanguageOptions INTERFACE WITH_EXCEPTIONS)
else ()
    message(STATUS "Compiling Halide WITHOUT exceptions.")
    target_compile_options(Halide_LanguageOptions INTERFACE
                           $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/EHs-c->
                           $<$<COMPILE_LANG_AND_ID:CXX,GCC,Clang,AppleClang>:-fno-exceptions>)
    target_compile_definitions(Halide_LanguageOptions INTERFACE
                               $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:_HAS_EXCEPTIONS=0>)
endif ()

if (LLVM_LIBCXX GREATER -1)
    message(STATUS "LLVM linked to libc++. Adding to interface requirements.")
    target_compile_options(Halide_LanguageOptions INTERFACE
                           $<$<STREQUAL:$<TARGET_PROPERTY:LINKER_LANGUAGE>,CXX>:-stdlib=libc++>)
    target_link_options(Halide_LanguageOptions INTERFACE
                        $<$<STREQUAL:$<TARGET_PROPERTY:LINKER_LANGUAGE>,CXX>:-stdlib=libc++>)
endif ()

install(TARGETS Halide_LanguageOptions EXPORT Halide_Targets)

##
# Install LLVM static object dependencies (not the files!) if needed.
##

# When building the Halide library as a static library, we have to expose
# our LLVM dependencies so that CMake has the information it needs for the
# installed package. When building it as a shared library, LLVM becomes a
# fully private implementation detail as far as CMake is concerned, so it
# then doesn't need to expose any LLVM-related dependencies through targets.
if (NOT BUILD_SHARED_LIBS)
    install(TARGETS Halide_LLVM
            EXPORT Halide_Targets)
endif ()
