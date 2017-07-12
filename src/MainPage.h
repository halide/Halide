/** \file
 * This file only exists to contain the front-page of the documentation
 */

/** \mainpage Halide
 *
 * Halide is a programming language designed to make it easier to
 * write high-performance image processing code on modern
 * machines. Its front end is embedded in C++. Compiler
 * targets include x86/SSE, ARM v7/NEON, CUDA, Native Client,
 * OpenCL, and Metal.
 *
 * You build a Halide program by writing C++ code using objects of
 * type \ref Halide::Var, \ref Halide::Expr, and \ref Halide::Func,
 * and then calling \ref Halide::Func::compile_to_file to generate an
 * object file and header (good for deploying large routines), or
 * calling \ref Halide::Func::realize to JIT-compile and run the
 * pipeline immediately (good for testing small routines).
 *
 * To learn Halide, we recommend you start with the <a href=examples.html>tutorials</a>.
 *
 * You can also look in the test folder for many small examples that
 * use Halide's various features, and in the apps folder for some
 * larger examples that statically compile halide pipelines. In
 * particular check out local_laplacian, bilateral_grid, and
 * interpolate.
 *
 * Below are links to the documentation for the important classes in Halide.
 *
 * For defining, scheduling, and evaluating basic pipelines:
 *
 * Halide::Func, Halide::Stage, Halide::Var
 *
 * Our image data type:
 *
 * Halide::Buffer
 *
 * For passing around and reusing halide expressions:
 *
 * Halide::Expr
 *
 * For representing scalar and image parameters to pipelines:
 *
 * Halide::Param, Halide::ImageParam
 *
 * For writing functions that reduce or scatter over some domain:
 *
 * Halide::RDom
 *
 * For writing and evaluating functions that return multiple values:
 *
 * Halide::Tuple, Halide::Realization
 *
 */

/**
 * \example tutorial/lesson_01_basics.cpp
 * \example tutorial/lesson_02_input_image.cpp
 * \example tutorial/lesson_03_debugging_1.cpp
 * \example tutorial/lesson_04_debugging_2.cpp
 * \example tutorial/lesson_05_scheduling_1.cpp
 * \example tutorial/lesson_06_realizing_over_shifted_domains.cpp
 * \example tutorial/lesson_07_multi_stage_pipelines.cpp
 * \example tutorial/lesson_08_scheduling_2.cpp
 * \example tutorial/lesson_09_update_definitions.cpp
 * \example tutorial/lesson_10_aot_compilation_generate.cpp
 * \example tutorial/lesson_10_aot_compilation_run.cpp
 * \example tutorial/lesson_11_cross_compilation.cpp
 * \example tutorial/lesson_12_using_the_gpu.cpp
 * \example tutorial/lesson_13_tuples.cpp
 * \example tutorial/lesson_14_types.cpp
 * \example tutorial/lesson_15_generators.cpp
 */
