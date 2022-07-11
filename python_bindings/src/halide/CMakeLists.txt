set(SOURCES
    PyArgument.cpp
    PyBoundaryConditions.cpp
    PyBuffer.cpp
    PyCallable.cpp
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
list(TRANSFORM SOURCES PREPEND "_halide/")

pybind11_add_module(Halide_Python MODULE ${SOURCES})
add_library(Halide::Python ALIAS Halide_Python)
set_target_properties(Halide_Python
                      PROPERTIES
                      LIBRARY_OUTPUT_NAME _halide
                      EXPORT_NAME Python)
target_link_libraries(Halide_Python PRIVATE Halide::Halide)

##
# Packaging
##

include(GNUInstallDirs)

set(Halide_INSTALL_PYTHONDIR "${CMAKE_INSTALL_LIBDIR}/python3/site-packages"
    CACHE STRING "Path to Halide Python bindings folder")

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        DESTINATION "${Halide_INSTALL_PYTHONDIR}"
        COMPONENT Halide_Python
        PATTERN "CMakeLists.txt" EXCLUDE
        PATTERN "*/_halide*" EXCLUDE)

install(TARGETS Halide_Python
        LIBRARY DESTINATION "${Halide_INSTALL_PYTHONDIR}/halide"
        COMPONENT Halide_Python)

# TODO: when we upgrade to CMake 3.22, replace with RUNTIME_DEPENDENCY_SET?
get_property(halide_is_imported TARGET Halide::Halide PROPERTY IMPORTED)
get_property(halide_type TARGET Halide::Halide PROPERTY TYPE)
if (halide_is_imported AND halide_type STREQUAL "SHARED_LIBRARY")
    # The following might be a bit confusing, but installing both libHalide
    # and its SONAME symbolic link causes the following bad behavior:
    #   1. CMake does the right thing and installs libHalide.so.15.0.0
    #      (TARGET_FILE) as a real file and libHalide.so.15
    #      (TARGET_SONAME_FILE_NAME) as a symbolic link to the former.
    #   2. Setuptools dutifully packs both of these into a Python wheel, which
    #      is a structured zip file. Zip files do not support symbolic links.
    #      Thus, two independent copies of libHalide are inserted, bloating the
    #      package.
    # The Python module (on Unix systems) links to the SONAME file, and
    # installing the symbolic link directly results in a broken link. Hence,
    # the renaming dance here.

    if (NOT MSVC)
        set(rename_arg RENAME "$<TARGET_SONAME_FILE_NAME:Halide::Halide>")
    else ()
        # DLL systems do not have sonames.
        set(rename_arg "")
    endif ()

    install(FILES "$<TARGET_FILE:Halide::Halide>"
            DESTINATION "${Halide_INSTALL_PYTHONDIR}/halide"
            COMPONENT Halide_Python
            ${rename_arg})
endif ()
