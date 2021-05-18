cmake_minimum_required(VERSION 3.16)
project(xc-generators)

##
## Dependencies
##

find_package(Halide REQUIRED)

##
## Create generator target
##

add_executable(add_gen add.cpp)
add_executable(xc::add_gen ALIAS add_gen)
target_link_libraries(add_gen PRIVATE Halide::Generator)

##
## Export (staging)
##

export(TARGETS add_gen
       NAMESPACE xc::
       FILE "${CMAKE_CURRENT_BINARY_DIR}/lib/cmake/xc-generators/xc-generators-config.cmake")
