#[==========================================================================[
  HalidePackageConfigHelpers

  This module contains a system for declaring that an export file might
  depend on another CMake package that was found by find_package. Such
  dependencies are collected in a project-wide property (rather than a
  variable) along with a snippet of code that reconstructs the original
  call.

  Then, after we have installed an export file via install(EXPORT), we can
  call a helper to add install rules that will read the file as-generated
  by CMake to check whether any of these packages could be required.

  CMake does not like to expose this information, in part because generator
  expressions make computing the eventual link set undecidable. Even so,
  for our purposes if `Pkg::` appears in our link-libraries list, then
  we need to find_package(Pkg). This module implements that heuristic.

  So why is this hard? It's because checking whether a dependency is
  actually included is very complicated. A library will appear if:

    1. It is SHARED or MODULE
    2. It linked privately to a STATIC target
         - These appear as $<LINK_ONLY:${dep}>
    3. It is STATIC and linked publicly to a SHARED target;
    4. It is INTERFACE or ALIAS and linked publicly
    5. It is included transitively via (4) and meets (1), (2), or (3)
    6. I am not sure this set of rules is exhaustive.

  There is an experimental feature in CMake 3.30 that will some day
  replace this module.
#]==========================================================================]

##
# Helper for registering package dependencies

function(_Halide_pkgdep PKG)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "PACKAGE_VARS")

    set(code "")
    foreach (var IN LISTS ARG_PACKAGE_VARS)
        string(APPEND code "set(${var} [[${${var}}]])\n")
    endforeach ()

    if ("${${PKG}_COMPONENTS}" STREQUAL "")
        string(APPEND code "find_dependency(${PKG} ${${PKG}_VERSION})")
    else ()
        string(APPEND code
               "find_dependency(\n"
               "    ${PKG} ${${PKG}_VERSION}\n"
               "    COMPONENTS ${${PKG}_COMPONENTS}\n"
               ")")
    endif ()

    set_property(DIRECTORY "${PROJECT_SOURCE_DIR}" APPEND PROPERTY pkgdeps "${PKG}")
    set_property(DIRECTORY "${PROJECT_SOURCE_DIR}" PROPERTY "pkgdeps[${PKG}]" "${code}")
endfunction()

##
# Helper for generating a file containing find_dependency() invocations
# by applying a heuristic to the actual dependency set.

function(_Halide_install_code)
    # This is just to keep the code in cmake_install.cmake readable.
    set(code "")
    set(sep "")
    math(EXPR ARGC "${ARGC} - 1")
    foreach (i RANGE "${ARGC}")
        string(APPEND code "${sep}${ARGV${i}}")
        set(sep "\n  ")
    endforeach ()
    install(CODE "${code}" COMPONENT "${ARG_COMPONENT}")
endfunction()

function(_Halide_install_pkgdeps)
    cmake_parse_arguments(
        PARSE_ARGV 0 ARG "" "COMPONENT;DESTINATION;FILE_NAME;EXPORT_FILE" ""
    )

    set(depFile "${CMAKE_CURRENT_BINARY_DIR}/${ARG_FILE_NAME}")

    _Halide_install_code(
        "file(READ \"\${CMAKE_INSTALL_PREFIX}/${ARG_DESTINATION}/${ARG_EXPORT_FILE}\" target_cmake)"
        "file(WRITE \"${depFile}.in\" \"\")"
    )

    get_property(pkgdeps DIRECTORY "${PROJECT_SOURCE_DIR}" PROPERTY pkgdeps)
    foreach (dep IN LISTS pkgdeps)
        get_property(pkgcode DIRECTORY "${PROJECT_SOURCE_DIR}" PROPERTY "pkgdeps[${dep}]")
        _Halide_install_code(
            "if (target_cmake MATCHES \"${dep}::\")"
            "  file(APPEND \"${depFile}.in\""
            "       [===[${pkgcode}]===] \"\\n\")"
            "endif ()"
        )
    endforeach ()

    _Halide_install_code(
        "configure_file(\"${depFile}.in\" \"${depFile}\" COPYONLY)"
    )

    install(
        FILES "${depFile}"
        DESTINATION "${ARG_DESTINATION}"
        COMPONENT "${ARG_COMPONENT}"
    )
endfunction()