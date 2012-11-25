#include "IR.h"

namespace HalideInternal {

    Expr::Expr(int x) : IRHandle(new IntImm(x)) {
    }

    Expr::Expr(float x) : IRHandle(new FloatImm(x)) {
    }

}
