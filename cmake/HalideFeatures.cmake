if (PROJECT_IS_TOP_LEVEL)
    include(FeatureSummary)
    cmake_language(
        DEFER DIRECTORY "${Halide_SOURCE_DIR}"
        CALL feature_summary WHAT ENABLED_FEATURES DISABLED_FEATURES
    )
endif ()

function(_Halide_feature_info opt doc)
    if (NOT PROJECT_IS_TOP_LEVEL)
        return()
    endif ()

    set(notice "")
    if (ARG_ADVANCED)
        cmake_language(GET_MESSAGE_LOG_LEVEL log_level)
        if (log_level MATCHES "^(VERBOSE|DEBUG|TRACE)$")
            set(notice " (advanced)")
        else ()
            return()
        endif ()
    endif ()

    add_feature_info("${opt}${notice}" "${opt}" "${doc}")
endfunction()

function(Halide_feature OPTION DOC DEFAULT)
    cmake_parse_arguments(PARSE_ARGV 3 ARG "ADVANCED" "" "DEPENDS")

    if (DEFAULT STREQUAL "TOP_LEVEL")
        set(default_value "${PROJECT_IS_TOP_LEVEL}")
    elseif (DEFAULT STREQUAL "AUTO")
        set(default_value ${ARG_DEPENDS})
    else ()
        set(default_value ${DEFAULT})
    endif ()

    if (${default_value})
        set(default_value ON)
    else ()
        set(default_value OFF)
    endif ()

    option("${OPTION}" "${DOC}" "${default_value}")
    if (ARG_ADVANCED)
        mark_as_advanced("${OPTION}")
    endif ()

    if (${OPTION} AND DEFINED ARG_DEPENDS AND NOT (${ARG_DEPENDS}))
        list(JOIN ARG_DEPENDS " " depends)
        message(WARNING "${OPTION} forcibly disabled -- requires ${depends}")
        set("${OPTION}" 0)
        set("${OPTION}" "${${OPTION}}" CACHE BOOL "${DOC}" FORCE)
    endif ()

    _Halide_feature_info("${OPTION}" "${DOC}")

    set("${OPTION}" "${${OPTION}}" PARENT_SCOPE)
endfunction()
