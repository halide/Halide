project(python_bindings)
cmake_minimum_required(VERSION 3.1.3)

find_package(pybind11 REQUIRED)

#TODO: Remove after integrating it to the main cmake
find_package(Threads QUIET)
set(HALIDE_DISTRIB_DIR "${CMAKE_CURRENT_LIST_DIR}/../distrib")
include("${HALIDE_DISTRIB_DIR}/halide.cmake")
##

pybind11_add_module(halide
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
target_link_libraries(halide PRIVATE "${HALIDE_COMPILER_LIB}")
target_include_directories(halide PRIVATE "${HALIDE_INCLUDE_DIR}" "${HALIDE_TOOLS_DIR}")
install(TARGETS halide LIBRARY DESTINATION bin)

_halide_library_runtime(host HALIDE_TARGET)

enable_testing()

function(test_correctness_generator BASENAME)
  set(options )
  set(oneValueArgs)
  set(multiValueArgs HALIDE_TARGET_FEATURES)
  cmake_parse_arguments(args "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
  halide_library("${BASENAME}"
                 SRCS "correctness/${BASENAME}_generator.cpp"
                 EXTRA_OUTPUTS python_extension
                 HALIDE_TARGET_FEATURES ${args_HALIDE_TARGET_FEATURES})
  _halide_genfiles_dir("${BASENAME}" GENFILES_DIR)
  add_library("ext_${BASENAME}" SHARED "${GENFILES_DIR}/${BASENAME}.py.cpp")
  set_target_properties("ext_${BASENAME}" PROPERTIES PREFIX ""
                                                     OUTPUT_NAME "${BASENAME}"
                                                     LINKER_LANGUAGE C)
  target_link_libraries("ext_${BASENAME}" PRIVATE "${BASENAME}_cc" ${HALIDE_TARGET})
  target_include_directories("ext_${BASENAME}" PRIVATE "${GENFILES_DIR}"
                                                       "${HALIDE_INCLUDE_DIR}"
                                                       "${HALIDE_TOOLS_DIR}"
                                                       "${PYTHON_INCLUDE_DIR}")
  add_test(NAME "test_correctness_${BASENAME}_generator"
           COMMAND "${PYTHON_EXECUTABLE}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/correctness/${BASENAME}_test.py")

  set_property(TEST "test_correctness_${BASENAME}_generator"
               #PROPERTY ENVIRONMENT "PYTHONPATH=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
               PROPERTY ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}")
endfunction()

function(test_correctness_pystub BASENAME)
  add_library("${BASENAME}" SHARED "stub/PyStub.cpp"
                                   "stub/PyStubImpl.cpp"
                                   "correctness/${BASENAME}_generator.cpp")
  set_target_properties("${BASENAME}" PROPERTIES PREFIX ""
                                                 OUTPUT_NAME "${BASENAME}"
                                                 LINKER_LANGUAGE C)
  target_include_directories("${BASENAME}" PRIVATE "${HALIDE_INCLUDE_DIR}"
                                                   "${HALIDE_TOOLS_DIR}"
                                                   "${PYTHON_INCLUDE_DIR}")
  target_compile_definitions("${BASENAME}" PRIVATE "HALIDE_PYSTUB_GENERATOR_NAME=${BASENAME}")
endfunction()

test_correctness_pystub(simplestub)
test_correctness_pystub(complexstub)
test_correctness_pystub(partialbuildmethod)
test_correctness_pystub(nobuildmethod)

function(test_group GROUP BASENAME)
  add_test(NAME "test_${GROUP}_${BASENAME}"
           COMMAND "${PYTHON_EXECUTABLE}"
                   "${CMAKE_CURRENT_SOURCE_DIR}/${GROUP}/${BASENAME}.py")
  set_property(TEST "test_${GROUP}_${BASENAME}"
               #PROPERTY ENVIRONMENT "PYTHONPATH=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
               PROPERTY ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}")
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

add_test(NAME "test_tutorial_lesson_10_halide"
         COMMAND "${CMAKE_CXX_COMPILER}"
                 "${CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS}"
                 "${CMAKE_SHARED_LIBRARY_C_FLAGS}"
                 "-o"
                 "lesson_10_halide${CMAKE_SHARED_LIBRARY_SUFFIX}"
                 "${CMAKE_CURRENT_BINARY_DIR}/lesson_10_halide.py.cpp"
                 "${CMAKE_CURRENT_BINARY_DIR}/lesson_10_halide.o"
                 "${CMAKE_INCLUDE_FLAG_C}${PYTHON_INCLUDE_DIR}"
                 "${CMAKE_INCLUDE_FLAG_C}${HALIDE_INCLUDE_DIR}")

test_group(tutorial lesson_10_aot_compilation_run)
test_group(tutorial lesson_11_cross_compilation)
test_group(tutorial lesson_12_using_the_gpu)
test_group(tutorial lesson_13_tuples)
test_group(tutorial lesson_14_types)

set_property(TEST "test_tutorial_lesson_10_aot_compilation_run"
             PROPERTY DEPENDS "test_tutorial_lesson_10_halide")
set_property(TEST "test_tutorial_lesson_10_halide"
             PROPERTY DEPENDS "lesson_10_aot_compilation_generate")
