set(SOURCES
    PyArgument.cpp
    PyBoundaryConditions.cpp
    PyBuffer.cpp
    PyConciseCasts.cpp
    PyDerivative.cpp
    PyEnums.cpp
    PyError.cpp
    PyExpr.cpp
    PyExternFuncArgument.cpp
    PyFunc.cpp
    PyFuncRef.cpp
    PyHalide.cpp
    PyImageParam.cpp
    PyInlineReductions.cpp
    PyIROperator.cpp
    PyLambda.cpp
    PyLoopLevel.cpp
    PyMachineParams.cpp
    PyModule.cpp
    PyParam.cpp
    PyPipeline.cpp
    PyRDom.cpp
    PyStage.cpp
    PyTarget.cpp
    PyTuple.cpp
    PyType.cpp
    PyVar.cpp
    PyVarOrRVar.cpp
    )

pybind11_add_module(Halide_Python MODULE SYSTEM ${SOURCES})
add_library(Halide::Python ALIAS Halide_Python)
set_target_properties(Halide_Python
                      PROPERTIES
                      LIBRARY_OUTPUT_NAME halide
                      EXPORT_NAME Python)
target_link_libraries(Halide_Python PRIVATE Halide::Halide)

if (WIN32 AND BUILD_SHARED_LIBS)
    # There's precious little information about why Python only sometimes prevents DLLs from loading from the PATH on Windows.
    # This workaround places a copy of Halide.dll next to our Python module.
    # Ref: https://stackoverflow.com/questions/59860465/pybind11-importerror-dll-not-found-when-trying-to-import-pyd-in-python-int
    # Ref: https://bugs.python.org/issue36085
    # Ref: https://docs.python.org/3/whatsnew/3.8.html#bpo-36085-whatsnew
    add_custom_command(TARGET Halide_Python POST_BUILD
                       COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Halide::Halide> $<TARGET_FILE_DIR:Halide::Python>
                       VERBATIM)
endif ()
