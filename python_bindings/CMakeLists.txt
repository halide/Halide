add_subdirectory(${PYBIND11_PATH} ${CMAKE_CURRENT_BINARY_DIR}/pybind)

pybind11_add_module(HalidePython
	src/PyArgument.cpp
	src/PyBoundaryConditions.cpp
	src/PyBuffer.cpp
	src/PyConciseCasts.cpp
	src/PyEnums.cpp
	src/PyError.cpp
	src/PyExpr.cpp
	src/PyExternFuncArgument.cpp
	src/PyFunc.cpp
	src/PyFuncRef.cpp
	src/PyHalide.cpp
	src/PyImageParam.cpp
	src/PyInlineReductions.cpp
	src/PyIROperator.cpp
	src/PyLambda.cpp
	src/PyLoopLevel.cpp
	src/PyMachineParams.cpp
	src/PyModule.cpp
	src/PyOutputs.cpp
	src/PyParam.cpp
	src/PyPipeline.cpp
	src/PyRDom.cpp
	src/PyStage.cpp
	src/PyTarget.cpp
	src/PyTuple.cpp
	src/PyType.cpp
	src/PyVar.cpp
	src/PyVarOrRVar.cpp
)

add_definitions(-DHALIDE_PYSTUB_GENERATOR_NAME=Halide)

target_include_directories(HalidePython PRIVATE "${HALIDE_INCLUDE_DIR}" "${HALIDE_TOOLS_DIR}")

set_target_properties(HalidePython 
	PROPERTIES 
	OUTPUT_NAME "halide"
	IMPORT_PREFIX "python_"
)

target_link_libraries(HalidePython PRIVATE ${HALIDE_COMPILER_LIB})

install(TARGETS HalidePython
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION bin
        ARCHIVE DESTINATION lib)