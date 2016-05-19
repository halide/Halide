#include "Var.h"
#include "Util.h"

namespace Halide {

Var::Var(const std::string &n) : _name(n) {
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
