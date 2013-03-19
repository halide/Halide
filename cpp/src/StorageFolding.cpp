#include "StorageFolding.h"
#include "Bounds.h"
#include "IROperator.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "Substitute.h"
#include "Simplify.h"
#include "Bounds.h"
#include "IRPrinter.h"
#include "Log.h"
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
        if (op->name == func) {
            vector<Expr> args = op->args;
            args[dim] = args[dim] % factor;
            expr = new Call(op->type, op->name, args, op->call_type, 
                            op->func, op->image, op->param);
        }
    }

    void visit(const Provide *op) {
        IRMutator::visit(op);
        op = stmt.as<Provide>();
        assert(op);
        if (op->name == func) {
            vector<Expr> args = op->args;
            args[dim] = args[dim] % factor;
            stmt = new Provide(op->name, op->value, args);
        }
    }

public:
    FoldStorageOfFunction(string f, int d, Expr e) : func(f), dim(d), factor(e) {}
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
            // We can't proceed into a parallel for loop
            stmt = op;
            return;
        }
            
        map<string, Region> regions = regions_touched(op->body);
        const Region &region = regions[func];

        if (!region.size()) {
            // This for loop doesn't use this function
            stmt = op;
            return;
        }
        
        // Try each dimension in turn from outermost in
        for (size_t i = region.size(); i > 0; i--) {
            Expr min = region[i-1].min;
            Expr extent = region[i-1].extent;
            Expr loop_var = new Variable(Int(32), op->name);

            // The min has to be monotonic with the loop variable            
            Expr prev_min = substitute(op->name, loop_var - 1, min);
            Expr monotonic_increasing = simplify(min >= prev_min);
            Expr monotonic_decreasing = simplify(min <= prev_min);

            //Expr min_deriv = simplify(finite_difference(min, op->name));

            // The max of the extent over all values of the loop variable must be a constant
            Scope<Interval> scope;
            scope.push(op->name, Interval(op->min, op->min + op->extent - 1));
            Expr max_extent = bounds_of_expr_in_scope(extent, scope).max;
            scope.pop(op->name);

            max_extent = simplify(max_extent);

            log(2) << "Considering folding " << func << " over for loop over " << op->name << '\n'
                   << "Min: " << min << '\n'
                   << "Extent: " << extent << '\n'
                   << "Monotonic increasing: " << monotonic_increasing << '\n'
                   << "Monotonic decreasing: " << monotonic_decreasing << '\n'
                   << "Max extent: " << max_extent << '\n';

            const IntImm *max_extent_int = max_extent.as<IntImm>();
            if ((equal(monotonic_increasing, const_true()) || 
                 equal(monotonic_decreasing, const_true()))
                && max_extent_int) {
                int extent = max_extent_int->value;
                log(2) << "Proceeding...\n";

                int factor = 1;
                while (factor < extent) factor *= 2;

                dim_folded = (int)i-1;
                fold_factor = factor;
                stmt = FoldStorageOfFunction(func, (int)i-1, factor).mutate(op);

                return;
            }
        }

        // No luck
        stmt = op;        

    }
    
public:    
    int dim_folded;
    Expr fold_factor;
    AttemptStorageFoldingOfFunction(string f) : func(f) {}
};

// Look for opportunities for storage folding in a statement
class StorageFolding : public IRMutator {
    using IRMutator::visit;

    void visit(const Realize *op) {
        AttemptStorageFoldingOfFunction folder(op->name);
        Stmt new_body = folder.mutate(op->body);

        if (new_body.same_as(op->body)) {
            stmt = op;
        } else {
            Region bounds = op->bounds;

            bounds[folder.dim_folded] = Range(0, folder.fold_factor);
            
            stmt = new Realize(op->name, op->type, bounds, new_body);
        }
    }
};

Stmt storage_folding(Stmt s) {
    return StorageFolding().mutate(s);
}

}
}
