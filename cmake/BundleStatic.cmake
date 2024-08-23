cmake_minimum_required(VERSION 3.28)

##
# Merge all the static library dependencies of TARGET into the library as a
# POST_BUILD step.

function(_bundle_static_replace VAR BEFORE AFTER)
    string(REPLACE "$<" "$\\\\<" AFTER "${AFTER}")
    string(REPLACE ">" "$<ANGLE-R>" AFTER "${AFTER}")
    string(REPLACE "," "$<COMMA>" AFTER "${AFTER}")
    string(REPLACE ";" "$<SEMICOLON>" AFTER "${AFTER}")
    set("${VAR}" "$<LIST:TRANSFORM,${${VAR}},REPLACE,${BEFORE},${AFTER}>")
    set("${VAR}" "$<LIST:TRANSFORM,${${VAR}},REPLACE,\\\\<,<>")
    set("${VAR}" "$<GENEX_EVAL:${${VAR}}>" PARENT_SCOPE)
endfunction()

function(_bundle_static_check_output VAR)
    execute_process(COMMAND ${ARGN} OUTPUT_VARIABLE "${VAR}" RESULT_VARIABLE "_${VAR}" ERROR_QUIET)
    if (_${VAR})
        set("${VAR}" "")
    endif ()
    set("${VAR}" "${${VAR}}" PARENT_SCOPE)
endfunction()

function(_bundle_static_is_apple_libtool result item)
    _bundle_static_check_output(version_info "${item}" -V)
    if (version_info MATCHES "Apple, Inc.")
        set(result 1 PARENT_SCOPE)
    else ()
        set(result 0 PARENT_SCOPE)
    endif ()
endfunction()

function(bundle_static TARGET)
    get_property(type TARGET "${TARGET}" PROPERTY TYPE)
    if (NOT type STREQUAL "STATIC_LIBRARY")
        return()
    endif ()

    # The following code is quite subtle. First, it "recursively" (up to a depth
    # limit) expands all the INTERFACE_LINK_LIBRARIES of the TARGET. Once the
    # full set of library dependencies has been determined, it filters just
    # the static libraries and replaces them with their on-disk locations.

    # Start with the $<LINK_ONLY:$<BUILD_LOCAL_INTERFACE:...>> dependencies of
    # the target. These are the privately-linked static and interface libraries
    # that the user intends to delete upon export.
    set(cmd "$<TARGET_PROPERTY:${TARGET},INTERFACE_LINK_LIBRARIES>")
    set(cmd "$<FILTER:${cmd},INCLUDE,LINK_ONLY:..BUILD_LOCAL_INTERFACE>")

    # Repeatedly expand and flatten: T ~> T, T.INTERFACE_LINK_LIBRARIES
    foreach (i RANGE 5)
        _bundle_static_replace(
            cmd "(.+)" "$<$<TARGET_EXISTS:\\1>:\\1;$<TARGET_PROPERTY:\\1,INTERFACE_LINK_LIBRARIES>>"
        )
        set(cmd "$<LIST:REMOVE_DUPLICATES,$<GENEX_EVAL:${cmd}>>")
    endforeach ()

    # Ensure we are only including targets
    _bundle_static_replace(cmd "(.+)" "$<TARGET_NAME_IF_EXISTS:\\1>")

    # Rewrite T ~> T^T.TYPE  -- we use ^ as a delimiter
    _bundle_static_replace(cmd "(.+)" "\\1^$<TARGET_PROPERTY:\\1,TYPE>")
    set(cmd "$<GENEX_EVAL:${cmd}>")

    # Select exactly the set of static libraries
    set(cmd "$<FILTER:${cmd},INCLUDE,\\^STATIC_LIBRARY$>")

    # Rewrite T^... ~> $<TARGET_FILE:T>
    _bundle_static_replace(cmd "^([^^]+)\\^.+$" "$<TARGET_FILE:\\1>")

    # Rename the target to target.tmp
    add_custom_command(
        TARGET "${TARGET}" POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E rename "$<TARGET_FILE:${TARGET}>" "$<TARGET_FILE:${TARGET}>.tmp"
        VERBATIM
    )

    # Finally merge everything together using the platform tool.
    find_program(LIB lib.exe HINTS "${CMAKE_AR}")
    if (WIN32 AND LIB)
        add_custom_command(
            TARGET "${TARGET}" POST_BUILD
            COMMAND "${LIB}" "/out:$<TARGET_FILE:${TARGET}>" "$<TARGET_FILE:${TARGET}>.tmp" "${cmd}"
            COMMAND_EXPAND_LISTS
            VERBATIM
        )
        return()
    endif ()

    find_program(LIBTOOL libtool VALIDATOR _bundle_static_is_apple_libtool)
    if (APPLE AND LIBTOOL)
        add_custom_command(
            TARGET "${TARGET}" POST_BUILD
            COMMAND "${LIBTOOL}" -static -o "$<TARGET_FILE:${TARGET}>" "$<TARGET_FILE:${TARGET}>.tmp" "${cmd}"
            COMMAND_EXPAND_LISTS
            VERBATIM
        )
        return()
    endif ()

    _bundle_static_check_output(version_info "${CMAKE_AR}" V)
    if (version_info MATCHES "GNU")
        string(CONFIGURE [[
            create $<TARGET_FILE:@TARGET@>
            addlib $<TARGET_FILE:@TARGET@>.tmp
            $<LIST:JOIN,$<LIST:TRANSFORM,@cmd@,PREPEND,addlib >,
            >
            save
            end
        ]] mri_script)
        string(REGEX REPLACE "(^|\n) +" "\\1" mri_script "${mri_script}")

        file(GENERATE OUTPUT "fuse-${TARGET}.mri"
             CONTENT "${mri_script}" TARGET "${TARGET}")

        add_custom_command(
            TARGET "${TARGET}" POST_BUILD
            COMMAND "${CMAKE_AR}" -M < "${CMAKE_CURRENT_BINARY_DIR}/fuse-${TARGET}.mri"
            VERBATIM
        )
        return()
    endif ()

    message(FATAL_ERROR "bundle_static_libs not implemented for the present toolchain")
endfunction()
