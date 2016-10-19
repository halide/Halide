#include "Var.h"
#include "Util.h"

#include <atomic>

namespace Halide {

/** Construct a Var with the given name */
Var::Var(const std::string &n, unsigned int unique_ivar_or_zero)
    : _name(n), unique_ivar_or_zero(unique_ivar_or_zero) {
}

Var::Var() : Var(Internal::make_entity_name(this, "Halide::Var", 'v'), 0)  {
}

Var Var::implicit(int n) {
    return Var("_" + std::to_string(n));
}

bool Var::is_implicit(const std::string &name) {
    return Internal::starts_with(name, "_") &&
        name.find_first_not_of("0123456789", 1) == std::string::npos;
}

unsigned int IVar::next_ivar_id() {
    static std::atomic<int> ivar_id_counter;
    return ++ivar_id_counter;
}

}
