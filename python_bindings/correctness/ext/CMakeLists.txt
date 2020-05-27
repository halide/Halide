set(TARGET_addconstant host)
set(TARGET_bit host)
set(TARGET_user_context host-user_context)

foreach (GEN IN ITEMS addconstant bit user_context)
    # Create the Halide generator executable
    add_executable(${GEN}.gen ../${GEN}_generator.cpp)
    target_link_libraries(${GEN}.gen PRIVATE Halide::Generator)

    # Call it to generate the Python extension cpp file
    add_halide_library(ext_${GEN}
                       FROM ${GEN}.gen
                       GENERATOR ${GEN}
                       FUNCTION_NAME ${GEN}
                       PYTHON_EXTENSION ${GEN}_py_cpp
                       TARGETS ${TARGET_${GEN}})

    # Create the module from the generated library and .py.cpp
    Python3_add_library(py_${GEN} MODULE ${${GEN}_py_cpp})
    target_link_libraries(py_${GEN} PRIVATE ext_${GEN})
    set_target_properties(py_${GEN} PROPERTIES OUTPUT_NAME ${GEN}) # Python3_add_library adds target info to name.
endforeach ()
