#include "RotateRegisters.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "ModulusRemainder.h"
#include "Monotonic.h"
#include "Scope.h"
#include "Simplify.h"

#include <optional>

namespace Halide {
namespace Internal {

using std::map;
using std::pair;
using std::string;
using std::vector;

namespace {

// Something equal to e mod the given factor when both are multiplied by m,
// with as many of its Mods and multiples of the factor removed as that
// allows, so that it changes by a constant amount per loop iteration.
Expr unwrap(const Expr &e, int64_t m, int64_t factor) {
    if (const Add *a = e.as<Add>()) {
        return Add::make(unwrap(a->a, m, factor), unwrap(a->b, m, factor));
    } else if (const Sub *s = e.as<Sub>()) {
        return Sub::make(unwrap(s->a, m, factor), unwrap(s->b, m, factor));
    } else if (const Mul *mul = e.as<Mul>()) {
        if (auto c = as_const_int(mul->b)) {
            if ((*c * m) % factor == 0) {
                return make_zero(e.type());
            }
            return Mul::make(unwrap(mul->a, m * *c, factor), mul->b);
        }
    } else if (const Mod *mod = e.as<Mod>()) {
        // a % c differs from a by a multiple of c.
        if (auto c = as_const_int(mod->b); c && (*c * m) % factor == 0) {
            return unwrap(mod->a, m, factor);
        }
    } else if (const Ramp *r = e.as<Ramp>()) {
        return Ramp::make(unwrap(r->base, m, factor), r->stride, r->lanes);
    } else if (const Broadcast *b = e.as<Broadcast>()) {
        return Broadcast::make(unwrap(b->value, m, factor), b->lanes);
    }
    return e;
}

// A scalar expression for a lane of a vector expression, in terms of a
// variable standing for the lane. Lanes of other kinds of vector than
// broadcasts and ramps of scalars can't be expressed.
class Scalarize : public IRMutator {
    using IRMutator::visit;

    Expr fail(const Expr &e) {
        failed = true;
        return make_zero(e.type().element_of());
    }

    Expr visit(const Ramp *op) override {
        if (op->base.type().is_vector()) {
            return fail(op);
        }
        return mutate(op->base) + Variable::make(Int(32), lane) * mutate(op->stride);
    }

    Expr visit(const Broadcast *op) override {
        if (op->value.type().is_vector()) {
            return fail(op);
        }
        return mutate(op->value);
    }

    // Every vector variable is bound by a let inside the expression.
    Expr visit(const Variable *op) override {
        return Variable::make(op->type.element_of(), op->name);
    }

    Expr visit(const Cast *op) override {
        return Cast::make(op->type.element_of(), mutate(op->value));
    }

    Expr visit(const Reinterpret *op) override {
        if (op->type.lanes() != op->value.type().lanes()) {
            return fail(op);
        }
        return Reinterpret::make(op->type.element_of(), mutate(op->value));
    }

    Expr visit(const Call *op) override {
        return op->type.is_vector() ? fail(op) : op;
    }

    Expr visit(const Load *op) override {
        return op->type.is_vector() ? fail(op) : op;
    }

    Expr visit(const Shuffle *op) override {
        return op->type.is_vector() ? fail(op) : op;
    }

    Expr visit(const VectorReduce *op) override {
        return fail(op);
    }

public:
    using IRMutator::mutate;
    string lane;
    bool failed = false;
};

Expr scalarize(const Expr &e, const string &lane) {
    Scalarize s;
    s.lane = lane;
    Expr result = s.mutate(e);
    return s.failed ? Expr() : result;
}

// A dimension of an allocation in registers: the slots whose index is
// multiplier * q + r + multiplier * factor * t, for 0 <= q < factor and
// 0 <= r < multiplier, share every t and r and differ only in q.
struct Digit {
    int64_t factor, multiplier;

