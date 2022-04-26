#include "LowerWarpShuffles.h"

#include "ExprUsesVar.h"
#include "IREquality.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "LICM.h"
#include "Simplify.h"
#include "Solve.h"
#include "Substitute.h"
#include <utility>

// In CUDA, allocations stored in registers and shared across lanes
// look like private per-lane allocations, even though communication
// across lanes is possible. So while we model then as allocations
// outside the loop over lanes, we need to codegen them as allocations
// inside the loop over lanes. So the lanes collectively share
// responsibility for storing the allocation. We will stripe the
// storage across the lanes (think RAID 0). This is basically the
// opposite of RewriteAccessToVectorAlloc in Vectorize.cpp.
//
// If there were no constraints, we could just arbitrarily slice
// things up, e.g. on a per-element basis (stride one), but we have
// the added wrinkle that while threads can load from anywhere, they
// can only store into their own stripe, so we need to analyze the
// existing stores in order to figure out a striping that corresponds
// to the stores taking place. In fact, a common pattern is having
// lanes responsible for an adjacent pair of values, which gives us a
// stride of two.
//
// This lowering pass determines a good stride for each allocation,
// then moves the allocation inside the loop over lanes. Loads and
// stores have their indices rewritten to reflect the striping, and
// loads from outside a lane's own stripe become warp shuffle
// intrinsics. Finally, warp shuffles must be hoisted outside of
// conditionals, because they return undefined values if either the
// source or destination lanes are inactive.

namespace Halide {
namespace Internal {

using std::pair;
using std::string;
using std::vector;

namespace {

// Try to reduce all terms in an affine expression modulo a given
// modulus, making as many simplifications as possible. Used for
// eliminating terms from nested affine expressions. This is much more
// aggressive about eliminating terms than using % and then
// calling the simplifier.
Expr reduce_expr_helper(Expr e, const Expr &modulus) {
    if (is_const_one(modulus)) {
        return make_zero(e.type());
    } else if (is_const(e)) {
        return simplify(e % modulus);
    } else if (const Add *add = e.as<Add>()) {
        return (reduce_expr_helper(add->a, modulus) + reduce_expr_helper(add->b, modulus));
    } else if (const Sub *sub = e.as<Sub>()) {
        return (reduce_expr_helper(sub->a, modulus) - reduce_expr_helper(sub->b, modulus));
    } else if (const Mul *mul = e.as<Mul>()) {
        if (is_const(mul->b) && can_prove(modulus % mul->b == 0)) {
            return reduce_expr_helper(mul->a, simplify(modulus / mul->b)) * mul->b;
        } else {
            return reduce_expr_helper(mul->a, modulus) * reduce_expr_helper(mul->b, modulus);
        }
    } else if (const Ramp *ramp = e.as<Ramp>()) {
        return Ramp::make(reduce_expr_helper(ramp->base, modulus), reduce_expr_helper(ramp->stride, modulus), ramp->lanes);
    } else if (const Broadcast *b = e.as<Broadcast>()) {
        return Broadcast::make(reduce_expr_helper(b->value, modulus), b->lanes);
    } else {
        return e;
    }
}

Expr reduce_expr(Expr e, const Expr &modulus, const Scope<Interval> &bounds) {
    e = reduce_expr_helper(simplify(e, true, bounds), modulus);
    if (is_const_one(simplify(e >= 0 && e < modulus, true, bounds))) {
        return e;
    } else {
        return e % modulus;
    }
}

// Substitute the gpu loop variables inwards to make future passes simpler
class SubstituteInLaneVar : public IRMutator {
    using IRMutator::visit;

    Scope<int> gpu_vars;
    string lane_var;

    template<typename LetStmtOrLet>
    auto visit_let(const LetStmtOrLet *op) -> decltype(op->body) {
        if (!lane_var.empty() && expr_uses_var(op->value, lane_var) && is_pure(op->value)) {
            auto solved = solve_expression(simplify(op->value), lane_var);
            if (solved.fully_solved) {
                return mutate(substitute(op->name, solved.result, op->body));
            } else {
                return IRMutator::visit(op);
            }
        } else {
            return IRMutator::visit(op);
        }
    }

    Expr visit(const Let *op) override {
        return visit_let(op);
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let(op);
    }

