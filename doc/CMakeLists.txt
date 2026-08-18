find_package(Doxygen REQUIRED doxygen)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

find_program(SPHINX_BUILD_EXECUTABLE sphinx-build REQUIRED)

execute_process(
    COMMAND
        "${Python3_EXECUTABLE}"
        -c "import myst_parser, breathe, pydata_sphinx_theme, sphinxcontrib.video, sphinx_design"
    RESULT_VARIABLE _docs_python_deps_missing
    OUTPUT_QUIET ERROR_QUIET
)
if (_docs_python_deps_missing)
    message(
        FATAL_ERROR "WITH_DOCS requires the Python 'myst-parser', 'breathe', 'pydata-sphinx-theme', "
        "'sphinxcontrib-video', and 'sphinx-design' packages for the interpreter at ${Python3_EXECUTABLE}. "
        "Run: uv sync --group docs"
    )
endif ()

##
# Doxygen only emits XML here -- it's Breathe's data source for the API
# reference, not Halide's rendered documentation output any more (Sphinx is,
# via the `doc` target below).
##

set(DOXYGEN_ALPHABETICAL_INDEX NO)
set(DOXYGEN_BUILTIN_STL_SUPPORT YES)
set(DOXYGEN_CASE_SENSE_NAMES NO)
set(DOXYGEN_CLASS_DIAGRAMS NO)
set(DOXYGEN_DISTRIBUTE_GROUP_DOC YES)
set(DOXYGEN_EXAMPLE_PATH "${Halide_SOURCE_DIR}/tutorial")
# mini_qurt.h vendors Qualcomm's own QuRT SDK doc-comment conventions
# (@datatypes, \ref to static functions) that don't match our Doxygen
# config and never resolve to anything user-facing anyway (it's Hexagon
# runtime-internal, entirely outside the Halide:: namespace this reference
# documents) -- excluded rather than reworking vendored comments to fit.
set(DOXYGEN_EXCLUDE bin "${Halide_SOURCE_DIR}/src/runtime/mini_qurt.h")
set(DOXYGEN_EXTRACT_ALL YES)
set(DOXYGEN_EXTRACT_LOCAL_CLASSES NO)
set(DOXYGEN_FILE_PATTERNS *.h)
set(DOXYGEN_GENERATE_HTML NO)
set(DOXYGEN_GENERATE_TREEVIEW YES)
set(DOXYGEN_GENERATE_XML YES)
set(DOXYGEN_HIDE_FRIEND_COMPOUNDS YES)
set(DOXYGEN_HIDE_IN_BODY_DOCS YES)
set(DOXYGEN_HIDE_UNDOC_CLASSES YES)
set(DOXYGEN_HIDE_UNDOC_MEMBERS YES)
set(DOXYGEN_JAVADOC_AUTOBRIEF YES)
# Doxygen's own preprocessor doesn't expand macros by default, so
# HALIDE_ALWAYS_INLINE/HALIDE_EXPORT/etc. (Util.h, HalideRuntime.h) show up
# verbatim in the XML as an unrecognized leading token on the declaration --
# Breathe's C++ parser (which re-parses that text itself, separately from
# Doxygen) then fails on it, since it's not a real keyword or attribute
# syntax. EXPAND_ONLY_PREDEF limits expansion to just the macros listed
# here, substituted with their POSIX/GCC-style expansion (the one Doxygen's
# own preprocessor would otherwise pick anyway, since _MSC_VER is never
# defined during a doc build) so Breathe sees an ordinary declaration.
set(DOXYGEN_MACRO_EXPANSION YES)
set(DOXYGEN_EXPAND_ONLY_PREDEF YES)
set(DOXYGEN_PREDEFINED
    "HALIDE_ALWAYS_INLINE=inline"
    "HALIDE_NEVER_INLINE="
    "HALIDE_NO_USER_CODE_INLINE="
    "HALIDE_EXPORT="
    "HALIDE_EXPORT_SYMBOL="
    "HALIDE_MUST_USE_RESULT="
    "HALIDE_FUNCTION_ATTRS="
    "HALIDE_ATTRIBUTE_ALIGN(x)="
    "HALIDE_ATTRIBUTE_DEPRECATED(x)="
)
set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/doxygen")
set(DOXYGEN_QT_AUTOBRIEF YES)
set(DOXYGEN_QUIET YES)
set(DOXYGEN_RECURSIVE YES)
set(DOXYGEN_REFERENCED_BY_RELATION YES)
set(DOXYGEN_REFERENCES_RELATION YES)
set(DOXYGEN_SORT_BY_SCOPE_NAME YES)
set(DOXYGEN_SORT_MEMBER_DOCS NO)
set(DOXYGEN_SOURCE_BROWSER YES)
set(DOXYGEN_STRIP_CODE_COMMENTS NO)

