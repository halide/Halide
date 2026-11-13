#include "RotateRegisters.h"
#include "Deinterleave.h"
#include "ExprUsesVar.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "ModulusRemainder.h"
#include "Scope.h"
#include "Simplify.h"
#include "Substitute.h"

#include <algorithm>
#include <optional>
#include <set>

namespace Halide {
namespace Internal {

using std::map;
using std::pair;
using std::string;
using std::vector;

namespace {

using Lanes = vector<int64_t>;

bool has_loads(const Expr &e) {
    bool result = false;
    visit_with(
        e,
        [&](auto *, const Load *) { result = true; },
        [&](auto *self, const Call *op) {
            result |= (op->call_type == Call::Halide || op->call_type == Call::Image ||
                       op->call_type == Call::Extern || op->call_type == Call::ExternCPlusPlus);
            self->visit_base(op);
        });
    return result;
}

// The value of each lane of an integer expression built from constants.
std::optional<Lanes> const_lanes(const Expr &e) {
    auto binary = [](const Expr &a, const Expr &b, auto op) -> std::optional<Lanes> {
        auto la = const_lanes(a), lb = const_lanes(b);
        if (!la || !lb) {
            return std::nullopt;
        }
        for (size_t i = 0; i < la->size(); i++) {
            (*la)[i] = op((*la)[i], (*lb)[i]);
        }
        return la;
    };
    if (const IntImm *i = e.as<IntImm>()) {
        return Lanes{i->value};
    } else if (const Broadcast *b = e.as<Broadcast>()) {
        auto v = const_lanes(b->value);
        if (!v) {
            return std::nullopt;
        }
        Lanes result;
        for (int i = 0; i < b->lanes; i++) {
            result.insert(result.end(), v->begin(), v->end());
        }
        return result;
    } else if (const Ramp *r = e.as<Ramp>()) {
        auto base = const_lanes(r->base), stride = const_lanes(r->stride);
        if (!base || !stride) {
            return std::nullopt;
        }
        Lanes result;
        for (int i = 0; i < r->lanes; i++) {
            for (size_t j = 0; j < base->size(); j++) {
                result.push_back((*base)[j] + i * (*stride)[j]);
            }
        }
        return result;
    } else if (const Add *a = e.as<Add>()) {
        return binary(a->a, a->b, [](int64_t x, int64_t y) { return x + y; });
    } else if (const Sub *s = e.as<Sub>()) {
        return binary(s->a, s->b, [](int64_t x, int64_t y) { return x - y; });
    } else if (const Mul *m = e.as<Mul>()) {
        return binary(m->a, m->b, [](int64_t x, int64_t y) { return x * y; });
    } else if (const Div *d = e.as<Div>()) {
        return binary(d->a, d->b, [](int64_t x, int64_t y) { return div_imp(x, y); });
    } else if (const Mod *m = e.as<Mod>()) {
        return binary(m->a, m->b, [](int64_t x, int64_t y) { return mod_imp(x, y); });
    } else if (const Shuffle *s = e.as<Shuffle>()) {
        Lanes all;
        for (const Expr &v : s->vectors) {
            auto l = const_lanes(v);
            if (!l) {
                return std::nullopt;
            }
            all.insert(all.end(), l->begin(), l->end());
        }
        Lanes result;
        for (int i : s->indices) {
            result.push_back(all[i]);
        }
        return result;
    }
    return std::nullopt;
}

Expr make_lanes(const Lanes &v) {
    if (v.size() == 1) {
        return make_const(Int(32), v[0]);
    }
    int64_t stride = v[1] - v[0];
    bool dense = true;
    for (size_t i = 1; i < v.size(); i++) {
        dense &= v[i] - v[i - 1] == stride;
    }
    if (dense) {
        if (stride == 0) {
            return Broadcast::make(make_const(Int(32), v[0]), (int)v.size());
        }
        return Ramp::make(make_const(Int(32), v[0]), make_const(Int(32), stride), (int)v.size());
    }
    vector<Expr> scalars;
    for (int64_t i : v) {
        scalars.push_back(make_const(Int(32), i));
    }
    return Shuffle::make_concat(scalars);
}

// A dimension of an allocation in registers: the slots whose index is
// multiplier * q + r + multiplier * factor * t, for 0 <= q < factor and
// 0 <= r < multiplier, share every t and r and differ only in q.
struct Digit {
    int64_t factor, multiplier;

