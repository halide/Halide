find_package(nlohmann_json REQUIRED)
find_package(cxxopts REQUIRED)

add_executable(logcat2json logcat2json.cpp)
target_compile_features(logcat2json PRIVATE cxx_std_17)
target_link_libraries(logcat2json PRIVATE 
  nlohmann_json::nlohmann_json
  cxxopts::cxxopts
)

add_library(InstrumentedGenerator INTERFACE)
add_library(Halide::InstrumentedGenerator ALIAS InstrumentedGenerator)
target_link_libraries(InstrumentedGenerator INTERFACE Halide::Halide)
target_include_directories(InstrumentedGenerator INTERFACE 
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")

set(HEXAGON_SDK_ROOT $ENV{HEXAGON_SDK_ROOT})
if (EXISTS ${HEXAGON_SDK_ROOT})
    message(STATUS "Building Hexagon profiling library enabled")
    add_custom_target(hexagon-profiling
        COMMAND cmake -S ${CMAKE_CURRENT_SOURCE_DIR}/dsp
                -B ${CMAKE_CURRENT_BINARY_DIR}/dsp
                -DCMAKE_TOOLCHAIN_FILE=${HEXAGON_SDK_ROOT}/build/cmake/hexagon_toolchain.cmake
        COMMAND cmake --build ${CMAKE_CURRENT_BINARY_DIR}/dsp
        BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/dsp/libhexagon-profiling.so
        COMMENT "Building Hexagon profiling library"
    )
    add_dependencies(InstrumentedGenerator PRIVATE hexagon-profiling)
else()
    message(STATUS "Building Hexagon profiling library disabled")
endif()
