#ifndef HALIDE_DEBUG_H
#define HALIDE_DEBUG_H

/** \file
 * Defines functions for debug logging during code generation.
 */

#include <cstdlib>
#include <iostream>
#include <string>

namespace Halide {

struct Expr;
struct Type;
// Forward declare some things from IRPrinter, which we can't include yet.
std::ostream &operator<<(std::ostream &stream, const Expr &);
std::ostream &operator<<(std::ostream &stream, const Type &);

class Module;
std::ostream &operator<<(std::ostream &stream, const Module &);

struct Target;
/** Emit a halide Target in a human readable form */
std::ostream &operator<<(std::ostream &stream, const Target &);

namespace Internal {

struct Stmt;
std::ostream &operator<<(std::ostream &stream, const Stmt &);

struct LoweredFunc;
std::ostream &operator<<(std::ostream &, const LoweredFunc &);

/** For optional debugging during codegen, use the debug class as
 * follows:
 *
 \code
 debug(verbosity) << "The expression is " << expr << "\n";
 \endcode
 *
 * verbosity of 0 always prints, 1 should print after every major
 * stage, 2 should be used for more detail, and 3 should be used for
 * tracing everything that occurs. The verbosity with which to print
 * is determined by the value of the environment variable
 * HL_DEBUG_CODEGEN
 */

class debug {
    const bool logging;

public:
    debug(int verbosity)
        : logging(verbosity <= debug_level()) {
    }

    template<typename T>
    debug &operator<<(T &&x) {
        if (logging) {
            std::cerr << std::forward<T>(x);
        }
        return *this;
    }

    static int debug_level();
};

/** Allow easily printing the contents of containers, or std::vector-like containers,
 *  in debug output. Used like so:
 *        std::vector<Type> arg_types;
 *        debug(4) << "arg_types: " << PrintSpan(arg_types) << "\n";
 * Which results in output like "arg_types: { uint8x8, uint8x8 }" on one line. */
template<typename T>
struct PrintSpan {
    const T &span;
    PrintSpan(const T &span)
        : span(span) {
    }
};

template<typename StreamT, typename T>
inline StreamT &operator<<(StreamT &stream, const PrintSpan<T> &wrapper) {
    stream << "{ ";
    const char *sep = "";
    for (const auto &e : wrapper.span) {
        stream << sep << e;
        sep = ", ";
    }
    stream << " }";
    return stream;
}

/** Allow easily printing the contents of spans, or std::vector-like spans,
 *  in debug output. Used like so:
 *        std::vector<Type> arg_types;
 *        debug(4) << "arg_types: " << PrintSpan(arg_types) << "\n";
 * Which results in output like:
 *     arg_types:
 *     {
 *             uint8x8,
 *             uint8x8,
 *     }
 * Indentation uses a tab character. */
template<typename T>
struct PrintSpanLn {
    const T &span;
    PrintSpanLn(const T &span)
        : span(span) {
    }
};

template<typename StreamT, typename T>
inline StreamT &operator<<(StreamT &stream, const PrintSpanLn<T> &wrapper) {
    stream << "\n{\n";
    for (const auto &e : wrapper.span) {
        stream << "\t" << e << ",\n";
    }
    stream << "}\n";
    return stream;
}

}  // namespace Internal
}  // namespace Halide

#endif
