cmake_minimum_required(VERSION 3.2.2)

include("../HalideAppTests.cmake")

# This file contains a CMake build for a basic OSX app that runs a series of
# Generator tests and displays the results. Generators must be
# added to the app below and then configured in an html file loaded at
# runtime.
project(test_osx)

# We want all of our targets to put their binaries in a common path, not based
# on the platform name.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_OSX_ARCHITECTURES "$(ARCHS_STANDARD)")

# Create a target for the Mac OS X app. This function will setup all of the app
# state and compile and link in the specified test source files. Generators are
# added to the test below.
halide_add_osx_app(TARGET test_osx
  TEST_SOURCES ../tests/example_test.cpp
  TEST_FUNCTIONS example_test
  )

# Create a generator executable and use it to generate a halide function for the
# host target.
halide_add_osx_generator_to_app(TARGET test_osx
  GENERATOR_TARGET example
  GENERATOR_NAME example
  GENERATED_FUNCTION example4
  GENERATOR_SOURCES ../tests/example_generator.cpp
  GENERATOR_ARGS target=host channels=4
  )

# Pass the same generator target name with a different GENERATED_FUNCTION and
# Halide target string. This will cause the generator to be run a second time
# with a different target enabled. The test code referenced above uses both of
# these halide generated functions.
halide_add_osx_generator_to_app(TARGET test_osx
  GENERATOR_TARGET example
  GENERATOR_NAME example
  GENERATED_FUNCTION example4_glsl
  GENERATOR_SOURCES ../tests/example_generator.cpp
  GENERATOR_ARGS target=host-opengl channels=4
  )

# Stamp out a few more with different GeneratorParams.
halide_add_osx_generator_to_app(TARGET test_osx
  GENERATOR_TARGET example
  GENERATOR_NAME example
  GENERATED_FUNCTION example3
  GENERATOR_SOURCES ../tests/example_generator.cpp
  GENERATOR_ARGS target=host channels=3
  )
halide_add_osx_generator_to_app(TARGET test_osx
  GENERATOR_TARGET example
  GENERATOR_NAME example
  GENERATED_FUNCTION example3_glsl
  GENERATOR_SOURCES ../tests/example_generator.cpp
  GENERATOR_ARGS target=host-opengl channels=3
  )

halide_add_osx_generator_to_app(TARGET test_osx
  GENERATOR_TARGET example
  GENERATOR_NAME example
  GENERATED_FUNCTION example2
  GENERATOR_SOURCES ../tests/example_generator.cpp
  GENERATOR_ARGS target=host channels=2
  )
halide_add_osx_generator_to_app(TARGET test_osx
  GENERATOR_TARGET example
  GENERATOR_NAME example
  GENERATED_FUNCTION example2_glsl
  GENERATOR_SOURCES ../tests/example_generator.cpp
  GENERATOR_ARGS target=host-opengl channels=2
  )

halide_add_osx_generator_to_app(TARGET test_osx
  GENERATOR_TARGET example
  GENERATOR_NAME example
  GENERATED_FUNCTION example1
  GENERATOR_SOURCES ../tests/example_generator.cpp
  GENERATOR_ARGS target=host channels=1
  )
halide_add_osx_generator_to_app(TARGET test_osx
  GENERATOR_TARGET example
  GENERATOR_NAME example
  GENERATED_FUNCTION example1_glsl
  GENERATOR_SOURCES ../tests/example_generator.cpp
  GENERATOR_ARGS target=host-opengl channels=1
  )
