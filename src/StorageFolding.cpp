#include "StorageFolding.h"
#include "IROperator.h"
#include "IRMutator.h"
#include "Simplify.h"
#include "Bounds.h"
#include "IRPrinter.h"
#include "Debug.h"
#include "Derivative.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;
using std::map;

// Fold the storage of a function in a particular dimension by a particular factor
class FoldStorageOfFunction : public IRMutator {
    string func;
    int dim;
    Expr factor;

    using IRMutator::visit;

    void visit(const Call *op) {
        IRMutator::visit(op);
        op = expr.as<Call>();
        internal_assert(op);
        if (op->name == func && op->call_type == Call::Halide) {
            vector<Expr> args = op->args;
            internal_assert(dim < (int)args.size());
            args[dim] = is_one(factor) ? 0 : (args[dim] % factor);
            expr = Call::make(op->type, op->name, args, op->call_type,
                              op->func, op->value_index, op->image, op->param);
        }
    }

    void visit(const Provide *op) {
        IRMutator::visit(op);
        op = stmt.as<Provide>();
        internal_assert(op);
        if (op->name == func) {
            vector<Expr> args = op->args;
            args[dim] = is_one(factor) ? 0 : (args[dim] % factor);
            stmt = Provide::make(op->name, op->values, args);
        }
    }

public:
    FoldStorageOfFunction(string f, int d, Expr e) :
        func(f), dim(d), factor(e) {}
};

// Attempt to fold the storage of a particular function in a statement
class AttemptStorageFoldingOfFunction : public IRMutator {
    string func;

    using IRMutator::visit;

    void visit(const ProducerConsumer *op) {
        if (op->name == func) {
            // Can't proceed into the pipeline for this func
            stmt = op;
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const For *op) {
        if (op->for_type != ForType::Serial && op->for_type != ForType::Unrolled) {
            // We can't proceed into a parallel for loop.

            // TODO: If there's no overlap between the region touched
            // by the threads as this loop counter varies
            // (i.e. there's no cross-talk between threads), then it's
            // safe to proceed.
            stmt = op;
            return;
        }

        Box box = box_touched(op->body, func);

        Stmt result = op;

        // Try each dimension in turn from outermost in
        for (size_t i = box.size(); i > 0; i--) {
            Expr min = simplify(box[i-1].min);
            Expr max = simplify(box[i-1].max);

            debug(3) << "\nConsidering folding " << func << " over for loop over " << op->name << '\n'
                     << "Min: " << min << '\n'
                     << "Max: " << max << '\n';

            // The min or max has to be monotonic with the loop
            // variable, and should depend on the loop variable.
            if (is_monotonic(min, op->name) == MonotonicIncreasing ||
                is_monotonic(max, op->name) == MonotonicDecreasing) {

                // The max of the extent over all values of the loop variable must be a constant
                Expr extent = simplify(max - min);
                Scope<Interval> scope;
                scope.push(op->name, Interval(Variable::make(Int(32), op->name + ".loop_min"),
                                              Variable::make(Int(32), op->name + ".loop_max")));
                Expr max_extent = bounds_of_expr_in_scope(extent, scope).max;
                scope.pop(op->name);

                max_extent = simplify(max_extent);

                const IntImm *max_extent_int = max_extent.as<IntImm>();
                if (max_extent_int) {
                    int extent = max_extent_int->value;

                    int factor = 1;
                    while (factor <= extent) factor *= 2;

                    debug(3) << "Proceeding with factor " << factor << "\n";

                    Fold fold = {(int)i - 1, factor};
                    dims_folded.push_back(fold);
                    result = FoldStorageOfFunction(func, (int)i - 1, factor).mutate(result);

                    Expr step = finite_difference(min, op->name);

                    if (is_one(simplify(extent < step))) {
                        // There's no overlapping usage between loop
                        // iterations, so we can continue to search
                        // for further folding opportinities
                        // recursively.
                    } else {
                        stmt = result;
                        return;
                    }

                } else {
                    debug(3) << "Not folding because extent not bounded by a constant\n"
                             << "extent = " << extent << "\n"
                             << "max extent = " << max_extent << "\n";
                }
            } else {
                debug(3) << "Not folding because loop min or max not monotonic in the loop variable\n"
                         << "min = " << min << "\n"
                         << "max = " << max << "\n";
            }
        }

        // Any folds that took place folded dimensions away entirely, so we can proceed recursively.
        if (const For *f = result.as<For>()) {
            Stmt body = mutate(f->body);
            if (body.same_as(f->body)) {
                stmt = result;
            } else {
                stmt = For::make(op->name, op->min, op->extent, op->for_type, op->device_api, body);
            }
        } else {
            stmt = result;
        }

    }

public:
    struct Fold {
        int dim;
        Expr factor;
    };
    vector<Fold> dims_folded;

    AttemptStorageFoldingOfFunction(string f) : func(f) {}
};

/** Check if a buffer's allocated is referred to directly via an
 * intrinsic. If so we should leave it alone. (e.g. it may be used
 * extern). */
class IsBufferSpecial : public IRVisitor {
public:
    string func;
    bool special;

