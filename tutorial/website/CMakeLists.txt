find_package(Python3 COMPONENTS Interpreter REQUIRED)

find_program(GDB_EXECUTABLE gdb)
find_program(LLDB_EXECUTABLE lldb)
if (NOT GDB_EXECUTABLE AND NOT LLDB_EXECUTABLE)
    message(FATAL_ERROR "WITH_TUTORIAL_WEBSITE requires gdb or lldb to be installed")
endif ()

set(Halide_TUTORIAL_WEBSITE_DEBUGGER "AUTO"
    CACHE STRING
    "Which debugger tutorial/website should use to capture output snippets: AUTO, GDB, or LLDB"
)
set_property(CACHE Halide_TUTORIAL_WEBSITE_DEBUGGER PROPERTY STRINGS AUTO GDB LLDB)

execute_process(
    COMMAND "${Python3_EXECUTABLE}" -c "import jinja2, markupsafe, pygments"
    RESULT_VARIABLE _tutorial_website_python_deps_missing
    OUTPUT_QUIET ERROR_QUIET
)
if (_tutorial_website_python_deps_missing)
    message(
        FATAL_ERROR "WITH_TUTORIAL_WEBSITE requires the Python 'jinja2', 'markupsafe', and 'pygments' packages for the "
        "interpreter at ${Python3_EXECUTABLE}. Run: uv sync --group tutorial-website"
    )
endif ()

##
# Build a manifest mapping each lesson source file to its built executable,
# and (since it's the same GLOBAL property) the list of lesson source files
# that have one.
#
# Executable output paths aren't known until generate time (they depend on
# the generator and config), so this uses file(GENERATE) with
# $<TARGET_FILE:...> rather than trying to compute paths ourselves; see
# tutorial/CMakeLists.txt's _Halide_register_tutorial_website_target for
# where HALIDE_TUTORIAL_SOURCE_TARGETS is populated.
##

get_property(_tutorial_source_targets GLOBAL PROPERTY HALIDE_TUTORIAL_SOURCE_TARGETS)

set(_manifest_entries "")
set(_tutorial_targets "")
# lesson_15_generators_usage.sh is the only lesson with no executable of its
# own (it's a shell script demonstrating command-line usage, not compiled
# code), so it's the one lesson source HALIDE_TUTORIAL_SOURCE_TARGETS above
# doesn't cover.
set(_lesson_source_files "${Halide_SOURCE_DIR}/tutorial/lesson_15_generators_usage.sh")
foreach (_pair IN LISTS _tutorial_source_targets)
    string(REPLACE "=" ";" _pair "${_pair}")
    list(GET _pair 0 _source)
    list(GET _pair 1 _target)
    list(APPEND _tutorial_targets "${_target}")
    list(APPEND _manifest_entries "  \"${_source}\": \"$<TARGET_FILE:${_target}>\"")
    list(APPEND _lesson_source_files "${Halide_SOURCE_DIR}/tutorial/${_source}")
endforeach ()
list(JOIN _manifest_entries ",\n" _manifest_body)

set(_manifest_file "${CMAKE_CURRENT_BINARY_DIR}/manifest.json")
file(GENERATE OUTPUT "${_manifest_file}" CONTENT "{\n${_manifest_body}\n}\n")

set(_debugger_args "")
if (GDB_EXECUTABLE)
    list(APPEND _debugger_args --gdb "${GDB_EXECUTABLE}")
endif ()
if (LLDB_EXECUTABLE)
    list(APPEND _debugger_args --lldb "${LLDB_EXECUTABLE}")
endif ()

string(TOLOWER "${Halide_TUTORIAL_WEBSITE_DEBUGGER}" _debugger_preference)

set(_output_dir "${CMAKE_CURRENT_BINARY_DIR}/site")

