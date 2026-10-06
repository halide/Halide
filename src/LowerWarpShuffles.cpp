#include "LowerWarpShuffles.h"

#include "Deinterleave.h"
#include "ExprUsesVar.h"
#include "IREquality.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "LICM.h"
#include "Simplify.h"
#include "Solve.h"
#include "Substitute.h"
#include <algorithm>
#include <functional>
#include <set>
#include <utility>

// In CUDA and Metal, allocations stored in registers and shared across lanes
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
    e = reduce_expr_helper(simplify(e, bounds), modulus);
    if (is_const_one(simplify(e >= 0 && e < modulus, bounds))) {
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
            } else if (const Expr *e = dependent_vars.find(var->name)) {
                return *e;
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
        // that only lane zero is active.
        if (can_prove(!op->condition || Variable::make(Int(32), lane_var) <= 0)) {
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
            bind_bounds_if(is_const(op->min) && is_const(op->max),
                           bounds, op->name, Interval(op->min, op->max));
        ScopedBinding<Expr>
            bound_dependent_if((expr_uses_vars(op->min, dependent_vars) ||
                                expr_uses_vars(op->max, dependent_vars)),
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
            message << "And the following indices by lane zero:\n";
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
        return is_const_one(simplify(e, bounds));
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
            if (s.defined()) {
                s = simplify(s);
            }
            // A load that doesn't depend on the lane says nothing
            // about the striping.
            if (!stride.defined() && !is_const_zero(s)) {
                stride = s;
            }
        }

        if (!stride.defined()) {
            // This allocation must only accessed via single-threaded stores.
            stride = 1;
        }

        for (const Expr &e : single_stores) {
            // If only thread zero was active for the store, that makes the proof simpler.
            Expr simpler = substitute(lane_var, 0, e);
            bool this_ok = can_prove(reduce_expr(simpler / stride, warp_size, bounds) == 0);
            if (!this_ok) {
                bad.push_back(e);
            }
            ok = ok && this_ok;
        }

        if (!ok) {
            fail(bad);
        }

        return stride;
    }
};

// Make a call to one of Metal's simd_shuffle functions (or
// halide_metal_simd_shuffle_relative, which CodeGen_Metal_Dev expands
// to simd_shuffle relative to the current SIMD-group lane id).
Expr metal_shuffle(const string &name, Expr value, const Expr &arg) {
    // Metal shuffles 16- and 32-bit scalars and vectors
    // natively. Zero-extend narrower types to 32 bits.
    Type type = value.type();
    Type shuffle_type = type;
    if (type.bits() < 16) {
        shuffle_type = UInt(32, type.lanes());
        value = cast(shuffle_type, reinterpret(type.with_code(Type::UInt), value));
    } else {
        user_assert(type.bits() <= 32) << "Warp shuffles not supported for this type: " << type << "\n";
    }
    // Shuffles must be executed by all participating lanes, so mark them
    // as impure to stop later passes from moving them into lane-dependent
    // control flow (e.g. the if statements that guard stores done by
    // lane zero alone).
    Expr shuffled = Call::make(shuffle_type, name, {value, arg}, Call::Extern);
    if (shuffle_type != type) {
        // Narrow it back down
        shuffled = reinterpret(type, cast(type.with_code(Type::UInt), shuffled));
    }
    return shuffled;
}

// Rewrite stores into warp-level allocations whose index does not
// depend on the lane, so that the lanes don't clobber each other's
// stripes once the allocation is striped across them:
//
// - A mutex-free atomic update of the form a[i] = a[i] op v, where op
//   is commutative and associative, is a reduction of v across the
//   lanes. It becomes a butterfly reduction using register shuffles,
//   and lane zero alone updates a[i] with the result. Every lane must
//   participate in the shuffles, so the update must not be inside any
//   condition or loop that depends on the lane, and the lane loop must
//   not need masking.
//
// - Any other store of a lane-invariant value is performed by lane
//   zero alone, provided lane zero executes it.
class LowerWarpReductions : public IRMutator {
    using IRMutator::visit;

    const string &lane_var;
    const int warp_size;
    const std::set<string> &warp_allocs;