    IsBufferSpecial(string f) : func(f), special(false) {}
private:

    using IRVisitor::visit;

    void visit(const Call *call) {
        if (call->call_type == Call::Intrinsic &&
            call->name == func) {
            special = true;
        }
    }
};

// Look for opportunities for storage folding in a statement
class StorageFolding : public IRMutator {
    using IRMutator::visit;

    void visit(const Realize *op) {
        Stmt body = mutate(op->body);

        AttemptStorageFoldingOfFunction folder(op->name);
        IsBufferSpecial special(op->name);
        op->accept(&special);

        if (special.special) {
            debug(3) << "Not attempting to fold " << op->name << " because it is referenced by an intrinsic\n";
            if (body.same_as(op->body)) {
                stmt = op;
            } else {
                stmt = Realize::make(op->name, op->types, op->bounds, op->condition, body);
            }
        } else {
            debug(3) << "Attempting to fold " << op->name << "\n";
            Stmt new_body = folder.mutate(body);

            if (new_body.same_as(op->body)) {
                stmt = op;
            } else if (new_body.same_as(body)) {
                stmt = Realize::make(op->name, op->types, op->bounds, op->condition, body);
            } else {
                Region bounds = op->bounds;

                for (size_t i = 0; i < folder.dims_folded.size(); i++) {
                    int d = folder.dims_folded[i].dim;
                    Expr f = folder.dims_folded[i].factor;
                    internal_assert(d >= 0 &&
                                    d < (int)bounds.size());

                    bounds[d] = Range(0, f);
                }

                stmt = Realize::make(op->name, op->types, bounds, op->condition, new_body);
            }
        }
    }
};

// Because storage folding runs before simplification, it's useful to
// at least substitute in constants before running it, and also simplify the RHS of Let Stmts.
class SubstituteInConstants : public IRMutator {
    using IRMutator::visit;

    Scope<Expr> scope;
    void visit(const LetStmt *op) {
        Expr value = simplify(mutate(op->value));

        Stmt body;
        if (is_const(value)) {
            scope.push(op->name, value);
            body = mutate(op->body);
            scope.pop(op->name);
        } else {
            body = mutate(op->body);
        }

        if (body.same_as(op->body) && value.same_as(op->value)) {
            stmt = op;
        } else {
            stmt = LetStmt::make(op->name, value, body);
        }
    }

    void visit(const Variable *op) {
        if (scope.contains(op->name)) {
            expr = scope.get(op->name);
        } else {
            expr = op;
        }
    }
};

Stmt storage_folding(Stmt s) {
    s = SubstituteInConstants().mutate(s);
    s = StorageFolding().mutate(s);
    return s;
}

}
}