get_target_property(_doxygen_inputs Halide HEADER_SET_private_headers)
get_target_property(_doxygen_runtime_headers Halide_Runtime HEADER_SET_HEADERS)
list(APPEND _doxygen_inputs ${_doxygen_runtime_headers})
list(REMOVE_DUPLICATES _doxygen_inputs)
doxygen_add_docs(
    doc_doxygen ${_doxygen_inputs} USE_STAMP_FILE
    WORKING_DIRECTORY "${Halide_SOURCE_DIR}"
    COMMENT "Generate Doxygen XML for Breathe"
)

##
# Sphinx renders doc/*.md prose plus the Breathe-generated API reference into
# the final HTML site. This `doc` target (not `doc_doxygen` above) is what
# WITH_DOCS/deploy-docs.yml actually build and install.
#
# The Doxygen XML directory is passed in via `sphinx-build -D` (Breathe
# registers `breathe_projects` as a Sphinx config value, which -D can then
# override, including nested dict keys) rather than being hardcoded into
# conf.py, since the path depends on the CMake build directory.
##

set(_sphinx_output_dir "${CMAKE_CURRENT_BINARY_DIR}/sphinx/html")
set(_doxygen_xml_dir "${DOXYGEN_OUTPUT_DIRECTORY}/xml")

##
# Sphinx's source directory has to physically contain every file it renders
# (its own source discovery just scans that directory -- there's no way to
# register an out-of-tree file with it), but writing generated content
# straight into doc/ would permanently pollute the checked-out source tree.
# So conf.py/*.md are staged into a copy under the build tree, and the
# generated API reference is written directly there too, keeping doc/ itself
# untouched by any `doc` target build.
##

set(_sphinx_src_dir "${CMAKE_CURRENT_BINARY_DIR}/src")

# The hand-authored files that get staged verbatim into _sphinx_src_dir.
set(_sphinx_static_files
    "${CMAKE_CURRENT_SOURCE_DIR}/conf.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/index.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/guides.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/BuildingHalideWithCMake.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/CodeStyleCMake.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/CustomRuntimes.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/FuzzTesting.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/GeneratorCache.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/HalideCMakePackage.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/Hexagon.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/Python.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/RunGen.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/Testing.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/Vulkan.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/WebAssembly.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/WebGPU.md"
)

set(_staged_static_files "")
foreach (_src_file IN LISTS _sphinx_static_files)
    cmake_path(GET _src_file FILENAME _file_name)
    list(APPEND _staged_static_files "${_sphinx_src_dir}/${_file_name}")
endforeach ()

add_custom_command(
    OUTPUT ${_staged_static_files}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_sphinx_src_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${_sphinx_static_files} "${_sphinx_src_dir}/"
    DEPENDS ${_sphinx_static_files}
    COMMENT "Staging Sphinx source files into the build tree"
    VERBATIM
)

# doc/_static/*.css (referenced from conf.py's html_css_files) and
# doc/api/index.md (the hand-authored API Reference landing page, which
# toctrees to generate_api_reference.py's generated listing below) each have
# to be staged into their own subdirectory rather than flattened alongside
# the root-level files above.
set(_sphinx_static_css_files "${CMAKE_CURRENT_SOURCE_DIR}/_static/custom.css")
set(_staged_static_css_files "${_sphinx_src_dir}/_static/custom.css")

add_custom_command(
    OUTPUT ${_staged_static_css_files}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_sphinx_src_dir}/_static"
    COMMAND
        "${CMAKE_COMMAND}" -E copy_if_different ${_sphinx_static_css_files}
        "${_sphinx_src_dir}/_static/"
    DEPENDS ${_sphinx_static_css_files}
    COMMENT "Staging Sphinx static assets into the build tree"
    VERBATIM
)

