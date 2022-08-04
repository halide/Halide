add_subdirectory(halide)

# Now add a 'target' that exists just to conveniently give us a way to find this directory;
# we can't use $<TARGET_FILE_DIR:Halide_Python> because we need `import halide` to see
# this directory (not the one below us).
add_library(Halide_Python_Target_File_Dir UNKNOWN IMPORTED GLOBAL)
set_target_properties(Halide_Python_Target_File_Dir PROPERTIES
                      IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/nonexistent)

add_library(Halide::Python ALIAS Halide_Python_Target_File_Dir)
