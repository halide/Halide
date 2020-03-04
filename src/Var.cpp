#include "Var.h"

#include "Util.h"
#include <utility>

namespace Halide {

Var::Var(std::string n)
    : _name(std::move(n)) {
}

Var::Var()
    : _name(Internal::make_entity_name(this, "Halide:.*:Var", 'v')) {
}

Var Var::implicit(int n) {
    return Var("_" + std::to_string(n));
}

bool Var::is_implicit(const std::string &name) {
    return Internal::starts_with(name, "_") &&
           name.find_first_not_of("0123456789", 1) == std::string::npos;
}

namespace Internal {

std::vector<Var> make_argument_list(int dimensionality) {
    std::vector<Var> args(dimensionality);
    for (int i = 0; i < dimensionality; i++) {
        args[i] = Var::implicit(i);
    }
    return args;
}

}  // namespace Internal

}  // namespace Halide
