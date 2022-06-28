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
  
pybind11_add_module(Halide_Python MODULE ${SOURCES})
add_library(Halide::Python ALIAS Halide_Python)
set_target_properties(Halide_Python
                      PROPERTIES
                      LIBRARY_OUTPUT_NAME halide_
                      EXPORT_NAME Python)
target_link_libraries(Halide_Python PRIVATE Halide::Halide)

set(Halide_INSTALL_PYTHONDIR "${CMAKE_INSTALL_LIBDIR}/python3/site-packages"
CACHE STRING "Path to Halide Python bindings folder")
install(TARGETS Halide_Python
        LIBRARY DESTINATION ${Halide_INSTALL_PYTHONDIR} COMPONENT Halide_Python
        NAMELINK_COMPONENT Halide_Python)