    bool operator<(const Digit &other) const {
        return std::tie(factor, multiplier) < std::tie(other.factor, other.multiplier);
    }

    int64_t of(int64_t slot) const {
        return mod_imp(div_imp(slot, multiplier), factor);
    }

    // The slot that moves into the given one when the digit is rotated down
    // by the given amount.
    int64_t source(int64_t slot, int64_t amount) const {
        int64_t q = of(slot);
        return slot + multiplier * (mod_imp(q + amount, factor) - q);
    }
};

// A term (a % factor) * multiplier of an index, which is linear in it.
struct Term {
    Expr mod;
    Digit digit;
};

void find_terms(const Expr &e, int64_t multiplier, vector<Term> &terms) {
    if (const Add *a = e.as<Add>()) {
        find_terms(a->a, multiplier, terms);
        find_terms(a->b, multiplier, terms);
    } else if (const Sub *s = e.as<Sub>()) {
        find_terms(s->a, multiplier, terms);
        find_terms(s->b, -multiplier, terms);
    } else if (const Mul *m = e.as<Mul>()) {
        if (auto c = as_const_int(m->b)) {
            find_terms(m->a, multiplier * *c, terms);
        } else if (auto c = as_const_int(m->a)) {
            find_terms(m->b, multiplier * *c, terms);
        }
    } else if (const Ramp *r = e.as<Ramp>()) {
        find_terms(r->base, multiplier, terms);
    } else if (const Broadcast *b = e.as<Broadcast>()) {
        find_terms(b->value, multiplier, terms);
    } else if (const Mod *m = e.as<Mod>()) {
        if (auto f = as_const_int(m->b); f && *f >= 2) {
            terms.push_back({e, {*f, multiplier}});
        }
    }
}

// Which slots of an allocation in registers hold a value that may be read
// later, found by walking backwards. A load of the allocation bound to a let
// and stored straight back is a move, which carries liveness from the slot
// stored to the slot loaded instead of making the whole load live.
class SlotLiveness {
    const string &name;
    int64_t size;

    using Live = vector<bool>;

    // The slots of each let-bound load that a later move needs.
    map<string, Live> moved;

    // Whether any slot is live after each restore, on any visit.
    map<const IRNode *, bool> &live_after;
    const std::set<const IRNode *> &restores;

    void add(Live &live, const Expr &index, const Expr &predicate) {
        auto lanes = is_const_one(predicate) ? const_lanes(index) : std::nullopt;
        if (!lanes) {
            live.assign(size, true);
            return;
        }
        for (int64_t i : *lanes) {
            live[i] = true;
        }
    }

    void uses(Live &live, const IRHandle &ir) {
        visit_with(
            ir,
            [&](auto *self, const Load *op) {
                if (op->name == name) {
                    add(live, op->index, op->predicate);
                }
                self->visit_base(op);
            },
            [&](auto *, const Variable *op) {
                if (op->name == name || op->name == name + ".buffer") {
                    live.assign(size, true);
                } else if (auto it = moved.find(op->name); it != moved.end()) {
                    it->second.assign(it->second.size(), true);
                }
            });
    }

    bool stores_to_alloc(const Stmt &s) {
        bool result = false;
        visit_with(s, [&](auto *, const Store *op) { result |= op->name == name; });
        return result;
    }

    static Live merge(Live a, const Live &b) {
        for (size_t i = 0; i < a.size(); i++) {
            a[i] = a[i] || b[i];
        }
        return a;
    }

public:
    SlotLiveness(const string &name, int64_t size,
                 const std::set<const IRNode *> &restores,
                 map<const IRNode *, bool> &live_after)
        : name(name), size(size), live_after(live_after), restores(restores) {
    }

