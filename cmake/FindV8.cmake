if (EXISTS "${V8_INCLUDE_PATH}")
    message(DEPRECATION "V8_INCLUDE_PATH has been renamed to V8_INCLUDE_DIR")
    set(V8_INCLUDE_DIR "${V8_INCLUDE_PATH}")
    set(V8_INCLUDE_DIR "${V8_INCLUDE_PATH}" CACHE PATH "")
endif ()

find_path(V8_INCLUDE_DIR v8.h)

if (EXISTS "${V8_LIB_PATH}")
    message(DEPRECATION "V8_LIB_PATH has been renamed to V8_LIBRARY")
    set(V8_LIBRARY "${V8_LIB_PATH}")
    set(V8_LIBRARY "${V8_LIB_PATH}" CACHE FILEPATH "")
endif ()

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
