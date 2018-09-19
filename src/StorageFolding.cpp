#include "StorageFolding.h"
#include "Bounds.h"
#include "CSE.h"
#include "Debug.h"
#include "ExprUsesVar.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Monotonic.h"
#include "Simplify.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

namespace {

int64_t next_power_of_two(int64_t x) {
    return static_cast<int64_t>(1) << static_cast<int64_t>(std::ceil(std::log2(x)));
}

}  // namespace

using std::map;
using std::string;
using std::vector;

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

                    // TODO: dynamic footprint is no longer the min, and may be tracked separately on producer and consumer sides (head vs tail)
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
    string head, tail, loop_var;
    Expr sema_var;
    int dim;
    bool in_produce;
    const StorageDim &storage_dim;
    using IRMutator::visit;

    void visit(const ProducerConsumer *op) {
        if (op->name == func.name()) {
            Stmt body = op->body;
            if (op->is_producer) {
                if (func.has_extern_definition()) {
                    // We'll update the valid min at the buffer_crop call.
                    in_produce = true;
                    body = mutate(op->body);
                } else {
                    // Update valid range based on bounds written to.
                    Box b = box_provided(body, func.name());
                    Expr old_leading_edge =
                        Load::make(Int(32), head + "_next", 0, Buffer<>(), Parameter(), const_true());

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
                        Store::make(head, new_leading_edge_var, 0, Parameter(), const_true());
                    Stmt update_next_leading_edge =
                        Store::make(head + "_next", new_leading_edge_var, 0, Parameter(), const_true());

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

                    Stmt checks = Block::make({check_extent, check_in_valid_range,
                                               update_leading_edge, update_next_leading_edge});
                    if (func.schedule().async()) {
                        Expr to_acquire;
                        if (storage_dim.fold_forward) {
                            to_acquire = new_leading_edge_var - old_leading_edge;
                        } else {
                            to_acquire = old_leading_edge - new_leading_edge_var;
                        }
                        body = Block::make(checks, body);
                        body = Acquire::make(sema_var, to_acquire, body);
                        body = LetStmt::make(new_leading_edge_var_name, new_leading_edge, body);
                    } else {
                        checks = LetStmt::make(new_leading_edge_var_name, new_leading_edge, checks);
                        body = Block::make(checks, body);
                    }
                }

            } else {
                // Check the accessed range against the valid range.
                Box b = box_required(body, func.name());
                if (b.empty()) {
                    // Must be used in an extern call (TODO:
                    // assert this, TODO: What if it's used in an
                    // extern call and native Halide). We'll
                    // update the valid min at the buffer_crop
                    // call.
                    in_produce = false;
                    body = mutate(op->body);
                } else {
                    Expr leading_edge =
                        Load::make(Int(32), tail + "_next", 0, Buffer<>(), Parameter(), const_true());

                    if (func.schedule().async()) {
                        Expr new_leading_edge;
                        if (storage_dim.fold_forward) {
                            new_leading_edge = b[dim].min - 1 + storage_dim.fold_factor;
                        } else {
                            new_leading_edge = b[dim].max + 1 - storage_dim.fold_factor;
                        }
                        string new_leading_edge_name = unique_name('t');
                        Expr new_leading_edge_var = Variable::make(Int(32), new_leading_edge_name);
                        Expr to_release;
                        if (storage_dim.fold_forward) {
                            to_release = new_leading_edge_var - leading_edge;
                        } else {
                            to_release = leading_edge - new_leading_edge_var;
                        }
                        Expr release_producer =
                            Call::make(Int(32), "halide_semaphore_release", {sema_var, to_release}, Call::Extern);
                        // The consumer is going to get its own forked copy of the footprint, so it needs to update it too.
                        Stmt update_leading_edge = Store::make(tail, new_leading_edge_var, 0, Parameter(), const_true());
                        update_leading_edge = Block::make(Store::make(tail + "_next", new_leading_edge_var, 0, Parameter(), const_true()),
                                                          update_leading_edge);
                        update_leading_edge = Block::make(Evaluate::make(release_producer), update_leading_edge);
                        update_leading_edge = LetStmt::make(new_leading_edge_name, new_leading_edge, update_leading_edge);
                        body = Block::make(update_leading_edge, body);
                    } else {
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
            }


            stmt = ProducerConsumer::make(op->name, op->is_producer, body);
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const LetStmt *op) {
        if (starts_with(op->name, func.name() + ".") &&
            ends_with(op->name, ".tmp_buffer")) {

            Stmt body = op->body;
            Expr buf = Variable::make(type_of<halide_buffer_t *>(), op->name);

            if (in_produce) {
                // We're taking a crop of the buffer to act as an output
                // to an extern stage. Update the valid min or max
                // coordinate accordingly.

                Expr leading_edge;
                if (storage_dim.fold_forward) {
                    leading_edge =
                        Call::make(Int(32), Call::buffer_get_max, {buf, dim}, Call::Extern);
                } else {
                    leading_edge =
                        Call::make(Int(32), Call::buffer_get_min, {buf, dim}, Call::Extern);
                }

                Stmt update_leading_edge =
                    Store::make(head, leading_edge, 0, Parameter(), const_true());
                body = Block::make(update_leading_edge, body);

                // We don't need to make sure the min is moving
                // monotonically, because we can't do sliding window on
                // extern stages, so we don't have to worry about whether
                // we're preserving valid values from previous loop
                // iterations.

                if (func.schedule().async()) {
                    Expr old_leading_edge =
                        Load::make(Int(32), head, 0, Buffer<>(), Parameter(), const_true());
                    Expr to_acquire;
                    if (storage_dim.fold_forward) {
                        to_acquire = leading_edge - old_leading_edge;
                    } else {
                        to_acquire = old_leading_edge - leading_edge;
                    }
                    body = Acquire::make(sema_var, to_acquire, body);
                }
            } else {
                // We're taking a crop of the buffer to act as an input
                // to an extern stage. Update the valid min or max
                // coordinate accordingly.

                Expr leading_edge;
                if (storage_dim.fold_forward) {
                    leading_edge =
                        Call::make(Int(32), Call::buffer_get_min, {buf, dim}, Call::Extern) - 1 + storage_dim.fold_factor;
                } else {
                    leading_edge =
                        Call::make(Int(32), Call::buffer_get_max, {buf, dim}, Call::Extern) + 1 - storage_dim.fold_factor;
                }

                Stmt update_leading_edge =
                    Store::make(tail, leading_edge, 0, Parameter(), const_true());
                body = Block::make(update_leading_edge, body);

                if (func.schedule().async()) {
                    Expr old_leading_edge =
                        Load::make(Int(32), tail, 0, Buffer<>(), Parameter(), const_true());
                    Expr to_release;
                    if (storage_dim.fold_forward) {
                        to_release = leading_edge - old_leading_edge;
                    } else {
                        to_release = old_leading_edge - leading_edge;
                    }
                    Expr release_producer =
                        Call::make(Int(32), "halide_semaphore_release", {sema_var, to_release}, Call::Extern);
                    body = Block::make(Evaluate::make(release_producer), body);
                }
            }

            stmt = LetStmt::make(op->name, op->value, body);
        } else {
            IRMutator::visit(op);
        }

    }

public:
    InjectFoldingCheck(Function func,
                       string head, string tail,
                       string loop_var, Expr sema_var,
                       int dim, const StorageDim &storage_dim)
        : func(func),
          head(head), tail(tail), loop_var(loop_var), sema_var(sema_var),
          dim(dim), storage_dim(storage_dim) {
    }
};

struct Semaphore {
    string name;
    Expr var;
    Expr init;
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

        Expr loop_var = Variable::make(Int(32), op->name);
        Expr loop_min = Variable::make(Int(32), op->name + ".loop_min");
        Expr loop_max = Variable::make(Int(32), op->name + ".loop_max");

        string dynamic_footprint;

        Scope<Interval> bounds;
        bounds.push(op->name, Interval(op->min, simplify(op->min + op->extent - 1)));

        Scope<Interval> steady_bounds;
        steady_bounds.push(op->name, Interval(simplify(op->min + 1), simplify(op->min + op->extent - 1)));

        // Try each dimension in turn from outermost in
        for (size_t i = box.size(); i > 0; i--) {
            int dim = (int)(i-1);
            Expr min = simplify(box[dim].min);
            Expr max = simplify(box[dim].max);

            Expr min_provided, max_provided, min_required, max_required;
            if (func.schedule().async() && !explicit_only) {
                if (!provided.empty()) {
                    min_provided = simplify(provided[dim].min);
                    max_provided = simplify(provided[dim].max);
                }
                if (!required.empty()) {
                    min_required = simplify(required[dim].min);
                    max_required = simplify(required[dim].max);
                }
            }
            string sema_name = func.name() + ".folding_semaphore." + unique_name('_');
            Expr sema_var = Variable::make(type_of<halide_semaphore_t *>(), sema_name);

            // Consider the initial iteration and steady state
            // separately for all these proofs.
            Expr loop_var = Variable::make(Int(32), op->name);
            Expr steady_state = (op->min < loop_var);

            Expr min_steady = simplify(substitute(steady_state, const_true(), min), true, steady_bounds);
            Expr max_steady = simplify(substitute(steady_state, const_true(), max), true, steady_bounds);
            Expr min_initial = simplify(substitute(steady_state, const_false(), min), true, bounds);
            Expr max_initial = simplify(substitute(steady_state, const_false(), max), true, bounds);
            Expr extent_initial = simplify(substitute(loop_var, op->min, max_initial - min_initial + 1), true, bounds);
            Expr extent_steady = simplify(max_steady - min_steady + 1, true, steady_bounds);
            Expr extent = Max::make(extent_initial, extent_steady);
            extent = simplify(common_subexpression_elimination(extent), true, bounds);

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

            debug(3) << "\nConsidering folding " << func.name() << " over for loop over " << op->name << " dimension " << i - 1 << '\n'
                     << "Min: " << min << '\n'
                     << "Max: " << max << '\n'
                     << "Extent: " << extent << '\n'
                     << "explicit_factor: " << explicit_factor << '\n';

            // First, attempt to detect if the loop is monotonically
            // increasing or decreasing (if we allow automatic folding).
            bool can_fold_forwards = false, can_fold_backwards = false;

            if (!explicit_only) {
                // We can't clobber data that will be read later. If
                // async, the producer can't un-release slots in the
                // circular buffer.
                can_fold_forwards = (is_monotonic(min, op->name) == Monotonic::Increasing);
                can_fold_backwards = (is_monotonic(max, op->name) == Monotonic::Decreasing);
                if (func.schedule().async()) {
                    // Our semaphore acquire primitive can't take
                    // negative values, so we can't un-acquire slots
                    // in the circular buffer.
                    can_fold_forwards &= (is_monotonic(max_provided, op->name) == Monotonic::Increasing);
                    can_fold_backwards &= (is_monotonic(min_provided, op->name) == Monotonic::Decreasing);
                    // We need to be able to analyze the required footprint to know how much to release
                    can_fold_forwards &= min_required.defined();
                    can_fold_backwards &= max_required.defined();
                }
            }

            // Uncomment to pretend that static analysis always fails (for testing)
            // can_fold_forwards = can_fold_backwards = false;

            if (!can_fold_forwards && !can_fold_backwards) {
                if (explicit_factor.defined()) {
                    // If we didn't find a monotonic dimension, and we
                    // have an explicit fold factor, we need to
                    // dynamically check that the min/max do in fact
                    // monotonically increase/decrease. We'll allocate
                    // some stack space to store the valid footprint,
                    // update it outside produce nodes, and check it
                    // outside consume nodes.

                    string head, tail;
                    if (func.schedule().async()) {
                        // If we're async, we need to keep a separate
                        // counter for the producer and consumer. They
                        // are coupled by a semaphore. The counter
                        // represents the max index the producer may
                        // write to. The invariant is that the
                        // semaphore count is the difference between
                        // the counters. So...
                        //
                        // when folding forwards,  semaphore == head - tail
                        // when folding backwards, semaphore == tail - head
                        //
                        // We'll initialize to head = tail, and
                        // semaphore = 0. Every time the producer or
                        // consumer wants to move the counter, it must
                        // also acquire or release the semaphore to
                        // prevent them from diverging too far.
                        dynamic_footprint = func.name() + ".folding_semaphore." + op->name + unique_name('_');
                        head = dynamic_footprint + ".head";
                        tail = dynamic_footprint + ".tail";
                    } else {
                        dynamic_footprint = func.name() + "." + op->name + unique_name('_') + ".head";
                        head = tail = dynamic_footprint;
                    }

                    body = InjectFoldingCheck(func,
                                              head, tail,
                                              op->name,
                                              sema_var,
                                              dim,
                                              storage_dim).mutate(body);

                    if (storage_dim.fold_forward) {
                        can_fold_forwards = true;
                    } else {
                        can_fold_backwards = true;
                    }
                } else {
                    // Can't do much with this dimension
                    debug(3) << "Not folding because loop min or max not monotonic in the loop variable\n"
                             << "min = " << min << "\n"
                             << "max = " << max << "\n";
                    continue;
                }
            }

            internal_assert(can_fold_forwards || can_fold_backwards);

            Expr factor;
            if (explicit_factor.defined()) {
                if (dynamic_footprint.empty() && !func.schedule().async()) {
                    // We were able to prove monotonicity
                    // statically, but we may need a runtime
                    // assertion for maximum extent. In many cases
                    // it will simplify away. For async schedules
                    // it gets dynamically tracked anyway.
                    Expr error = Call::make(Int(32), "halide_error_fold_factor_too_small",
                                            {func.name(), storage_dim.var, explicit_factor, op->name, extent},
                                            Call::Extern);
                    body = Block::make(AssertStmt::make(extent <= explicit_factor, error), body);
                }
                factor = explicit_factor;
            } else {
                // The max of the extent over all values of the loop variable must be a constant
                Scope<Interval> scope;
                scope.push(op->name, Interval(loop_min, loop_max));
                Expr max_extent = find_constant_bound(extent, Direction::Upper, scope);
                scope.pop(op->name);

                const int max_fold = 1024;
                const int64_t *const_max_extent = as_const_int(max_extent);
                if (const_max_extent && *const_max_extent <= max_fold) {
                    factor = static_cast<int>(next_power_of_two(*const_max_extent));
                } else {
                        // Try a little harder to find a bounding power of two
                        int e = max_fold * 2;
                        bool success = false;
                        while (e > 0 && can_prove(extent <= e / 2)) {
                            success = true;
                            e /= 2;
                        }
                        if (success) {
                            factor = e;
                        } else {
                            debug(3) << "Not folding because extent not bounded by a constant not greater than " << max_fold << "\n"
                                     << "extent = " << extent << "\n"
                                     << "max extent = " << max_extent << "\n";
                            // Try the next dimension
                            continue;
                        }
                }
            }

            internal_assert(factor.defined());

            debug(3) << "Proceeding with factor " << factor << "\n";

            Fold fold = {(int)i - 1, factor};
            dims_folded.push_back(fold);
            {
                string head;
                if (!dynamic_footprint.empty() && func.schedule().async()) {
                    head = dynamic_footprint + ".head";
                } else {
                    head = dynamic_footprint;
                }
                body = FoldStorageOfFunction(func.name(), (int)i - 1, factor, head).mutate(body);
            }

            // If the producer is async, it can run ahead by
            // some amount controlled by a semaphore.
            if (func.schedule().async()) {
                Semaphore sema;
                sema.name = sema_name;
                sema.var = sema_var;
                sema.init = 0;

                if (dynamic_footprint.empty()) {
                    // We are going to do the sem acquires and releases using static analysis of the boxes accessed.
                    sema.init = factor;
                    // Do the analysis of how much to acquire and release statically
                    Expr to_acquire, to_release;
                    if (can_fold_forwards) {
                        Expr max_provided_prev = substitute(op->name, loop_var - 1, max_provided);
                        Expr min_required_next = substitute(op->name, loop_var + 1, min_required);
                        to_acquire = max_provided - max_provided_prev; // This is the first time we use these entries
                        to_release = min_required_next - min_required; // This is the last time we use these entries
                    } else {
                        internal_assert(can_fold_backwards);
                        Expr min_provided_prev = substitute(op->name, loop_var - 1, min_provided);
                        Expr max_required_next = substitute(op->name, loop_var + 1, max_required);
                        to_acquire = min_provided_prev - min_provided; // This is the first time we use these entries
                        to_release = max_required - max_required_next; // This is the last time we use these entries
                    }

                    // Logically we acquire the entire extent on
                    // the first iteration:

                    // to_acquire = select(loop_var > loop_min, to_acquire, extent);

                    // However it's simpler to implement this by
                    // just reducing the initial value on the
                    // semaphore by the difference, as long as it
                    // doesn't lift any inner names out of scope.

                    Expr fudge = simplify(substitute(op->name, loop_min, extent - to_acquire));
                    if (is_const(fudge) && can_prove(fudge <= sema.init)) {
                      sema.init -= fudge;
                    } else {
                      to_acquire = select(loop_var > loop_min, likely(to_acquire), extent);
                    }

                    // We may need dynamic assertions that a positive
                    // amount of the semaphore is acquired/released,
                    // and that the semaphore is initialized to a
                    // positive value. If we are able to prove it,
                    // these checks will simplify away.
                    string to_release_name = unique_name('t');
                    Expr to_release_var = Variable::make(Int(32), to_release_name);
                    string to_acquire_name = unique_name('t');
                    Expr to_acquire_var = Variable::make(Int(32), to_acquire_name);

                    Expr bad_fold_error =
                        Call::make(Int(32), "halide_error_bad_fold",
                                   {func.name(), storage_dim.var, op->name},
                                   Call::Extern);

                    Expr release_producer =
                        Call::make(Int(32), "halide_semaphore_release", {sema.var, to_release_var}, Call::Extern);
                    Stmt release = Evaluate::make(release_producer);
                    Stmt check_release = AssertStmt::make(to_release_var >= 0 && to_release <= factor, bad_fold_error);
                    release = Block::make(check_release, release);
                    release = LetStmt::make(to_release_name, to_release, release);

                    Stmt check_acquire = AssertStmt::make(to_acquire_var >= 0 && to_acquire_var <= factor, bad_fold_error);

                    body = Block::make(body, release);
                    body = Acquire::make(sema.var, to_acquire_var, body);
                    body = Block::make(check_acquire, body);
                    body = LetStmt::make(to_acquire_name, to_acquire, body);
                } else {
                    // We injected runtime tracking and semaphore logic already
                }
                dims_folded.back().semaphore = sema;
            }
  
            if (!dynamic_footprint.empty()) {
                if (func.schedule().async()) {
                    dims_folded.back().head = dynamic_footprint + ".head";
                    dims_folded.back().tail = dynamic_footprint + ".tail";
                } else {
                    dims_folded.back().head = dynamic_footprint;
                    dims_folded.back().tail.clear();
                }
                dims_folded.back().fold_forward = storage_dim.fold_forward;
            }

            Expr min_next = substitute(op->name, loop_var + 1, min);

            if (can_prove(max < min_next)) {
                // There's no overlapping usage between loop
                // iterations, so we can continue to search
                // for further folding opportunities
                // recursively.
            } else if (!body.same_as(op->body)) {
                stmt = For::make(op->name, op->min, op->extent, op->for_type, op->device_api, body);
                break;
            } else {
                stmt = op;
                debug(3) << "Not folding because loop min or max not monotonic in the loop variable\n"
                         << "min_initial = " << min_initial << "\n"
                         << "min_steady = " << min_steady << "\n"
                         << "max_initial = " << max_initial << "\n"
                         << "max_steady = " << max_steady << "\n";
                break;
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

        if (func.schedule().async() && !dynamic_footprint.empty() ) {
            // Step the counters backwards over the entire extent of
            // the realization, in case we're in an inner loop and are
            // going to run this loop again with the same
            // semaphore. Our invariant is that the difference between
            // the two counters is the semaphore.
            //
            // Doing this instead of synchronizing and resetting the
            // counters and semaphores lets producers advance to the
            // next scanline while a consumer is still on the last few
            // pixels of the previous scanline.

            Expr head = Load::make(Int(32), dynamic_footprint + ".head", 0, Buffer<>(), Parameter(), const_true());
            Expr tail = Load::make(Int(32), dynamic_footprint + ".tail", 0, Buffer<>(), Parameter(), const_true());
            Expr step = Variable::make(Int(32), func.name() + ".extent." + std::to_string(dims_folded.back().dim)) + dims_folded.back().factor;
            Stmt reset_head = Store::make(dynamic_footprint + ".head_next", head - step, 0, Parameter(), const_true());
            Stmt reset_tail = Store::make(dynamic_footprint + ".tail_next", tail - step, 0, Parameter(), const_true());
            stmt = Block::make({stmt, reset_head, reset_tail});
        }
    }

public:
    struct Fold {
        int dim;
        Expr factor;
        Semaphore semaphore;
        string head, tail;
        bool fold_forward;
    };
    vector<Fold> dims_folded;

    AttemptStorageFoldingOfFunction(Function f, bool explicit_only)
        : func(f), explicit_only(explicit_only) {}
};

// Look for opportunities for storage folding in a statement
class StorageFolding : public IRMutator2 {
    const map<string, Function> &env;

    using IRMutator2::visit;

    Stmt visit(const Realize *op) override {
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
            return op;
        } else if (folder.dims_folded.empty()) {
            return Realize::make(op->name, op->types, op->memory_type, op->bounds, op->condition, body);
        } else {
            Region bounds = op->bounds;

            // Collapse down the extent in the folded dimension
            for (size_t i = 0; i < folder.dims_folded.size(); i++) {
                int d = folder.dims_folded[i].dim;
                Expr f = folder.dims_folded[i].factor;
                internal_assert(d >= 0 &&
                                d < (int)bounds.size());
                bounds[d] = Range(0, f);
            }

            Stmt stmt = Realize::make(op->name, op->types, op->memory_type, bounds, op->condition, body);

            // Each fold may have an associated semaphore that needs initialization, along with some counters
            for (const auto &fold : folder.dims_folded) {
                auto sema = fold.semaphore;
                if (sema.var.defined()) {
                    Expr sema_space = Call::make(type_of<halide_semaphore_t *>(), "halide_make_semaphore",
                                                 {sema.init}, Call::Extern);
                    stmt = LetStmt::make(sema.name, sema_space, stmt);
                }
                Expr init;
                if (fold.fold_forward) {
                    init = op->bounds[fold.dim].min;
                } else {
                    init = op->bounds[fold.dim].min + op->bounds[fold.dim].extent - 1;
                }
                if (!fold.head.empty()) {
                    stmt = Block::make(Store::make(fold.head + "_next", init, 0, Parameter(), const_true()), stmt);
                    stmt = Allocate::make(fold.head + "_next", Int(32), MemoryType::Stack, {}, const_true(), stmt);
                    stmt = Block::make(Store::make(fold.head, init, 0, Parameter(), const_true()), stmt);
                    stmt = Allocate::make(fold.head, Int(32), MemoryType::Stack, {}, const_true(), stmt);
                }
                if (!fold.tail.empty()) {
                    internal_assert(func.schedule().async()) << "Expected a single counter for synchronous folding";
                    stmt = Block::make(Store::make(fold.tail + "_next", init, 0, Parameter(), const_true()), stmt);
                    stmt = Allocate::make(fold.tail + "_next", Int(32), MemoryType::Stack, {}, const_true(), stmt);
                    stmt = Block::make(Store::make(fold.tail, init, 0, Parameter(), const_true()), stmt);
                    stmt = Allocate::make(fold.tail, Int(32), MemoryType::Stack, {}, const_true(), stmt);
                }
            }

            return stmt;
        }
    }

public:
    StorageFolding(const map<string, Function> &env) : env(env) {}
};

// Because storage folding runs before simplification, it's useful to
// at least substitute in constants before running it, and also simplify the RHS of Let Stmts.
class SubstituteInConstants : public IRMutator2 {
    using IRMutator2::visit;

    Scope<Expr> scope;
    Stmt visit(const LetStmt *op) override {
        Expr value = simplify(mutate(op->value));

        Stmt body;
        if (is_const(value)) {
            ScopedBinding<Expr> bind(scope, op->name, value);
            body = mutate(op->body);
        } else {
            body = mutate(op->body);
        }

        if (body.same_as(op->body) && value.same_as(op->value)) {
            return op;
        } else {
            return LetStmt::make(op->name, value, body);
        }
    }

    Expr visit(const Variable *op) override {
        if (scope.contains(op->name)) {
            return scope.get(op->name);
        } else {
            return op;
        }
    }
};

Stmt storage_folding(Stmt s, const std::map<std::string, Function> &env) {
    s = SubstituteInConstants().mutate(s);
    s = StorageFolding(env).mutate(s);
    return s;
}

}  // namespace Internal
}  // namespace Halide
