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

class Expr;
class Type;
// Forward declare some things from IRPrinter, which we can't include yet.
EXPORT std::ostream &operator<<(std::ostream &stream, const Expr &);
EXPORT std::ostream &operator<<(std::ostream &stream, const Type &);

namespace Internal {

class Stmt;
std::ostream &operator<<(std::ostream &stream, const Stmt &);

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
    static int debug_level;
    static bool initialized;
    int verbosity;

    debug(int v) : verbosity(v) {
        if (!initialized) {
            // Read the debug level from the environment
            #ifdef _WIN32
            char lvl[32];
            size_t read = 0;
            getenv_s(&read, lvl, "HL_DEBUG_CODEGEN");
            if (read) {
            #else
            if (char *lvl = getenv("HL_DEBUG_CODEGEN")) {
            #endif
                debug_level = atoi(lvl);
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

struct error_report {
    bool condition;
    bool user;
    bool warning;
    const char *file;
    int line;
    const char *condition_string;
    error_report(bool c, bool u, bool w, const char *f, int l, const char *cs) :
        condition(c), user(u), warning(w), file(f), line(l), condition_string(cs) {
        if (condition) return;
        const std::string &source_loc = get_source_location();

        if (user) {
            // Only mention where inside of libHalide the error tripped if we have debug level > 0
            debug(1) << "User error triggered at " << f << ":" << l << "\n";
            if (condition_string) {
                debug(1) << "Condition failed: " << condition_string << "\n";
            }
            if (warning) {
                std::cerr << "Warning";
            } else {
                std::cerr << "Error";
            }
            if (source_loc.empty()) {
                std::cerr << ":\n";
            } else {
                std::cerr << " at " << source_loc << ":\n";
            }

        } else {
            std::cerr << "Internal ";
            if (warning) {
                std::cerr << "warning";
            } else {
                std::cerr << "error";
            }
            std::cerr << " at " << f << ":" << l;
            if (!source_loc.empty()) {
                std::cerr << " triggered by user code at " << source_loc << ":\n";
            } else {
                std::cerr << "\n";
            }
            if (condition_string) {
                std::cerr << "Condition failed: " << condition_string << "\n";
            }
        }
    }

    template<typename T>
    error_report &operator<<(T x) {
        if (condition) return *this;
        std::cerr << x;
        return *this;
    }

    ~error_report() {
        if (condition) return;
        // Once we're done reporting the problem, destroy the universe.
        // TODO: Add an option to error out on warnings too
        // TODO: Add an option to throw an exception instead
        if (!warning) abort();
    }
};

#define internal_error     Halide::Internal::error_report(false, false, false, __FILE__, __LINE__, NULL)
#define internal_assert(c) Halide::Internal::error_report(c,     false, false, __FILE__, __LINE__, #c)
#define user_error         Halide::Internal::error_report(false, true,  false, __FILE__, __LINE__, NULL)
#define user_assert(c)     Halide::Internal::error_report(c,     true,  false, __FILE__, __LINE__, #c)
#define user_warning       Halide::Internal::error_report(false, true,   true, __FILE__, __LINE__, NULL)

// The nicely named versions get cleaned up at the end of Halide.h,
// but user code might want to do halide-style user_asserts (e.g. the
// Extern macros introduce calls to user_assert), so for that purpose
// we define an equivalent macro that can be used outside of Halide.h
#define _halide_user_assert(c)     Halide::Internal::error_report(c,     true,  false, __FILE__, __LINE__, #c)

}
}

#endif
