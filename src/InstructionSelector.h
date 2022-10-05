#ifndef HALIDE_INSTRUCTION_SELECTOR_H
#define HALIDE_INSTRUCTION_SELECTOR_H

/** \file
 * Defines a base class for VectorInstruction selection.
 */

#include "CodeGen_LLVM.h"
#include "IR.h"
#include "IROperator.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "Scope.h"
#include "Simplify.h"
#include "Target.h"

namespace Halide {
namespace Internal {

/** A base class for vector instruction selection.
 *  The default implementation lowers int and uint
 *  div and mod, and splits VectorReduce nodes via
 *  CodeGen_LLVM::split_vector_reduce().
 */
class InstructionSelector : public IRGraphMutator {
protected:
    const Target &target;
    const CodeGen_LLVM *codegen;

    using IRGraphMutator::visit;
    Expr visit(const Div *) override;
    Expr visit(const Mod *) override;
    Expr visit(const VectorReduce *) override;
    Expr visit(const Let *) override;

public:
    InstructionSelector(const Target &target, const CodeGen_LLVM *codegen, const FuncValueBounds &fvb);

    // Very expensive bounds queries. Cached for performance.
    // Used in IRMatch.h predicate wrappers.
    template<typename T>
    bool is_upper_bounded(const Expr &expr, const T bound) {
        internal_assert(expr.type().element_of().can_represent(bound))
            << "Type of expr cannot represent upper bound:\n " << expr << "\n " << bound << "\n";

        Expr e = make_const(expr.type().element_of(), bound);
        Interval i = cached_get_interval(expr);

        // TODO: see above - we could get rid of can_prove if we use constant bounds queries instead.
        return i.has_upper_bound() && can_prove(i.max <= e);
    }

    template<typename T>
    bool is_lower_bounded(const Expr &expr, const T bound) {
        internal_assert(expr.type().element_of().can_represent(bound))
            << "Type of expr cannot represent lower bound:\n " << expr << "\n " << bound << "\n";

        Expr e = make_const(expr.type().element_of(), bound);
        Interval i = cached_get_interval(expr);
        // TODO: see above - we could get rid of can_prove if we use constant bounds queries instead.
        return i.has_lower_bound() && can_prove(i.min >= e);
    }

private:
    const FuncValueBounds &func_value_bounds;
    Scope<Interval> scope;
    std::map<Expr, Interval, IRDeepCompare> cache;

    Interval cached_get_interval(const Expr &expr);
};

}  // namespace Internal
}  // namespace Halide

#endif
