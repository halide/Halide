#include "CompilerGlobals.h"

namespace Halide {
namespace Internal {

namespace {

Globals the_real_globals;

}  // namespace

Globals &globals() {
    return the_real_globals;
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
