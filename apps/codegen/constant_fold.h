#ifndef CONSTANT_FOLD_H
#define CONSTANT_FOLD_H
#include "Halide.h"
#include "Simplify_Internal.h"

using namespace Halide;
using namespace Halide::Internal;

Expr fold_actual(const Expr &expr);

bool evaluate_predicate(const Expr &expr) {
    if (!expr.type().is_bool()) {
        std::cerr << "can't evaluate non-boolean predicate: " << expr << "\n";
        assert(false);
        return false;
    } else {
        Expr folded = fold_actual(expr);
        assert(is_const(folded)); // Always expect predicates to be constant-foldable
        const UIntImm *imm = folded.as<UIntImm>();
        assert(imm); // Should be folded to a boolean value.
        return imm->value > 0; // TODO: it should always be 1 or 0.
    }
}

#endif // CONSTANT_FOLD_H
