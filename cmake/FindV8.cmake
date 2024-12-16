find_path(V8_INCLUDE_DIR v8.h)

find_library(
    V8_LIBRARY
    NAMES v8_monolith
    PATH_SUFFIXES
    out.gn/x64.release.sample/obj
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
    V8
    REQUIRED_VARS V8_LIBRARY V8_INCLUDE_DIR
    HANDLE_COMPONENTS
)

if (V8_FOUND AND NOT TARGET V8::V8)
    add_library(V8::V8 UNKNOWN IMPORTED)
    set_target_properties(V8::V8 PROPERTIES IMPORTED_LOCATION "${V8_LIBRARY}")
    target_include_directories(V8::V8 INTERFACE "${V8_INCLUDE_DIR}")
endif ()
