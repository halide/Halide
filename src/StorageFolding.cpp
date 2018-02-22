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
    string dynamic_footprint;

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
        } else if (op->name == Call::buffer_crop) {
            Expr source = op->args[2];
            const Variable *buf_var = source.as<Variable>();
            if (buf_var &&
                starts_with(buf_var->name, func + ".") &&
                ends_with(buf_var->name, ".buffer")) {
                // We are taking a crop of a folded buffer. For now
                // we'll just assert that the crop doesn't wrap
                // around, so that the crop doesn't need to be treated
                // as a folded buffer too. But to take the crop, we
                // need to use folded coordinates, and then restore
                // the non-folded min after the crop operation.

                // Pull out the expressions we need
                internal_assert(op->args.size() >= 5);
                Expr mins_arg = op->args[3];
                Expr extents_arg = op->args[4];
                const Call *mins_call = mins_arg.as<Call>();
                const Call *extents_call = extents_arg.as<Call>();
                internal_assert(mins_call && extents_call);
                vector<Expr> mins = mins_call->args;
                const vector<Expr> &extents = extents_call->args;
                internal_assert(dim < (int)mins.size() && dim < (int)extents.size());
                Expr old_min = mins[dim];
                Expr old_extent = extents[dim];

                // Rewrite the crop args
                mins[dim] = old_min % factor;
                Expr new_mins = Call::make(type_of<int *>(), Call::make_struct, mins, Call::Intrinsic);
                vector<Expr> new_args = op->args;
                new_args[3] = new_mins;
                expr = Call::make(op->type, op->name, new_args, op->call_type);

                // Inject the assertion
                Expr no_wraparound = mins[dim] + extents[dim] <= factor;

                Expr valid_min = old_min;
                if (!dynamic_footprint.empty()) {
                    // If the footprint is being tracked dynamically, it's
                    // not enough to just check we don't overlap a
                    // fold. We also need to check the min against the
                    // valid min.
                    valid_min =
                        Load::make(Int(32), dynamic_footprint, 0, Buffer<>(), Parameter(), const_true());
                    Expr check = (old_min >= valid_min &&
                                  (old_min + old_extent - 1) < valid_min + factor);
                    no_wraparound = no_wraparound && check;
                }

                Expr error = Call::make(Int(32), "halide_error_bad_extern_fold",
                                        {Expr(func), Expr(dim), old_min, old_extent, valid_min, factor},
                                        Call::Extern);
                expr = Call::make(op->type, Call::require,
                                  {no_wraparound, expr, error}, Call::Intrinsic);

                // Restore the correct min coordinate
                expr = Call::make(op->type, Call::buffer_set_bounds,
                                  {expr, dim, old_min, old_extent}, Call::Extern);
            }
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
    FoldStorageOfFunction(string f, int d, Expr e, string p) :
        func(f), dim(d), factor(e), dynamic_footprint(p) {}
};

// Inject dynamic folding checks against a tracked live range.
class InjectFoldingCheck : public IRMutator {
    Function func;
    string footprint, loop_var;
    int dim;
    const StorageDim &storage_dim;
    using IRMutator::visit;

