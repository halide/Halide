function(test_correctness_generator BASENAME)
  set(options )
  set(oneValueArgs)
  set(multiValueArgs HALIDE_TARGET_FEATURES)
  cmake_parse_arguments(args "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
  halide_library("${BASENAME}_py"
                 SRCS "${BASENAME}_generator.cpp"
                 GENERATOR_NAME "${BASENAME}"
                 GENERATOR_ARGS "-n" "${BASENAME}_py"
                 FUNCTION_NAME "${BASENAME}"
                 EXTRA_OUTPUTS python_extension
                 HALIDE_TARGET_FEATURES ${args_HALIDE_TARGET_FEATURES})
  _halide_library_runtime(host HALIDE_TARGET)
  _halide_genfiles_dir("${BASENAME}_py" GENFILES_DIR)
  add_library("ext_${BASENAME}" MODULE "${GENFILES_DIR}/${BASENAME}_py.py.cpp")
  add_dependencies("ext_${BASENAME}" halide_py "${BASENAME}_py")
  set_target_properties("ext_${BASENAME}" PROPERTIES PREFIX ""
                                                     OUTPUT_NAME "${BASENAME}"
                                                     SUFFIX "${PYTHON_MODULE_EXTENSION}"
                                                     LINKER_LANGUAGE C
                                                     LINK_FLAGS "${HALIDE_PY_LINK_FLAG}")
  target_link_libraries("ext_${BASENAME}" PRIVATE "${BASENAME}_py_cc" ${HALIDE_TARGET})
  target_include_directories("ext_${BASENAME}" PRIVATE "${GENFILES_DIR}"
                                                       "${HALIDE_INCLUDE_DIR}"
                                                       "${HALIDE_TOOLS_DIR}"
                                                       "${PYTHON_INCLUDE_DIR}")
  add_test(NAME "python_bindings_correctness_${BASENAME}_generator"
           COMMAND "${PYTHON_EXECUTABLE}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/${BASENAME}_test.py")
  if (MSVC)
    set(ENV_PATH "$<TARGET_FILE_DIR:${HALIDE_COMPILER_LIB}>" $ENV{PATH})
    LIST(JOIN ENV_PATH "\;" TEST_PATH)
    set(ENV_PYTHONPATH "$<TARGET_FILE_DIR:halide_py>"
                       "$<TARGET_FILE_DIR:${HALIDE_COMPILER_LIB}>"
                       $ENV{PYTHONPATH})
    LIST(JOIN ENV_PYTHONPATH "\;" TEST_PYTHONPATH)
    target_link_libraries("ext_${BASENAME}" PRIVATE "${PYTHON_LIBRARY}")
    target_compile_options("ext_${BASENAME}" PRIVATE ${HALIDE_PY_RTTI_FLAG})
    set_property(TEST "python_bindings_correctness_${BASENAME}_generator"
                 PROPERTY ENVIRONMENT "PYTHONPATH=${TEST_PYTHONPATH}"
                                      "PATH=${TEST_PATH}")
  else()
    target_compile_options("ext_${BASENAME}" PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-suggest-override>
                                                     -Wno-cast-qual
                                                     ${HALIDE_PY_RTTI_FLAG})
    set_property(TEST "python_bindings_correctness_${BASENAME}_generator"
                 PROPERTY ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:halide_py>")
  endif()
endfunction()

function(test_correctness_pystub BASENAME)
  add_library("${BASENAME}" MODULE ${PYSTUB_SRCS}
                                   "${BASENAME}_generator.cpp")
  add_dependencies("${BASENAME}" halide_py)
  set_target_properties("${BASENAME}" PROPERTIES PREFIX ""
                                                 OUTPUT_NAME "${BASENAME}"
                                                 LINKER_LANGUAGE C
                                                 LINK_FLAGS "${HALIDE_PY_LINK_FLAG}")
  if (MSVC)
    target_link_libraries("${BASENAME}" PRIVATE "${PYTHON_LIBRARY}" "${HALIDE_COMPILER_LIB}")
    target_compile_options("${BASENAME}" PRIVATE "-EHsc" ${HALIDE_PY_RTTI_FLAG})
  else()
    target_compile_options("${BASENAME}" PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-suggest-override>
                                                 -Wno-cast-qual
                                                 ${HALIDE_PY_RTTI_FLAG})
  endif()
  target_include_directories("${BASENAME}" PRIVATE "${HALIDE_INCLUDE_DIR}"
                                                   "${HALIDE_TOOLS_DIR}"
                                                   "${pybind11_INCLUDE_DIRS}"
                                                   "${PYTHON_INCLUDE_DIR}")
  target_compile_definitions("${BASENAME}" PRIVATE "HALIDE_PYSTUB_GENERATOR_NAME=${BASENAME}")
endfunction()

test_correctness_pystub(simplestub)
test_correctness_pystub(complexstub)
test_correctness_pystub(partialbuildmethod)
test_correctness_pystub(nobuildmethod)

test_correctness_generator(addconstant)
test_group(correctness atomics)
test_group(correctness autodiff)
test_group(correctness basics)
test_correctness_generator(bit)
test_group(correctness boundary_conditions)
test_group(correctness buffer)
test_group(correctness compile_to)
test_group(correctness division)
test_group(correctness extern)
test_group(correctness float_precision_test)
test_group(correctness iroperator)
test_group(correctness multipass_constraints)
test_group(correctness rdom)
test_group(correctness target)
test_group(correctness tuple_select)
test_group(correctness type)
test_correctness_generator(user_context HALIDE_TARGET_FEATURES user_context)
test_group(correctness var)
