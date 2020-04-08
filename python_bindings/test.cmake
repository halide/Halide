enable_testing()

function(test_group GROUP BASENAME)
  add_test(NAME "python_bindings_${GROUP}_${BASENAME}"
           COMMAND "${PYTHON_EXECUTABLE}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/${BASENAME}.py")
  if (MSVC)
    set(ENV_PATH "$<TARGET_FILE_DIR:${HALIDE_COMPILER_LIB}>" $ENV{PATH})
    LIST(JOIN ENV_PATH "\;" TEST_PATH)
    set(ENV_PYTHONPATH "$<TARGET_FILE_DIR:halide_py>"
                       "$<TARGET_FILE_DIR:${HALIDE_COMPILER_LIB}>"
                       $ENV{PYTHONPATH})
    LIST(JOIN ENV_PYTHONPATH "\;" TEST_PYTHONPATH)
    set_property(TEST "python_bindings_${GROUP}_${BASENAME}"
                 PROPERTY ENVIRONMENT "PYTHONPATH=${TEST_PYTHONPATH}"
                                      "PATH=${TEST_PATH}")
  else()
    set_property(TEST "python_bindings_${GROUP}_${BASENAME}"
                 PROPERTY ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:halide_py>")
  endif()
endfunction()

add_subdirectory(stub)
add_subdirectory(correctness)
add_subdirectory(apps)
add_subdirectory(tutorial)