    void visit(const ProducerConsumer *op) {
        if (op->name == func.name()) {
            Stmt body = op->body;
            if (op->is_producer) {
                if (func.has_extern_definition()) {
                    // We'll update the valid min at the buffer_crop call.
                    body = mutate(op->body);
                } else {
                    // Update valid range based on bounds written to.
                    Box b = box_provided(body, func.name());
                    Expr old_leading_edge =
                        Load::make(Int(32), footprint, 0, Buffer<>(), Parameter(), const_true());

                    internal_assert(!b.empty());

                    // Track the logical address range the memory
                    // currently represents.
                    Expr new_leading_edge;
                    if (storage_dim.fold_forward) {
                        new_leading_edge = max(b[dim].max, old_leading_edge);
                    } else {
                        new_leading_edge = min(b[dim].min, old_leading_edge);
                    }

                    string new_leading_edge_var_name = unique_name('t');
                    Expr new_leading_edge_var = Variable::make(Int(32), new_leading_edge_var_name);

                    Stmt update_leading_edge =
                        Store::make(footprint, new_leading_edge_var, 0, Parameter(), const_true());

                    // Check the region being written to in this
                    // iteration lies within the range of coordinates
                    // currently represented.
                    Expr fold_non_monotonic_error =
                        Call::make(Int(32), "halide_error_bad_fold",
                                   {func.name(), storage_dim.var, loop_var},
                                   Call::Extern);

                    Expr in_valid_range;
                    if (storage_dim.fold_forward) {
                        in_valid_range = b[dim].min > new_leading_edge - storage_dim.fold_factor;
                    } else {
                        in_valid_range = b[dim].max < new_leading_edge + storage_dim.fold_factor;
                    }
                    Stmt check_in_valid_range =
                        AssertStmt::make(in_valid_range, fold_non_monotonic_error);

                    Expr extent = b[dim].max - b[dim].min + 1;

                    // Separately check the extent for *this* loop iteration fits.
                    Expr fold_too_small_error =
                        Call::make(Int(32), "halide_error_fold_factor_too_small",
                                   {func.name(), storage_dim.var, storage_dim.fold_factor, loop_var, extent},
                                   Call::Extern);

                    Stmt check_extent =
                        AssertStmt::make(extent <= storage_dim.fold_factor, fold_too_small_error);

                    Stmt checks = Block::make({check_extent, check_in_valid_range, update_leading_edge});
                    checks = LetStmt::make(new_leading_edge_var_name, new_leading_edge, checks);
                    body = Block::make(checks, body);
                }
            } else {

                // Check the accessed range against the valid range.
                Box b = box_required(body, func.name());

                if (!b.empty()) {
                    Expr leading_edge =
                        Load::make(Int(32), footprint, 0, Buffer<>(), Parameter(), const_true());

                    Expr check;
                    if (storage_dim.fold_forward) {
                        check = (b[dim].min > leading_edge - storage_dim.fold_factor && b[dim].max <= leading_edge);
                    } else {
                        check = (b[dim].max < leading_edge + storage_dim.fold_factor && b[dim].min >= leading_edge);
                    }
                    Expr bad_fold_error = Call::make(Int(32), "halide_error_bad_fold",
                                                     {func.name(), storage_dim.var, loop_var},
                                                     Call::Extern);
                    body = Block::make(AssertStmt::make(check, bad_fold_error), body);
                }
            }
            stmt = ProducerConsumer::make(op->name, op->is_producer, body);
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const LetStmt *op) {
        if (func.has_extern_definition() &&
            starts_with(op->name, func.name() + ".") &&
            ends_with(op->name, ".tmp_buffer")) {

            Stmt body = op->body;
            Expr buf = Variable::make(type_of<halide_buffer_t *>(), op->name);
            Expr leading_edge;
            if (storage_dim.fold_forward) {
                leading_edge =
                    Call::make(Int(32), Call::buffer_get_max, {buf, dim}, Call::Extern);
            } else {
                leading_edge =
                    Call::make(Int(32), Call::buffer_get_min, {buf, dim}, Call::Extern);
            }

            // We're taking a crop of the buffer to act as an output
            // to an extern stage. Update the valid min or max
            // coordinate accordingly.
            Stmt update_leading_edge =
                Store::make(footprint, leading_edge, 0, Parameter(), const_true());
            body = Block::make(update_leading_edge, body);

            // We don't need to make sure the min is moving
            // monotonically, because we can't do sliding window on
            // extern stages, so we don't have to worry about whether
            // we're preserving valid values from previous loop
            // iterations.

            stmt = LetStmt::make(op->name, op->value, body);
        } else {
            IRMutator::visit(op);
        }

    }

public:
    InjectFoldingCheck(Function func,
                       string footprint, string loop_var,
                       int dim, const StorageDim &storage_dim)
        : func(func),
          footprint(footprint), loop_var(loop_var),
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
            if (!is_pure(min) ||
                !is_pure(max) ||
                expr_uses_var(min, op->name) ||
                expr_uses_var(max, op->name)) {
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

                body = InjectFoldingCheck(func,
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
                    if (dynamic_footprint.empty()) {
                        // We were able to prove monotonicity
                        // statically, but we may need a runtime
                        // assertion for maximum extent. In many cases
                        // it will simplify away.
                        Expr error = Call::make(Int(32), "halide_error_fold_factor_too_small",
                                                {func.name(), storage_dim.var, explicit_factor, op->name, extent},
                                                Call::Extern);
                        body = Block::make(AssertStmt::make(extent <= explicit_factor, error), body);
                    }
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
                    body = FoldStorageOfFunction(func.name(), (int)i - 1, factor, dynamic_footprint).mutate(body);

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
                            Expr init_val;
                            if (min_monotonic_increasing) {
                                init_val = Int(32).min();
                            } else {
                                init_val = Int(32).max();
                            }
                            Stmt init_min = Store::make(dynamic_footprint, init_val, 0, Parameter(), const_true());
                            stmt = Block::make(init_min, stmt);
                            stmt = Allocate::make(dynamic_footprint, Int(32), MemoryType::Stack, {}, const_true(), stmt);
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

// Look for opportunities for storage folding in a statement
class StorageFolding : public IRMutator {
    const map<string, Function> &env;

    using IRMutator::visit;

    void visit(const Realize *op) {
        Stmt body = mutate(op->body);

        // Get the function associated with this realization, which
        // contains the explicit fold directives from the schedule.
        auto func_it = env.find(op->name);
        Function func = func_it != env.end() ? func_it->second : Function();

        // Don't attempt automatic storage folding if there is
        // more than one produce node for this func.
        bool explicit_only = count_producers(body, op->name) != 1;
        AttemptStorageFoldingOfFunction folder(func, explicit_only);
        debug(3) << "Attempting to fold " << op->name << "\n";
        body = folder.mutate(body);

        if (body.same_as(op->body)) {
            stmt = op;
        } else if (folder.dims_folded.empty()) {
            stmt = Realize::make(op->name, op->types, op->memory_type, op->bounds, op->condition, body);
        } else {
            Region bounds = op->bounds;

            for (size_t i = 0; i < folder.dims_folded.size(); i++) {
                int d = folder.dims_folded[i].dim;
                Expr f = folder.dims_folded[i].factor;
                internal_assert(d >= 0 &&
                                d < (int)bounds.size());

                bounds[d] = Range(0, f);
            }

            stmt = Realize::make(op->name, op->types, op->memory_type, bounds, op->condition, body);
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
