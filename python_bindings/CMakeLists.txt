find_package(pybind11 REQUIRED)

if (APPLE)
  set(HALIDE_PY_LINK_FLAG "-Wl,-undefined,dynamic_lookup")
elseif (NOT MSVC)
  set(HALIDE_PY_LINK_FLAG "-rdynamic")
endif()

if (NOT HALIDE_ENABLE_RTTI)
  if (NOT MSVC)
    set(HALIDE_PY_RTTI_FLAG "-fno-rtti")
  else()
    set(HALIDE_PY_RTTI_FLAG "/GR-")
  endif()
endif()

pybind11_add_module(halide_py
                    src/PyArgument.cpp            
                    src/PyBoundaryConditions.cpp  
                    src/PyBuffer.cpp              
                    src/PyConciseCasts.cpp        
                    src/PyDerivative.cpp          
                    src/PyEnums.cpp               
                    src/PyError.cpp               
                    src/PyExpr.cpp                
                    src/PyExternFuncArgument.cpp  
                    src/PyFunc.cpp                
                    src/PyFuncRef.cpp             
                    src/PyHalide.cpp              
                    src/PyIROperator.cpp        
                    src/PyImageParam.cpp        
                    src/PyInlineReductions.cpp  
                    src/PyLambda.cpp            
                    src/PyLoopLevel.cpp         
                    src/PyMachineParams.cpp     
                    src/PyModule.cpp    
                    src/PyParam.cpp     
                    src/PyPipeline.cpp  
                    src/PyRDom.cpp      
                    src/PyStage.cpp
                    src/PyTarget.cpp
                    src/PyTuple.cpp
                    src/PyType.cpp
                    src/PyVar.cpp
                    src/PyVarOrRVar.cpp)
set_target_properties(halide_py PROPERTIES LIBRARY_OUTPUT_NAME "halide"
                                           LINK_FLAGS "${HALIDE_PY_LINK_FLAG}")
target_link_libraries(halide_py PRIVATE "${HALIDE_COMPILER_LIB}")
target_include_directories(halide_py PRIVATE "${HALIDE_INCLUDE_DIR}" "${HALIDE_TOOLS_DIR}")
target_compile_options(halide_py PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-cast-qual>
                                         $<$<CXX_COMPILER_ID:GNU>:-Wno-suggest-override>)

install(TARGETS halide_py LIBRARY DESTINATION bin)

_halide_library_runtime(host HALIDE_TARGET)

enable_testing()

function(test_correctness_generator BASENAME)
  set(options )
  set(oneValueArgs)
  set(multiValueArgs HALIDE_TARGET_FEATURES)
  cmake_parse_arguments(args "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
  halide_library("${BASENAME}_py"
                 SRCS "correctness/${BASENAME}_generator.cpp"
                 GENERATOR_NAME "${BASENAME}"
                 GENERATOR_ARGS "-n" "${BASENAME}_py"
                 FUNCTION_NAME "${BASENAME}"
                 EXTRA_OUTPUTS python_extension
                 HALIDE_TARGET_FEATURES ${args_HALIDE_TARGET_FEATURES})
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
                   "${CMAKE_CURRENT_SOURCE_DIR}/correctness/${BASENAME}_test.py")
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
  add_library("${BASENAME}" MODULE "stub/PyStub.cpp"
                                   "stub/PyStubImpl.cpp"
                                   "correctness/${BASENAME}_generator.cpp")
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

function(test_group GROUP BASENAME)
  add_test(NAME "python_bindings_${GROUP}_${BASENAME}"
           COMMAND "${PYTHON_EXECUTABLE}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/${GROUP}/${BASENAME}.py")
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

test_correctness_generator(addconstant)
test_correctness_generator(bit)
test_group(correctness atomics)
test_group(correctness autodiff)
test_group(correctness basics)
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

test_group(apps bilateral_grid)
test_group(apps blur)
test_group(apps erode)
test_group(apps interpolate)
test_group(apps local_laplacian)

