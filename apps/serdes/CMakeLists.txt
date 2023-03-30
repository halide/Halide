cmake_minimum_required(VERSION 3.26)
project(flatbuffer_ir)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

# Find flatbuffer
find_package(Flatbuffers REQUIRED)

# Main executable
add_executable(roundtrip_test roundtrip_test.cpp)
target_link_libraries(roundtrip_test
                      PRIVATE
                      Halide)