#include "Expr.h"
#include "IR.h"

namespace Halide {
// FIXME: Using a cast is a bit of hack (having a single node represent the
// immediate would be preferrable). Note cast is going to higher precision so
// this does not lose any information
Expr::Expr(float16_t x) : IRHandle(Internal::Cast::make(Float(16), Expr((float) x))) {
}

}
