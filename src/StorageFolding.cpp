#include "StorageFolding.h"
#include "Bounds.h"
#include "IROperator.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "Substitute.h"
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
        assert(op);
        if (op->name == func && op->call_type == Call::Halide) {
            vector<Expr> args = op->args;
            assert(dim < (int)args.size());
            args[dim] = args[dim] % factor;
            expr = Call::make(op->type, op->name, args, op->call_type,
                              op->func, op->value_index, op->image, op->param);
        }
    }

    void visit(const Provide *op) {
        IRMutator::visit(op);
        op = stmt.as<Provide>();
        assert(op);
        if (op->name == func) {
            vector<Expr> args = op->args;
            args[dim] = args[dim] % factor;
            stmt = Provide::make(op->name, op->values, args, op->lazy);
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

    void visit(const Pipeline *op) {
        if (op->name == func) {
            // Can't proceed into the pipeline for this func
            stmt = op;
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const For *op) {
        if (op->for_type != For::Serial && op->for_type != For::Unrolled) {
            // We can't proceed into a parallel for loop.

            // TODO: If there's no overlap between the region touched
            // by the threads as this loop counter varies
            // (i.e. there's no cross-talk between threads), then it's
            // safe to proceed.
            stmt = op;
            return;
        }

        Region region = region_touched(op->body, func);

        Stmt result = op;

        // Try each dimension in turn from outermost in
        for (size_t i = region.size(); i > 0; i--) {
            Expr min = region[i-1].min;
            Expr extent = region[i-1].extent;

            debug(3) << "Considering folding " << func << " over for loop over " << op->name << '\n'
                     << "Min: " << min << '\n'
                     << "Extent: " << extent << '\n';
            // The min has to be monotonic with the loop variable, and
            // should depend on the loop variable.
            MonotonicResult m = is_monotonic(min, op->name);

            if (m != MonotonicIncreasing &&
                m != MonotonicDecreasing) {
                debug(3) << "Not folding because min is not monotonic in loop var\n";
                continue;
            }

            //Expr min_deriv = simplify(finite_difference(min, op->name));

            // The max of the extent over all values of the loop variable must be a constant
            Scope<Interval> scope;
            scope.push(op->name, Interval(op->min, op->min + op->extent - 1));
            Expr max_extent = bounds_of_expr_in_scope(extent, scope).max;
            scope.pop(op->name);

            max_extent = simplify(max_extent);

            const IntImm *max_extent_int = max_extent.as<IntImm>();
            if (max_extent_int) {
                int extent = max_extent_int->value;
                debug(3) << "Proceeding...\n";

                int factor = 1;
                while (factor < extent) factor *= 2;

                dim_folded = (int)i-1;
                fold_factor = factor;
                result = FoldStorageOfFunction(func, (int)i-1, factor).mutate(result);
            } else {
                debug(3) << "Not folding because extent not bounded by a constant\n"
                         << "extent = " << extent << "\n"
                         << "max extent = " << max_extent << "\n";
            }
        }

        stmt = result;

    }

public:
    int dim_folded;
    Expr fold_factor;
    AttemptStorageFoldingOfFunction(string f) : func(f), dim_folded(-1) {}
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
                stmt = Realize::make(op->name, op->types, op->bounds, op->lazy, body);
            }
        } else {
            debug(3) << "Attempting to fold " << op->name << "\n";
            Stmt new_body = folder.mutate(body);

            if (new_body.same_as(op->body)) {
                stmt = op;
            } else if (new_body.same_as(body)) {
                stmt = Realize::make(op->name, op->types, op->bounds, op->lazy, body);
            } else {
                Region bounds = op->bounds;

                assert(folder.dim_folded >= 0 &&
                       folder.dim_folded < (int)bounds.size());

                bounds[folder.dim_folded] = Range(0, folder.fold_factor);

                stmt = Realize::make(op->name, op->types, bounds, op->lazy, new_body);
            }
        }
    }
};

Stmt storage_folding(Stmt s) {
    return StorageFolding().mutate(s);
}

}
}
