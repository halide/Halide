# Filters a module-definition (.def) export list produced by
# `cmake -E __create_def`, dropping symbols that are not ours to export.
#
# Usage: cmake -DIN=<in.def> -DOUT=<out.def> -P FilterDllExports.cmake
#
# CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS exports every external symbol in Halide's
# object files, including COMDAT instantiations of std:: templates. The list is
# close to link.exe's limit of 65535 exports (LNK1189), so we drop symbols whose
# *enclosing scope* (the outermost namespace/class in the MSVC-decorated name)
# is std, stdext, Concurrency, llvm or clang. Clients instantiate their own
# copies of those templates. A substring match on "@std@@" would be wrong, as
# it also appears in the parameter encoding of Halide functions taking, e.g.,
# std::string.
#
# Whenever a name cannot be confidently reduced, it is kept.
#
# Performance: this runs on every link over ~65k lines, and character-level
# loops in CMake are far too slow. Instead we rely on the fact that a template
# argument list containing no nested template is a regular language. Innermost
# templates are repeatedly collapsed into a plain fragment until the qualified
# name is flat, then the outermost scope is read with a single regex.

if (NOT DEFINED IN OR NOT DEFINED OUT)
    message(FATAL_ERROR "Usage: cmake -DIN=<in.def> -DOUT=<out.def> -P FilterDllExports.cmake")
endif ()

set(EXPORT_LIMIT 65535)
set(MAX_PASSES 8)

# -- Building blocks of the mangled-name grammar --------------------------------

# An identifier fragment, e.g. `Halide@`.
set(ident [=[[A-Za-z_][A-Za-z0-9_]*@]=])
# A qualified name: fragments (identifiers or one-digit back-references)
# terminated by `@`, e.g. `Func@Halide@@`. Used after V/U/T/W4 in a type.
set(qual "((${ident}|[0-9])*@)")
# One template argument. Deliberately excludes anything that contains `@` or
# `?` in a way we do not model (function types `6`/`8`, arrays `Y`, `?`...), so
# such templates fail to match and their symbols are kept.
string(CONCAT arg
    "(" "[VUT]${qual}"  # class/struct/union by name
    "|" "W[0-9]${qual}"  # enum
    "|" [=[\$\$[A-Za-z]]=]  # $$Q, $$R, $$T, $$C...
    "|" [=[\$0[0-9A-P]*@]=]  # non-negative integer value
    "|" "_[A-Za-z0-9]"  # _N, _J, ...
    "|" "[0-579A-SXZ]"  # builtins, modifiers, back-references
    ")"
)

# An innermost template-id `?$name@<args>@`; \1 is the name.
set(template_re [=[\?\$([A-Za-z_][A-Za-z0-9_]*)@]=])
string(APPEND template_re "${arg}*@")

# The outermost scope of a flat qualified name: at least one fragment, then a
# final identifier, terminated by `@@`. \3 is the outermost scope.
set(scope_re "^(${ident}|[0-9])+([A-Za-z_][A-Za-z0-9_]*)@@")
# A qualified name of a single fragment, i.e. an unscoped global.
set(global_re "^[A-Za-z_][A-Za-z0-9_]*@@")

set(dropped_scope_re "^(std|stdext|Concurrency|llvm|clang)$")

# -- Main loop ------------------------------------------------------------------

file(READ "${IN}" content)
string(REPLACE "\r" "" content "${content}")
string(REPLACE "\n" ";" lines "${content}")

set(out "")
set(total 0)
set(dropped 0)
set(kept 0)
set(unparsed 0)

foreach (line IN LISTS lines)
    if (NOT line MATCHES "[^ \t]")
        continue()
    endif ()
    if (line MATCHES "^[ \t]*(EXPORTS|LIBRARY)")
        string(APPEND out "${line}\n")
        continue()
    endif ()

    string(REGEX REPLACE "^[ \t]+" "" symbol "${line}")
    string(REGEX REPLACE "[ \t].*$" "" symbol "${symbol}")
    math(EXPR total "${total} + 1")

    set(decision KEEP)

    # Strip the special-name prefix, leaving the qualified name. Anything
    # undecorated (extern "C") is kept as is.
    set(body "")
    if (symbol MATCHES "^\\?")
        if (symbol MATCHES "^\\?\\?_C")
            # String literal.
        elseif (symbol MATCHES "^\\?\\?\\$")
            # Template function: the name is `?$name@...`
            string(SUBSTRING "${symbol}" 1 -1 body)
        elseif (symbol MATCHES "^\\?\\?_R0\\?A[VU]")
            string(SUBSTRING "${symbol}" 8 -1 body)
        elseif (symbol MATCHES "^\\?\\?_R4")
            string(SUBSTRING "${symbol}" 5 -1 body)
        elseif (symbol MATCHES "^\\?\\?_R")
            set(decision UNPARSED)
        elseif (symbol MATCHES "^\\?\\?__[0-9A-Za-z]")
            string(SUBSTRING "${symbol}" 5 -1 body)
        elseif (symbol MATCHES "^\\?\\?_[0-9A-Za-z]")
            string(SUBSTRING "${symbol}" 4 -1 body)
        elseif (symbol MATCHES "^\\?\\?[0-9A-Z]")
            string(SUBSTRING "${symbol}" 3 -1 body)
        elseif (symbol MATCHES "^\\?\\?")
            set(decision UNPARSED)
        else ()
            string(SUBSTRING "${symbol}" 1 -1 body)
        endif ()
    endif ()

    if (NOT "${body}" STREQUAL "")
        foreach (pass RANGE 1 ${MAX_PASSES})
            if (NOT body MATCHES "\\?\\$")
                break()
            endif ()
            set(before "${body}")
            string(REGEX REPLACE "${template_re}" "\\1@" body "${body}")
            if (body STREQUAL before)
                break()
            endif ()
        endforeach ()

        if (body MATCHES "${scope_re}")
            if ("${CMAKE_MATCH_2}" MATCHES "${dropped_scope_re}")
                set(decision DROP)
            endif ()
        elseif (NOT body MATCHES "${global_re}")
            set(decision UNPARSED)
        endif ()
    endif ()

    if (decision STREQUAL DROP)
        math(EXPR dropped "${dropped} + 1")
        continue()
    elseif (decision STREQUAL UNPARSED)
        math(EXPR unparsed "${unparsed} + 1")
    endif ()
    math(EXPR kept "${kept} + 1")
    string(APPEND out "${line}\n")
endforeach ()

if (total EQUAL 0)
    message(FATAL_ERROR "No exports found in ${IN}")
endif ()

file(WRITE "${OUT}" "${out}")

message(
    VERBOSE "Halide.dll exports: ${total} total, ${dropped} filtered out, ${kept} remaining "
    "(${unparsed} kept only because their names could not be parsed); limit is ${EXPORT_LIMIT}"
)

if (kept GREATER EXPORT_LIMIT)
    message(
        FATAL_ERROR "Halide.dll would export ${kept} symbols after filtering, exceeding the "
        "linker limit of ${EXPORT_LIMIT} (LNK1189). Reduce the number of exported "
        "symbols or extend cmake/FilterDllExports.cmake."
    )
endif ()

math(EXPR warn_threshold "${EXPORT_LIMIT} * 95 / 100")
if (kept GREATER warn_threshold)
    message(
        WARNING "Halide.dll exports ${kept} symbols after filtering, over 95% of the linker "
        "limit of ${EXPORT_LIMIT}."
    )
endif ()
