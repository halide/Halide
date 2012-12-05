#ifndef IR_EQUALITY_H
#define IR_EQUALITY_H

#include "IR.h"

namespace HalideInternal {
    bool equal(Expr a, Expr b);
    bool equal(Stmt a, Stmt b);
};

#endif