    Live before(const Stmt &s, Live live) {
        if (!s.defined()) {
            return live;
        }
        if (restores.count(s.get())) {
            bool any = std::find(live.begin(), live.end(), true) != live.end();
            live_after[s.get()] |= any;
        }
        if (const Block *b = s.as<Block>()) {
            return before(b->first, before(b->rest, std::move(live)));
        } else if (const LetStmt *l = s.as<LetStmt>()) {
            const Load *load = l->value.as<Load>();
            auto src = (load && load->name == name && is_const_one(load->predicate)) ?
                           const_lanes(load->index) :
                           std::nullopt;
            if (!src) {
                live = before(l->body, std::move(live));
                uses(live, l->value);
                return live;
            }
            internal_assert(!moved.count(l->name)) << "Shadowed let: " << l->name << "\n";
            moved[l->name] = Live(src->size(), false);
            live = before(l->body, std::move(live));
            for (size_t i = 0; i < src->size(); i++) {
                if (moved[l->name][i]) {
                    live[(*src)[i]] = true;
                }
            }
            moved.erase(l->name);
            uses(live, load->index);
            return live;
        } else if (const Store *st = s.as<Store>()) {
            if (st->name != name) {
                uses(live, s);
                return live;
            }
            auto dst = is_const_one(st->predicate) ? const_lanes(st->index) : std::nullopt;
            const Variable *v = st->value.as<Variable>();
            auto it = v ? moved.find(v->name) : moved.end();
            if (dst) {
                Live killed = live;
                for (int64_t i : *dst) {
                    killed[i] = false;
                }
                if (it != moved.end()) {
                    for (size_t i = 0; i < dst->size(); i++) {
                        it->second[i] = it->second[i] || live[(*dst)[i]];
                    }
                    live = std::move(killed);
                    uses(live, st->index);
                    return live;
                }
                live = std::move(killed);
            }
            uses(live, st->value);
            uses(live, st->index);
            uses(live, st->predicate);
            return live;
        } else if (const For *f = s.as<For>()) {
            Live entry;
            auto extent = as_const_int(simplify(f->extent()));
            if (extent && *extent >= 0 && *extent <= size) {
                entry = std::move(live);
                for (int64_t i = 0; i < *extent; i++) {
                    entry = before(f->body, std::move(entry));
                }
            } else {
                // What's live at the end of each iteration.
                Live end = live;
                while (true) {
                    Live next = merge(live, before(f->body, end));
                    if (next == end) {
                        break;
                    }
                    end = std::move(next);
                }
                entry = can_prove(f->extent() >= 1) ? before(f->body, end) : end;
            }
            uses(entry, f->min);
            uses(entry, f->max);
            return entry;
        } else if (const IfThenElse *i = s.as<IfThenElse>()) {
            live = merge(before(i->then_case, live), before(i->else_case, live));
            uses(live, i->condition);
            return live;
        } else if (const ProducerConsumer *pc = s.as<ProducerConsumer>()) {
            return before(pc->body, std::move(live));
        } else if (const Allocate *a = s.as<Allocate>()) {
            live = before(a->body, std::move(live));
            for (const Expr &e : a->extents) {
                uses(live, e);
            }
            uses(live, a->condition);
            return live;
        } else if (s.as<Evaluate>() || s.as<AssertStmt>() || s.as<Free>()) {
            uses(live, s);
            return live;
        } else {
            // Something this doesn't look inside, which might do anything.
            if (stores_to_alloc(s)) {
                return Live(size, true);
            }
            uses(live, s);
            return live;
        }
    }
};

// Remove the restores after which nothing reads the allocation. Rotating
// values nobody reads only relabels the slots, which every access after it
// agrees on.
Stmt remove_dead_restores(const Stmt &s, const string &name, int64_t size,
                          const std::set<const IRNode *> &restores) {
    map<const IRNode *, bool> live_after;
    SlotLiveness(name, size, restores, live_after).before(s, vector<bool>(size, false));
    return mutate_with(s, [&](auto *self, const Block *op) -> Stmt {
        auto dead = [&](const Stmt &x) {
            auto it = live_after.find(x.get());
            return it != live_after.end() && !it->second;
        };
        Stmt first = self->mutate(op->first), rest = self->mutate(op->rest);
        if (dead(op->first)) {
            return rest;
        } else if (dead(op->rest)) {
            return first;
        }
        return Block::make(first, rest);
    });
}

// Rotate the contents of an allocation in registers, so that every load and
// store uses a constant index. Each term (a % factor) * multiplier of an index
// is a digit of the slot it addresses. A loop advances that digit by k each
// iteration if every access inside it advances a by k. The rotation at any
// point is then the sum over the enclosing loops of k times the iteration
// count, plus an offset common to the whole allocation, and the term becomes
// ((a - rotation) % factor) * multiplier, which is constant. The first
// traversal plans the rotation, and the second applies it.
class RotateAllocation : public IRMutator {
    const Allocate *alloc;
    int64_t size;
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

