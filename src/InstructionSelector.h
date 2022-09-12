#ifndef HALIDE_INSTRUCTION_SELECTOR_H
#define HALIDE_INSTRUCTION_SELECTOR_H

/** \file
 * Defines a base class for VectorInstruction selection.
 */

#include "CodeGen_LLVM.h"
#include "IR.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "Scope.h"
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

public:
    InstructionSelector(const Target &target, const CodeGen_LLVM *codegen, const FuncValueBounds &fvb);

    // Very expensive bounds queries. Cached for performance.
    // Used in IRMatch.h predicate wrappers.
    bool is_upper_bounded(const Expr &expr, const int64_t bound);
    bool is_upper_bounded(const Expr &expr, const uint64_t bound);
    bool is_lower_bounded(const Expr &expr, const int64_t bound);
    bool is_lower_bounded(const Expr &expr, const uint64_t bound);

private:
    const FuncValueBounds &func_value_bounds;
    Scope<Interval> scope;
    std::map<Expr, Interval, IRDeepCompare> cache;

    Interval cached_get_interval(const Expr &expr);
};

}  // namespace Internal
}  // namespace Halide

#endif
