#ifndef HALIDE_DEBUG_H
#define HALIDE_DEBUG_H

/** \file
 * Defines functions for debug logging during code generation.
 */

#include <iostream>
#include <string>
#include <stdlib.h>

#include "Introspection.h"

namespace Halide {

struct Expr;
struct Type;
// Forward declare some things from IRPrinter, which we can't include yet.
EXPORT std::ostream &operator<<(std::ostream &stream, const Expr &);
EXPORT std::ostream &operator<<(std::ostream &stream, const Type &);

class Module;
EXPORT std::ostream &operator<<(std::ostream &stream, const Module &);

namespace Internal {

struct Stmt;
EXPORT std::ostream &operator<<(std::ostream &stream, const Stmt &);

struct LoweredFunc;
EXPORT std::ostream &operator << (std::ostream &, const LoweredFunc &);

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

struct debug {
    EXPORT static int debug_level;
    EXPORT static bool initialized;
    int verbosity;

    debug(int v) : verbosity(v) {
        if (!initialized) {
            // Read the debug level from the environment
            size_t read;
            std::string lvl = get_env_variable("HL_DEBUG_CODEGEN", read);
            if (read) {
                debug_level = atoi(lvl.c_str());
            } else {
                debug_level = 0;
            }
            initialized = true;
        }
    }

    template<typename T>
    debug &operator<<(T x) {
        if (verbosity > debug_level) return *this;
        std::cerr << x;
        return *this;
    }
};

}
}

#endif
