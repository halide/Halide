function(_FindV8)
    # This is compatible with Ubuntu's libnode-dev package and with the default
    # instructions for building V8
    find_library(V8_LIBRARY
                 NAMES v8
                 PATH_SUFFIXES out/x64.release)

    find_library(V8_LIBPLATFORM_LIBRARY
                 NAMES v8_libplatform
                 PATH_SUFFIXES out/x64.release)

    find_library(V8_LIBBASE_LIBRARY
                 NAMES v8_libbase
                 PATH_SUFFIXES out/x64.release)

    find_path(V8_INCLUDE_PATH
              NAMES v8.h libplatform/libplatform.h
              PATH_SUFFIXES nodejs/deps/v8/include)

    mark_as_advanced(V8_LIBRARY V8_LIBPLATFORM_LIBRARY V8_LIBBASE_LIBRARY V8_INCLUDE_PATH)

    if (V8_INCLUDE_PATH AND EXISTS "${V8_INCLUDE_PATH}/v8-version.h")
        file(STRINGS "${V8_INCLUDE_PATH}/v8-version.h" V8_defines)

        set(V8_VERSION_MAJOR "")
        set(V8_VERSION_MINOR "")
        set(V8_VERSION_PATCH "")
        set(V8_VERSION_TWEAK "")

        if (";${V8_defines};" MATCHES ";#define V8_MAJOR_VERSION ([0-9]+);")
            set(V8_VERSION_MAJOR "${CMAKE_MATCH_1}")
        endif ()
        if (";${V8_defines};" MATCHES ";#define V8_MINOR_VERSION ([0-9]+);")
            set(V8_VERSION_MINOR "${CMAKE_MATCH_1}")
        endif ()
        if (";${V8_defines};" MATCHES ";#define V8_BUILD_NUMBER ([0-9]+);")
            set(V8_VERSION_PATCH "${CMAKE_MATCH_1}")
        endif ()
        if (";${V8_defines};" MATCHES ";#define V8_PATCH_LEVEL ([0-9]+);")
            set(V8_VERSION_TWEAK "${CMAKE_MATCH_1}")
        endif ()

        set(V8_VERSION "${V8_VERSION_MAJOR}.${V8_VERSION_MINOR}.${V8_VERSION_PATCH}.${V8_VERSION_TWEAK}")
        if (NOT V8_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+$")
            # Failed to detect version, unset all variables
            set(V8_VERSION_MAJOR "")
            set(V8_VERSION_MINOR "")
            set(V8_VERSION_PATCH "")
            set(V8_VERSION_TWEAK "")
            set(V8_VERSION "")
        endif ()

        if (V8_VERSION MATCHES "^(.+)\\.0$")
            # Remove tweak version when zero to conform to upstream numbering.
            # See: https://v8.dev/docs/version-numbers
            set(V8_VERSION "${CMAKE_MATCH_1}")
            set(V8_VERSION_TWEAK "")
        endif ()
    else ()
        set(V8_VERSION "")
    endif ()

    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(
        V8
        HANDLE_COMPONENTS
        REQUIRED_VARS V8_LIBRARY V8_INCLUDE_PATH
        VERSION_VAR V8_VERSION
    )

    if (V8_FOUND AND V8_LIBBASE_LIBRARY AND NOT TARGET V8::base)
        add_library(V8::base UNKNOWN IMPORTED)
        set_target_properties(V8::base PROPERTIES IMPORTED_LOCATION "${V8_LIBBASE_LIBRARY}")
        target_include_directories(V8::base INTERFACE "${V8_INCLUDE_PATH}")
    endif ()

    if (V8_FOUND AND V8_LIBPLATFORM_LIBRARY AND NOT TARGET V8::platform)
        add_library(V8::platform UNKNOWN IMPORTED)
        set_target_properties(V8::platform PROPERTIES IMPORTED_LOCATION "${V8_LIBPLATFORM_LIBRARY}")
        target_include_directories(V8::platform INTERFACE "${V8_INCLUDE_PATH}")
    endif ()

    if (V8_FOUND AND NOT TARGET V8::V8)
        add_library(V8::V8 UNKNOWN IMPORTED)
        set_target_properties(V8::V8 PROPERTIES IMPORTED_LOCATION "${V8_LIBRARY}")
        target_include_directories(V8::V8 INTERFACE "${V8_INCLUDE_PATH}")
        if (TARGET V8::base)
            target_link_libraries(V8::V8 INTERFACE V8::base)
        endif ()
        if (TARGET V8::platform)
            target_link_libraries(V8::V8 INTERFACE V8::platform)
        endif ()
    endif ()
endfunction()

_FindV8()

function(_FindV8)
endfunction()

function(_FindV8)
endfunction()
