include(FindPackageHandleStandardArgs)

find_path(V8_INCLUDE_PATH NAMES v8.h libplatform/libplatform.h)
find_library(V8_LIB_PATH NAMES v8)

mark_as_advanced(V8_INCLUDE_PATH V8_LIB_PATH)

find_package_handle_standard_args(
        V8
        HANDLE_COMPONENTS
        REQUIRED_VARS V8_INCLUDE_PATH V8_LIB_PATH
        VERSION_VAR V8_VERSION
)

if (V8_FOUND AND NOT TARGET Halide::V8)
    add_library(Halide::V8 STATIC IMPORTED)
    set_target_properties(Halide::V8 PROPERTIES IMPORTED_LOCATION "${V8_LIB_PATH}")
    target_include_directories(Halide::V8 INTERFACE "${V8_INCLUDE_PATH}")
endif ()
