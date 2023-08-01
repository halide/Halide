#include "CompilerGlobals.h"

#include "Error.h"

namespace Halide {
namespace Internal {

Globals &globals() {
    static Globals g;
    return g;
}

Globals::Globals() {
    random_float_counter = 0;
    random_uint_counter = 0;
    random_variable_counter = 0;
    for (int i = 0; i < num_unique_name_counters; i++) {
        unique_name_counters[i] = 0;
    }
}

Globals::Globals(const Globals &that) {
    this->copy_from(that);
}

Globals &Globals::operator=(const Globals &that) {
    this->copy_from(that);
    return *this;
}

void Globals::copy_from(const Globals &that) {
    internal_assert(this != &that);
    (void)this->random_float_counter.exchange(that.random_float_counter);
    (void)this->random_uint_counter.exchange(that.random_uint_counter);
    (void)this->random_variable_counter.exchange(that.random_variable_counter);
    for (int i = 0; i < num_unique_name_counters; i++) {
        (void)this->unique_name_counters[i].exchange(that.unique_name_counters[i]);
    }
}

void Globals::reset() {
    // We can't just reset to the value in our ctor, because statically-initialized
    // things (e.g. Var instances) might have altered us, and resetting the
    // unique_name_counters means that guarantees of unique names will be broken.
    // Instead, initialize a baseline state based on the first time reset() is
    // called, and use *that* for this and all subsequent reset() calls.
    static Globals baseline = globals();
    globals() = baseline;
}

}  // namespace Internal
}  // namespace Halide