##
# WITH_TUTORIAL_WEBSITE DEPENDS on WITH_PYTHON_BINDINGS (see the top-level
# CMakeLists.txt), so Halide::Python is always available here: tell the
# generator where it can `import halide`, so it can capture output from the
# Python lessons (lessons 1-14 only) the same way it captures C++ output via
# gdb/lldb.
#
# python_bindings/CMakeLists.txt does its own find_package(Python ...) and
# defines Python::Interpreter, but that's scoped to its own directory
# subtree (tutorial/ is a sibling of python_bindings/, added separately from
# the top-level CMakeLists.txt, so it can't see that target) -- and isn't
# necessarily the same interpreter anyway if this Python3 lookup resolved
# differently. Reusing Python3_EXECUTABLE (found above, required already for
# jinja2/pygments) instead relies on the same assumption as that lookup: a
# single project venv is active for the whole configure, so it's also the
# interpreter `halide` was built against.
#
# The PYTHONPATH entry mirrors python_bindings/cmake/AddPythonTest.cmake's
# add_python_test() exactly, since that's what makes `import halide` work
# for the Python bindings' own tests.
##

set(_python_manifest_file "${CMAKE_CURRENT_BINARY_DIR}/python_manifest.json")
file(GENERATE
    OUTPUT "${_python_manifest_file}"
    CONTENT "{\n  \"python_executable\": \"${Python3_EXECUTABLE}\",\n  \"pythonpath\": [\"$<PATH:NORMAL_PATH,$<TARGET_FILE_DIR:Halide::Python>/..>\"]\n}\n"
)

set(_generator_script "${CMAKE_CURRENT_SOURCE_DIR}/generate_tutorial_website.py")

# Every rendering process (sitemap/assets/each lesson) imports the same
# package, so a change to any of its source files must invalidate every
# output that could depend on it, not just whichever lesson it looks
# specific to. This repo's CMake style checks disallow file(GLOB ...) (it
# interacts poorly with incremental builds), so -- like the lesson list
# above -- this is listed explicitly; unlike the lesson list, there's no
# existing registration mechanism to derive it from, since these aren't
# CMake targets.
set(_tutorial_website_pkg_files
    "${_generator_script}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tutorial_website/__init__.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tutorial_website/capture.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tutorial_website/cli.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tutorial_website/highlight.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tutorial_website/lesson.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tutorial_website/manifest.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tutorial_website/py_capture_runner.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tutorial_website/render.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tutorial_website/sitemap.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tutorial_website/assets/lang-switch.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/tutorial_website/assets/style.css"
    "${CMAKE_CURRENT_SOURCE_DIR}/tutorial_website/templates/base.html.j2"
    "${CMAKE_CURRENT_SOURCE_DIR}/tutorial_website/templates/blocks.html.j2"
    "${CMAKE_CURRENT_SOURCE_DIR}/tutorial_website/templates/code_lesson.html.j2"
    "${CMAKE_CURRENT_SOURCE_DIR}/tutorial_website/templates/index.html.j2"
)

##
# One custom command per lesson (fanned out below) lets ninja build lesson
# pages in parallel and skip ones whose own inputs haven't changed, instead
# of the previous single always-rerun target that recomputed the whole site
# (all lessons' captured output, following gdb/lldb around each binary) on
# every build regardless of what changed.
##

##
# sitemap.json: cheap {number, slug, title, has_python} metadata for every
# lesson, shared by every page's navigation sidebar. Kept separate from each
# lesson's own render so that rendering lesson N doesn't require rescanning
# every *other* lesson's source file for its title -- but every lesson page
# still needs to be rebuilt if a title changes (or a lesson is added or
# removed), hence ${_lesson_source_files} below.
##

set(_sitemap_file "${CMAKE_CURRENT_BINARY_DIR}/sitemap.json")
add_custom_command(
    OUTPUT "${_sitemap_file}"
    COMMAND
        "${Python3_EXECUTABLE}" "${_generator_script}" sitemap
        --tutorial-dir "${Halide_SOURCE_DIR}/tutorial"
        --output "${_sitemap_file}"
    DEPENDS ${_lesson_source_files} ${_tutorial_website_pkg_files}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Building tutorial website sitemap"
    VERBATIM
)

