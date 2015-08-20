#include "Expr.h"
#include "IR.h"

namespace Halide {
// FIXME: This feels like a horrible hack. FloatImm should be able
// to support float16_t directly rather than relying on a cast.
//
// Note cast is going to higher precision so this does not lose any information
EXPORT Expr::Expr(float16_t x) : IRHandle(Internal::Cast::make(Float(16), Expr((float) x))) {
}

}
