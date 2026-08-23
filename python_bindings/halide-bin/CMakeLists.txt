# The binary wheel has no Python extension target. Its compiled installation
# payload is supplied by the top-level Halide project when scikit-build
# configures it; copy its small Python surface into the combined build-tree
# package here.
set(python_package_dir "${Halide_Python_PACKAGE_DIR}/bin")
set(python_sources "${CMAKE_CURRENT_SOURCE_DIR}/src/halide/bin/__init__.py")
set(stamp_file "$<CONFIG>/Halide_PythonBin_sources.stamp")

add_custom_command(
    OUTPUT "${stamp_file}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${python_package_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy -t "${python_package_dir}" "${python_sources}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${stamp_file}"
    DEPENDS "${python_sources}"
    VERBATIM
)

add_custom_target(Halide_PythonBin_sources DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/${stamp_file}")
