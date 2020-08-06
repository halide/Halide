##
# Find LLVM and check the version.
##

# Fallback configurations for weirdly built LLVMs
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL MinSizeRel Release RelWithDebInfo "")
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO RelWithDebInfo Release MinSizeRel "")
set(CMAKE_MAP_IMPORTED_CONFIG_RELEASE Release MinSizeRel RelWithDebInfo "")

find_package(LLVM ${HALIDE_REQUIRE_LLVM_VERSION} REQUIRED)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

if (LLVM_PACKAGE_VERSION VERSION_LESS 9.0)
    message(FATAL_ERROR "LLVM version must be 9.0 or newer")
endif ()

if (LLVM_PACKAGE_VERSION VERSION_GREATER 12.0)
    message(WARNING "Halide is not tested on LLVM versions beyond 12.0")
endif ()

list(APPEND LLVM_DEFINITIONS $<BUILD_INTERFACE:LLVM_VERSION=${LLVM_VERSION_MAJOR}${LLVM_VERSION_MINOR}>)

##
# Promote LLVM assembler target
##

set_target_properties(llvm-as PROPERTIES IMPORTED_GLOBAL TRUE)

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

##
# Create options that are initialized based on LLVM's config
##

set(LLVM_COMPONENTS mcjit bitwriter linker passes)
set(known_components AArch64 AMDGPU ARM Hexagon Mips NVPTX PowerPC RISCV WebAssembly X86)

# We don't support LLVM10 or below for wasm codegen.
if (LLVM_PACKAGE_VERSION VERSION_LESS 11.0)
    list(REMOVE_ITEM known_components WebAssembly)
endif ()

foreach (comp IN LISTS known_components)
    string(TOUPPER "TARGET_${comp}" OPTION)
    string(TOUPPER "WITH_${comp}" DEFINE)

    check_llvm_target(${comp} present)
    option(${OPTION} "Include ${comp} target" ${present})
    if (${OPTION})
        list(APPEND LLVM_DEFINITIONS $<BUILD_INTERFACE:${DEFINE}>)
        list(APPEND LLVM_COMPONENTS ${comp})
    endif ()
endforeach ()

if (TARGET_WEBASSEMBLY)
    ##
    # lld libraries -- inexplicably, LLVM doesn't put these in the CMake imports,
    # but we need them, so we fake it here by abusing the LLVM${comp} convention.
    ##

    find_library(LLD_WASM lldWasm HINTS ${LLVM_LIBRARY_DIRS})
    add_library(LLVMLLDWasm STATIC IMPORTED)
    set_target_properties(LLVMLLDWasm PROPERTIES IMPORTED_LOCATION ${LLD_WASM})

    find_library(LLD_COMMON lldCommon HINTS ${LLVM_LIBRARY_DIRS})
    add_library(LLVMLLDCommon STATIC IMPORTED)
    set_target_properties(LLVMLLDCommon PROPERTIES IMPORTED_LOCATION ${LLD_COMMON})

    target_link_libraries(LLVMLLDWasm INTERFACE LLVMLLDCommon)
    target_link_libraries(LLVMLLDCommon INTERFACE LLVMLTO LLVMOption)

    if (NOT LLD_WASM OR NOT LLD_COMMON)
        message(FATAL_ERROR
                "Could not find both lldWasm and lldCommon in ${LLVM_LIBRARY_DIRS}."
                "Configure with -DTARGET_WEBASSEMBLY=NO to disable WebAssembly."
                "Do you need to install liblld-${LLVM_VERSION_MAJOR}?")
    endif ()

    list(APPEND LLVM_COMPONENTS LLDWasm LLDCommon)
endif ()

##
# Create Halide::LLVM library alias pointing to the correct LLVM
# among shared, static, and bundled.
##

option(Halide_BUNDLE_LLVM "When built as a static library, include LLVM's objects." OFF)
option(Halide_SHARED_LLVM "Link against shared LLVM (disables components)." OFF)

llvm_map_components_to_libnames(LLVM_LIBNAMES ${LLVM_COMPONENTS})

if (Halide_BUNDLE_LLVM AND NOT BUILD_SHARED_LIBS)
    include(BundleStatic)
    bundle_static(LIBRARIES ${LLVM_LIBNAMES} TARGET Halide_LLVM)
else ()
    add_library(Halide_LLVM INTERFACE)
    if (Halide_SHARED_LLVM)
        set(LLVM_LIBNAMES LLVM)
        target_link_libraries(Halide_LLVM INTERFACE ${CMAKE_DL_LIBS})
    endif ()
    target_link_libraries(Halide_LLVM INTERFACE ${LLVM_LIBNAMES})
    set_target_properties(${LLVM_LIBNAMES} PROPERTIES IMPORTED_GLOBAL TRUE)
endif ()

# Attach the include dirs and (patched) definitions to the target, where they belong.
set_target_properties(Halide_LLVM PROPERTIES EXPORT_NAME LLVM)
target_include_directories(Halide_LLVM INTERFACE $<BUILD_INTERFACE:${LLVM_INCLUDE_DIRS}>)
target_compile_definitions(Halide_LLVM INTERFACE ${LLVM_DEFINITIONS})

add_library(Halide::LLVM ALIAS Halide_LLVM)

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
    target_compile_definitions(Halide_LanguageOptions INTERFACE HALIDE_WITH_EXCEPTIONS)
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