test_group(tutorial lesson_01_basics)
test_group(tutorial lesson_02_input_image)
test_group(tutorial lesson_03_debugging_1)
test_group(tutorial lesson_04_debugging_2)
test_group(tutorial lesson_05_scheduling_1)
test_group(tutorial lesson_06_realizing_over_shifted_domains)
test_group(tutorial lesson_07_multi_stage_pipelines)
test_group(tutorial lesson_08_scheduling_2)
test_group(tutorial lesson_09_update_definitions)
test_group(tutorial lesson_10_aot_compilation_generate)

if (MSVC)
  add_test(NAME "python_bindings_tutorial_lesson_10_halide_compile"
           COMMAND "${CMAKE_CXX_COMPILER}"
                   ${HALIDE_PY_RTTI_FLAG}
                   "/NOLOGO"
                   "/c"
                   "${CMAKE_CURRENT_BINARY_DIR}/lesson_10_halide.py.cpp"
                   "${CMAKE_INCLUDE_FLAG_C}${PYTHON_INCLUDE_DIR}"
                   "${CMAKE_INCLUDE_FLAG_C}${HALIDE_INCLUDE_DIR}")
  add_test(NAME "python_bindings_tutorial_lesson_10_halide"
           COMMAND "${CMAKE_LINKER}"
                   ${HALIDE_PY_LINK_FLAG}
                   "/NOIMPLIB"
                   "/DLL"
                   "/NOLOGO"
                   "/OUT:$<TARGET_FILE_DIR:${HALIDE_COMPILER_LIB}>/lesson_10_halide${PYTHON_MODULE_EXTENSION}"
                   "${CMAKE_CURRENT_BINARY_DIR}/lesson_10_halide.py${CMAKE_CXX_OUTPUT_EXTENSION}"
                   "${CMAKE_CURRENT_BINARY_DIR}/lesson_10_halide.o"
                   "${PYTHON_LIBRARY}")
  set_property(TEST "python_bindings_tutorial_lesson_10_halide"
               PROPERTY DEPENDS "python_bindings_tutorial_lesson_10_halide_compile")
  set_property(TEST "python_bindings_tutorial_lesson_10_halide_compile"
               PROPERTY DEPENDS "python_bindings_tutorial_lesson_10_aot_compilation_generate")
else()
  if (APPLE)
    set(EXTRA_FLAG "-bundle" "-Wl,-headerpad_max_install_names"
                   "${CMAKE_CXX_SYSROOT_FLAG}" "${CMAKE_OSX_SYSROOT}")
  else()
    set(EXTRA_FLAG "-shared")
  endif()
  add_test(NAME "python_bindings_tutorial_lesson_10_halide"
           COMMAND "${CMAKE_CXX_COMPILER}"
                   ${EXTRA_FLAG}
                   "${CMAKE_SHARED_MODULE_CXX_FLAGS}"
                   ${HALIDE_PY_LINK_FLAG}
                   ${HALIDE_PY_RTTI_FLAG}
                   "-o"
                   "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/lesson_10_halide${CMAKE_SHARED_MODULE_SUFFIX}"
                   "${CMAKE_CURRENT_BINARY_DIR}/lesson_10_halide.py.cpp"
                   "${CMAKE_CURRENT_BINARY_DIR}/lesson_10_halide.o"
                   "${CMAKE_INCLUDE_FLAG_C}${PYTHON_INCLUDE_DIR}"
                   "${CMAKE_INCLUDE_FLAG_C}${HALIDE_INCLUDE_DIR}")
  set_property(TEST "python_bindings_tutorial_lesson_10_halide"
               PROPERTY DEPENDS "python_bindings_tutorial_lesson_10_aot_compilation_generate")
endif()

test_group(tutorial lesson_10_aot_compilation_run)
test_group(tutorial lesson_11_cross_compilation)
test_group(tutorial lesson_12_using_the_gpu)
test_group(tutorial lesson_13_tuples)
test_group(tutorial lesson_14_types)

set_property(TEST "python_bindings_tutorial_lesson_10_aot_compilation_run"
             PROPERTY DEPENDS "python_bindings_tutorial_lesson_10_halide")
