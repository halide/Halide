add_executable(build_halide_h build_halide_h.cpp)
add_executable(HalideHost::build_halide_h ALIAS build_halide_h)
target_compile_options(build_halide_h PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/wd4996>)

add_executable(binary2cpp binary2cpp.cpp)
add_executable(HalideHost::binary2cpp ALIAS binary2cpp)
target_compile_options(binary2cpp PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/wd4996>)

# Use of ${CMAKE_BINARY_DIR} here is intentional; exported targets should
# be available by using the top-level build directory as <Pkg>_ROOT.
export(TARGETS build_halide_h binary2cpp
       NAMESPACE HalideHost::
       FILE "${CMAKE_BINARY_DIR}/share/HalideHost/HalideHost-exports.cmake")

# This is a horrible hack around a series of bad behaviors:
#
# 1. find_package(LLVM) in CMake can only load one set of targets, but in
#    the cross-compiling scenario, we need both LLVM host tools and target
#    libraries.
# 2. The LLVM CMake package is horribly broken generally, and also does not
#    support loading just the tools (ie. executables) as a separate package.
# 3. The export() command above chokes on IMPORTED targets. The developers
#    generally insist that exporting an imported target is ill-defined, but
#    I do not see why that is the case.
#
# Thus, there are two options:
#
# 1. We could use `find_program` for llvm-as and clang. However, when we find
#    LLVM in the Halide host build, which is required for the Halide target
#    build, we get a perfectly good llvm-as target, anyway. Same deal for clang.
#    Also, find_program would require a bunch of hard-coded HINTS to keep things
#    consistent and would require our users to give paths to those tools manually.
# 2. We re-create the llvm-as and clang targets in our own namespace and then
#    import the targets that export _can_ handle (see 3 above) here. The downside
#    here is that the target Halide build will necessarily use the same LLVM host
#    tools as used by the host build. So there's a potential for conflicts where
#    someone wants to build host Halide with LLVM 12, say, and target Halide with
#    LLVM 13. Not supporting this seems totally fine.
#
# This is option 2.
file(GENERATE
     OUTPUT "${CMAKE_BINARY_DIR}/share/HalideHost/HalideHostConfig.cmake"
     CONTENT [[
include(${CMAKE_CURRENT_LIST_DIR}/HalideHost-exports.cmake)

add_executable(HalideHost::llvm-as IMPORTED)
set_property(TARGET HalideHost::llvm-as PROPERTY IMPORTED_LOCATION "$<TARGET_FILE:llvm-as>")

add_executable(HalideHost::clang IMPORTED)
set_property(TARGET HalideHost::clang PROPERTY IMPORTED_LOCATION "$<TARGET_FILE:clang>")
]])
