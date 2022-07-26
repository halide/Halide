add_library(Halide_Plugin INTERFACE)
add_library(Halide::Plugin ALIAS Halide_Plugin)
target_include_directories(Halide_Plugin INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(Halide_Plugin INTERFACE Halide::Halide)

add_library(ASLog STATIC ASLog.cpp)
target_include_directories(ASLog PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

# Sigh, header-only libraries shouldn't be special
add_library(ParamParser INTERFACE)
target_include_directories(ParamParser INTERFACE
                           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