    // Conditions enclosing the current statement within the lane loop.
    vector<Expr> conditions;
    // Variables whose values may differ across the lanes.
    Scope<> lane_dependent;
    // Allocations inside the lane loop, which are private to each lane.
    Scope<> per_lane_allocs;
    // Number of enclosing loops with lane-dependent bounds.
    int divergent_loops = 0;

    Expr this_lane() const {
        return Variable::make(Int(32), lane_var);
    }

    // Do all lanes execute the current statement?
    bool all_lanes_active() const {
        return divergent_loops == 0 &&
               std::all_of(conditions.begin(), conditions.end(),
                           [&](const Expr &c) { return lane_invariant(c); });
    }

    // Does lane zero execute the current statement whenever any lane does?
    bool lane_zero_active() const {
        return divergent_loops == 0 &&
               std::all_of(conditions.begin(), conditions.end(), [&](const Expr &c) {
                   return lane_invariant(c) || can_prove(substitute(lane_var, 0, c));
               });
    }

    // Conservatively check if an Expr has the same value in every lane.
    bool lane_invariant(const Expr &e) const {
        if (expr_uses_vars(e, lane_dependent)) {
            return false;
        }
        bool result = true;
        visit_with(
            e,
            [&](auto *self, const Load *op) {
                result &= !per_lane_allocs.contains(op->name);
                self->visit_base(op);
            },
            [&](auto *self, const Call *op) {
                result &= op->is_pure();
                self->visit_base(op);
            });
        return result;
    }

    Stmt visit(const Allocate *op) override {
        ScopedBinding<> bind(per_lane_allocs, op->name);
        return IRMutator::visit(op);
    }

    Stmt visit(const LetStmt *op) override {
        ScopedBinding<> bind_if(!lane_invariant(op->value), lane_dependent, op->name);
        return IRMutator::visit(op);
    }

    Stmt visit(const IfThenElse *op) override {
        Expr condition = mutate(op->condition);
        conditions.push_back(condition);
        Stmt then_case = mutate(op->then_case);
        conditions.back() = !condition;
        Stmt else_case = mutate(op->else_case);
        conditions.pop_back();
        return IfThenElse::make(condition, then_case, else_case);
    }

    Stmt visit(const For *op) override {
        bool divergent = !lane_invariant(op->min) || !lane_invariant(op->max);
        ScopedValue<int> old(divergent_loops, divergent_loops + divergent);
        ScopedBinding<> bind_if(divergent, lane_dependent, op->name);
        return IRMutator::visit(op);
    }

    Stmt visit(const Store *op) override {
        if (warp_allocs.count(op->name) &&
            divergent_loops == 0 &&
            is_const_one(op->predicate) &&
            lane_invariant(op->index) &&
            lane_invariant(op->value) &&
            lane_zero_active()) {
            return IfThenElse::make(this_lane() <= 0, Stmt(op));
        }
        return op;
    }