    template<typename Fn>
    auto with_binding(const string &n, const Expr &value, Fn &&fn) {
        bindings.emplace_back(n, value);
        auto result = fn();
        bindings.pop_back();
        return result;
    }

    // How far each loop moves each digit per iteration.
    map<Digit, map<const For *, int64_t>> steps;

    // The part of the rotation of each digit that no loop inside the
    // allocation changes.
    map<Digit, Expr> offsets;

    // The amount each digit rotates by is always a multiple of its grain.
    map<Digit, int64_t> grain;

    // The groups of slots to move together when rotating: the slots of each
    // store, and wherever rotation can move them to.
    std::set<Lanes> units;

    // The restores after each loop, which are only needed if something reads
    // the allocation afterwards.
    std::set<const IRNode *> restores;

    using IRMutator::visit;

    // How much an expression changes per iteration of a loop.
    std::optional<int64_t> step(const Expr &e, const Loop &l) {
        Expr it = Variable::make(Int(32), l.iteration);
        return as_const_int(simplify(substitute(l.iteration, it + 1, e) - e));
    }

    // The value of a term with the rotation applied.
    std::optional<Expr> rotated(const Term &term) {
        const Mod *mod = term.mod.as<Mod>();
        int lanes = mod->type.lanes();
        auto &loop_steps = steps[term.digit];
        Expr e = rewrap_used_lets(mod->a, bindings);
        if (has_loads(e)) {
            return std::nullopt;
        }
        Expr residual = e;
        Scope<> iterations;
        for (const Loop &l : loops) {
            iterations.push(l.iteration);
            auto k = step(e, l);
            if (!k) {
                return std::nullopt;
            }
            auto [it, inserted] = loop_steps.emplace(l.op, *k);
            if (!inserted && mod_imp<int64_t>(it->second - *k, term.digit.factor) != 0) {
                return std::nullopt;
            }
            Expr i = Variable::make(Int(32), l.iteration);
            residual -= make_const(e.type(), *k) * (lanes > 1 ? Broadcast::make(i, lanes) : i);
        }
        residual = simplify(residual);
        if (expr_uses_vars(residual, iterations)) {
            return std::nullopt;
        }
        Expr &offset = offsets[term.digit];
        if (!offset.defined()) {
            offset = lanes > 1 ? extract_lane(residual, 0) : residual;
        }
        auto delta = const_lanes(simplify(residual - (lanes > 1 ? Broadcast::make(offset, lanes) : offset)));
        if (!delta) {
            return std::nullopt;
        }
        for (int64_t &d : *delta) {
            d = mod_imp(d, term.digit.factor);
        }
        return make_lanes(*delta);
    }

    // The constant index of an access with the rotation applied, after
    // checking that its terms really are digits of the slots it addresses.
    std::optional<Lanes> rotated_index(Expr index) {
        vector<Term> terms;
        find_terms(index, 1, terms);
        if (terms.empty() && size >= 2) {
            // An index in bounds is its own value mod the size.
            index = index % make_const(index.type(), size);
            terms.push_back({index, {size, 1}});
        }
        map<Digit, pair<Expr, Expr>> rotated_terms;
        for (const Term &t : terms) {
            auto q = rotated(t);
            if (t.digit.multiplier <= 0 || !q ||
                !rotated_terms.emplace(t.digit, pair<Expr, Expr>{t.mod, *q}).second) {
                return std::nullopt;
            }
        }
        auto with = [&](const Digit *zeroed) {
            Expr e = index;
            for (const auto &[d, t] : rotated_terms) {
                const auto &[mod, q] = t;
                e = substitute(mod, &d == zeroed ? make_zero(mod.type()) : q, e);
            }
            return const_lanes(e);
        };
        auto result = with(nullptr);
        if (!result) {
            return std::nullopt;
        }
        for (int64_t i : *result) {
            if (i < 0 || i >= size) {
                return std::nullopt;
            }
        }
        for (const auto &[d, t] : rotated_terms) {
            auto zeroed = with(&d);
            if (!zeroed) {
                return std::nullopt;
            }
            for (size_t i = 0; i < result->size(); i++) {
                int64_t diff = (*result)[i] - (*zeroed)[i];
                if (diff % d.multiplier != 0 || d.of((*result)[i]) != diff / d.multiplier) {
                    return std::nullopt;
                }
            }
        }
        if (rotated_terms.size() != digits_per_access) {
            if (digits_per_access == (size_t)-1) {
                digits_per_access = rotated_terms.size();
            } else {
                return std::nullopt;
            }
        }
        return result;
    }

