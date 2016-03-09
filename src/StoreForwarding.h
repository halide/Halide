#ifndef HALIDE_STORE_FORWARDING_H
#define HALIDE_STORE_FORWARDING_H

#include "Expr.h"

namespace Halide {
namespace Internal {

Stmt store_forwarding(Stmt);

}
}

#endif