    Stmt visit(const For *op) override {

        if (op->for_type == ForType::GPULane) {
            lane_var = op->name;
        }

        return IRMutator::visit(op);
    }
};

// Determine a good striping stride for an allocation, by inspecting
// loads and stores.
class DetermineAllocStride : public IRVisitor {

    using IRVisitor::visit;

    const string &alloc, &lane_var;
    Expr warp_size;
    bool single_thread = false;
    vector<Expr> loads, stores, single_stores;

    // The derivatives of all the variables in scope w.r.t the
    // lane_var. If something isn't in this scope, the derivative can
    // be assumed to be zero.
    Scope<Expr> dependent_vars;

    Scope<Interval> bounds;

    // Get the derivative of an integer expression w.r.t the warp
    // lane. Returns an undefined Expr if the result is non-trivial.
    Expr warp_stride(const Expr &e) {
        if (is_const(e)) {
            return 0;
        } else if (const Variable *var = e.as<Variable>()) {
            if (var->name == lane_var) {
                return 1;
            } else if (dependent_vars.contains(var->name)) {
                return dependent_vars.get(var->name);
            } else {
                return 0;
            }
        } else if (const Add *add = e.as<Add>()) {
            Expr sa = warp_stride(add->a), sb = warp_stride(add->b);
            if (sa.defined() && sb.defined()) {
                return sa + sb;
            }
        } else if (const Sub *sub = e.as<Sub>()) {
            Expr sa = warp_stride(sub->a), sb = warp_stride(sub->b);
            if (sa.defined() && sb.defined()) {
                return sa - sb;
            }
        } else if (const Mul *mul = e.as<Mul>()) {
            Expr sa = warp_stride(mul->a), sb = warp_stride(mul->b);
            if (sa.defined() && sb.defined() && is_const_zero(sb)) {
                return sa * mul->b;
            }
        } else if (const Broadcast *b = e.as<Broadcast>()) {
            return warp_stride(b->value);
        } else if (const Ramp *r = e.as<Ramp>()) {
            Expr sb = warp_stride(r->base);
            Expr ss = warp_stride(r->stride);
            if (sb.defined() && ss.defined() && is_const_zero(ss)) {
                return sb;
            }
        } else if (const Let *let = e.as<Let>()) {
            ScopedBinding<Expr> bind(dependent_vars, let->name, warp_stride(let->value));
            return warp_stride(let->body);
        } else if (!expr_uses_vars(e, dependent_vars)) {
            return 0;
        }

        return Expr();
    }

    void visit(const Let *op) override {
        ScopedBinding<Expr> bind(dependent_vars, op->name, warp_stride(op->value));
        IRVisitor::visit(op);
    }

    void visit(const LetStmt *op) override {
        ScopedBinding<Expr> bind(dependent_vars, op->name, warp_stride(op->value));
        IRVisitor::visit(op);
    }

    void visit(const Store *op) override {
        if (op->name == alloc) {
            if (single_thread) {
                single_stores.push_back(op->index);
            } else {
                stores.push_back(op->index);
            }
        }
        IRVisitor::visit(op);
    }

    void visit(const Load *op) override {
        if (op->name == alloc) {
            loads.push_back(op->index);
        }
        IRVisitor::visit(op);
    }