    // Every access must have a term for every digit.
    size_t digits_per_access = -1;

    Expr access_index(const Expr &index, bool is_store) {
        if (failed) {
            return Expr();
        }
        auto i = rotated_index(index);
        if (!i) {
            failed = true;
            return Expr();
        }
        if (is_store) {
            units.insert(*i);
        }
        return make_lanes(*i);
    }

    Expr visit(const Load *op) override {
        Expr e = IRMutator::visit(op);
        if (op->name == alloc->name) {
            Expr index = access_index(op->index, false);
            if (applying) {
                const Load *l = e.as<Load>();
                internal_assert(l && index.defined());
                e = l->with(index, l->predicate, ModulusRemainder());
            }
        }
        return e;
    }

    Stmt visit(const Store *op) override {
        Stmt s = IRMutator::visit(op);
        if (op->name == alloc->name) {
            Expr index = access_index(op->index, true);
            if (applying) {
                const Store *st = s.as<Store>();
                internal_assert(st && index.defined());
                s = st->with(st->value, index, st->predicate, ModulusRemainder());
            }
        }
        return s;
    }

    Expr visit(const Variable *op) override {
        // Something like an extern stage sees the whole buffer.
        failed |= op->name == alloc->name || op->name == alloc->name + ".buffer";
        return op;
    }

    // Move every value along a digit down by the given amount, wrapping
    // around, a vector at a time where possible.
    Stmt shift(const Digit &d, int64_t amount) {
        vector<pair<string, Expr>> lets;
        vector<Stmt> stores;
        for (const Lanes &dst : units) {
            Lanes src;
            for (int64_t i : dst) {
                src.push_back(d.source(i, amount));
            }
            for (size_t start = 0, end; start < dst.size(); start = end) {
                end = start + 1;
                while (end < dst.size() &&
                       (end == start + 1 ||
                        (dst[end] - dst[end - 1] == dst[start + 1] - dst[start] &&
                         src[end] - src[end - 1] == src[start + 1] - src[start]))) {
                    end++;
                }
                Lanes dst_piece(dst.begin() + start, dst.begin() + end);
                Lanes src_piece(src.begin() + start, src.begin() + end);
                Type t = alloc->type.with_lanes((int)dst_piece.size());
                string n = unique_name(alloc->name + ".slot");
                lets.emplace_back(n, Load::make(t, alloc->name, make_lanes(src_piece)));
                stores.push_back(Store::make(alloc->name, Variable::make(t, n), make_lanes(dst_piece)));
            }
        }
        Stmt s = Block::make(stores);
        for (auto it = lets.rbegin(); it != lets.rend(); it++) {
            s = LetStmt::make(it->first, it->second, s);
        }
        return s;
    }

    Stmt rotate(const Digit &d, const Expr &amount) {
        Expr a = simplify(Mod::make(amount, make_const(Int(32), d.factor)));
        if (auto c = as_const_int(a)) {
            return *c == 0 ? Stmt() : shift(d, *c);
        }
        // Rotate by each bit of the amount in turn, so that the indices stay
        // constant.
        string n = unique_name(alloc->name + ".rotation");
        Expr var = Variable::make(Int(32), n);
        vector<Stmt> steps;
        for (int64_t b = grain.at(d); b < d.factor; b *= 2) {
            Expr bit_set = (var / make_const(Int(32), b)) % 2 == 1;
            steps.push_back(IfThenElse::make(bit_set, shift(d, b)));
        }
        return LetStmt::make(n, a, Block::make(steps));
    }

