#ifndef HANNK_TRANSFORMS_H
#define HANNK_TRANSFORMS_H

#include <unordered_set>

#include "interpreter/ops.h"

namespace hannk {

// Rewrites ops to be in-place operations when possible.
void in_place(Op *op);

// Remove ops that are unused.
void remove_dead_ops(OpGroup *op);

// Add pad ops before ops that need it, so those ops can
// assume everything needed of the input is in bounds.
// New ops will have prepare() called on them; this will return false
// if any of those calls fail.
[[nodiscard]] bool pad_for_ops(OpGroup *op);

// Execute ops that are constant, and mark the results
// constant as well.
void fold_constants(OpGroup *op);

// Verify that no Op comes before any of its input Tensors are produced.
bool check_op_order(OpGroup *op_group, std::unordered_set<Tensor *> &valid_tensors);

template<typename T>
inline T *cast_op(Op *x) {
    class Caster : public OpVisitor {
    public:
        T *result = nullptr;

        void visit(T *op) override {
            result = op;
        }
    };

    Caster caster;
    x->accept(&caster);
    if (caster.result == x) {
        return caster.result;
    } else {
        return nullptr;
    }
}

}  // namespace hannk

#endif  // HANNK_TRANSFORMS_H