set(_sphinx_static_api_files "${CMAKE_CURRENT_SOURCE_DIR}/api/index.md")
set(_staged_static_api_files "${_sphinx_src_dir}/api/index.md")

add_custom_command(
    OUTPUT ${_staged_static_api_files}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_sphinx_src_dir}/api"
    COMMAND
        "${CMAKE_COMMAND}" -E copy_if_different ${_sphinx_static_api_files} "${_sphinx_src_dir}/api/"
    DEPENDS ${_sphinx_static_api_files}
    COMMENT "Staging the API Reference landing page into the build tree"
    VERBATIM
)

##
# doc/api/*.md equivalent (one page per Halide:: class/struct, plus a
# reference.md toctree over them) is generated from Doxygen's XML rather than
# hand-maintained -- see generate_api_reference.py's docstring -- and written
# into api/generated/ within the staged tree above, never into doc/ itself.
# Kept in its own subdirectory (rather than directly in api/, alongside the
# hand-authored index.md staged above) so this rule's `rm -rf` can freely
# clean out stale generated pages without racing the other custom command
# that stages index.md into the same directory. Only the reference.md marker
# page is a declared OUTPUT (the per-type pages it also writes aren't known
# until the script inspects Doxygen's XML) -- the tutorial pages below use
# the same tracked-marker approach.
##

set(_api_reference_script "${CMAKE_CURRENT_SOURCE_DIR}/generate_api_reference.py")
set(_api_reference_marker "${_sphinx_src_dir}/api/generated/reference.md")

add_custom_command(
    OUTPUT "${_api_reference_marker}"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${_sphinx_src_dir}/api/generated"
    COMMAND
        "${Python3_EXECUTABLE}" "${_api_reference_script}"
        --xml-dir "${_doxygen_xml_dir}"
        --output-dir "${_sphinx_src_dir}/api/generated"
    DEPENDS doc_doxygen "${_api_reference_script}"
    COMMENT "Generating Halide API reference pages from Doxygen XML"
    VERBATIM
)

##
# Tutorial pages: one MyST page per tutorial/lesson_*.cpp (or .sh), with
# captured gdb/lldb output for interesting statements inlined, plus (where a
# same-stem file exists under python_bindings/tutorial/) the lesson's Python
# translation with its own captured output, switchable via a sphinx-design
# tab-set -- see generate_tutorial_website.py's docstring. WITH_DOCS depends
# on WITH_TUTORIALS AND WITH_PYTHON_BINDINGS (top-level CMakeLists.txt), and
# tutorial/CMakeLists.txt's _Halide_register_tutorial_website_target
# populates the HALIDE_TUTORIAL_SOURCE_TARGETS global property this reads
# (source file => built executable target) whenever WITH_DOCS is on, forcing
# -g -O0 -fno-omit-frame-pointer on those targets so single-line stepping is
# reliable. Written directly into the staged tree above, never into doc/
# itself.
##

find_program(GDB_EXECUTABLE gdb)
find_program(LLDB_EXECUTABLE lldb)
if (NOT GDB_EXECUTABLE AND NOT LLDB_EXECUTABLE)
    message(
        FATAL_ERROR "WITH_DOCS requires gdb or lldb to be installed, to capture tutorial lesson output"
    )
endif ()

set(Halide_TUTORIAL_WEBSITE_DEBUGGER "AUTO"
    CACHE STRING
    "Which debugger the tutorial page generator should use to capture output snippets: AUTO, GDB, or LLDB"
)
set_property(CACHE Halide_TUTORIAL_WEBSITE_DEBUGGER PROPERTY STRINGS AUTO GDB LLDB)

get_property(_tutorial_source_targets GLOBAL PROPERTY HALIDE_TUTORIAL_SOURCE_TARGETS)