##
# Shared assets (style.css, lang-switch.js, figures/) plus index.html. Also
# its own rule, separate from any lesson: unlike lesson pages, these aren't
# safe to produce from several concurrent per-lesson render processes, since
# concurrent writes/copies into the same shared output paths would race.
#
# tutorial/figures isn't tracked as a DEPENDS here (also to avoid
# file(GLOB ...)): the previous always-rerun target never tracked it either,
# so this isn't a regression, and figures change rarely enough that a full
# reconfigure when one does is an acceptable way to pick it up.
##

add_custom_command(
    OUTPUT
        "${_output_dir}/index.html"
        "${_output_dir}/style.css"
        "${_output_dir}/lang-switch.js"
    COMMAND
        "${Python3_EXECUTABLE}" "${_generator_script}" assets
        --tutorial-dir "${Halide_SOURCE_DIR}/tutorial"
        --sitemap "${_sitemap_file}"
        --output-dir "${_output_dir}"
        --python-manifest "${_python_manifest_file}"
    DEPENDS
        "${_sitemap_file}"
        ${_tutorial_website_pkg_files}
        Halide::Python
        "${_python_manifest_file}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Generating Halide tutorials website assets"
    VERBATIM
)

set(_lesson_htmls "")
foreach (_source_file IN LISTS _lesson_source_files)
    cmake_path(GET _source_file STEM _slug)
    cmake_path(GET _source_file FILENAME _source_name)

    set(_lesson_depends
        "${_source_file}" "${_sitemap_file}" "${_manifest_file}" ${_tutorial_website_pkg_files}
    )

    # Not every lesson has a compiled binary (e.g. lesson_15_generators_usage.sh
    # is illustrative shell only, with no registered target) -- only add a
    # target dependency where _Halide_register_tutorial_website_target
    # actually registered one for this source file.
    #
    # Note: because these targets live in tutorial/CMakeLists.txt -- a
    # different directory than this one -- CMake's Ninja generator adds an
    # order-only dependency on *every* target in that directory (all ~27
    # lesson binaries), not just this one, to preserve legacy cross-directory
    # ordering guarantees. That's unavoidable short of moving this logic into
    # tutorial/CMakeLists.txt itself, and isn't a regression: the previous
    # single monolithic target depended on every lesson binary directly
    # anyway. What this loop still buys is real: once all binaries exist,
    # each lesson's (often multi-second) capture-and-render step becomes its
    # own ninja job instead of one long serial pass over every lesson.
    foreach (_pair IN LISTS _tutorial_source_targets)
        string(REPLACE "=" ";" _pair "${_pair}")
        list(GET _pair 0 _pair_source)
        if (_pair_source STREQUAL _source_name)
            list(GET _pair 1 _pair_target)
            list(APPEND _lesson_depends "${_pair_target}")
        endif ()
    endforeach ()

    # Mirrors lesson.py's _python_source(): a same-stem file under
    # python_bindings/tutorial/ is that lesson's Python translation.
    set(_python_source "${Halide_SOURCE_DIR}/python_bindings/tutorial/${_slug}.py")
    if (EXISTS "${_python_source}")
        list(APPEND _lesson_depends "${_python_source}")
    endif ()

    set(_lesson_html "${_output_dir}/${_slug}.html")
    add_custom_command(
        OUTPUT "${_lesson_html}"
        COMMAND
            "${Python3_EXECUTABLE}" "${_generator_script}" lesson
            --slug "${_slug}"
            --manifest "${_manifest_file}"
            --tutorial-dir "${Halide_SOURCE_DIR}/tutorial"
            --sitemap "${_sitemap_file}"
            --run-cwd "${CMAKE_CURRENT_BINARY_DIR}/.."
            --output-dir "${_output_dir}"
            --prefer "${_debugger_preference}"
            ${_debugger_args}
            --python-manifest "${_python_manifest_file}"
        DEPENDS ${_lesson_depends} Halide::Python "${_python_manifest_file}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Generating tutorial page: ${_slug}.html"
        VERBATIM
    )
    list(APPEND _lesson_htmls "${_lesson_html}")
endforeach ()

add_custom_target(tutorial_website ALL DEPENDS "${_output_dir}/index.html" ${_lesson_htmls})
