#include "RebaseLoopsToZero.h"
#include "IRMutator.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

using std::string;

namespace {

bool should_rebase(ForType type) {
    switch (type) {
    case ForType::Extern:
    case ForType::GPUBlock:
    case ForType::GPUThread:
    case ForType::GPULane:
        return false;
    default:
        return true;
    }
}

class RebaseLoopsToZero : public IRMutator {
    using IRMutator::visit;

    Stmt visit(const For *op) override {
        if (!should_rebase(op->for_type)) {
            return IRMutator::visit(op);
        }
        Stmt body = mutate(op->body);
        string name = op->name;
        if (!is_const_zero(op->min)) {
            // Renaming the loop (intentionally) invalidates any .loop_min/.loop_max lets.
            name = op->name + ".rebased";
            Expr loop_var = Variable::make(Int(32), name);
            body = LetStmt::make(op->name, loop_var + op->min, body);
        }
        if (body.same_as(op->body)) {
            return op;
        } else {
            return For::make(name, 0, op->extent, op->for_type, op->partition_policy, op->device_api, body);
        }
    }
};

}  // namespace

Stmt rebase_loops_to_zero(const Stmt &s) {
    return RebaseLoopsToZero().mutate(s);
}

}  // namespace Internal
}  // namespace Halide