set(_tutorial_manifest_entries "")
# lesson_15_generators_usage.sh is the only lesson with no executable of its
# own (it's a shell script demonstrating command-line usage, not compiled
# code), so it's the one lesson source HALIDE_TUTORIAL_SOURCE_TARGETS above
# doesn't cover.
set(_tutorial_source_files "${Halide_SOURCE_DIR}/tutorial/lesson_15_generators_usage.sh")
foreach (_pair IN LISTS _tutorial_source_targets)
    string(REPLACE "=" ";" _pair "${_pair}")
    list(GET _pair 0 _source)
    list(GET _pair 1 _target)
    list(APPEND _tutorial_manifest_entries "  \"${_source}\": \"$<TARGET_FILE:${_target}>\"")
    list(APPEND _tutorial_source_files "${Halide_SOURCE_DIR}/tutorial/${_source}")
endforeach ()
list(JOIN _tutorial_manifest_entries ",\n" _tutorial_manifest_body)

set(_tutorial_manifest_file "${CMAKE_CURRENT_BINARY_DIR}/tutorial_manifest.json")
file(GENERATE OUTPUT "${_tutorial_manifest_file}" CONTENT "{\n${_tutorial_manifest_body}\n}\n")

set(_tutorial_debugger_args "")
if (GDB_EXECUTABLE)
    list(APPEND _tutorial_debugger_args --gdb "${GDB_EXECUTABLE}")
endif ()
if (LLDB_EXECUTABLE)
    list(APPEND _tutorial_debugger_args --lldb "${LLDB_EXECUTABLE}")
endif ()

string(TOLOWER "${Halide_TUTORIAL_WEBSITE_DEBUGGER}" _tutorial_debugger_preference)

##
# WITH_DOCS DEPENDS on WITH_PYTHON_BINDINGS (top-level CMakeLists.txt), so
# Halide::Python is always available here: tell the generator where it can
# `import halide`, so it can capture output from the Python translation of
# each lesson the same way it captures C++ output via gdb/lldb.
#
# Reusing Python3_EXECUTABLE (found above, required already for the Sphinx
# build's own myst-parser/breathe/etc. dependencies) relies on the same
# assumption as that lookup: a single project venv is active for the whole
# configure, so it's also the interpreter `halide` was built against.
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

set(_tutorial_generator_script "${CMAKE_CURRENT_SOURCE_DIR}/generate_tutorial_website.py")
set(_tutorial_output_dir "${_sphinx_src_dir}/tutorial")

# Every rendering process (sitemap/assets/each lesson) imports the same
# package, so a change to any of its source files must invalidate every
# output that could depend on it, not just whichever lesson it looks
# specific to. This repo's CMake style checks disallow file(GLOB ...), so --
# like the lesson list above -- this is listed explicitly.
set(_tutorial_website_pkg_files
    "${_tutorial_generator_script}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tutorial_website/__init__.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tutorial_website/capture.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tutorial_website/cli.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tutorial_website/lesson.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tutorial_website/manifest.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tutorial_website/py_capture_runner.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tutorial_website/render.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tutorial_website/sitemap.py"
)

set(_tutorial_sitemap_file "${CMAKE_CURRENT_BINARY_DIR}/tutorial_sitemap.json")
add_custom_command(
    OUTPUT "${_tutorial_sitemap_file}"
    COMMAND
        "${Python3_EXECUTABLE}" "${_tutorial_generator_script}" sitemap
        --tutorial-dir "${Halide_SOURCE_DIR}/tutorial"
        --output "${_tutorial_sitemap_file}"
    DEPENDS ${_tutorial_source_files} ${_tutorial_website_pkg_files}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Building tutorial sitemap"
    VERBATIM
)

##
# Build index.md and the multi-part-lesson group pages.
##

set(_tutorial_index_file "${_tutorial_output_dir}/index.md")
add_custom_command(
    OUTPUT "${_tutorial_index_file}"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${_tutorial_output_dir}"
    COMMAND
        "${Python3_EXECUTABLE}" "${_tutorial_generator_script}" assets
        --tutorial-dir "${Halide_SOURCE_DIR}/tutorial"
        --sitemap "${_tutorial_sitemap_file}"
        --output-dir "${_tutorial_output_dir}"
        --python-manifest "${_python_manifest_file}"
    DEPENDS
        Halide::Python
        "${_tutorial_sitemap_file}"
        ${_tutorial_website_pkg_files}
        "${_python_manifest_file}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Generating tutorial index"
    VERBATIM
)

