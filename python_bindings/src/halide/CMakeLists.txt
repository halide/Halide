pybind11_add_module(Halide_Python)
add_library(Halide::Python ALIAS Halide_Python)

set_target_properties(
    Halide_Python
    PROPERTIES
    OUTPUT_NAME halide_
    EXPORT_NAME Python
)

target_sources(
    Halide_Python
    PRIVATE
    halide_/PyArgument.cpp
    halide_/PyBoundaryConditions.cpp
    halide_/PyBuffer.cpp
    halide_/PyCallable.cpp
    halide_/PyConciseCasts.cpp
    halide_/PyDerivative.cpp
    halide_/PyEnums.cpp
    halide_/PyError.cpp
    halide_/PyExpr.cpp
    halide_/PyExternFuncArgument.cpp
    halide_/PyFunc.cpp
    halide_/PyFuncRef.cpp
    halide_/PyGenerator.cpp
    halide_/PyHalide.cpp
    halide_/PyImageParam.cpp
    halide_/PyInlineReductions.cpp
    halide_/PyIROperator.cpp
    halide_/PyLambda.cpp
    halide_/PyLoopLevel.cpp
    halide_/PyModule.cpp
    halide_/PyParam.cpp
    halide_/PyParameter.cpp
    halide_/PyPipeline.cpp
    halide_/PyRDom.cpp
    halide_/PyStage.cpp
    halide_/PyTarget.cpp
    halide_/PyTuple.cpp
    halide_/PyType.cpp
    halide_/PyVar.cpp
    halide_/PyVarOrRVar.cpp
)

# It is technically still possible for a user to override the LIBRARY_OUTPUT_DIRECTORY by setting
# CMAKE_LIBRARY_OUTPUT_DIRECTORY_<CONFIG>, but they do so at their own peril. If a user needs to
# do this, they should use the CMAKE_PROJECT_Halide_Python_INCLUDE_BEFORE variable to override it
# just for this project, rather than globally, and they should ensure that the last path component
# is `halide`. Otherwise, the tests will break.
set_target_properties(
    Halide_Python PROPERTIES LIBRARY_OUTPUT_DIRECTORY "$<CONFIG>/halide"
)

if (Halide_ASAN_ENABLED)
    set_target_properties(
        Halide_Python PROPERTIES CMAKE_SHARED_LINKER_FLAGS -shared-libasan
    )
endif ()

target_link_libraries(Halide_Python PRIVATE Halide::Halide)

# TODO: There's precious little information about why Python only sometimes prevents DLLs from loading from the PATH
#   on Windows. This workaround places a copy of Halide.dll (and any other dependencies) next to our Python module.
#   Ref: https://stackoverflow.com/questions/59860465/pybind11-importerror-dll-not-found-when-trying-to-import-pyd-in-python-int
#   Ref: https://bugs.python.org/issue36085
#   Ref: https://docs.python.org/3/whatsnew/3.8.html#bpo-36085-whatsnew
add_custom_command(
    TARGET Halide_Python POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:Halide_Python> $<TARGET_RUNTIME_DLLS:Halide_Python>
    COMMAND_EXPAND_LISTS
    VERBATIM
)

# Copy our Python source files over so that we have a valid package in the binary directory.
set(python_sources
    __init__.py
    _generator_helpers.py
    imageio.py)

list(TRANSFORM python_sources PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/"
     OUTPUT_VARIABLE python_sources_source_dir)

set(stamp_file "$<CONFIG>/Halide_Python_sources.stamp")
add_custom_command(
    OUTPUT "${stamp_file}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory $<TARGET_FILE_DIR:Halide_Python>
    COMMAND "${CMAKE_COMMAND}" -E copy -t $<TARGET_FILE_DIR:Halide_Python> ${python_sources_source_dir}
    COMMAND "${CMAKE_COMMAND}" -E touch ${stamp_file}
    DEPENDS ${python_sources_source_dir}
    VERBATIM
)

add_custom_target(Halide_Python_sources DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/${stamp_file}")
add_dependencies(Halide_Python Halide_Python_sources)
