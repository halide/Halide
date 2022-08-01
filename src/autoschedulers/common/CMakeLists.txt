add_library(Halide_Plugin INTERFACE)
add_library(Halide::Plugin ALIAS Halide_Plugin)
target_include_directories(Halide_Plugin INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

target_link_libraries(Halide_Plugin INTERFACE Halide::Halide)
# It's important to use dynamic lookups for undefined symbols here: all of libHalide
# is expected to be present (in the loading binary), so we explicitly make the symbols
# undefined rather than dependent on libHalide.so.
target_link_options(Halide_Plugin INTERFACE
                    $<$<PLATFORM_ID:Linux>:-rdynamic>
                    $<$<PLATFORM_ID:Darwin>:-undefined dynamic_lookup>)

add_library(ASLog STATIC ASLog.cpp)
target_include_directories(ASLog PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

# Sigh, header-only libraries shouldn't be special
add_library(ParamParser INTERFACE)
target_include_directories(ParamParser INTERFACE
                           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
