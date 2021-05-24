cmake_minimum_required(VERSION 3.19)

file(STRINGS "${CPACK_RESOURCE_FILE_LICENSE}" copyright_line LIMIT_COUNT 1)
string(TIMESTAMP timestamp "%a, %d %b %Y %H:%M:%S" UTC)

find_program(GZIP gzip)
if (NOT GZIP)
    message(FATAL_ERROR "Could not find gzip")
endif ()

foreach (comp IN LISTS CPACK_COMPONENTS_ALL)
    string(TOUPPER "CPACK_DEBIAN_${comp}_PACKAGE_NAME" package_name_var)
    string(TOLOWER "${${package_name_var}}" package_name)

    # Write copyright information to the package.
    configure_file("${CMAKE_CURRENT_LIST_DIR}/copyright"
                   "${CPACK_TEMPORARY_DIRECTORY}/${comp}/usr/share/doc/${package_name}/copyright"
                   @ONLY NO_SOURCE_PERMISSIONS)

    # Write changelog to the package.
    set(changelog "${CPACK_TEMPORARY_DIRECTORY}/${comp}/usr/share/doc/${package_name}/changelog")
    configure_file("${CMAKE_CURRENT_LIST_DIR}/changelog" "${changelog}"
                   @ONLY NO_SOURCE_PERMISSIONS)
    execute_process(COMMAND "${GZIP}" -n9 "${changelog}" COMMAND_ERROR_IS_FATAL ANY)
endforeach ()
