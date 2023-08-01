#include "CompilerGlobals.h"

namespace Halide {
namespace Internal {

Globals &globals() {
    static Globals g;
    return g;
}

Globals::Globals() {
    reset();
}

void Globals::reset() {
    random_float_counter = 0;
    random_uint_counter = 0;
    random_variable_counter = 0;
    for (int i = 0; i < num_unique_name_counters; i++) {
        unique_name_counters[i] = 0;
    }
}

}  // namespace Internal
}  // namespace Halide
