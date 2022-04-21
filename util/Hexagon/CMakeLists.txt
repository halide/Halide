find_package(nlohmann_json REQUIRED)
find_package(cxxopts REQUIRED)

if (nlohmann_json_FOUND AND cxxopts_FOUND)
  add_executable(logcat2json logcat2json.cpp)

  target_compile_features(logcat2json PRIVATE cxx_std_17)

  target_link_libraries(logcat2json PRIVATE 
    nlohmann_json::nlohmann_json
    cxxopts::cxxopts
  )
endif()

add_library(InstrumentedGenerator INTERFACE)
add_library(Halide::InstrumentedGenerator ALIAS InstrumentedGenerator)
target_link_libraries(InstrumentedGenerator INTERFACE Halide::Halide)
target_include_directories(InstrumentedGenerator INTERFACE 
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
