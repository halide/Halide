#include "StorageFolding.h"
#include "IROperator.h"
#include "IRMutator.h"
#include "Simplify.h"
#include "Bounds.h"
#include "IRPrinter.h"
#include "Substitute.h"
#include "Debug.h"
#include "Monotonic.h"
#include "ExprUsesVar.h"

namespace Halide {
namespace Internal {

namespace {

int64_t next_power_of_two(int64_t x) {
    return static_cast<int64_t>(1) << static_cast<int64_t>(std::ceil(std::log2(x)));
}

}  // namespace

using std::string;
using std::vector;
using std::map;

// Count the number of producers of a particular func.
class CountProducers : public IRVisitor {
    const std::string &name;

    void visit(const ProducerConsumer *op) {
        if (op->is_producer && (op->name == name)) {
            count++;
        } else {
            IRVisitor::visit(op);
        }
    }

    using IRVisitor::visit;

public:
    int count = 0;

    CountProducers(const std::string &name) : name(name) {}
};

int count_producers(Stmt in, const std::string &name) {
    CountProducers counter(name);
    in.accept(&counter);
    return counter.count;
}

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

// Inject dynamic folding checks against a tracked live range.
class InjectFoldingCheck : public IRMutator {
    string func, footprint, loop_var;
    int dim;
    const StorageDim &storage_dim;
    using IRMutator::visit;

    void visit(const ProducerConsumer *op) {
        if (op->name == func) {
            if (op->is_producer) {
                // Update valid range based on bounds written
                // to. (TODO: if extern use the .min/.max symbols
                // instead.)
                Box b = box_provided(op->body, func);
                Expr old_min =
                    Load::make(Int(32), footprint, 0, Buffer<>(), Parameter(), const_true());

                // Track the logical address range the memory
                // currently represents.
                Expr new_valid_max, new_valid_min;
                if (storage_dim.fold_forward) {
                    new_valid_min = b[dim].max - (storage_dim.fold_factor - 1);
                } else {
                    new_valid_min = b[dim].min;
                }

                Stmt update_min =
                    Store::make(footprint, new_valid_min, 0, Parameter(), const_true());
                Expr extent = b[dim].max - b[dim].min + 1;
                Expr fold_too_small_error = Call::make(Int(32), "halide_error_fold_factor_too_small",
                                                       {func, storage_dim.var, storage_dim.fold_factor, loop_var, extent},
                                                       Call::Extern);
                Stmt check_extent =
                    AssertStmt::make(extent <= storage_dim.fold_factor, fold_too_small_error);
                stmt = Block::make({check_extent, update_min, op});
            } else {
                // Check the accessed range against the valid
                // range. TODO: if consumed by extern stage use bounds
                // query result.
                Box b = box_required(op->body, func);

                Expr valid_min =
                    Load::make(Int(32), footprint, 0, Buffer<>(), Parameter(), const_true());

                Expr check = (b[dim].min >= valid_min && b[dim].max < valid_min + storage_dim.fold_factor);
                Expr bad_fold_error = Call::make(Int(32), "halide_error_bad_fold",
                                                 {func, storage_dim.var, loop_var},
                                                 Call::Extern);
                stmt = Block::make(AssertStmt::make(check, bad_fold_error), op);
            }
        } else {
            IRMutator::visit(op);
        }
    }

public:
    InjectFoldingCheck(string func, string footprint, string loop_var,
                       int dim, const StorageDim &storage_dim)
        : func(func), footprint(footprint), loop_var(loop_var),
          dim(dim), storage_dim(storage_dim) {}
};



// Attempt to fold the storage of a particular function in a statement
class AttemptStorageFoldingOfFunction : public IRMutator {
    Function func;
    bool explicit_only;

    using IRMutator::visit;

