#include "Var.h"
#include "Util.h"

namespace Halide {

Var::Var(const std::string &n) : _name(n) {
    // Make sure we don't get a unique name with the same name as this
    // later. Implicits are constructed at startup and this deadlocks
    // on Windows do to lock use. Plus the char version of unique_name
    // can return names that are the same as implicits anyway, so
    // it shouldn't matter much to protect them.
    // TODO: Either there is some guarantee needed here or not. If so,
    // it should be explicitly stated, if not, then this should not be
    // done.
    if (!is_implicit(n)) {
        Internal::unique_name(n, false);
    }
}

Var::Var() : _name(Internal::make_entity_name(this, "Halide::Var", 'v')) {
}

Var Var::implicit(int n) {
    return Var("_" + std::to_string(n));
}

bool Var::is_implicit(const std::string &name) {
    return Internal::starts_with(name, "_") &&
        name.find_first_not_of("0123456789", 1) == std::string::npos;
}

}
