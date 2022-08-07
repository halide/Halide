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
list(TRANSFORM SOURCES PREPEND "halide_/")

pybind11_add_module(Halide_Python MODULE ${SOURCES})
add_library(Halide::Python ALIAS Halide_Python)
set_target_properties(Halide_Python
                      PROPERTIES
                      LIBRARY_OUTPUT_NAME halide_
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

##
# Packaging
##

include(CMakeDependentOption)
include(GNUInstallDirs)

set(Halide_INSTALL_PYTHONDIR "${CMAKE_INSTALL_LIBDIR}/python3/site-packages"
    CACHE STRING "Path to the Python site-packages folder")

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        DESTINATION "${Halide_INSTALL_PYTHONDIR}"
        COMPONENT Halide_Python
        FILES_MATCHING
        PATTERN "*.py"
        PATTERN "*/halide_" EXCLUDE
        PATTERN "*/__pycache__" EXCLUDE)

install(TARGETS Halide_Python
        LIBRARY DESTINATION "${Halide_INSTALL_PYTHONDIR}/halide"
        COMPONENT Halide_Python
        NAMELINK_COMPONENT Halide_Python)

get_property(halide_is_imported TARGET Halide::Halide PROPERTY IMPORTED)
get_property(halide_type TARGET Halide::Halide PROPERTY TYPE)
cmake_dependent_option(
    Halide_Python_INSTALL_IMPORTED_DEPS "" OFF
    "halide_is_imported;halide_type STREQUAL \"SHARED_LIBRARY\"" OFF
)

if (Halide_Python_INSTALL_IMPORTED_DEPS)
    # The following might be a bit confusing, but installing both libHalide
    # and its SONAME symbolic link causes the following bad behavior:
    #   1. CMake does the right thing and installs libHalide.so.X.Y.Z
    #      (TARGET_FILE) as a real file and libHalide.so.X
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

    # TODO: when we upgrade to CMake 3.22, replace with RUNTIME_DEPENDENCY_SET?
    install(FILES "$<TARGET_FILE:Halide::Halide>"
            DESTINATION "${Halide_INSTALL_PYTHONDIR}/halide"
            COMPONENT Halide_Python
            ${rename_arg})
endif ()

if (
    NOT CMAKE_INSTALL_RPATH  # Honor user overrides
    AND NOT halide_is_imported  # Imported Halide means user is responsible for RPATH
    AND halide_type STREQUAL "SHARED_LIBRARY"  # No need to set RPATH if statically linked
)
    if (APPLE)
        set(rbase @loader_path)
    else ()
        set(rbase $ORIGIN)
    endif ()

    file(RELATIVE_PATH lib_dir
        "${CMAKE_CURRENT_BINARY_DIR}/${Halide_INSTALL_PYTHONDIR}/halide"
        "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}")

    set_target_properties(Halide_Python PROPERTIES INSTALL_RPATH "${rbase}/${lib_dir}")
endif ()
