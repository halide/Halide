find_path(V8_INCLUDE_DIR v8.h)

# A few suffixes that commonly occur when following V8's build instructions.
set(_FindV8_PATH_SUFFIXES "")
foreach (out IN ITEMS out out.gn)
    foreach (arch IN ITEMS x64)
        foreach (config IN ITEMS release debug)
            list(APPEND _FindV8_PATH_SUFFIXES "${out}/${arch}.${config}.static/obj")
            list(APPEND _FindV8_PATH_SUFFIXES "${out}/${arch}.${config}.sample/obj")
        endforeach ()
    endforeach ()
endforeach ()

find_library(
    V8_LIBRARY
    NAMES v8_monolith
    PATH_SUFFIXES ${_FindV8_PATH_SUFFIXES}
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
