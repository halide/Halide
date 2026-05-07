# Contributing CMake code to Halide

This document specifies the coding standards we adhere to when authoring new
CMake code. If you need directions for building Halide, see
[BuildingHalideWithCMake.md]. If you are looking for Halide's CMake package
documentation, see [HalideCMakePackage.md].

This document is necessary for two major reasons. First, due to its long
history, size, and dedication to backwards compatibility, CMake is _incredibly_
difficult to learn and full of traps. Second, Halide bundles its own LLVM-based
native code generator, which CMake deeply does not expect. This means we
routinely push CMake's build model to its limit.

Therefore, we must be careful to write high-quality CMake code so that it is
clear when CMake's limitations are being tested. While not comprehensive, the
guide outlines the code quality expectations we have as they apply to CMake.

When contributing new CMake code to Halide, keep in mind that the minimum
version is 3.28. Therefore, it is not only possible, but _required_, to use
modern CMake best practices.

<!-- TOC -->

- [Contributing CMake code to Halide](#contributing-cmake-code-to-halide)
- [General guidelines and best practices](#general-guidelines-and-best-practices)
  - [Prohibited modules list](#prohibited-modules-list)
    - [FetchContent](#fetchcontent)
  - [Prohibited commands list](#prohibited-commands-list)
  - [Prohibited variables list](#prohibited-variables-list)
- [Adding tests](#adding-tests)
- [Adding apps](#adding-apps)

<!-- TOC -->

# General guidelines and best practices

The following are some common mistakes that lead to subtly broken builds.

- **Reading the build directory.** While setting up the build, the build
  directory should be considered _write only_. Using the build directory as a
  read/write temporary directory is acceptable as long as all temp files are
  cleaned up by the end of configuration.
- **Not using [generator expressions][cmake-genex].** Declarative is better than
  imperative and this is no exception. Conditionally adding to a target property
  can leak unwanted details about the build environment into packages. Some
  information is not accurate or available except via generator expressions,
  e.g. the build configuration.
- **Using the wrong variable.** `CMAKE_SOURCE_DIR` doesn't always point to the
  Halide source root. When someone uses Halide via
  [`FetchContent`][fetchcontent], it will point to _their_ source root instead.
  The correct variable is [`Halide_SOURCE_DIR`][project-name_source_dir]. If you
  want to know if the compiler is MSVC, check it directly with the
  [`MSVC`][msvc] variable; don't use [`WIN32`][win32]. That will be wrong when
  compiling with clang on Windows. In most cases, however, a generator
  expression will be more appropriate.
- **Using directory properties.** Directory properties have vexing behavior and
  are essentially deprecated from CMake 3.0+. Propagating target properties is
  the way of the future.
- **Using the wrong visibility.** Target properties can be `PRIVATE`,
  `INTERFACE`, or both (aka `PUBLIC`). Pick the most conservative one for each
  scenario. Refer to the [transitive usage requirements][cmake-propagation] docs
  for more information.
- **Needlessly expanding variables** The [`if`][cmake_if] and
  [`foreach`][cmake_foreach] commands generally expand variables when provided
  by name. Expanding such variables manually can unintentionally change the
  behavior of the command. Use `foreach (item IN LISTS list)` instead of
  `foreach (item ${list})`. Similarly, use `if (varA STREQUAL varB)` instead of
  `if ("${varA}" STREQUAL "${varB}")` and _definitely_ don't use
  `if (${varA} STREQUAL ${varB})` since that will fail (in the best case) if
  either variable's value contains a semicolon (due to argument expansion).

## Prohibited modules list

All deprecated, legacy, and "miscellaneous" (internal) modules are prohibited.
The list of these may be found in the upstream documentation:
https://cmake.org/cmake/help/latest/manual/cmake-modules.7.html#deprecated-modules

### FetchContent

At the moment, only one supported module is prohibited: `FetchContent`. There
are many reasons to avoid its use:

01. It brings third-party CMake code into the build, which can cause all sorts
    of issues. In the common case, third-party projects hard-code incompatible
    build settings, which are tricky to work around in CMake. In the worst case,
    they can set cache variables or directory properties that break the
    including project's build. Worse still, those cache variables persist in
    `CMakeCache.txt` even after the dependency is removed or replaced, so the
    only reliable fix is a clean reconfigure. Because configuration is expected
    to be idempotent, these failures can be difficult to diagnose.
02. It is a poor fit for cross-compilation scenarios that require separate host
    and target artifacts. FetchContent inlines the dependency's project into the
    including build, so it is configured with the same toolchain as the rest of
    that build. For instance, a project may need both the flatbuffers compiler
    for the host system and the flatbuffers library for the target system. This
    scenario is not supported by FetchContent's population model.
03. It performs network access at configure time. This makes air-gapped and
    offline builds awkward. `FETCHCONTENT_FULLY_DISCONNECTED=ON` only works
    after a successful first configure and adds latency to every fresh
    configure. Source pinning is also weak: only commit SHAs are truly
    immutable, branch and tag refs can be moved server-side, and `URL_HASH` is
    opt-in. Package managers like vcpkg require hash-pinned archives by default
    and produce a baseline that can be locked.
04. It does not maintain a persistent source or binary cache outside the build
    tree. Populated sources and build products live under the build directory by
    default, so deleting the build directory also deletes them. A fresh build
    can therefore require another download and rebuild of dependencies, rather
    than just rebuilding the top-level project. This also couples dependency
    iteration to the parent project: tweaking a dependency's options forces a
    parent reconfigure, and the dependency cannot be built or tested in
    isolation.
05. The above issues exacerbate diamond dependency problems. Even if a
    consistent version happens to be chosen, different intermediate dependencies
    along each branch might impose incompatible build settings. For instance,
    one project might try to enable an optional feature while another project
    disables it.
06. Applying local fixes to dependencies is awkward. `FetchContent_Declare`'s
    `PATCH_COMMAND` runs an arbitrary shell snippet that is hard to review,
    version, or attribute. vcpkg ports keep patches as versioned `.patch` files
    alongside the portfile, so they appear in code review and survive upstream
    version bumps cleanly.
07. It pollutes the cache and target environment, even when steps are taken to
    exclude test and utility targets. This clutters both graphical IDE
    interfaces and the diagnostic output of build tools like Ninja (e.g. its
    dependency graph and build profiler).
08. Targets created by FetchContent are considered _first-party_ targets,
    meaning that special care must be taken when writing installation and
    packaging rules. This complexity compounds when simultaneously supporting
    other dependency resolution mechanisms that create third-party (i.e.
    `IMPORTED`) targets.
09. It produces no provenance, license, or SBOM metadata. Package managers like
    vcpkg and Conan emit machine-readable manifests of versions, licenses, and
    source hashes that compliance tooling can consume. FetchContent emits
    nothing, so every audit becomes a manual exercise.
10. FetchContent requests can be intercepted by a Dependency Provider which can
    only be chosen by the top-level project. That means code that appears to
    vendor a specific source tree can instead be redirected to some other
    dependency resolution mechanism, such as a package manager. This makes the
    resulting targets and build settings less predictable, and it compounds the
    first-party versus imported-target packaging issues described above.

After broader approval, third-party dependencies must be consumed with
`find_package`. This also lets packagers and distributors substitute a system or
pre-built copy, rather than forcing every downstream to rebuild dependencies
from source. We use vcpkg in CI to manage our dependencies. If vcpkg lacks a
port, you must write a custom port in `cmake/vcpkg` (for the main Halide build)
or `apps/vcpkg/ports` (for the apps).

## Prohibited commands list

As mentioned above, using directory properties is brittle, and they are
therefore _not allowed_. The following functions may not appear in any new CMake
code.

| Command                             | Alternative                                                                                        |
| ----------------------------------- | -------------------------------------------------------------------------------------------------- |
| `add_compile_definitions`           | Use [`target_compile_definitions`][target_compile_definitions]                                     |
| `add_compile_options`               | Use [`target_compile_options`][target_compile_options]                                             |
| `add_definitions`                   | Use [`target_compile_definitions`][target_compile_definitions]                                     |
| `add_link_options`                  | Use [`target_link_options`][target_link_options], but prefer not to use either                     |
| `include_directories`               | Use [`target_include_directories`][target_include_directories]                                     |
| `link_directories`                  | Use [`target_link_libraries`][target_link_libraries]                                               |
| `link_libraries`                    | Use [`target_link_libraries`][target_link_libraries]                                               |
| `remove_definitions`                | [Generator expressions][cmake-genex] in [`target_compile_definitions`][target_compile_definitions] |
| `set_directory_properties`          | Use (cache) variables or target properties                                                         |
| `set_property(DIRECTORY)`           | Use (cache) variables or target properties (custom properties excluded, but require justification) |
| `target_link_libraries(target lib)` | Use [`target_link_libraries`][target_link_libraries] _with a visibility specifier_ (eg. `PRIVATE`) |

As an example, it was once common practice to write code similar to this:

```cmake
# WRONG: do not do this
include_directories(include)
add_library(my_lib source1.cpp ..)
```

However, this has two major pitfalls. First, it applies to _all_ targets created
in that directory, even those before the call to `include_directories` and those
created in [`include()`][include]-ed CMake files. As CMake files get larger and
more complex, this behavior gets harder to pinpoint. This is particularly vexing
when using the `link_libraries` or `add_definitions` commands. Second, this form
does not provide a way to _propagate_ the include directory to consumers of
`my_lib`. The correct way to do this is:

```cmake
# CORRECT
add_library(my_lib source1.cpp ...)
target_sources(
    my_lib
    PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES include/header1.h
)
```

This is better in many ways. It only affects the target in question. It
propagates the include path to the targets linking to it (via `PUBLIC`). It also
correctly exports the host-filesystem-specific include path when installing or
packaging the target and installs the headers themselves, too.

If common properties need to be grouped together, use an INTERFACE target
(better) or write a function (worse).

There are also several functions that are disallowed for other reasons:

| Command                         | Reason                                                      | Alternative                                                                    |
| ------------------------------- | ----------------------------------------------------------- | ------------------------------------------------------------------------------ |
| `aux_source_directory`          | Interacts poorly with incremental builds and Git            | List source files explicitly                                                   |
| `build_command`                 | CTest internal function                                     | Use CTest build-and-test mode via [`CMAKE_CTEST_COMMAND`][cmake_ctest_command] |
| `cmake_host_system_information` | Usually misleading information.                             | Inspect [toolchain][cmake-toolchains] variables and use generator expressions. |
| `cmake_policy(... OLD)`         | OLD policies are deprecated by definition.                  | Instead, fix the code to work with the new policy.                             |
| `create_test_sourcelist`        | We use our own unit testing solution                        | See the [adding tests](#adding-tests) section.                                 |
| `define_property`               | Adds unnecessary complexity                                 | Use a cache variable. Exceptions under special circumstances.                  |
| `enable_language`               | Halide is C/C++ only                                        | [`FindCUDAToolkit`][findcudatoolkit], appropriately guarded.                   |
| `file(GLOB ...)`                | Interacts poorly with incremental builds and Git            | List source files explicitly. Allowed if not globbing for source files.        |
| `fltk_wrap_ui`                  | Halide does not use FLTK                                    | None                                                                           |
| `include_external_msproject`    | Halide must remain portable                                 | Write a CMake package config file or find module.                              |
| `include_guard`                 | Use of recursive inclusion is not allowed                   | Write (recursive) functions.                                                   |
| `include_regular_expression`    | Changes default dependency checking behavior                | None                                                                           |
| `load_cache`                    | Superseded by [`ExternalProject`][externalproject]          | Write a vcpkg port or present a case for an exception.                         |
| `macro`                         | CMake macros are not hygienic and are therefore error-prone | Use functions instead.                                                         |
| `site_name`                     | Privacy: do not want leak host name information             | Provide a cache variable, generate a unique name.                              |
| `variable_watch`                | Debugging helper                                            | None. Not needed in production.                                                |

Do not introduce new dependencies without broader approval. Once approved, add
dependencies to `vcpkg.json` or create a custom port, and consume them with
[`find_package`][find_package] rather than `FetchContent`.

## Prohibited variables list

Any variables that are specific to languages that are not enabled should, of
course, be avoided. But of greater concern are variables that are easy to misuse
or should not be overridden for our end-users. The following (non-exhaustive)
list of variables shall not be used in code merged into main.

| Variable                        | Reason                                        | Alternative                                                                                             |
| ------------------------------- | --------------------------------------------- | ------------------------------------------------------------------------------------------------------- |
| `CMAKE_ROOT`                    | Code smell                                    | Rely on `find_package` search options; include `HINTS` if necessary                                     |
| `CMAKE_DEBUG_TARGET_PROPERTIES` | Debugging helper                              | None                                                                                                    |
| `CMAKE_FIND_DEBUG_MODE`         | Debugging helper                              | None                                                                                                    |
| `CMAKE_RULE_MESSAGES`           | Debugging helper                              | None                                                                                                    |
| `CMAKE_VERBOSE_MAKEFILE`        | Debugging helper                              | None                                                                                                    |
| `CMAKE_BACKWARDS_COMPATIBILITY` | Deprecated                                    | None                                                                                                    |
| `CMAKE_BUILD_TOOL`              | Deprecated                                    | `${CMAKE_COMMAND} --build` or [`CMAKE_MAKE_PROGRAM`][cmake_make_program] (but see below)                |
| `CMAKE_CACHEFILE_DIR`           | Deprecated                                    | [`CMAKE_BINARY_DIR`][cmake_binary_dir], but see below                                                   |
| `CMAKE_CFG_INTDIR`              | Deprecated                                    | `$<CONFIG>`, `$<TARGET_FILE:..>`, target resolution of [`add_custom_command`][add_custom_command], etc. |
| `CMAKE_CL_64`                   | Deprecated                                    | [`CMAKE_SIZEOF_VOID_P`][cmake_sizeof_void_p]                                                            |
| `CMAKE_COMPILER_IS_*`           | Deprecated                                    | [`CMAKE_<LANG>_COMPILER_ID`][cmake_lang_compiler_id]                                                    |
| `CMAKE_HOME_DIRECTORY`          | Deprecated                                    | [`CMAKE_SOURCE_DIR`][cmake_source_dir], but see below                                                   |
| `CMAKE_DIRECTORY_LABELS`        | Directory property                            | None                                                                                                    |
| `CMAKE_BUILD_TYPE`              | Only applies to single-config generators.     | `$<CONFIG>`                                                                                             |
| `CMAKE_*_FLAGS*` (w/o `_INIT`)  | User-only                                     | Write a [toolchain][cmake-toolchains] file with the corresponding `_INIT` variable                      |
| `CMAKE_COLOR_MAKEFILE`          | User-only                                     | None                                                                                                    |
| `CMAKE_ERROR_DEPRECATED`        | User-only                                     | None                                                                                                    |
| `CMAKE_CONFIGURATION_TYPES`     | We only support the four standard build types | None                                                                                                    |

Of course feel free to insert debugging helpers _while developing_ but please
remove them before review. Finally, the following variables are allowed, but
their use must be motivated:

| Variable                                       | Reason                                              | Alternative                                                                                  |
| ---------------------------------------------- | --------------------------------------------------- | -------------------------------------------------------------------------------------------- |
| [`CMAKE_SOURCE_DIR`][cmake_source_dir]         | Points to global source root, not Halide's.         | [`Halide_SOURCE_DIR`][project-name_source_dir] or [`PROJECT_SOURCE_DIR`][project_source_dir] |
| [`CMAKE_BINARY_DIR`][cmake_binary_dir]         | Points to global build root, not Halide's           | [`Halide_BINARY_DIR`][project-name_binary_dir] or [`PROJECT_BINARY_DIR`][project_binary_dir] |
| [`CMAKE_MAKE_PROGRAM`][cmake_make_program]     | CMake abstracts over differences in the build tool. | Prefer CTest's build and test mode or CMake's `--build` mode                                 |
| [`CMAKE_CROSSCOMPILING`][cmake_crosscompiling] | Often misleading.                                   | Inspect relevant variables directly, eg. [`CMAKE_SYSTEM_NAME`][cmake_system_name]            |
| [`BUILD_SHARED_LIBS`][build_shared_libs]       | Could override user setting                         | None, but be careful to restore value when overriding for a dependency                       |

Any use of these functions or variables will block a PR.

# Adding tests

When adding a file to any of the folders under `test`, be aware that CI expects
that every `.c` and `.cpp` appears in the `CMakeLists.txt` file _on its own
line_, possibly as a comment. This is to avoid globbing and also to ensure that
added files are not missed.

For most test types, it should be as simple as adding to the existing lists.
Generator tests are trickier, but following the existing examples is a safe way
to go.

# Adding apps

If you're contributing a new app to Halide: great! Thank you! There are a few
guidelines you should follow when writing a new app.

- Write the app as if it were a top-level project. You should call
  `find_package(Halide)` and set the C++ version to 11.
- Call [`enable_testing()`][enable_testing] and add a small test that runs the
  app.
- Don't assume your app will have access to a GPU. Write your schedules to be
  robust to varying buildbot hardware.
- Don't assume your app will be run on a specific OS, architecture, or bitness.
  Write your apps to be robust (ideally efficient) on all supported platforms.
- If you rely on any additional packages, don't include them as `REQUIRED`,
  instead test to see if their targets are available and, if not, call
  `return()` before creating any targets. In this case, print a
  `message(STATUS "[SKIP] ...")`, too.
- Look at the existing apps for examples.
- Test your app with ctest before opening a PR. Apps are built as part of the
  test, rather than the main build.

[add_custom_command]: https://cmake.org/cmake/help/latest/command/add_custom_command.html
[buildinghalidewithcmake.md]: ./BuildingHalideWithCMake.md
[build_shared_libs]: https://cmake.org/cmake/help/latest/variable/BUILD_SHARED_LIBS.html
[cmake-genex]: https://cmake.org/cmake/help/latest/manual/cmake-generator-expressions.7.html
[cmake-propagation]: https://cmake.org/cmake/help/latest/manual/cmake-buildsystem.7.html#transitive-usage-requirements
[cmake-toolchains]: https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html
[cmake_binary_dir]: https://cmake.org/cmake/help/latest/variable/CMAKE_BINARY_DIR.html
[cmake_crosscompiling]: https://cmake.org/cmake/help/latest/variable/CMAKE_CROSSCOMPILING.html
[cmake_ctest_command]: https://cmake.org/cmake/help/latest/variable/CMAKE_CTEST_COMMAND.html
[cmake_foreach]: https://cmake.org/cmake/help/latest/command/foreach.html
[cmake_if]: https://cmake.org/cmake/help/latest/command/if.html
[cmake_lang_compiler_id]: https://cmake.org/cmake/help/latest/variable/CMAKE_LANG_COMPILER_ID.html
[cmake_make_program]: https://cmake.org/cmake/help/latest/variable/CMAKE_MAKE_PROGRAM.html
[cmake_sizeof_void_p]: https://cmake.org/cmake/help/latest/variable/CMAKE_SIZEOF_VOID_P.html
[cmake_source_dir]: https://cmake.org/cmake/help/latest/variable/CMAKE_SOURCE_DIR.html
[cmake_system_name]: https://cmake.org/cmake/help/latest/variable/CMAKE_SYSTEM_NAME.html
[enable_testing]: https://cmake.org/cmake/help/latest/command/enable_testing.html
[externalproject]: https://cmake.org/cmake/help/latest/module/ExternalProject.html
[fetchcontent]: https://cmake.org/cmake/help/latest/module/FetchContent.html
[findcudatoolkit]: https://cmake.org/cmake/help/latest/module/FindCUDAToolkit.html
[find_package]: https://cmake.org/cmake/help/latest/command/find_package.html
[halidecmakepackage.md]: ./HalideCMakePackage.md
[include]: https://cmake.org/cmake/help/latest/command/include.html
[msvc]: https://cmake.org/cmake/help/latest/variable/MSVC.html
[project-name_binary_dir]: https://cmake.org/cmake/help/latest/variable/PROJECT-NAME_BINARY_DIR.html
[project-name_source_dir]: https://cmake.org/cmake/help/latest/variable/PROJECT-NAME_SOURCE_DIR.html
[project_binary_dir]: https://cmake.org/cmake/help/latest/variable/PROJECT_BINARY_DIR.html
[project_source_dir]: https://cmake.org/cmake/help/latest/variable/PROJECT_SOURCE_DIR.html
[target_compile_definitions]: https://cmake.org/cmake/help/latest/command/target_compile_definitions.html
[target_compile_options]: https://cmake.org/cmake/help/latest/command/target_compile_options.html
[target_include_directories]: https://cmake.org/cmake/help/latest/command/target_include_directories.html
[target_link_libraries]: https://cmake.org/cmake/help/latest/command/target_link_libraries.html
[target_link_options]: https://cmake.org/cmake/help/latest/command/target_link_options.html
[win32]: https://cmake.org/cmake/help/latest/variable/WIN32.html
