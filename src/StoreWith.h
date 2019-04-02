#ifndef HALIDE_INTERNAL_STORE_WITH
#define HALIDE_INTERNAL_STORE_WITH

#include <map>

#include "IR.h"

// TODO: file docs

namespace Halide {
namespace Internal {

// TODO: docs

Stmt lower_store_with(const Stmt &s, const std::map<std::string, Function> &env);

}
}

#endif