    Stmt visit(const For *op) override {
        Expr min = mutate(op->min);
        Expr max = mutate(op->max);
        string iteration = unique_name(op->name + ".iteration");
        loops.push_back({op, iteration});
        Stmt body = with_binding(op->name, op->min + Variable::make(Int(32), iteration),
                                 [&]() { return mutate(op->body); });
        loops.pop_back();
        if (!applying) {
            return op;
        }
        vector<Stmt> loop_restores;
        for (const auto &[d, loop_steps] : steps) {
            auto it = loop_steps.find(op);
            int64_t k = it == loop_steps.end() ? 0 : mod_imp(it->second, d.factor);
            if (k == 0) {
                continue;
            }
            body = Block::make(body, shift(d, k));
            Stmt restore = rotate(d, -make_const(Int(32), k) * op->extent());
            if (restore.defined()) {
                restores.insert(restore.get());
                loop_restores.push_back(restore);
            }
        }
        Stmt s = op->with(min, max, body);
        if (!loop_restores.empty()) {
            s = Block::make(s, Block::make(loop_restores));
        }
        return s;
    }

    Expr visit(const Let *op) override {
        Expr value = mutate(op->value);
        Expr body = with_binding(op->name, op->value, [&]() { return mutate(op->body); });
        return op->with(value, body);
    }

    Stmt visit(const LetStmt *op) override {
        Expr value = mutate(op->value);
        Stmt body = with_binding(op->name, op->value, [&]() { return mutate(op->body); });
        return op->with(value, body);
    }

    // Check that the digits don't overlap and that there's something to
    // rotate, and find all the places rotation can move the stored values.
    bool plan() {
        if (digits_per_access != steps.size()) {
            return false;
        }
        vector<Digit> digits;
        for (const auto &[d, s] : steps) {
            digits.push_back(d);
        }
        std::sort(digits.begin(), digits.end(), [](const Digit &a, const Digit &b) {
            return a.multiplier < b.multiplier;
        });
        int64_t below = 1;
        for (const Digit &d : digits) {
            if (d.multiplier % below != 0) {
                return false;
            }
            below = d.multiplier * d.factor;
        }
        bool any = false;
        for (const auto &[d, loop_steps] : steps) {
            int64_t g = d.factor;
            for (const auto &[loop, k] : loop_steps) {
                if (mod_imp(k, d.factor) != 0) {
                    if (loop->for_type != ForType::Serial) {
                        // Can't rotate between the iterations of a parallel
                        // loop.
                        return false;
                    }
                    any = true;
                    g = gcd(g, k);
                }
            }
            grain[d] = std::abs(g);
        }
        vector<Lanes> pending(units.begin(), units.end());
        while (!pending.empty()) {
            Lanes u = std::move(pending.back());
            pending.pop_back();
            for (const auto &[d, g] : grain) {
                Lanes v = u;
                for (int64_t &i : v) {
                    i = d.source(i, g);
                }
                if (units.insert(v).second) {
                    pending.push_back(std::move(v));
                }
            }
        }
        return any;
    }

public:
    RotateAllocation(const Allocate *alloc, int64_t size)
        : alloc(alloc), size(size) {
    }

    Stmt run(const Stmt &body) {
        mutate(body);
        if (failed || !plan()) {
            return body;
        }
        debug(3) << "Rotating " << alloc->name << "\n";
        applying = true;
        Stmt result = mutate(body);
        internal_assert(!failed);
        return remove_dead_restores(result, alloc->name, size, restores);
    }
};

class RotateRegisters : public IRMutator {
    using IRMutator::visit;

    Stmt visit(const Allocate *op) override {
        Stmt body = mutate(op->body);
        if (op->memory_type == MemoryType::Register && !op->new_expr.defined()) {
            int64_t size = 1;
            for (const Expr &e : op->extents) {
                auto c = as_const_int(e);
                size = c ? size * *c : 0;
            }
            if (size > 0) {
                body = RotateAllocation(op, size).run(body);
            }
        }
        return op->with(op->extents, op->condition, body);
    }
};

}  // namespace

Stmt rotate_registers(const Stmt &s) {
    return RotateRegisters()(s);
}

}  // namespace Internal
}  // namespace Halide
