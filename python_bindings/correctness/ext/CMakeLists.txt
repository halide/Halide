set(FEATURES_user_context user_context)

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
                       FEATURES ${FEATURES_${GEN}}
                       TARGETS cmake)

    # Create the module from the generated library and .py.cpp
    Python3_add_library(py_${GEN} MODULE ${${GEN}_py_cpp})
    target_link_libraries(py_${GEN} PRIVATE ext_${GEN})
    set_target_properties(py_${GEN} PROPERTIES OUTPUT_NAME ${GEN}) # Python3_add_library adds target info to name.

    # Fake up a linker script that will export *just* the PyInit entry
    # point we want. (If we don't do this we can have interesting failures
    # when loading multiple of these Python extensions in the same space.)
    #
    # TODO: How to do this for Windows as well?
    if (APPLE)
        configure_file(ext.ldscript.apple.in "${CMAKE_CURRENT_BINARY_DIR}/${GEN}.ldscript")
        target_link_options(py_${GEN} PRIVATE "-Wl,-exported_symbols_list,${CMAKE_CURRENT_BINARY_DIR}/${GEN}.ldscript")
    elseif (UNIX)
        configure_file(ext.ldscript.linux.in "${CMAKE_CURRENT_BINARY_DIR}/${GEN}.ldscript")
        target_link_options(py_${GEN} PRIVATE "-Wl,--version-script,${CMAKE_CURRENT_BINARY_DIR}/${GEN}.ldscript")
    endif()

endforeach ()
