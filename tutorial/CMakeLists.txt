option(WITH_TUTORIALS "Build tutorials" ON)

include_directories ("${CMAKE_BINARY_DIR}/include")
link_directories ("${LLVM_LIB}")

if (WITH_TUTORIALS)
  message(STATUS "Building tutorials enabled")
  find_package(PNG)
  macro(use_libpng TARGET)
    target_compile_definitions(${TARGET} PRIVATE ${PNG_DEFINITIONS})
    target_include_directories(${TARGET} PRIVATE ${PNG_INCLUDE_DIRS})
    target_link_libraries(${TARGET} ${PNG_LIBRARIES})
  endmacro()
  macro(add_tutorial source_file)
    string(REPLACE ".cpp" "" name ${source_file})
    halide_project(${name} "tutorials" ${source_file})
  endmacro()

  # Declare tutorials
  add_tutorial(lesson_01_basics.cpp)
  add_tutorial(lesson_03_debugging_1.cpp)
  add_tutorial(lesson_04_debugging_2.cpp)
  add_tutorial(lesson_05_scheduling_1.cpp)
  add_tutorial(lesson_06_realizing_over_shifted_domains.cpp)
  add_tutorial(lesson_11_cross_compilation.cpp)
  add_tutorial(lesson_13_tuples.cpp)
  add_tutorial(lesson_14_types.cpp)
  if (PNG_FOUND)
    add_tutorial(lesson_02_input_image.cpp)
    use_libpng(lesson_02_input_image)
    add_tutorial(lesson_07_multi_stage_pipelines.cpp)
    use_libpng(lesson_07_multi_stage_pipelines)
    add_tutorial(lesson_08_scheduling_2.cpp)
    use_libpng(lesson_08_scheduling_2)
    add_tutorial(lesson_09_update_definitions.cpp)
    use_libpng(lesson_09_update_definitions)
    add_tutorial(lesson_12_using_the_gpu.cpp)
    use_libpng(lesson_12_using_the_gpu)
  else()
    message(WARNING "Not building all tutorials due to missing libpng")
  endif()

  option(BUILD_AOT_TUTORIAL "Build the ahead-of-time compilation tutorial" ON)
  if (BUILD_AOT_TUTORIAL)
    # Tutorial 10 requires that we build a generator then run it
    # so we can build the final executable
    add_tutorial(lesson_10_aot_compilation_generate)

    # This is name of the object file we expect lesson_10_aot_compilation_generate
    # to emit.
    set(lesson_10_gen_obj "lesson_10_halide.o")
    set(lesson_10_gen_header lesson_10_halide.h)

    # Note specifyng the command as a target should cause CMake to automatically
    # substitute the absolute path to the target and setup a dependency
    add_custom_command(OUTPUT ${lesson_10_gen_obj} ${lesson_10_gen_header}
      COMMAND lesson_10_aot_compilation_generate
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
      COMMENT "Running Generator for lesson 10"
    )

    # Note that this executable is pretty much standalone (it doesn't need LLVM) although it
    # seems to need to link with pthread.
    add_executable(lesson_10_aot_compilation_run
                   lesson_10_aot_compilation_run.cpp
                   ${lesson_10_gen_header}
                   ${lesson_10_gen_obj})
    target_include_directories(lesson_10_aot_compilation_run PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    # FIXME: This probably isn't portable
    if (NOT MSVC)
      target_link_libraries(lesson_10_aot_compilation_run pthread dl)
    endif()
  endif()
else()
  message(STATUS "Building tutorials disabled")
endif()
