##
# Find LLVM and check the version.
##

find_package(LLVM ${HALIDE_REQUIRE_LLVM_VERSION} REQUIRED)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

if ("${LLVM_PACKAGE_VERSION}" VERSION_LESS 9.0)
    message(FATAL_ERROR "LLVM version must be 9.0 or newer")
endif ()

if ("${LLVM_PACKAGE_VERSION}" VERSION_GREATER 12.0)
    message(WARNING "Halide is not tested on LLVM versions beyond 12.0")
endif ()

set(LLVM_PACKAGE_VERSION "${LLVM_PACKAGE_VERSION}" CACHE INTERNAL "The version of the LLVM in use")

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
        target_compile_definitions(Halide_LLVM INTERFACE ${DEFINE})
        list(APPEND LLVM_COMPONENTS ${comp})
    endif ()
endforeach ()

if (TARGET_WEBASSEMBLY)
    ##
    # lld libraries -- inexplicably, LLVM doesn't put these in the CMake imports,
    # but we need them, so let's fake it here:
    ##
    find_library(LLD_WASM lldWasm HINTS ${LLVM_LIBRARY_DIRS})
    add_library(LLDWasm STATIC IMPORTED)
    set_target_properties(LLDWasm PROPERTIES IMPORTED_LOCATION ${LLD_WASM})

    find_library(LLD_COMMON lldCommon HINTS ${LLVM_LIBRARY_DIRS})
    add_library(LLDCommon STATIC IMPORTED)
    set_target_properties(LLDCommon PROPERTIES IMPORTED_LOCATION ${LLD_COMMON})

    if (NOT LLD_WASM OR NOT LLD_COMMON)
        message(WARNING
                "Could not find both lldWasm and lldCommon in ${LLVM_LIBRARY_DIRS}. "
                "Configure with -DTARGET_WEBASSEMBLY=NO to suppress. "
                "Do you need to install liblld-${LLVM_VERSION_MAJOR}?")
    else ()
        target_link_libraries(Halide_LLVM INTERFACE LLDWasm LLDCommon)

        # lto and options aren't needed to *generate* WebAssembly, but they
        # are needed to link in LLDWasm.
        list(APPEND LLVM_COMPONENTS lto option)
    endif ()
endif ()

##
# Finish setting up llvm library
#
# Ideally, we would use llvm_config (instead of hardcoding LLVM lib name below):
#    if (LLVM_USE_SHARED_LLVM_LIBRARY)
#        set(LLVM_USE_SHARED "USE_SHARED")
#    endif()
#    llvm_config(Halide_LLVM ${LLVM_USE_SHARED} ${LLVM_COMPONENTS})
# However, llvm_config (LLVM 10.0) does not accept INTERFACE_LIBRARY targets,
# so the below code does what llvm_config() does, with the slight difference
# that we link exclusively to the shared library without fallback to static
# libraries for symbols not resolved by the shared library.
##

if (LLVM_USE_SHARED_LLVM_LIBRARY)
    set(LLVM_LIBNAMES LLVM)
else ()
    llvm_map_components_to_libnames(LLVM_LIBNAMES ${LLVM_COMPONENTS})
endif ()
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
    install(TARGETS Halide_LLVM EXPORT Halide_Targets)
endif ()
