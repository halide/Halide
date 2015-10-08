#include "Expr.h"
#include "IR.h"
#include "RoundingMode.h"

namespace Halide {
// FIXME: Using a cast is a bit of hack (having a single node represent the
// immediate would be preferrable). Note cast is going to higher precision so
// this does not lose any information
//
// We have to specify a rounding mode when creating the cast because it
// represents casting a higher precision type to a lower precision type.
// In this case the rounding mode shouldn't actually matter as we know that
// the stored float can be represented as a float16_t precisely.
Expr::Expr(float16_t x) : IRHandle(Internal::Cast::make(Float(16),
                                                        Expr((float) x),
                                                        RoundingMode::ToNearestTiesToEven)) {
}

}