    void visit(const IfThenElse *op) override {
        // When things drop down to a single thread, we have different
        // constraints, so notice that. Check if the condition implies
        // the lane var is at most one.
        if (can_prove(!op->condition || Variable::make(Int(32), lane_var) <= 1)) {
            bool old_single_thread = single_thread;
            single_thread = true;
            op->then_case.accept(this);
            single_thread = old_single_thread;
            if (op->else_case.defined()) {
                op->else_case.accept(this);
            }
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const For *op) override {
        ScopedBinding<Interval>
            bind_bounds_if(is_const(op->min) && is_const(op->extent),
                           bounds, op->name, Interval(op->min, simplify(op->min + op->extent - 1)));
        ScopedBinding<Expr>
            bound_dependent_if((expr_uses_vars(op->min, dependent_vars) ||
                                expr_uses_vars(op->extent, dependent_vars)),
                               dependent_vars, op->name, Expr());
        IRVisitor::visit(op);
    }

    void fail(const vector<Expr> &bad) {
        std::ostringstream message;
        message
            << "Access pattern for " << alloc << " does not meet the requirements for its store_at location. "
            << "All access to an allocation scheduled inside a loop over GPU "
            << "threads and outside a loop over GPU lanes must obey the following constraint:\n"
            << "The index must be affine in " << lane_var << " with a consistent linear "
            << "term across all stores, and a constant term which, when divided by the stride "
            << "(rounding down), becomes a multiple of the warp size (" << warp_size << ").\n";
        if (!stores.empty()) {
            message << alloc << " is stored to at the following indices by multiple lanes:\n";
            for (const Expr &e : stores) {
                message << "  " << e << "\n";
            }
        }
        if (!single_stores.empty()) {
            message << "And the following indicies by lane zero:\n";
            for (const Expr &e : single_stores) {
                message << "  " << e << "\n";
            }
        }
        if (!loads.empty()) {
            message << "And loaded from at the following indices:\n";
            for (const Expr &e : loads) {
                message << "  " << e << "\n";
            }
        }
        message << "The problematic indices are:\n";
        for (const Expr &e : bad) {
            message << "  " << e << "\n";
        }
        user_error << message.str();
    }

public:
    DetermineAllocStride(const string &alloc, const string &lane_var, const Expr &warp_size)
        : alloc(alloc), lane_var(lane_var), warp_size(warp_size) {
        dependent_vars.push(lane_var, 1);
    }

    // A version of can_prove which exploits the constant bounds we've been tracking
    bool can_prove(const Expr &e) {
        return is_const_one(simplify(e, true, bounds));
    }

    Expr get_stride() {
        bool ok = true;
        Expr stride;
        Expr var = Variable::make(Int(32), lane_var);
        vector<Expr> bad;
        for (const Expr &e : stores) {
            Expr s = warp_stride(e);
            if (s.defined()) {
                // Constant-fold
                s = simplify(s);
            }
            if (!stride.defined()) {
                stride = s;
            }

            // Check the striping pattern of this store corresponds to
            // any already discovered on previous stores.
            bool this_ok = (s.defined() &&
                            (can_prove(stride == s) &&
                             can_prove(reduce_expr(e / stride - var, warp_size, bounds) == 0)));

            internal_assert(stride.defined());

            if (!this_ok) {
                bad.push_back(e);
            }
            ok = ok && this_ok;
        }

        for (const Expr &e : loads) {
            // We can handle any access pattern for loads, but it's
            // better if the stride matches up because then it's just
            // a register access, not a warp shuffle.
            Expr s = warp_stride(e);
            if (!stride.defined()) {
                stride = s;
            }
        }

        if (stride.defined()) {
            for (const Expr &e : single_stores) {
                // If only thread zero was active for the store, that makes the proof simpler.
                Expr simpler = substitute(lane_var, 0, e);
                bool this_ok = can_prove(reduce_expr(simpler / stride, warp_size, bounds) == 0);
                if (!this_ok) {
                    bad.push_back(e);
                }
                ok = ok && this_ok;
            }
        }

        if (!ok) {
            fail(bad);
        }

        if (!stride.defined()) {
            // This allocation must only accessed via single-threaded stores.
            stride = 1;
        }

        return stride;
    }
};

// Move allocations outside the loop over lanes into the loop over
// lanes (using the striping described above), and rewrites
// stores/loads to them as cuda register shuffle intrinsics.
class LowerWarpShuffles : public IRMutator {
    using IRMutator::visit;

    Expr warp_size, this_lane;
    string this_lane_name;
    bool may_use_warp_shuffle;
    vector<Stmt> allocations;
    struct AllocInfo {
        int size;
        Expr stride;
    };
    Scope<AllocInfo> allocation_info;
    Scope<Interval> bounds;
    int cuda_cap;

