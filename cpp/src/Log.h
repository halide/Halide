#ifndef HALIDE_LOG_H
#define HALIDE_LOG_H

/** \file
 * Defines functions for debug logging during code generation.
 */

#include <iostream>
#include "IR.h"
#include <string>

namespace Halide {
namespace Internal {

/** For optional debugging during codegen, use the log class as
 * follows: 
 * 
 \code
 log(verbosity) << "The expression is " << expr << std::endl; 
 \endcode
 *
 * verbosity of 0 always prints, 1 should print after every major
 * stage, 2 should be used for more detail, and 3 should be used for
 * tracing everything that occurs. The verbosity with which to print
 * is determined by the value of the environment variable
 * HL_DEBUG_CODEGEN
 */

struct log {
    static int debug_level;
    static bool initialized;
    int verbosity;

    log(int v) : verbosity(v) {
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
    log &operator<<(T x) {
        if (verbosity > debug_level) return *this;
        std::cerr << x;
        return *this;
    }
};

}
}

#endif
