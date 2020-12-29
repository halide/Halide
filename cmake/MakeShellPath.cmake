##
# Convenience function for creating shell paths
##

function(make_shell_path OUTVAR)
    if (WIN32)
        set(SEP "\\$<SEMICOLON>")
    else ()
        set(SEP ":")
    endif ()

    list(TRANSFORM ARGN REPLACE "^(.+)$" "$<SHELL_PATH:\\1>")
    string(REPLACE ";" "${SEP}" ARGN "${ARGN}")
    set(${OUTVAR} "${ARGN}" PARENT_SCOPE)
endfunction()