    Stmt visit(const Atomic *op) override {
        if (!op->mutex_name.empty()) {
            return IRMutator::visit(op);
        }

        vector<pair<string, Expr>> lets;
        Stmt body = op->body;
        vector<ScopedBinding<>> bindings;
        while (const LetStmt *let = body.as<LetStmt>()) {
            lets.emplace_back(let->name, let->value);
            bindings.emplace_back(!lane_invariant(let->value), lane_dependent, let->name);
            body = let->body;
        }

        const Store *store = body.as<Store>();
        if (!store ||
            !warp_allocs.count(store->name) ||
            !is_const_one(store->predicate) ||
            !lane_invariant(store->index)) {
            return IRMutator::visit(op);
        }

        auto is_self_load = [&](const Expr &e) {
            const Load *load = e.as<Load>();
            return load && load->name == store->name && equal(load->index, store->index);
        };

        // Match a[i] = a[i] op v or a[i] = v op a[i]
        Type t = store->value.type();
        Expr a, b;
        std::function<Expr(Expr, Expr)> combine;
        if (const Add *add = store->value.as<Add>()) {
            a = add->a;
            b = add->b;
            combine = [](Expr x, Expr y) { return std::move(x) + std::move(y); };
        } else if (const Mul *mul = store->value.as<Mul>()) {
            a = mul->a;
            b = mul->b;
            combine = [](Expr x, Expr y) { return std::move(x) * std::move(y); };
        } else if (const Min *min = store->value.as<Min>()) {
            a = min->a;
            b = min->b;
            combine = [](Expr x, Expr y) { return Halide::min(std::move(x), std::move(y)); };
        } else if (const Max *max = store->value.as<Max>()) {
            a = max->a;
            b = max->b;
            combine = [](Expr x, Expr y) { return Halide::max(std::move(x), std::move(y)); };
        } else {
            return IRMutator::visit(op);
        }
        if (is_self_load(b)) {
            std::swap(a, b);
        }
        if (!is_self_load(a)) {
            return IRMutator::visit(op);
        }
        bool b_loads_self = false;
        visit_with(b, [&](auto *self, const Load *load) {
            b_loads_self |= (load->name == store->name);
            self->visit_base(load);
        });
        if (b_loads_self) {
            return IRMutator::visit(op);
        }

        user_assert(all_lanes_active())
            << "Can't lower the atomic update of " << store->name
            << " to a reduction across the gpu lanes, because not all lanes execute it. "
            << "The extent of the loop over gpu lanes must be a power of two, "
            << "and the update must not be inside a condition or loop that depends on the lane.\n";

        Expr value = b;
        for (int k = warp_size / 2; k >= 1; k /= 2) {
            string name = unique_name('t');
            Expr var = Variable::make(t, name);
            lets.emplace_back(name, value);
            value = combine(var, metal_shuffle("simd_shuffle_xor", var, make_const(UInt(16), k)));
        }
        string name = unique_name('t');
        lets.emplace_back(name, value);
        value = Variable::make(t, name);

        Stmt result = store->with(combine(a, value), store->index, store->predicate, store->alignment);
        result = IfThenElse::make(this_lane() <= 0, result);
        while (!lets.empty()) {
            result = LetStmt::make(lets.back().first, lets.back().second, result);
            lets.pop_back();
        }
        return result;
    }

public:
    LowerWarpReductions(const string &lane_var, int warp_size, const std::set<string> &warp_allocs)
        : lane_var(lane_var), warp_size(warp_size), warp_allocs(warp_allocs) {
        lane_dependent.push(lane_var);
    }
};

// Remove the Free of an allocation that is being moved elsewhere.
class RemoveFree : public IRMutator {
    using IRMutator::visit;

    const string &name;

    Stmt visit(const Free *op) override {
        if (op->name == name) {
            return Evaluate::make(0);
        }
        return op;
    }

public:
    RemoveFree(const string &name)
        : name(name) {
    }
};

