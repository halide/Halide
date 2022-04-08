find_package(nlohmann_json)
find_package(cxxopts)

if (nlohmann_json_FOUND)
if (cxxopts_FOUND)
  add_executable(logcat2json logcat2json.cpp)

  target_compile_features(logcat2json PRIVATE cxx_std_17)

  target_link_libraries(logcat2json PRIVATE 
    nlohmann_json::nlohmann_json
    cxxopts::cxxopts
  )
endif()
endif()

add_library(instrumented_generator INTERFACE)
target_link_libraries(instrumented_generator INTERFACE Halide::Halide)
target_include_directories(instrumented_generator INTERFACE include)