set(_tutorial_pages "")
foreach (_source_file IN LISTS _tutorial_source_files)
    cmake_path(GET _source_file STEM _slug)
    cmake_path(GET _source_file FILENAME _source_name)

    set(_tutorial_lesson_depends
        "${_source_file}" "${_tutorial_index_file}" "${_tutorial_manifest_file}" Halide::Python
        "${_python_manifest_file}" ${_tutorial_website_pkg_files}
    )

    # Mirrors lesson.py's _python_source(): a same-stem file under
    # python_bindings/tutorial/ is that lesson's Python translation.
    set(_python_source_file "${Halide_SOURCE_DIR}/python_bindings/tutorial/${_slug}.py")
    if (EXISTS "${_python_source_file}")
        list(APPEND _tutorial_lesson_depends "${_python_source_file}")
    endif ()

    # Not every lesson has a compiled binary (e.g. lesson_15_generators_usage.sh
    # is illustrative shell only, with no registered target) -- only add a
    # target dependency where _Halide_register_tutorial_website_target
    # actually registered one for this source file.
    #
    # Note: because these targets live in tutorial/CMakeLists.txt -- a
    # different directory than this one -- CMake's Ninja generator adds an
    # order-only dependency on *every* target in that directory (all ~27
    # lesson binaries), not just this one, to preserve legacy cross-directory
    # ordering guarantees. That's unavoidable short of moving lesson-binary
    # registration itself into this file, and isn't a regression: a single
    # monolithic target depended on every lesson binary directly before this
    # was split into one custom command per lesson too. What this loop still
    # buys is real: once all binaries exist, each lesson's (often multi-
    # second) capture-and-render step becomes its own ninja job instead of
    # one long serial pass over every lesson.
    foreach (_pair IN LISTS _tutorial_source_targets)
        string(REPLACE "=" ";" _pair "${_pair}")
        list(GET _pair 0 _pair_source)
        if (_pair_source STREQUAL _source_name)
            list(GET _pair 1 _pair_target)
            list(APPEND _tutorial_lesson_depends "${_pair_target}")
        endif ()
    endforeach ()

    set(_tutorial_page "${_tutorial_output_dir}/${_slug}.md")
    set(_tutorial_page_depfile "${CMAKE_CURRENT_BINARY_DIR}/tutorial_${_slug}.d")
    add_custom_command(
        OUTPUT "${_tutorial_page}"
        COMMAND
            "${Python3_EXECUTABLE}" "${_tutorial_generator_script}" lesson
            --slug "${_slug}"
            --manifest "${_tutorial_manifest_file}"
            --tutorial-dir "${Halide_SOURCE_DIR}/tutorial"
            --run-cwd "${Halide_BINARY_DIR}/tutorial"
            --output-dir "${_tutorial_output_dir}"
            --depfile "${_tutorial_page_depfile}"
            --prefer "${_tutorial_debugger_preference}"
            ${_tutorial_debugger_args}
            --python-manifest "${_python_manifest_file}"
        DEPENDS ${_tutorial_lesson_depends}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Generating tutorial page: ${_slug}.md"
        DEPFILE "${_tutorial_page_depfile}"
        VERBATIM
    )
    list(APPEND _tutorial_pages "${_tutorial_page}")
endforeach ()

add_custom_target(tutorial_website ALL DEPENDS "${_tutorial_index_file}" ${_tutorial_pages})

add_custom_command(
    OUTPUT "${_sphinx_output_dir}/index.html"
    COMMAND
        "${SPHINX_BUILD_EXECUTABLE}" -b html
        -D "breathe_projects.Halide=${_doxygen_xml_dir}"
        -d "${CMAKE_CURRENT_BINARY_DIR}/sphinx/doctrees"
        "${_sphinx_src_dir}" "${_sphinx_output_dir}"
    DEPENDS
        doc_doxygen ${_staged_static_files} ${_staged_static_css_files} ${_staged_static_api_files}
        "${_api_reference_marker}" "${_tutorial_index_file}" ${_tutorial_pages}
    COMMENT "Generating Halide documentation website (Sphinx)"
    VERBATIM
)

add_custom_target(doc ALL DEPENDS "${_sphinx_output_dir}/index.html")