    // The slot that moves into the given one when the digit is rotated down
    // by the given amount.
    int64_t source(int64_t slot, int64_t amount) const {
        int64_t q = mod_imp(div_imp(slot, multiplier), factor);
        return slot + multiplier * (mod_imp(q + amount, factor) - q);
    }
};

// Rotate the contents of an allocation in registers, so that every load and
// store uses a constant index. Each dimension of the allocation is a digit of
// the slot an access addresses, whose value is some a % factor. A loop
// advances the digit by k each iteration if every access inside it advances a
// by k. The rotation at any point is then the sum over the enclosing loops of
// k times the iteration count, plus an offset common to the whole allocation,
// and the digit becomes (a - rotation) % factor, which is constant. Values are
// moved a store at a time, so every store must write the same number of slots,
// aligned to that number. The first traversal plans the rotation, and the
// second applies it.
class RotateAllocation : public IRMutator {
    const Allocate *alloc;
    int64_t size;
    vector<Digit> digits;
    bool applying = false;
    bool failed = false;

    struct Loop {
        const For *op;
        // A variable standing for the loop var minus the loop min.
        string iteration;
    };
    vector<Loop> loops;

    // The enclosing lets and loop vars inside the allocation, outermost first.
    // Each loop var is its loop's min plus an iteration variable.
    vector<pair<string, Expr>> bindings;

    // How far each loop moves each digit per iteration.
    vector<map<const For *, int64_t>> steps;

    // The part of the rotation of each digit that no loop inside the
    // allocation changes.
    vector<Expr> offsets;

    // The number of slots each store writes, which rotation moves together.
    int64_t unit = 0;

    // The number of loads of the allocation visited so far, and in total.
    int loads = 0, total_loads = 0;

    // The number of loads visited by the end of each loop that isn't inside
    // another loop within the allocation.
    map<const For *, int> loads_at_end;

    // The rotations that undo what each loop leaves behind.
    map<const For *, vector<pair<int, int64_t>>> restores;

    using IRMutator::visit;

    // How much an expression changes when a variable increases by one.
    std::optional<int64_t> step(const Expr &e, const string &v) {
        ConstantInterval d = derivative_bounds(e, v);
        if (d.is_single_point()) {
            return d.min;
        }
        return std::nullopt;
    }

    // The value of a digit with the rotation applied in lane zero, and how
    // much it changes per lane, given something equal to it mod the factor.
    // Every step is exact, so what remains after removing them is the same
    // in every iteration and lane.
    std::optional<pair<int64_t, int64_t>> rotated(int digit, const Expr &e, const string &lane) {
        Expr residual = e;
        for (const Loop &l : loops) {
            auto k = step(e, l.iteration);
            if (!k) {
                return std::nullopt;
            }
            auto [it, inserted] = steps[digit].emplace(l.op, *k);
            if (!inserted && mod_imp<int64_t>(it->second - *k, digits[digit].factor) != 0) {
                return std::nullopt;
            }
            residual -= make_const(Int(32), *k) * Variable::make(Int(32), l.iteration);
        }
        auto lane_step = step(e, lane);
        if (!lane_step) {
            return std::nullopt;
        }
        residual -= make_const(Int(32), *lane_step) * Variable::make(Int(32), lane);
        Expr &offset = offsets[digit];
        if (!offset.defined()) {
            offset = residual;
        }
        auto delta = as_const_int(simplify(residual - offset));
        if (!delta) {
            return std::nullopt;
        }
        return pair{mod_imp(*delta, digits[digit].factor), *lane_step};
    }

    std::nullopt_t fail() {
        failed = true;
        return std::nullopt;
    }

    // The slots an access addresses with the rotation applied. At most one
    // digit varies across the lanes, so lane i addresses
    // base + multiplier * ((first + i * stride) % factor).
    struct Slots {
        int64_t base = 0, multiplier = 0, factor = 1, first = 0, stride = 0;
        int lanes = 1;

        int64_t operator[](int i) const {
            return base + multiplier * mod_imp(first + i * stride, factor);
        }
    };

