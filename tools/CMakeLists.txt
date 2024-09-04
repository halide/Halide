##
# Build time tools
##

add_executable(build_halide_h build_halide_h.cpp)
target_compile_options(build_halide_h PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/wd4996>)

add_executable(binary2cpp binary2cpp.cpp)
target_compile_options(binary2cpp PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/wd4996>)

add_executable(regexp_replace regexp_replace.cpp)
target_compile_options(regexp_replace PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/wd4996>)

##
# Interface target for enabling PNG/JPEG support in Halide
##

# TODO: if/when CMake fixes https://gitlab.kitware.com/cmake/cmake/-/issues/25033
# then move find_package(PNG/JPEG) here.

add_library(Halide_ImageIO INTERFACE)
add_library(Halide::ImageIO ALIAS Halide_ImageIO)
set_target_properties(Halide_ImageIO PROPERTIES EXPORT_NAME ImageIO)

target_link_libraries(Halide_ImageIO
                      INTERFACE
                      Halide::Runtime
                      $<TARGET_NAME_IF_EXISTS:PNG::PNG>
                      $<TARGET_NAME_IF_EXISTS:JPEG::JPEG>)
target_compile_definitions(Halide_ImageIO
                           INTERFACE
                           $<$<NOT:$<TARGET_EXISTS:PNG::PNG>>:HALIDE_NO_PNG>
                           $<$<NOT:$<TARGET_EXISTS:JPEG::JPEG>>:HALIDE_NO_JPEG>)
target_sources(Halide_ImageIO INTERFACE FILE_SET HEADERS FILES halide_image_io.h)

##
# RunGenMain
##

add_library(Halide_RunGenMain INTERFACE)
add_library(Halide::RunGenMain ALIAS Halide_RunGenMain)
set_target_properties(Halide_RunGenMain PROPERTIES EXPORT_NAME RunGenMain)

target_sources(Halide_RunGenMain INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/RunGenMain.cpp>)
target_sources(Halide_RunGenMain INTERFACE FILE_SET HEADERS FILES RunGen.h)
target_link_libraries(Halide_RunGenMain INTERFACE Halide::Runtime Halide::ImageIO Halide::Tools)

##
# Generator meta-target
##

add_library(Halide_GenGen STATIC GenGen.cpp)
add_library(Halide::GenGen ALIAS Halide_GenGen)
set_target_properties(Halide_GenGen PROPERTIES EXPORT_NAME GenGen)

target_link_libraries(Halide_GenGen PUBLIC Halide::Halide ${CMAKE_DL_LIBS})

add_library(Halide_Generator INTERFACE)
add_library(Halide::Generator ALIAS Halide_Generator)
set_target_properties(Halide_Generator PROPERTIES EXPORT_NAME Generator)

target_link_libraries(
    Halide_Generator INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,Halide::GenGen>"
)

## gengen tool for building runtimes in add_halide_runtime
add_executable(Halide_GenRT GenGen.cpp)
add_executable(Halide::GenRT ALIAS Halide_GenRT)
set_target_properties(Halide_GenRT PROPERTIES EXPORT_NAME GenRT OUTPUT_NAME gengen)

target_link_libraries(Halide_GenRT PRIVATE Halide::Halide ${CMAKE_DL_LIBS})

_Halide_place_dll(Halide_GenRT)

##
# Dependency-free header-only libs
##

add_library(Halide_Tools INTERFACE)
add_library(Halide::Tools ALIAS Halide_Tools)
set_target_properties(Halide_Tools PROPERTIES EXPORT_NAME Tools)

target_sources(
    Halide_Tools
    INTERFACE
    FILE_SET HEADERS
    FILES
    halide_benchmark.h
    halide_image.h
    halide_image_info.h
    halide_malloc_trace.h
    halide_trace_config.h
)

##
# Simple thread pool
##

add_library(Halide_ThreadPool INTERFACE)
add_library(Halide::ThreadPool ALIAS Halide_ThreadPool)
set_target_properties(Halide_ThreadPool PROPERTIES EXPORT_NAME ThreadPool)

target_link_libraries(Halide_ThreadPool INTERFACE Threads::Threads)
target_sources(Halide_ThreadPool INTERFACE FILE_SET HEADERS FILES halide_thread_pool.h)