// Move allocations outside the loop over lanes into the loop over
// lanes (using the striping described above), and rewrites
// stores/loads to them as register shuffle intrinsics (CUDA shfl or
// Metal simd_shuffle).
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
    DeviceAPI device_api;
    int cuda_cap;

    Stmt visit(const For *op) override {
        ScopedBinding<Interval>
            bind_if(is_const(op->min) && is_const(op->max),
                    bounds, op->name, Interval(op->min, op->max));
        if (!this_lane.defined() && op->for_type == ForType::GPULane) {

            bool should_mask = false;
            ScopedValue<Expr> old_warp_size(warp_size);
            Expr extent = simplify(op->extent());
            if (op->for_type == ForType::GPULane) {
                auto loop_size = as_const_int(extent);
                user_assert(loop_size && *loop_size <= 32)
                    << "gpu lanes loop must have constant extent of at most 32: " << extent << "\n";

                // Select a warp size - the smallest power of two that contains the loop size
                int64_t ws = 1;
                while (ws < *loop_size) {
                    ws *= 2;
                }
                should_mask = (ws != *loop_size);
                warp_size = make_const(Int(32), ws);
            } else {
                warp_size = extent;
            }
            this_lane_name = op->name;
            this_lane = Variable::make(Int(32), op->name);
            may_use_warp_shuffle = (op->for_type == ForType::GPULane);

            Stmt body = op->body;

            if (device_api == DeviceAPI::Metal) {
                std::set<string> warp_allocs;
                for (const Stmt &s : allocations) {
                    warp_allocs.insert(s.as<Allocate>()->name);
                }
                if (should_mask) {
                    // The excess lanes are masked off below.
                    body = IfThenElse::make(this_lane <= op->max, body);
                }
                body = LowerWarpReductions(op->name, (int)*as_const_int(warp_size), warp_allocs)(body);
            }

            // Figure out the shrunken size of the hoisted allocations
            // and populate the scope.
            for (const Stmt &s : allocations) {
                const Allocate *alloc = s.as<Allocate>();
                internal_assert(alloc && alloc->extents.size() == 1);
                // The allocation has been moved into the lane loop,
                // with storage striped across the warp lanes, so the
                // size required per-lane is the old size divided by
                // the number of lanes (rounded up).
                Expr extent = op->extent();
                Expr new_size = (alloc->extents[0] + extent - 1) / extent;
                new_size = simplify(new_size, bounds);
                new_size = find_constant_bound(new_size, Direction::Upper, bounds);
                auto sz = as_const_int(new_size);
                user_assert(sz) << "Warp-level allocation with non-constant size: "
                                << alloc->extents[0] << ". Use Func::bound_extent.";
                DetermineAllocStride stride(alloc->name, op->name, warp_size);
                body.accept(&stride);
                allocation_info.push(alloc->name, {(int)(*sz), stride.get_stride()});
            }

            body = mutate(body);

            if (should_mask && device_api != DeviceAPI::Metal) {
                // Mask off the excess lanes in the warp
                body = IfThenElse::make(this_lane <= op->max, body, Stmt());
            }

            // Wrap the hoisted warp-level allocations, at their new
            // reduced size.
            for (const Stmt &s : allocations) {
                const Allocate *alloc = s.as<Allocate>();
                internal_assert(alloc && alloc->extents.size() == 1);
                int new_size = allocation_info.get(alloc->name).size;
                allocation_info.pop(alloc->name);
                body = alloc->with({new_size}, alloc->condition,
                                   Block::make(body, Free::make(alloc->name)));
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
                body = alloc->with(alloc->extents, alloc->condition,
                                   Block::make(body, Free::make(alloc->name)));
            }
            allocations.clear();

            return op->with(op->min, op->min + warp_size - 1, body);
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
            const Interval *in = bounds.find(this_lane_name);
            internal_assert(in);
            Interval interval = *in;
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
        if (const auto *alloc = allocation_info.find(op->name)) {
            Expr idx = mutate(op->index);
            Expr value = mutate(op->value);
            Expr stride = alloc->stride;
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
            Expr in_warp_idx = simplify((idx / (warp_size * stride)) * stride + reduce_expr(idx, stride, bounds), bounds);
            return op->with(value, in_warp_idx, op->predicate, ModulusRemainder());
        } else {
            return IRMutator::visit(op);
        }
    }

    // Metal shuffles address SIMD-group lanes. The gpu lanes loop is the
    // innermost thread dimension, so each group of gpu lanes occupies
    // consecutive SIMD-group lanes, and an offset in gpu lane index is
    // the same offset in SIMD-group lane id. simd_shuffle_down/up require
    // the offset to be uniform across the SIMD-group, so they are only
    // used for constant offsets. Everything else is a general gather
    // relative to the current SIMD-group lane id, which CodeGen_Metal_Dev
    // emits as simd_shuffle.
    Expr make_metal_shuffle(Type type, Expr base_val, Expr lane) {
        if (const Broadcast *b = lane.as<Broadcast>()) {
            lane = b->value;
        }

        if (lane.type().is_vector()) {
            // Each vector element comes from a different lane, so do
            // one shuffle per element.
            vector<Expr> elems;
            for (int i = 0; i < type.lanes(); i++) {
                elems.push_back(make_metal_shuffle(type.element_of(),
                                                   extract_lane(base_val, i),
                                                   extract_lane(lane, i)));
            }
            return Shuffle::make_concat(elems);
        }

        Expr delta = simplify(lane - this_lane, bounds);
        if (auto c = as_const_int(delta)) {
            if (*c == 0) {
                return base_val;
            } else if (*c > 0) {
                return metal_shuffle("simd_shuffle_down", base_val, make_const(UInt(16), *c));
            } else {
                return metal_shuffle("simd_shuffle_up", base_val, make_const(UInt(16), -*c));
            }
        } else {
            return metal_shuffle("halide_metal_simd_shuffle_relative", base_val, delta);
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
            return simplify(equiv, bounds);
        }

        // Load the value to be shuffled
        Expr base_val = Load::make(type, name, idx);

        Expr scalar_lane = lane;
        if (const Broadcast *b = scalar_lane.as<Broadcast>()) {
            scalar_lane = b->value;
        }
        if (equal(scalar_lane, this_lane)) {
            // This is a regular load. No shuffling required.
            return base_val;
        }

        internal_assert(may_use_warp_shuffle) << name << ", " << idx << ", " << lane << "\n";

        // Move this_lane as far left as possible in the expression to
        // reduce the number of cases to check below.
        lane = solve_expression(lane, this_lane_name).result;

        if (device_api == DeviceAPI::Metal) {
            return make_metal_shuffle(type, base_val, lane);
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
        std::optional<int> bits;

        Expr shuffled;
        Expr membermask = (int)0xffffffff;
        if (expr_match(this_lane + wild, lane, result)) {
            // We know that 0 <= lane + wild < warp_size by how we
            // constructed it, so we can just do a shuffle down.
            Expr down = Call::make(shuffle_type, "llvm.nvvm.shfl" + sync_suffix + ".down" + intrin_suffix,
                                   shfl_args({membermask, base_val, result[0], 31}), Call::PureExtern);
            shuffled = down;
        } else if (expr_match((this_lane + wild) % wild, lane, result) &&
                   (bits = is_const_power_of_two_integer(result[1])) &&
                   *bits <= 5) {
            result[0] = simplify(result[0] % result[1], bounds);
            // Rotate. Mux a shuffle up and a shuffle down. Uses fewer
            // intermediate registers than using a general gather for
            // this.
            Expr mask = (1 << *bits) - 1;
            Expr down = Call::make(shuffle_type, "llvm.nvvm.shfl" + sync_suffix + ".down" + intrin_suffix,
                                   shfl_args({membermask, base_val, result[0], mask}), Call::PureExtern);
            Expr up = Call::make(shuffle_type, "llvm.nvvm.shfl" + sync_suffix + ".up" + intrin_suffix,
                                 shfl_args({membermask, base_val, (1 << *bits) - result[0], 0}), Call::PureExtern);
            Expr cond = (this_lane >= (1 << *bits) - result[0]);
            Expr equiv = select(cond, up, down);
            shuffled = simplify(equiv, bounds);
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
        if (const auto *alloc = allocation_info.find(op->name)) {
            Expr idx = mutate(op->index);
            Expr stride = alloc->stride;

            // Break the index into lane and stripe components
            Expr lane = simplify(reduce_expr(idx / stride, warp_size, bounds), bounds);
            idx = simplify((idx / (warp_size * stride)) * stride + reduce_expr(idx, stride, bounds), bounds);
            // We don't want the idx to depend on the lane var, so try to eliminate it
            idx = simplify(solve_expression(idx, this_lane_name).result, bounds);
            return make_warp_load(op->type, op->name, idx, lane);
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const Allocate *op) override {
        if (this_lane.defined() ||
            is_gpu_shared(op->memory_type) ||
            op->memory_type == MemoryType::Heap) {
            // Not a warp-level allocation. Warp-level storage is per-lane
            // register storage; shared and heap (global) memory are never
            // striped across lanes.
            return IRMutator::visit(op);
        } else {
            // Pick up this allocation and deposit it inside the loop over lanes at reduced size.
            // Its Free moves with it.
            allocations.emplace_back(op);
            return mutate(RemoveFree(op->name)(op->body));
        }
    }

public:
    LowerWarpShuffles(DeviceAPI device_api, int cuda_cap)
        : device_api(device_api), cuda_cap(cuda_cap) {
    }
};

bool is_warp_shuffle(const Call *op) {
    return starts_with(op->name, "llvm.nvvm.shfl.") ||
           op->name == "simd_shuffle_down" ||
           op->name == "simd_shuffle_up" ||
           op->name == "simd_shuffle_xor" ||
           op->name == "halide_metal_simd_shuffle_relative";
}

class HoistWarpShufflesFromSingleIfStmt : public IRMutator {
    using IRMutator::visit;

    // Buffers stored to inside the if, at or before the current point.
    Scope<> stored_to;

    // Lets inside the if whose values depend on a store inside the
    // if, so can't be computed before it.
    Scope<> computed_inside;

    // The lifted values, in an order in which each one only depends on
    // the ones before it.
    vector<pair<string, Expr>> lifted_lets;

    Expr visit(const Call *op) override {
        // If it was written outside this if clause but read inside of
        // it, we need to hoist it.
        if (is_warp_shuffle(op) &&
            !expr_uses_vars(op, stored_to)) {
            string name = unique_name('t');
            lifted_lets.emplace_back(name, op);
            return Variable::make(op->type, name);
        } else {
            return IRMutator::visit(op);
        }
    }

    template<typename LetOrLetStmt>
    auto visit_let(const LetOrLetStmt *op) -> decltype(op->body) {
        Expr value = mutate(op->value);

        // Anything lifted from the body comes after anything lifted
        // from the value, and may depend on this let.
        size_t first_lifted_from_body = lifted_lets.size();
        bool depends_on_store = expr_uses_vars(value, stored_to) ||
                                expr_uses_vars(value, computed_inside);
        ScopedBinding<> bind_if(depends_on_store, computed_inside, op->name);
        auto body = mutate(op->body);

        // If any of the lifted expressions use this, we also need to
        // lift this.
        bool should_lift = false;
        for (size_t i = first_lifted_from_body; i < lifted_lets.size(); i++) {
            should_lift |= expr_uses_var(lifted_lets[i].second, op->name);
        }

        if (should_lift && depends_on_store) {
            // A shuffle depends on a value that can't be computed until
            // partway through the if, so we can't hoist it.
            success = false;
        }

        if (should_lift) {
            lifted_lets.insert(lifted_lets.begin() + first_lifted_from_body, {op->name, value});
            return body;
        } else {
            return LetOrLetStmt::make(op->name, value, body);
        }
    }

    Expr visit(const Let *op) override {
        return visit_let(op);
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let(op);
    }

    Stmt visit(const For *op) override {
        // A load early in the loop body may read a value stored later
        // in the body on a previous iteration.
        visit_with(op->body, [&](auto *self, const Store *store) {
            stored_to.push(store->name);
            self->visit_base(store);
        });
        Stmt body = mutate(op->body);
        bool fail = false;
        for (const auto &p : lifted_lets) {
            fail |= expr_uses_var(p.second, op->name);
        }
        if (fail) {
            // We can't hoist. We need to bail out here.
            success = false;
        } else {
            debug(3) << "Successfully hoisted shuffle out of for loop\n";
        }
        return op->with(op->min, op->max, body);
    }

    Stmt visit(const Store *op) override {
        // The value and index are computed before the store happens.
        Stmt s = IRMutator::visit(op);
        stored_to.push(op->name);
        return s;
    }

public:
    bool success = true;

    Stmt rewrap(const Stmt &s) {
        Stmt result = rewrap_all_lets(s, lifted_lets);
        lifted_lets.clear();
        return result;
    }
};

// Push an if statement inwards until it doesn't contain any warp shuffles
class MoveIfStatementInwards : public IRMutator {
    using IRMutator::visit;

    Stmt visit(const Store *op) override {
        // Compute any warp shuffles in the store just before it, so that
        // all the lanes participate in them.
        HoistWarpShufflesFromSingleIfStmt hoister;
        Stmt store = hoister(Stmt(op));
        return hoister.rewrap(IfThenElse::make(condition, store, Stmt()));
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

    DeviceAPI device_api;

    // Variables that may differ across the lanes of a warp.
    Scope<> lane_dependent;

    // Conservatively check if an Expr may differ across the lanes of a
    // warp. Loads may be from per-lane registers, and impure calls
    // include the shuffles themselves.
    bool may_depend_on_lane(const Expr &e) {
        if (expr_uses_vars(e, lane_dependent)) {
            return true;
        }
        bool result = false;
        visit_with(
            e,
            [&](auto *, const Load *) {
                result = true;
            },
            [&](auto *self, const Call *op) {
                result |= !op->is_pure();
                self->visit_base(op);
            });
        return result;
    }

    Stmt visit(const For *op) override {
        ScopedBinding<> bind_if(op->for_type == ForType::GPULane ||
                                    may_depend_on_lane(op->min) ||
                                    may_depend_on_lane(op->max),
                                lane_dependent, op->name);
        return IRMutator::visit(op);
    }

    Stmt visit(const LetStmt *op) override {
        ScopedBinding<> bind_if(may_depend_on_lane(op->value), lane_dependent, op->name);
        return IRMutator::visit(op);
    }

    Stmt visit(const IfThenElse *op) override {
        if (device_api == DeviceAPI::Metal &&
            !may_depend_on_lane(op->condition)) {
            // All the lanes in a warp take the same branch, so they are
            // all active for any shuffles inside it.
            return IRMutator::visit(op);
        }

        // Move all Exprs that contain a shuffle out of the body of
        // the if.
        Stmt then_case = mutate(op->then_case);
        Stmt else_case = mutate(op->else_case);

        // Stores in one branch don't happen before loads in the other,
        // so hoist from each separately.
        HoistWarpShufflesFromSingleIfStmt then_hoister, else_hoister;
        Stmt hoisted_then_case = then_hoister(then_case);
        Stmt hoisted_else_case = else_hoister(else_case);
        if (then_hoister.success && else_hoister.success) {
            Stmt s = IfThenElse::make(op->condition, hoisted_then_case, hoisted_else_case);
            return then_hoister.rewrap(else_hoister.rewrap(s));
        } else {
            // Need to move the ifstmt further inwards instead.
            Stmt s = IfThenElse::make(op->condition, then_case, else_case);
            internal_assert(!else_case.defined()) << "Cannot hoist warp shuffle out of " << s << "\n";
            string pred_name = unique_name('p');
            s = MoveIfStatementInwards(Variable::make(op->condition.type(), pred_name))(then_case);
            return LetStmt::make(pred_name, op->condition, s);
        }
    }

public:
    HoistWarpShuffles(DeviceAPI device_api)
        : device_api(device_api) {
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
        if ((op->device_api == DeviceAPI::CUDA ||
             op->device_api == DeviceAPI::Metal) &&
            has_lane_loop(op)) {
            // Apple GPUs have 32-wide SIMD-groups, so a group of up to
            // 32 gpu lanes always fits within one. Other GPUs (e.g. Intel)
            // may run a kernel with narrower SIMD-groups.
            user_assert(op->device_api != DeviceAPI::Metal || target.arch == Target::ARM)
                << "gpu_lanes() is only supported for Metal on Apple GPUs (arm targets). "
                << "Target: " << target << "\n";
            Stmt s = op;
            s = LowerWarpShuffles(op->device_api, target.get_cuda_capability_lower_bound())(s);
            s = HoistWarpShuffles(op->device_api)(s);
            return simplify(s);
        } else {
            return IRMutator::visit(op);
        }
    }

    const Target &target;

public:
    LowerWarpShufflesInEachKernel(const Target &target)
        : target(target) {
    }
};

}  // namespace

Stmt lower_warp_shuffles(Stmt s, const Target &t) {
    s = hoist_loop_invariant_values(s);
    s = SubstituteInLaneVar()(s);
    s = simplify(s);
    s = LowerWarpShufflesInEachKernel(t)(s);
    return s;
};

}  // namespace Internal
}  // namespace Halide
