#ifndef HALIDE_DEBUG_H
#define HALIDE_DEBUG_H

/** \file
 * Defines functions for debug logging during code generation.
 */

#include <iostream>
#include <stdlib.h>
#include <string>

#include "Introspection.h"

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
 debug(verbosity) << "The expression is " << expr << std::endl;
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
    debug(int verbosity) : logging(verbosity <= debug_level()) {}

    template<typename T>
    debug &operator<<(T&& x) {
        if (logging) {
            std::cerr << std::forward<T>(x);
        }
        return *this;
    }

    static int debug_level();
};

}  // namespace Internal
}  // namespace Halide

#endif