    Stmt visit(const For *op) override {
        ScopedBinding<Interval>
            bind_if(is_const(op->min) && is_const(op->extent),
                    bounds, op->name, Interval(op->min, simplify(op->min + op->extent - 1)));
        if (!this_lane.defined() && op->for_type == ForType::GPULane) {

            bool should_mask = false;
            ScopedValue<Expr> old_warp_size(warp_size);
            if (op->for_type == ForType::GPULane) {
                const int64_t *loop_size = as_const_int(op->extent);
                user_assert(loop_size && *loop_size <= 32)
                    << "CUDA gpu lanes loop must have constant extent of at most 32: " << op->extent << "\n";

                // Select a warp size - the smallest power of two that contains the loop size
                int64_t ws = 1;
                while (ws < *loop_size) {
                    ws *= 2;
                }
                should_mask = (ws != *loop_size);
                warp_size = make_const(Int(32), ws);
            } else {
                warp_size = op->extent;
            }
            this_lane_name = op->name;
            this_lane = Variable::make(Int(32), op->name);
            may_use_warp_shuffle = (op->for_type == ForType::GPULane);

            Stmt body = op->body;

            // Figure out the shrunken size of the hoisted allocations
            // and populate the scope.
            for (const Stmt &s : allocations) {
                const Allocate *alloc = s.as<Allocate>();
                internal_assert(alloc && alloc->extents.size() == 1);
                // The allocation has been moved into the lane loop,
                // with storage striped across the warp lanes, so the
                // size required per-lane is the old size divided by
                // the number of lanes (rounded up).
                Expr new_size = (alloc->extents[0] + op->extent - 1) / op->extent;
                new_size = simplify(new_size, true, bounds);
                new_size = find_constant_bound(new_size, Direction::Upper, bounds);
                const int64_t *sz = as_const_int(new_size);
                user_assert(sz) << "Warp-level allocation with non-constant size: "
                                << alloc->extents[0] << ". Use Func::bound_extent.";
                DetermineAllocStride stride(alloc->name, op->name, warp_size);
                body.accept(&stride);
                allocation_info.push(alloc->name, {(int)(*sz), stride.get_stride()});
            }

            body = mutate(op->body);

            if (should_mask) {
                // Mask off the excess lanes in the warp
                body = IfThenElse::make(this_lane < op->extent, body, Stmt());
            }

            // Wrap the hoisted warp-level allocations, at their new
            // reduced size.
            for (const Stmt &s : allocations) {
                const Allocate *alloc = s.as<Allocate>();
                internal_assert(alloc && alloc->extents.size() == 1);
                int new_size = allocation_info.get(alloc->name).size;
                allocation_info.pop(alloc->name);
                body = Allocate::make(alloc->name, alloc->type, alloc->memory_type,
                                      {new_size}, alloc->condition,
                                      body, alloc->new_expr, alloc->free_function);
            }
            allocations.clear();

            this_lane = Expr();
            this_lane_name.clear();
            may_use_warp_shuffle = false;

            // Mutate the body once more to apply the same transformation to any inner loops
            body = mutate(body);

            // Rewrap any hoisted allocations that weren't placed outside some inner loop
            for (const Stmt &s : allocations) {
                const Allocate *alloc = s.as<Allocate>();
                body = Allocate::make(alloc->name, alloc->type, alloc->memory_type,
                                      alloc->extents, alloc->condition,
                                      body, alloc->new_expr, alloc->free_function);
            }
            allocations.clear();

            return For::make(op->name, op->min, warp_size,
                             op->for_type, op->device_api, body);
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const IfThenElse *op) override {
        // Consider lane-masking if-then-elses when determining the
        // active bounds of the lane index.
        //
        // FuseGPULoopNests injects conditionals of the form lane <
        // limit_val when portions parts of the kernel to certain
        // threads, so we need to match that pattern. Things that come
        // from GuardWithIf can also inject <=.
        const LT *lt = op->condition.as<LT>();
        const LE *le = op->condition.as<LE>();
        if ((lt && equal(lt->a, this_lane) && is_const(lt->b)) ||
            (le && equal(le->a, this_lane) && is_const(le->b))) {
            Expr condition = mutate(op->condition);
            internal_assert(bounds.contains(this_lane_name));
            Interval interval = bounds.get(this_lane_name);
            interval.max = lt ? simplify(lt->b - 1) : le->b;
            ScopedBinding<Interval> bind(bounds, this_lane_name, interval);
            Stmt then_case = mutate(op->then_case);
            Stmt else_case = mutate(op->else_case);
            return IfThenElse::make(condition, then_case, else_case);
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const Store *op) override {
        if (allocation_info.contains(op->name)) {
            Expr idx = mutate(op->index);
            Expr value = mutate(op->value);
            Expr stride = allocation_info.get(op->name).stride;
            internal_assert(stride.defined() && warp_size.defined());

            // Reduce the index to an index in my own stripe. We have
            // already validated the legality of this in
            // DetermineAllocStride. We split the flat index into into
            // a three-dimensional index using warp_size and
            // stride. The innermost dimension is at most the stride,
            // and is in the index within one contiguous chunk stored
            // by a lane. The middle dimension corresponds to
            // lanes. It's the one we're striping across, so it should
            // be eliminated. The outermost dimension is whatever bits
            // are left over. If everything is a power of two, you can
            // think of this as erasing some of the bits in the middle
            // of the index and shifting the high bits down to cover
            // them. Reassembling the result into a flat address gives
            // the expression below.
            Expr in_warp_idx = simplify((idx / (warp_size * stride)) * stride + reduce_expr(idx, stride, bounds), true, bounds);
            return Store::make(op->name, value, in_warp_idx, op->param, op->predicate, ModulusRemainder());
        } else {
            return IRMutator::visit(op);
        }
    }

    Expr make_warp_load(Type type, const string &name, const Expr &idx, Expr lane) {
        // idx: The index of the value within the local allocation
        // lane: Which thread's value we want. If it's our own, we can just use a load.

        // Do the equivalent load, and then ask for another lane's
        // value of the result. For this to work idx
        // must not depend on the thread ID.

        // We handle other cases by converting it to a select tree
        // that muxes between all possible values.

        if (expr_uses_var(idx, this_lane_name)) {
            Expr equiv = make_warp_load(type, name, make_zero(idx.type()), lane);
            int elems = allocation_info.get(name).size;
            for (int i = 1; i < elems; i++) {
                // Load the right lanes from stripe number i
                equiv = select(idx >= i, make_warp_load(type, name, make_const(idx.type(), i), lane), equiv);
            }
            return simplify(equiv, true, bounds);
        }

        // Load the value to be shuffled
        Expr base_val = Load::make(type, name, idx, Buffer<>(),
                                   Parameter(), const_true(idx.type().lanes()), ModulusRemainder());

        Expr scalar_lane = lane;
        if (const Broadcast *b = scalar_lane.as<Broadcast>()) {
            scalar_lane = b->value;
        }
        if (equal(scalar_lane, this_lane)) {
            // This is a regular load. No shuffling required.
            return base_val;
        }

        // Make 32-bit with a combination of reinterprets and zero extension
        Type shuffle_type = type;
        if (type.bits() < 32) {
            shuffle_type = UInt(32, type.lanes());
            base_val = cast(shuffle_type, reinterpret(type.with_code(Type::UInt), base_val));
        } else if (type.bits() == 64) {
            // TODO: separate shuffles of the low and high halves and then recombine.
            user_error << "Warp shuffles of 64-bit types not yet implemented\n";
        } else {
            user_assert(type.bits() == 32) << "Warp shuffles not supported for this type: " << type << "\n";
        }

        internal_assert(may_use_warp_shuffle) << name << ", " << idx << ", " << lane << "\n";

        // We must add .sync after volta architecture:
        // https://docs.nvidia.com/cuda/volta-tuning-guide/index.html
        string sync_suffix = "";
        if (cuda_cap >= 70) {
            sync_suffix = ".sync";
        }

        auto shfl_args = [&](const std::vector<Expr> &args) {
            if (cuda_cap >= 70) {
                return args;
            }
            return std::vector({args[1], args[2], args[3]});
        };

        string intrin_suffix;
        if (shuffle_type.is_float()) {
            intrin_suffix = ".f32";
        } else {
            intrin_suffix = ".i32";
        }

        Expr wild = Variable::make(Int(32), "*");
        vector<Expr> result;
        int bits = 0;

        // Move this_lane as far left as possible in the expression to
        // reduce the number of cases to check below.
        lane = solve_expression(lane, this_lane_name).result;

        Expr shuffled;
        Expr membermask = (int)0xffffffff;
        if (expr_match(this_lane + wild, lane, result)) {
            // We know that 0 <= lane + wild < warp_size by how we
            // constructed it, so we can just do a shuffle down.
            Expr down = Call::make(shuffle_type, "llvm.nvvm.shfl" + sync_suffix + ".down" + intrin_suffix,
                                   shfl_args({membermask, base_val, result[0], 31}), Call::PureExtern);
            shuffled = down;
        } else if (expr_match((this_lane + wild) % wild, lane, result) &&
                   is_const_power_of_two_integer(result[1], &bits) &&
                   bits <= 5) {
            result[0] = simplify(result[0] % result[1], true, bounds);
            // Rotate. Mux a shuffle up and a shuffle down. Uses fewer
            // intermediate registers than using a general gather for
            // this.
            Expr mask = (1 << bits) - 1;
            Expr down = Call::make(shuffle_type, "llvm.nvvm.shfl" + sync_suffix + ".down" + intrin_suffix,
                                   shfl_args({membermask, base_val, result[0], mask}), Call::PureExtern);
            Expr up = Call::make(shuffle_type, "llvm.nvvm.shfl" + sync_suffix + ".up" + intrin_suffix,
                                 shfl_args({membermask, base_val, (1 << bits) - result[0], 0}), Call::PureExtern);
            Expr cond = (this_lane >= (1 << bits) - result[0]);
            Expr equiv = select(cond, up, down);
            shuffled = simplify(equiv, true, bounds);
        } else {
            // The format of the mask is a pain. The high bits tell
            // you how large the a warp is for this instruction
            // (i.e. is it a shuffle within groups of 8, or a shuffle
            // within groups of 16?). The low bits serve as a clamp on
            // the max value pulled from. We don't use that, but it
            // could hypothetically be used for boundary conditions.
            Expr mask = simplify(((31 & ~(warp_size - 1)) << 8) | 31);
            // The idx variant can do a general gather. Use it for all other cases.
            shuffled = Call::make(shuffle_type, "llvm.nvvm.shfl" + sync_suffix + ".idx" + intrin_suffix,
                                  shfl_args({membermask, base_val, lane, mask}), Call::PureExtern);
        }
        // TODO: There are other forms, like butterfly and clamp, that
        // don't need to use the general gather

        if (shuffled.type() != type) {
            user_assert(shuffled.type().bits() > type.bits());
            // Narrow it back down
            shuffled = reinterpret(type, cast(type.with_code(Type::UInt), shuffled));
        }
        return shuffled;
    }

    Expr visit(const Load *op) override {
        if (allocation_info.contains(op->name)) {
            Expr idx = mutate(op->index);
            Expr stride = allocation_info.get(op->name).stride;

            // Break the index into lane and stripe components
            Expr lane = simplify(reduce_expr(idx / stride, warp_size, bounds), true, bounds);
            idx = simplify((idx / (warp_size * stride)) * stride + reduce_expr(idx, stride, bounds), true, bounds);
            // We don't want the idx to depend on the lane var, so try to eliminate it
            idx = simplify(solve_expression(idx, this_lane_name).result, true, bounds);
            return make_warp_load(op->type, op->name, idx, lane);
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const Allocate *op) override {
        if (this_lane.defined() || op->memory_type == MemoryType::GPUShared) {
            // Not a warp-level allocation
            return IRMutator::visit(op);
        } else {
            // Pick up this allocation and deposit it inside the loop over lanes at reduced size.
            allocations.emplace_back(op);
            return mutate(op->body);
        }
    }

public:
    LowerWarpShuffles(int cuda_cap)
        : cuda_cap(cuda_cap) {
    }
};

class HoistWarpShufflesFromSingleIfStmt : public IRMutator {
    using IRMutator::visit;

    Scope<int> stored_to;
    vector<pair<string, Expr>> lifted_lets;

    Expr visit(const Call *op) override {
        // If it was written outside this if clause but read inside of
        // it, we need to hoist it.
        if (starts_with(op->name, "llvm.nvvm.shfl.") &&
            !expr_uses_vars(op, stored_to)) {
            string name = unique_name('t');
            lifted_lets.emplace_back(name, op);
            return Variable::make(op->type, name);
        } else {
            return IRMutator::visit(op);
        }
    }

    template<typename ExprOrStmt, typename LetOrLetStmt>
    ExprOrStmt visit_let(const LetOrLetStmt *op) {
        Expr value = mutate(op->value);
        ExprOrStmt body = mutate(op->body);

        // If any of the lifted expressions use this, we also need to
        // lift this.
        bool should_lift = false;
        for (const auto &p : lifted_lets) {
            should_lift |= expr_uses_var(p.second, op->name);
        }

        if (should_lift) {
            lifted_lets.push_back({op->name, value});
            return body;
        } else {
            return LetOrLetStmt::make(op->name, value, body);
        }
    }

    Expr visit(const Let *op) override {
        return visit_let<Expr>(op);
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let<Stmt>(op);
    }

    Stmt visit(const For *op) override {
        Stmt body = mutate(op->body);
        bool fail = false;
        for (const auto &p : lifted_lets) {
            fail |= expr_uses_var(p.second, op->name);
        }
        if (fail) {
            // We can't hoist. We need to bail out here.
            body = rewrap(body);
            success = false;
        } else {
            debug(3) << "Successfully hoisted shuffle out of for loop\n";
        }
        return For::make(op->name, op->min, op->extent, op->for_type, op->device_api, body);
    }

    Stmt visit(const Store *op) override {
        stored_to.push(op->name, 0);
        return IRMutator::visit(op);
    }

public:
    bool success = true;

    Stmt rewrap(Stmt s) {
        while (!lifted_lets.empty()) {
            const pair<string, Expr> &p = lifted_lets.back();
            s = LetStmt::make(p.first, p.second, s);
            lifted_lets.pop_back();
        }
        return s;
    }
};

// Push an if statement inwards until it doesn't contain any warp shuffles
class MoveIfStatementInwards : public IRMutator {
    using IRMutator::visit;

    Stmt visit(const Store *op) override {
        // We've already hoisted warp shuffles out of stores
        return IfThenElse::make(condition, op, Stmt());
    }

    Expr condition;

public:
    MoveIfStatementInwards(Expr c)
        : condition(std::move(c)) {
    }
};

// The destination *and source* for warp shuffles must be active
// threads, or the value is undefined, so we want to lift them out of
// if statements.
class HoistWarpShuffles : public IRMutator {
    using IRMutator::visit;

    Stmt visit(const IfThenElse *op) override {
        // Move all Exprs that contain a shuffle out of the body of
        // the if.
        Stmt then_case = mutate(op->then_case);
        Stmt else_case = mutate(op->else_case);

        HoistWarpShufflesFromSingleIfStmt hoister;
        then_case = hoister.mutate(then_case);
        else_case = hoister.mutate(else_case);
        Stmt s = IfThenElse::make(op->condition, then_case, else_case);
        if (hoister.success) {
            return hoister.rewrap(s);
        } else {
            // Need to move the ifstmt further inwards instead.
            internal_assert(!else_case.defined()) << "Cannot hoist warp shuffle out of " << s << "\n";
            string pred_name = unique_name('p');
            s = MoveIfStatementInwards(Variable::make(op->condition.type(), pred_name)).mutate(then_case);
            return LetStmt::make(pred_name, op->condition, s);
        }
    }
};

class HasLaneLoop : public IRVisitor {
    using IRVisitor::visit;

    void visit(const For *op) override {
        result = result || op->for_type == ForType::GPULane;
        IRVisitor::visit(op);
    }

public:
    bool result = false;
};

bool has_lane_loop(const Stmt &s) {
    HasLaneLoop l;
    s.accept(&l);
    return l.result;
}

class LowerWarpShufflesInEachKernel : public IRMutator {
    using IRMutator::visit;

    Stmt visit(const For *op) override {
        if (op->device_api == DeviceAPI::CUDA && has_lane_loop(op)) {
            Stmt s = op;
            s = LowerWarpShuffles(cuda_cap).mutate(s);
            s = HoistWarpShuffles().mutate(s);
            return simplify(s);
        } else {
            return IRMutator::visit(op);
        }
    }

    int cuda_cap;

public:
    LowerWarpShufflesInEachKernel(int cuda_cap)
        : cuda_cap(cuda_cap) {
    }
};

}  // namespace

Stmt lower_warp_shuffles(Stmt s, const Target &t) {
    s = hoist_loop_invariant_values(s);
    s = SubstituteInLaneVar().mutate(s);
    s = simplify(s);
    s = LowerWarpShufflesInEachKernel(t.get_cuda_capability_lower_bound()).mutate(s);
    return s;
};

}  // namespace Internal
}  // namespace Halide
