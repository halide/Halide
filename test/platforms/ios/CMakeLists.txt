# This file contains a CMake build for a basic iOS app that runs a series of
# test functions and displays the results.

# This project contains targets that build for both iOS and Mac OS X.

cmake_minimum_required(VERSION 2.8.5)

include("../HalideAppTests.cmake")

# In iOS Halide development, (for ahead-of-time compilation) Halide is built for
# OS X and these tests must be built for iOS. This is accomplished below by
# setting several Xcode attributes below to build one target as a device app
# bundle and the rest as host command line tools.

project(test_ios)

# We want all of our targets to put their binaries in a common path, not based
# on the platform name.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Create a target for the Mac OS X app. This function will setup all of the app
# state and compile and link in the specified test source files. Generators are
# added to the test below.
halide_add_ios_app(TARGET test_ios
  TEST_SOURCES ../tests/test_example_generator.cpp
  TEST_FUNCTIONS test_example_generator
  )

# Note that the Halide target string passed to these generators is ios-arm-64
# only. This particular example does not attempt to build a multi-architecture
# iOS app. Be sure to build the app for an arm64-only scheme in Xcode as opposed
# to a generic "iOS Device" scheme

# Create a generator executable and use it to generate a halide function for the
# host target.
halide_add_ios_generator_to_app(TARGET test_ios
  GENERATOR_TARGET example
  GENERATOR_NAME example
  GENERATED_FUNCTION example
  GENERATOR_SOURCES ../../generator/example_generator.cpp
  GENERATOR_ARGS -e html target=ios-arm-64
  )

# Pass the same generator target name with a different GENERATED_FUNCTION and
# Halide target string. This will cause the generator to be run a second time
# with a different target enabled. The test code referenced above uses both of
# these halide generated functions.
halide_add_ios_generator_to_app(TARGET test_ios
  GENERATOR_TARGET example
  GENERATOR_NAME example
  GENERATED_FUNCTION example_glsl
  GENERATOR_SOURCES ../../generator/example_generator.cpp
  GENERATOR_ARGS -e html target=ios-arm-64-opengl
  )
