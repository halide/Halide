#ifndef HL_REWRITE_RULE_H
#define HL_REWRITE_RULE_H

#include "Halide.h"

struct RewriteRule {
    const Halide::Expr before;
    const Halide::Expr after;
    const Halide::Expr pred;
};

#endif // HL_REWRITE_RULE_H
