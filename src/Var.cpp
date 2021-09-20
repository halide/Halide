#include "Var.h"
#include "IR.h"
#include "Util.h"

namespace Halide {

Var::Var(const std::string &n)
    : e(Internal::Variable::make(Int(32), n)) {
}

Var::Var()
    : e(Internal::Variable::make(Int(32), Internal::make_entity_name(this, "Halide:.*:Var", 'v'))) {
}

Var Var::implicit(int n) {
    return Var("_" + std::to_string(n));
}

bool Var::is_implicit(const std::string &name) {
    return Internal::starts_with(name, "_") &&
           name.find_first_not_of("0123456789", 1) == std::string::npos;
}

const std::string &Var::name() const {
    return e.as<Internal::Variable>()->name;
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
