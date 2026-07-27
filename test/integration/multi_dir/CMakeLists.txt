cmake_minimum_required(VERSION 3.28)
project(multi_dir)

enable_testing()

# main.cpp lives here (rather than duplicated in each consumer) since it's
# identical for both flavors; shared_consumer and static_consumer each
# reference it via a relative path.

# Two independent find_package(Halide COMPONENTS shared/static) calls in
# sibling directories -- neither an ancestor of the other -- can each select
# their own flavor of Halide::Halide without conflict.
add_subdirectory(shared_consumer)
add_subdirectory(static_consumer)

# A subdirectory that requests a flavor conflicting with the one already
# loaded by an ancestor directory must fail cleanly instead of silently
# keeping the ancestor's flavor. Off by default; see
# multi_dir_conflict_fails_cleanly in ../CMakeLists.txt, which configures
# this same project a second time with this option set.
option(multi_dir_TEST_CONFLICT
    "Load HalideCompiler (shared) here, then probe a conflicting static request from a subdirectory"
    OFF
)
if (multi_dir_TEST_CONFLICT)
    find_package(Halide REQUIRED COMPONENTS shared JIT)
    add_subdirectory(conflicting_consumer)
endif ()