    void visit(const ProducerConsumer *op) {
        if (op->name == func.name()) {
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

        Stmt body = op->body;
        Box provided = box_provided(body, func.name());
        Box required = box_required(body, func.name());
        Box box = box_union(provided, required);

        string dynamic_footprint;

        // Try each dimension in turn from outermost in
        for (size_t i = box.size(); i > 0; i--) {
            int dim = (int)(i-1);
            Expr min = simplify(box[dim].min);
            Expr max = simplify(box[dim].max);

            const StorageDim &storage_dim = func.schedule().storage_dims()[dim];
            Expr explicit_factor;
            if (expr_uses_var(min, op->name) || expr_uses_var(max, op->name)) {
                // We only use the explicit fold factor if the fold is
                // relevant for this loop. If the fold isn't relevant
                // for this loop, the added asserts will be too
                // conservative.
                explicit_factor = storage_dim.fold_factor;
            }

            debug(3) << "\nConsidering folding " << func.name() << " over for loop over " << op->name << '\n'
                     << "Min: " << min << '\n'
                     << "Max: " << max << '\n';

            // First, attempt to detect if the loop is monotonically
            // increasing or decreasing (if we allow automatic folding).
            bool min_monotonic_increasing = !explicit_only &&
                (is_monotonic(min, op->name) == Monotonic::Increasing);

            bool max_monotonic_decreasing = !explicit_only &&
                (is_monotonic(max, op->name) == Monotonic::Decreasing);

            if (!min_monotonic_increasing && !max_monotonic_decreasing &&
                explicit_factor.defined()) {
                // If we didn't find a monotonic dimension, and we
                // have an explicit fold factor, we need to
                // dynamically check that the min/max do in fact
                // monotonically increase/decrease. We'll allocate
                // some stack space to store the valid footprint,
                // update it outside produce nodes, and check it
                // outside consume nodes.
                dynamic_footprint = func.name() + "." + op->name + ".footprint";

                body = InjectFoldingCheck(func.name(),
                                          dynamic_footprint,
                                          op->name,
                                          dim,
                                          storage_dim).mutate(body);
                if (storage_dim.fold_forward) {
                    min_monotonic_increasing = true;
                } else {
                    max_monotonic_decreasing = true;
                }
            }

            // The min or max has to be monotonic with the loop
            // variable, and should depend on the loop variable.
            if (min_monotonic_increasing || max_monotonic_decreasing) {
                Expr extent = simplify(max - min + 1);
                Expr factor;
                if (explicit_factor.defined()) {
                    factor = explicit_factor;
                } else {
                    // The max of the extent over all values of the loop variable must be a constant
                    Scope<Interval> scope;
                    scope.push(op->name, Interval(Variable::make(Int(32), op->name + ".loop_min"),
                                                  Variable::make(Int(32), op->name + ".loop_max")));
                    Expr max_extent = find_constant_bound(extent, Direction::Upper, scope);
                    scope.pop(op->name);

                    const int max_fold = 1024;
                    const int64_t *const_max_extent = as_const_int(max_extent);
                    if (const_max_extent && *const_max_extent <= max_fold) {
                        factor = static_cast<int>(next_power_of_two(*const_max_extent));
                    } else {
                        debug(3) << "Not folding because extent not bounded by a constant not greater than " << max_fold << "\n"
                                 << "extent = " << extent << "\n"
                                 << "max extent = " << max_extent << "\n";
                    }
                }

                if (factor.defined()) {
                    debug(3) << "Proceeding with factor " << factor << "\n";

                    Fold fold = {(int)i - 1, factor};
                    dims_folded.push_back(fold);
                    body = FoldStorageOfFunction(func.name(), (int)i - 1, factor).mutate(body);

                    Expr next_var = Variable::make(Int(32), op->name) + 1;
                    Expr next_min = substitute(op->name, next_var, min);
                    if (can_prove(max < next_min)) {
                        // There's no overlapping usage between loop
                        // iterations, so we can continue to search
                        // for further folding opportinities
                        // recursively.
                    } else if (!body.same_as(op->body)) {
                        stmt = For::make(op->name, op->min, op->extent, op->for_type, op->device_api, body);
                        if (!dynamic_footprint.empty()) {
                            stmt = Allocate::make(dynamic_footprint, Int(32), {}, const_true(), stmt);
                        }
                        return;
                    } else {
                        stmt = op;
                        return;
                    }
                }
            } else {
                debug(3) << "Not folding because loop min or max not monotonic in the loop variable\n"
                         << "min = " << min << "\n"
                         << "max = " << max << "\n";
            }
        }

        // If there's no communication of values from one loop
        // iteration to the next (which may happen due to sliding),
        // then we're safe to fold an inner loop.
        if (box_contains(provided, required)) {
            body = mutate(body);
        }

        if (body.same_as(op->body)) {
            stmt = op;
        } else {
            stmt = For::make(op->name, op->min, op->extent, op->for_type, op->device_api, body);
        }
    }

public:
    struct Fold {
        int dim;
        Expr factor;
    };
    vector<Fold> dims_folded;

    AttemptStorageFoldingOfFunction(Function f, bool explicit_only)
        : func(f), explicit_only(explicit_only) {}
};

/** Check if a buffer's allocated is referred to directly via an
 * intrinsic. If so we should leave it alone. (e.g. it may be used
 * extern). */
class IsBufferSpecial : public IRVisitor {
public:
    string func;
    bool special = false;

    IsBufferSpecial(string f) : func(f) {}
private:

    using IRVisitor::visit;

    void visit(const Variable *var) {
        if (var->type.is_handle() &&
            var->name == func + ".buffer") {
            special = true;
        }
    }
};

// Look for opportunities for storage folding in a statement
class StorageFolding : public IRMutator {
    const map<string, Function> &env;

    using IRMutator::visit;

    void visit(const Realize *op) {
        Stmt body = mutate(op->body);

        IsBufferSpecial special(op->name);
        op->accept(&special);

        // Get the function associated with this realization, which
        // contains the explicit fold directives from the schedule.
        auto func_it = env.find(op->name);
        Function func = func_it != env.end() ? func_it->second : Function();

        if (special.special) {
            for (const StorageDim &i : func.schedule().storage_dims()) {
                user_assert(!i.fold_factor.defined())
                    << "Dimension " << i.var << " of " << op->name
                    << " cannot be folded because it is accessed by extern or device stages.\n";
            }

            debug(3) << "Not attempting to fold " << op->name << " because its buffer is used\n";
            if (body.same_as(op->body)) {
                stmt = op;
            } else {
                stmt = Realize::make(op->name, op->types, op->bounds, op->condition, body);
            }
        } else {
            // Don't attempt automatic storage folding if there is
            // more than one produce node for this func.
            bool explicit_only = count_producers(body, op->name) != 1;
            AttemptStorageFoldingOfFunction folder(func, explicit_only);
            debug(3) << "Attempting to fold " << op->name << "\n";
            body = folder.mutate(body);

            if (body.same_as(op->body)) {
                stmt = op;
            } else if (folder.dims_folded.empty()) {
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

                stmt = Realize::make(op->name, op->types, bounds, op->condition, body);
            }
        }
    }

public:
    StorageFolding(const map<string, Function> &env) : env(env) {}
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

Stmt storage_folding(Stmt s, const std::map<std::string, Function> &env) {
    s = SubstituteInConstants().mutate(s);
    s = StorageFolding(env).mutate(s);
    return s;
}

}
}