    // The slots an index addresses with the rotation applied, or nothing if
    // the allocation can't be rotated. An index in bounds is the sum of its
    // digits times their multipliers.
    std::optional<Slots> rotated_index(const Expr &index) {
        Slots slots;
        slots.lanes = index.type().lanes();
        string lane = unique_name("lane");
        Expr e = scalarize(rewrap_used_lets(index, bindings), lane);
        if (!e.defined() || !is_pure(e)) {
            return fail();
        }
        Scope<Interval> lane_bounds;
        lane_bounds.push(lane, Interval(0, slots.lanes - 1));
        for (size_t i = 0; i < digits.size(); i++) {
            const Digit &d = digits[i];
            Expr q = simplify((e / make_const(Int(32), d.multiplier)) % make_const(Int(32), d.factor),
                              lane_bounds);
            auto r = rotated(i, unwrap(q, 1, d.factor), lane);
            if (!r) {
                return fail();
            }
            int64_t stride = mod_imp(r->second, d.factor);
            if (stride == 0) {
                slots.base += r->first * d.multiplier;
            } else if (slots.multiplier) {
                return fail();
            } else {
                slots.multiplier = d.multiplier;
                slots.factor = d.factor;
                slots.first = r->first;
                slots.stride = stride;
            }
        }
        return slots;
    }

    bool is_unit(const Slots &s) const {
        bool consecutive = s.lanes == 1 ||
                           (s.multiplier == 1 && s.stride == 1 && s.first + s.lanes <= s.factor);
        return consecutive && s.lanes == unit && s[0] % unit == 0;
    }

    Expr unit_index(int64_t start) const {
        Expr s = make_const(Int(32), start);
        return unit == 1 ? s : Ramp::make(s, 1, (int)unit);
    }

    // Load some slots as a shuffle of loads of whole units, so that no load
    // straddles the boundary between two of them.
    Expr load_from_units(const Slots &slots) {
        vector<Expr> unit_loads;
        map<int64_t, int> first_lane;
        vector<int> indices;
        for (int i = 0; i < slots.lanes; i++) {
            int64_t slot = slots[i];
            int64_t start = slot - slot % unit;
            auto [it, inserted] = first_lane.emplace(start, (int)(unit_loads.size() * unit));
            if (inserted) {
                unit_loads.push_back(Load::make(alloc->type.with_lanes((int)unit), alloc->name,
                                                unit_index(start)));
            }
            indices.push_back(it->second + (int)(slot - start));
        }
        return Shuffle::make(unit_loads, indices);
    }

    Expr visit(const Load *op) override {
        Expr e = IRMutator::visit(op);
        if (op->name == alloc->name) {
            loads++;
            auto slots = rotated_index(op->index);
            if (applying) {
                const Load *l = e.as<Load>();
                internal_assert(l && slots);
                if (is_unit(*slots)) {
                    e = l->with(unit_index((*slots)[0]), l->predicate, ModulusRemainder());
                } else {
                    // Every slot is safe to load, so the predicate can be
                    // dropped.
                    e = load_from_units(*slots);
                }
            }
        }
        return e;
    }

    Stmt visit(const Store *op) override {
        Stmt s = IRMutator::visit(op);
        if (op->name == alloc->name) {
            auto slots = rotated_index(op->index);
            if (slots && unit == 0) {
                unit = slots->lanes;
            }
            if (slots && (size % unit != 0 || !is_unit(*slots))) {
                slots = fail();
            }
            if (applying) {
                const Store *st = s.as<Store>();
                internal_assert(st && slots);
                s = st->with(st->value, unit_index((*slots)[0]), st->predicate, ModulusRemainder());
            }
        }
        return s;
    }

    Expr visit(const Variable *op) override {
        // Something like an extern stage sees the whole buffer.
        failed |= op->name == alloc->name || op->name == alloc->name + ".buffer";
        return op;
    }

    // Whether rotating a digit by the given amount moves whole units.
    bool moves_units(const Digit &d, int64_t amount) const {
        if (d.multiplier % unit == 0 || mod_imp(amount, d.factor) == 0) {
            return true;
        }
        return unit % d.multiplier == 0 && (d.factor * d.multiplier) % unit == 0 &&
               (amount * d.multiplier) % unit == 0;
    }

    // Move every value along a digit down by the given amount, wrapping
    // around.
    Stmt shift(const Digit &d, int64_t amount) {
        vector<pair<string, Expr>> lets;
        vector<Stmt> stores;
        Type t = alloc->type.with_lanes((int)unit);
        for (int64_t start = 0; start < size; start += unit) {
            string n = unique_name(alloc->name + ".slot");
            lets.emplace_back(n, Load::make(t, alloc->name, unit_index(d.source(start, amount))));
            stores.push_back(Store::make(alloc->name, Variable::make(t, n), unit_index(start)));
        }
        Stmt s = Block::make(stores);
        for (auto it = lets.rbegin(); it != lets.rend(); it++) {
            s = LetStmt::make(it->first, it->second, s);
        }
        return s;
    }

