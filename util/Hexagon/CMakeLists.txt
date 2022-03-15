cmake_minimum_required(VERSION 3.16)
project(hexagon-instrument
    VERSION 0.1
    LANGUAGES CXX)

include(FetchContent)

add_executable(logcat2json logcat2json.cpp)
set_target_properties(logcat2json PROPERTIES
    CXX_STANDARD 17
  )
FetchContent_Declare(
  json
  URL https://github.com/nlohmann/json/releases/download/v3.10.5/json.tar.xz
)
FetchContent_MakeAvailable(json)

FetchContent_Declare(
  cxxopts
  URL https://github.com/jarro2783/cxxopts/archive/refs/tags/v3.0.0.tar.gz
)
FetchContent_MakeAvailable(cxxopts)

target_link_libraries(logcat2json PRIVATE 
  nlohmann_json::nlohmann_json
  cxxopts
  )
