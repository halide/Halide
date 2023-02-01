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

add_library(Halide_ImageIO INTERFACE)
add_library(Halide::ImageIO ALIAS Halide_ImageIO)
set_target_properties(Halide_ImageIO PROPERTIES EXPORT_NAME ImageIO)
target_link_libraries(Halide_ImageIO
                      INTERFACE
                      $<TARGET_NAME_IF_EXISTS:PNG::PNG>
                      $<TARGET_NAME_IF_EXISTS:JPEG::JPEG>)
target_compile_definitions(Halide_ImageIO
                           INTERFACE
                           $<$<NOT:$<TARGET_EXISTS:PNG::PNG>>:HALIDE_NO_PNG>
                           $<$<NOT:$<TARGET_EXISTS:JPEG::JPEG>>:HALIDE_NO_JPEG>)
target_include_directories(Halide_ImageIO INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

##
# Utility targets meant for users
##

add_library(Halide_RunGenMain INTERFACE)
add_library(Halide::RunGenMain ALIAS Halide_RunGenMain)
target_sources(Halide_RunGenMain INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/RunGenMain.cpp>)
target_link_libraries(Halide_RunGenMain INTERFACE Halide::Runtime Halide::ImageIO Halide::Tools)
set_target_properties(Halide_RunGenMain PROPERTIES EXPORT_NAME RunGenMain)

add_library(Halide_Generator INTERFACE)
add_library(Halide::Generator ALIAS Halide_Generator)
target_sources(Halide_Generator INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/GenGen.cpp>)
target_link_libraries(Halide_Generator INTERFACE Halide::Halide ${CMAKE_DL_LIBS})
set_target_properties(Halide_Generator PROPERTIES EXPORT_NAME Generator)

add_library(Halide_Tools INTERFACE)
add_library(Halide::Tools ALIAS Halide_Tools)
target_include_directories(Halide_Tools INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
set_target_properties(Halide_Tools PROPERTIES EXPORT_NAME Tools)

add_library(Halide_ThreadPool INTERFACE)
add_library(Halide::ThreadPool ALIAS Halide_ThreadPool)
target_link_libraries(Halide_ThreadPool INTERFACE Threads::Threads)
target_include_directories(Halide_Tools INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
set_target_properties(Halide_ThreadPool PROPERTIES EXPORT_NAME ThreadPool)