    Stmt visit(const For *op) override {
        Expr min = mutate(op->min);
        Expr max = mutate(op->max);
        string iteration = unique_name(op->name + ".iteration");
        loops.push_back({op, iteration});
        bindings.emplace_back(op->name, op->min + Variable::make(Int(32), iteration));
        Stmt body = mutate(op->body);
        bindings.pop_back();
        loops.pop_back();
        if (!applying) {
            if (loops.empty()) {
                loads_at_end[op] = loads;
            }
            return op;
        }
        for (size_t i = 0; i < digits.size(); i++) {
            auto it = steps[i].find(op);
            int64_t k = it == steps[i].end() ? 0 : mod_imp(it->second, digits[i].factor);
            if (k != 0) {
                body = Block::make(body, shift(digits[i], k));
            }
        }
        Stmt s = op->with(min, max, body);
        for (const auto &[i, amount] : restores[op]) {
            s = Block::make(s, shift(digits[i], amount));
        }
        return s;
    }

    Expr visit(const Let *op) override {
        Expr value = mutate(op->value);
        bindings.emplace_back(op->name, op->value);
        Expr body = mutate(op->body);
        bindings.pop_back();
        return op->with(value, body);
    }

    Stmt visit(const LetStmt *op) override {
        Expr value = mutate(op->value);
        bindings.emplace_back(op->name, op->value);
        Stmt body = mutate(op->body);
        bindings.pop_back();
        return op->with(value, body);
    }

    // Check that there's something to rotate, that every rotation moves whole
    // units, and that the rotation left behind by each loop is constant
    // wherever it must be undone.
    bool plan() {
        bool any = false;
        for (size_t i = 0; i < digits.size(); i++) {
            const Digit &d = digits[i];
            for (const auto &[loop, step] : steps[i]) {
                int64_t k = mod_imp(step, d.factor);
                if (k == 0) {
                    continue;
                }
                if (loop->for_type != ForType::Serial || !moves_units(d, k)) {
                    return false;
                }
                any = true;
                // Values left rotated by a loop that isn't inside another
                // loop within the allocation are only needed if they're
                // loaded afterwards.
                auto it = loads_at_end.find(loop);
                if (it != loads_at_end.end() && it->second == total_loads) {
                    continue;
                }
                auto amount = as_const_int(simplify((make_const(Int(32), -k) * loop->extent()) %
                                                    make_const(Int(32), d.factor)));
                if (!amount || !moves_units(d, *amount)) {
                    return false;
                }
                if (*amount != 0) {
                    restores[loop].emplace_back(i, *amount);
                }
            }
        }
        return any;
    }

public:
    RotateAllocation(const Allocate *alloc, int64_t size)
        : alloc(alloc), size(size) {
        int64_t multiplier = 1;
        for (const Expr &e : alloc->extents) {
            int64_t factor = *as_const_int(e);
            if (factor >= 2) {
                digits.push_back({factor, multiplier});
            }
            multiplier *= factor;
        }
        steps.resize(digits.size());
        offsets.resize(digits.size());
    }

    Stmt run(const Stmt &body) {
        mutate(body);
        total_loads = loads;
        if (failed || unit == 0 || !plan()) {
            return body;
        }
        debug(3) << "Rotating " << alloc->name << "\n";
        applying = true;
        loads = 0;
        Stmt result = mutate(body);
        internal_assert(!failed);
        return result;
    }
};

}  // namespace

Stmt rotate_registers(const Stmt &s) {
    return mutate_with(s, [](auto *self, const Allocate *op) {
        Stmt body = self->mutate(op->body);
        if (op->memory_type == MemoryType::Register && !op->new_expr.defined()) {
            int32_t size = op->constant_allocation_size();
            if (size > 0) {
                body = RotateAllocation(op, size).run(body);
            }
        }
        return op->with(op->extents, op->condition, body);
    });
}

}  // namespace Internal
}  // namespace Halide
