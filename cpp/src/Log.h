#ifndef HALIDE_LOG_H
#define HALIDE_LOG_H

#include <iostream>
#include "IR.h"
#include <string>

namespace Halide {
namespace Internal {

/* For optional debugging during codegen, use the log class as
 * follows: 
 * 
 * log(verbosity) << "The expression is " << expr << std::endl; 
 * 
 * verbosity of 0 always prints, 1 should print after every
 * major stage, 2 should be used for more detail, and 3 should
 * be used for tracing everything that occurs. 
 */

struct log {
    static int debug_level;
    static bool initialized;
    int verbosity;

    log(int v) : verbosity(v) {
        if (!initialized) {
            // Read the debug level from the environment
            if (char *lvl = getenv("HL_DEBUG_CODEGEN")) {
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
