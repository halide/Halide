#include "VectorizeGPR.h"
#include "Deinterleave.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Scope.h"
#include "Simplify.h"
#include "Substitute.h"
#include "Target.h"

#include <map>
#include <optional>
#include <sstream>
#include <vector>

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

namespace {

std::optional<int64_t> as_const_integer(const Expr &e) {
    if (std::optional<int64_t> i = as_const_int(e)) {
        return i;
    } else if (const auto u = as_const_uint(e)) {
        return (int64_t)*u;
    }
    return std::nullopt;
}

// v shifted left or right by n bits, or zero if n is at least 64, where
// the C++ shift would be undefined. Words are never wider than 64 bits,
// so bits shifted that far would be dropped anyway.
uint64_t shl(uint64_t v, int n) {
    internal_assert(n >= 0) << "Negative shift: " << n << "\n";
    return (n < 0 || n >= 64) ? 0 : (v << n);
}

uint64_t shr(uint64_t v, int n) {
    internal_assert(n >= 0) << "Negative shift: " << n << "\n";
    return (n < 0 || n >= 64) ? 0 : (v >> n);
}

uint64_t low_bits(int bits) {
    return bits >= 64 ? ~(uint64_t)0 : (shl(1, bits) - 1);
}

// The name of the Func that a loop belongs to. Loop names have the
// form func.s<stage>.var...
string func_name_of_loop(const string &loop_name) {
    for (size_t i = loop_name.find(".s"); i != string::npos; i = loop_name.find(".s", i + 1)) {
        size_t j = i + 2;
        while (j < loop_name.size() && std::isdigit((unsigned char)loop_name[j])) {
            j++;
        }
        if (j > i + 2 && j < loop_name.size() && loop_name[j] == '.') {
            return loop_name.substr(0, i);
        }
    }
    return loop_name;
}

// Rewrites all vectors in the body of a GPR-vectorized loop as unsigned
// scalar words with the lanes packed together, lane 0 in the low bits.
class LowerGPRVectors : public IRMutator {
    const string loop_name;
    const int max_word_bits;

    // Names of vector lets and let stmts, which are now bound to words.
    Scope<> word_vars;

    using IRMutator::mutate;
    using IRMutator::visit;

    template<typename... Args>
    Expr fail(const Expr &e, Args &&...args) {
        std::ostringstream why;
        (why << ... << args);
        user_error << "In the schedule for Func " << func_name_of_loop(loop_name)
                   << ", loop " << loop_name
                   << " is vectorized with VectorizeIn::GPR, but it contains an "
                   << "operation that cannot be done lane-wise in a general-purpose register: "
                   << why.str() << ":\n  " << e << "\n"
                   << "Use VectorizeIn::SIMD (the default) to vectorize this loop in SIMD registers instead.\n";
        return Expr();
    }

    static Type word_type(const Type &t) {
        return UInt(t.bits() * t.lanes());
    }

    // The word with the given value in every lane.
    static uint64_t splat_bits(const Type &t, uint64_t v) {
        uint64_t w = 0;
        for (int i = 0; i < t.lanes(); i++) {
            w |= shl(v & low_bits(t.bits()), t.bits() * i);
        }
        return w;
    }

    static Expr splat(const Type &t, uint64_t v) {
        return make_const(word_type(t), splat_bits(t, v));
    }

    static Expr packed(const Type &t, const vector<int64_t> &lanes) {
        uint64_t w = 0;
        for (int i = 0; i < t.lanes(); i++) {
            w |= shl((uint64_t)lanes[i] & low_bits(t.bits()), t.bits() * i);
        }
        return make_const(word_type(t), w);
    }

    // The word with the scalar x of the lane type (or the corresponding
    // unsigned type) in every lane.
    static Expr splat_runtime(const Type &t, Expr x) {
        if (const auto c = as_const_uint(x)) {
            return splat(t, *c);
        } else if (const auto c = as_const_int(x)) {
            return splat(t, (uint64_t)*c);
        }
        Type lane_t = UInt(t.bits());
        if (x.type() != lane_t) {
            x = reinterpret(lane_t, x);
        }
        return cast(word_type(t), x) * splat(t, 1);
    }

    // The value of each lane of v, if they are all constants.
    std::optional<vector<int64_t>> const_lanes(const Expr &v) {
        const int lanes = v.type().lanes();
        auto as_int = as_const_integer;
        if (const auto c = as_int(v)) {
            return vector<int64_t>(lanes, *c);
        }
        if (const Ramp *r = v.as<Ramp>()) {
            const auto base = as_int(r->base), stride = as_int(r->stride);
            if (base && stride) {
                vector<int64_t> result(lanes);
                for (int i = 0; i < lanes; i++) {
                    result[i] = *base + *stride * i;
                }
                return result;
            }
        }
        vector<int64_t> result(lanes);
        for (int i = 0; i < lanes; i++) {
            const auto c = as_int(simplify(extract_lane(v, i)));
            if (!c) {
                return std::nullopt;
            }
            result[i] = *c;
        }
        return result;
    }

    // The offset of each lane of a vector index from lane 0, if they are
    // all constants. Sets base to the index of lane 0.
    std::optional<vector<int64_t>> lane_offsets(const Expr &index, Expr *base) {
        const int lanes = index.type().lanes();
        if (const Ramp *r = index.as<Ramp>(); r && r->base.type().is_scalar()) {
            if (const auto stride = as_const_int(r->stride)) {
                *base = r->base;
                vector<int64_t> result(lanes);
                for (int i = 0; i < lanes; i++) {
                    result[i] = *stride * i;
                }
                return result;
            }
        }
        if (const Broadcast *b = index.as<Broadcast>(); b && b->value.type().is_scalar()) {
            *base = b->value;
            return vector<int64_t>(lanes, 0);
        }
        *base = simplify(extract_lane(index, 0));
        vector<int64_t> result(lanes);
        for (int i = 0; i < lanes; i++) {
            const auto c = as_const_int(simplify(extract_lane(index, i) - *base));
            if (!c) {
                return std::nullopt;
            }
            result[i] = *c;
        }
        return result;
    }

    Expr swar_add(const Type &t, const Expr &a, const Expr &b) {
        // Add the lanes without their top bits, so no carry crosses into
        // the next lane, then fix up the top bits with an xor.
        const uint64_t top = shl(1, t.bits() - 1);
        Expr h = splat(t, top), not_h = splat(t, ~top);
        return ((a & not_h) + (b & not_h)) ^ ((a ^ b) & h);
    }

    Expr swar_sub(const Type &t, const Expr &a, const Expr &b) {
        // Subtract with the top bit of each lane of a set, so no borrow
        // crosses into the next lane, then fix up the top bits.
        const uint64_t top = shl(1, t.bits() - 1);
        Expr h = splat(t, top), not_h = splat(t, ~top);
        return ((a | h) - (b & not_h)) ^ ((a ^ ~b) & h);
    }

    Expr word_const(const Type &t, uint64_t v) {
        return make_const(word_type(t), v);
    }

    // Shift every lane of w by the constant c. Shifts by at least the
    // lane width fill the lane with zeros (or sign bits for a right shift
    // of signed lanes).
    Expr shift_const(const Type &t, const Expr &w, int64_t c, bool left) {
        if (c < 0) {
            return shift_const(t, w, -c, !left);
        } else if (c == 0) {
            return w;
        }
        const int bits = t.bits();
        if (left) {
            if (c >= bits) {
                return word_const(t, 0);
            }
            return (w << word_const(t, c)) & splat(t, shl(low_bits(bits), (int)c) & low_bits(bits));
        } else {
            if (c >= bits) {
                if (!t.is_int()) {
                    return word_const(t, 0);
                }
                c = bits - 1;
            }
            Expr result = (w >> word_const(t, c)) & splat(t, shr(low_bits(bits), (int)c));
            if (t.is_int()) {
                // Sign-extend from bit (bits - 1 - c) of each lane.
                Expr m = splat(t, shl(1, bits - 1 - (int)c));
                result = swar_sub(t, result ^ m, m);
            }
            return result;
        }
    }

    // Shift each lane of w by a constant that may differ per lane.
    Expr shift_lanes(const Type &t, const Expr &w, const vector<int64_t> &amounts, bool left) {
        std::map<int64_t, uint64_t> lanes_by_amount;
        for (int i = 0; i < t.lanes(); i++) {
            lanes_by_amount[amounts[i]] |= shl(low_bits(t.bits()), t.bits() * i);
        }
        if (lanes_by_amount.size() == 1) {
            return shift_const(t, w, amounts[0], left);
        }
        Expr result;
        for (const auto &[amount, lanes] : lanes_by_amount) {
            Expr part = shift_const(t, w, amount, left) & word_const(t, lanes);
            result = result.defined() ? (result | part) : part;
        }
        return result;
    }

    // Shift every lane of w by the scalar y, which must be less than the
    // lane width. A negative signed y shifts in the other direction.
    Expr shift_uniform(const Type &t, const Expr &w, const Expr &y, bool left) {
        if (y.type().is_int()) {
            Type ut = y.type().with_code(Type::UInt);
            Expr pos = shift_uniform(t, w, cast(ut, y), left);
            Expr neg = shift_uniform(t, w, cast(ut, -y), !left);
            return select(y >= 0, pos, neg);
        }
        const Type wt = word_type(t), lane_t = UInt(t.bits());
        const int bits = t.bits();
        Expr ys = cast(lane_t, y);
        Expr all_ones = make_const(lane_t, low_bits(bits));
        if (left) {
            return (w << cast(wt, y)) & splat_runtime(t, all_ones << ys);
        }
        Expr result = (w >> cast(wt, y)) & splat_runtime(t, all_ones >> ys);
        if (t.is_int()) {
            Expr m = splat_runtime(t, make_const(lane_t, shl(1, bits - 1)) >> ys);
            result = swar_sub(t, result ^ m, m);
        }
        return result;
    }

    // log2 of each lane of v, if they are all positive constant powers of
    // two (or zero, which gets the lane width, if allow_zero is set).
    std::optional<vector<int64_t>> const_log2_lanes(const Expr &v, bool allow_zero) {
        auto c = const_lanes(v);
        if (!c) {
            return std::nullopt;
        }
        const uint64_t mask = low_bits(v.type().bits());
        for (int64_t &x : *c) {
            const uint64_t u = (uint64_t)x & mask;
            if (u == 0 && allow_zero) {
                x = v.type().bits();
            } else if (const auto p = is_const_power_of_two_integer(u); p && (v.type().is_uint() || *p < v.type().bits() - 1)) {
                x = *p;
            } else {
                return std::nullopt;
            }
        }
        return c;
    }

    // A word with all the bits of lane i set if lane i of the boolean
    // vector cond is true, for a word of lanes of type t.
    Expr mask_word(const Type &t, const Expr &cond) {
        if (auto c = const_lanes(cond)) {
            vector<int64_t> mask(t.lanes());
            for (int i = 0; i < t.lanes(); i++) {
                mask[i] = (*c)[i] ? -1 : 0;
            }
            return packed(t, mask);
        } else if (const Broadcast *op = cond.as<Broadcast>(); op && op->value.type().is_scalar()) {
            return Select::make(mutate(op->value), splat(t, ~(uint64_t)0), splat(t, 0));
        } else if (const Select *op = cond.as<Select>()) {
            if (const Broadcast *b = op->condition.as<Broadcast>(); b && b->value.type().is_scalar()) {
                return Select::make(mutate(b->value), mask_word(t, op->true_value), mask_word(t, op->false_value));
            }
            Expr m = mask_word(t, op->condition);
            return (mask_word(t, op->true_value) & m) | (mask_word(t, op->false_value) & ~m);
        } else if (const And *op = cond.as<And>()) {
            return mask_word(t, op->a) & mask_word(t, op->b);
        } else if (const Or *op = cond.as<Or>()) {
            return mask_word(t, op->a) | mask_word(t, op->b);
        } else if (const Not *op = cond.as<Not>()) {
            return ~mask_word(t, op->a);
        } else if (const Call *op = Call::as_intrinsic(cond, {Call::likely, Call::likely_if_innermost})) {
            return mask_word(t, op->args[0]);
        }
        // Evaluate the condition one lane at a time.
        Expr w;
        for (int i = 0; i < t.lanes(); i++) {
            Expr lane = Select::make(mutate(simplify(extract_lane(cond, i))),
                                     word_const(t, shl(low_bits(t.bits()), t.bits() * i)),
                                     word_const(t, 0));
            w = w.defined() ? (w | lane) : lane;
        }
        return w;
    }

    Expr word_of_load(const Load *op) {
        const Type &t = op->type;
        const int lanes = t.lanes();
        if (!is_const_one(op->predicate)) {
            // Load each lane separately, if its predicate is true.
            Expr result;
            for (int i = 0; i < lanes; i++) {
                Expr p = mutate(simplify(extract_lane(op->predicate, i)));
                Expr index = mutate(simplify(extract_lane(op->index, i)));
                Expr lane = op->with(index, const_true(), ModulusRemainder());
                lane = Call::make(t.element_of(), Call::if_then_else,
                                  {p, lane, make_zero(t.element_of())}, Call::PureIntrinsic);
                lane = cast(word_type(t), reinterpret(UInt(t.bits()), lane));
                if (i > 0) {
                    lane = lane << word_const(t, t.bits() * i);
                }
                result = result.defined() ? (result | lane) : lane;
            }
            return result;
        }
        Expr base;
        const auto offsets = lane_offsets(op->index, &base);
        if (!offsets) {
            return fail(op, "a gather (a load with non-constant spacing between lanes)");
        }
        base = mutate(base);
        // Find the shortest period of the lane offsets. A period of one
        // is a broadcast of a scalar load, and a period of lanes is a
        // dense load.
        int period = 1;
        while (period < lanes) {
            bool ok = lanes % period == 0;
            for (int i = 0; ok && i < lanes; i++) {
                ok = (*offsets)[i] == i % period;
            }
            if (ok) {
                break;
            }
            period++;
        }
        bool dense = true;
        for (int i = 0; i < period; i++) {
            dense = dense && (*offsets)[i] == i;
        }
        if (!dense || (period > 1 && !is_power_of_two(period * t.bits()))) {
            return fail(op, "a load from addresses that are not consecutive");
        }
        if (period == 1) {
            Expr scalar = op->with(base, const_true(), op->alignment);
            return splat_runtime(t, scalar);
        }
        Type chunk = t.with_lanes(period);
        Expr index = Ramp::make(base, 1, period);
        Expr load = op->with(index, const_true(period), op->alignment);
        Expr w = Reinterpret::make(UInt(chunk.bits() * period), load);
        if (period == lanes) {
            return w;
        }
        uint64_t replicate = 0;
        for (int i = 0; i < lanes; i += period) {
            replicate |= shl(1, t.bits() * i);
        }
        return cast(word_type(t), w) * word_const(t, replicate);
    }

    Expr word_of_shuffle(const Shuffle *op) {
        const Type &t = op->type;
        if (op->is_slice() && op->slice_stride() == 1 && op->vectors.size() == 1) {
            const Expr &v = op->vectors[0];
            if (v.type().bits() * v.type().lanes() <= max_word_bits) {
                Expr w = to_word(v);
                return cast(word_type(t), w >> make_const(w.type(), op->slice_begin() * t.bits()));
            }
        } else if (op->is_concat()) {
            Expr result;
            int lane = 0;
            for (const Expr &v : op->vectors) {
                Expr w = cast(word_type(t), to_word_or_scalar(v));
                if (lane > 0) {
                    w = w << word_const(t, lane * t.bits());
                }
                result = result.defined() ? (result | w) : w;
                lane += v.type().lanes();
            }
            return result;
        } else if (op->is_broadcast() && op->vectors.size() == 1) {
            const Expr &v = op->vectors[0];
            Expr w = cast(word_type(t), to_word_or_scalar(v));
            const int bits = v.type().bits() * v.type().lanes();
            uint64_t replicate = 0;
            for (int i = 0; i < t.bits() * t.lanes(); i += bits) {
                replicate |= shl(1, i);
            }
            return w * word_const(t, replicate);
        }
        if (auto c = const_lanes(op)) {
            return packed(t, *c);
        }
        return fail(op, "a shuffle that moves values between lanes");
    }

    // The word for a vector, or the bits of a scalar as an unsigned
    // integer of the same width.
    Expr to_word_or_scalar(const Expr &e) {
        if (e.type().is_vector()) {
            return to_word(e);
        }
        Expr s = mutate(e);
        return s.type().is_uint() ? s : reinterpret(UInt(s.type().bits()), s);
    }

    // The word for a vector-valued Let or LetStmt body, given its value.
    template<typename LetOrLetStmt, typename Body, typename F>
    auto visit_let(const LetOrLetStmt *op, const Body &body, F mutate_body) {
        const Expr &value = op->value;
        const Type &t = value.type();
        if (t.is_vector() &&
            (t.is_bool() || !(t.is_int() || t.is_uint()) || t.bits() * t.lanes() > max_word_bits)) {
            // This vector can't be a word, but it may only be used in
            // ways that don't need one (e.g. as a condition with constant
            // lanes, or as the index of a load), so put it back into its
            // uses.
            return mutate_body(substitute(op->name, value, body));
        }
        Expr new_value;
        if (value.type().is_vector()) {
            new_value = to_word(value);
        } else {
            new_value = mutate(value);
        }
        ScopedBinding<> bind(value.type().is_vector(), word_vars, op->name);
        auto new_body = mutate_body(body);
        return LetOrLetStmt::make(op->name, new_value, new_body);
    }

    // Truncate the lanes of e to the narrower integer type t, by
    // truncating the leaves of operations that commute with truncation.
    // Returns an undefined Expr if that's not possible.
    static Expr narrow(const Expr &e, const Type &t) {
        const Type &et = e.type();
        if (!(et.is_int() || et.is_uint()) || et.bits() < t.bits()) {
            return Expr();
        } else if (et.bits() == t.bits()) {
            return et == t ? e : Cast::make(t, e);
        } else if (const Broadcast *op = e.as<Broadcast>(); op && op->value.type().is_scalar()) {
            return Broadcast::make(cast(t.element_of(), op->value), op->lanes);
        } else if (const Ramp *op = e.as<Ramp>(); op && op->base.type().is_scalar()) {
            return Ramp::make(cast(t.element_of(), op->base), cast(t.element_of(), op->stride), op->lanes);
        } else if (const Cast *op = e.as<Cast>()) {
            return narrow(op->value, t);
        }
        auto narrow_both = [&](const Expr &a, const Expr &b, auto make) {
            Expr na = narrow(a, t), nb = narrow(b, t);
            return (na.defined() && nb.defined()) ? make(na, nb) : Expr();
        };
        if (const Add *op = e.as<Add>()) {
            return narrow_both(op->a, op->b, Add::make);
        } else if (const Sub *op = e.as<Sub>()) {
            return narrow_both(op->a, op->b, Sub::make);
        } else if (const Mul *op = e.as<Mul>()) {
            return narrow_both(op->a, op->b, Mul::make);
        } else if (const Call *op = e.as<Call>();
                   op && op->is_intrinsic({Call::bitwise_and, Call::bitwise_or, Call::bitwise_xor, Call::bitwise_not})) {
            vector<Expr> args;
            for (const Expr &a : op->args) {
                args.push_back(narrow(a, t));
                if (!args.back().defined()) {
                    return Expr();
                }
            }
            return Call::make(t, op->name, args, op->call_type);
        }
        return Expr();
    }

    Expr to_word(const Expr &e) {
        const Type &t = e.type();
        internal_assert(t.is_vector());
        if (t.is_bool()) {
            return fail(e, "a boolean vector whose lanes are not all constant");
        }
        if (!t.is_int() && !t.is_uint()) {
            return fail(e, "a vector of type ", t, ", which does not have integer lanes");
        }
        const int bits = t.bits() * t.lanes();
        if (bits > max_word_bits) {
            return fail(e, "a vector of type ", t, " (", bits, " bits), which does not fit in a ",
                        max_word_bits, "-bit general-purpose register");
        }
        if (!is_power_of_two(bits)) {
            return fail(e, "a vector of type ", t, " (", bits, " bits), which is not a power of two bits wide");
        }

        if (const Load *op = e.as<Load>()) {
            return word_of_load(op);
        } else if (const Variable *op = e.as<Variable>()) {
            if (word_vars.contains(op->name)) {
                return Variable::make(word_type(t), op->name);
            }
        } else if (const Broadcast *op = e.as<Broadcast>()) {
            if (op->value.type().is_scalar()) {
                return splat_runtime(t, mutate(op->value));
            }
            Expr w = cast(word_type(t), to_word(op->value));
            const int inner_bits = op->value.type().bits() * op->value.type().lanes();
            uint64_t replicate = 0;
            for (int i = 0; i < bits; i += inner_bits) {
                replicate |= shl(1, i);
            }
            return w * word_const(t, replicate);
        } else if (const Ramp *op = e.as<Ramp>()) {
            if (auto c = const_lanes(e)) {
                return packed(t, *c);
            } else if (const auto stride = as_const_integer(op->stride); stride && op->base.type().is_scalar()) {
                vector<int64_t> offsets(t.lanes());
                for (int i = 0; i < t.lanes(); i++) {
                    offsets[i] = *stride * i;
                }
                return swar_add(t, splat_runtime(t, mutate(op->base)), packed(t, offsets));
            }
        } else if (const Let *op = e.as<Let>()) {
            return visit_let(op, op->body, [&](const Expr &b) { return to_word(b); });
        } else if (const Cast *op = e.as<Cast>()) {
            if (op->value.type().bits() == t.bits() &&
                (op->value.type().is_int() || op->value.type().is_uint())) {
                return to_word(op->value);
            } else if (Expr n = narrow(op->value, t); n.defined()) {
                return to_word(simplify(n));
            }
        } else if (const Reinterpret *op = e.as<Reinterpret>()) {
            if (op->value.type().bits() * op->value.type().lanes() == bits) {
                return to_word_or_scalar(op->value);
            }
        } else if (const Add *op = e.as<Add>()) {
            return swar_add(t, to_word(op->a), to_word(op->b));
        } else if (const Sub *op = e.as<Sub>()) {
            return swar_sub(t, to_word(op->a), to_word(op->b));
        } else if (const Mul *op = e.as<Mul>()) {
            if (auto amounts = const_log2_lanes(op->b, true)) {
                return shift_lanes(t, to_word(op->a), *amounts, true);
            } else if (auto amounts = const_log2_lanes(op->a, true)) {
                return shift_lanes(t, to_word(op->b), *amounts, true);
            }
            return fail(e, "a multiplication (only multiplication by a constant power of two is supported)");
        } else if (const Div *op = e.as<Div>()) {
            if (auto amounts = const_log2_lanes(op->b, false)) {
                return shift_lanes(t, to_word(op->a), *amounts, false);
            }
            return fail(e, "a division (only division by a constant power of two is supported)");
        } else if (const Mod *op = e.as<Mod>()) {
            if (auto amounts = const_log2_lanes(op->b, false)) {
                vector<int64_t> masks(t.lanes());
                for (int i = 0; i < t.lanes(); i++) {
                    masks[i] = (int64_t)(low_bits((int)(*amounts)[i]));
                }
                return to_word(op->a) & packed(t, masks);
            }
            return fail(e, "a modulo (only modulo by a constant power of two is supported)");
        } else if (const Select *op = e.as<Select>()) {
            if (const Broadcast *b = op->condition.as<Broadcast>(); b && b->value.type().is_scalar()) {
                return Select::make(mutate(b->value), to_word(op->true_value), to_word(op->false_value));
            }
            Expr m = mask_word(t, op->condition);
            if (is_const_zero(m)) {
                return to_word(op->false_value);
            } else if (as_const_uint(m) == low_bits(bits)) {
                return to_word(op->true_value);
            }
            return (to_word(op->true_value) & m) | (to_word(op->false_value) & ~m);
        } else if (const Shuffle *op = e.as<Shuffle>()) {
            return word_of_shuffle(op);
        } else if (const Call *op = e.as<Call>()) {
            if (op->is_intrinsic(Call::bitwise_and)) {
                return to_word(op->args[0]) & to_word(op->args[1]);
            } else if (op->is_intrinsic(Call::bitwise_or)) {
                return to_word(op->args[0]) | to_word(op->args[1]);
            } else if (op->is_intrinsic(Call::bitwise_xor)) {
                return to_word(op->args[0]) ^ to_word(op->args[1]);
            } else if (op->is_intrinsic(Call::bitwise_not)) {
                return ~to_word(op->args[0]);
            } else if (op->is_intrinsic({Call::shift_left, Call::shift_right})) {
                const bool left = op->is_intrinsic(Call::shift_left);
                const Expr &amount = op->args[1];
                if (auto c = const_lanes(amount)) {
                    return shift_lanes(t, to_word(op->args[0]), *c, left);
                } else if (const Broadcast *b = amount.as<Broadcast>(); b && b->value.type().is_scalar()) {
                    return shift_uniform(t, to_word(op->args[0]), mutate(b->value), left);
                }
                return fail(e, "a shift by an amount that varies across the lanes and is not constant");
            }
        }

        if (auto c = const_lanes(e)) {
            return packed(t, *c);
        }
        return fail(e, "this operation");
    }

public:
    LowerGPRVectors(const string &loop_name, int max_word_bits)
        : loop_name(loop_name), max_word_bits(max_word_bits) {
    }

    Expr mutate(const Expr &e) override {
        if (e.defined() && e.type().is_vector()) {
            return fail(e, "a vector used by an operation on scalars");
        }
        return IRMutator::mutate(e);
    }

protected:
    Stmt visit(const Store *op) override {
        const Type &t = op->value.type();
        if (!t.is_vector()) {
            return IRMutator::visit(op);
        }
        if (!is_const_one(op->predicate)) {
            // Store each lane separately, if its predicate is true.
            const string name = unique_name('w');
            Expr w = Variable::make(word_type(t), name);
            Stmt stores;
            for (int i = 0; i < t.lanes(); i++) {
                Expr p = mutate(simplify(extract_lane(op->predicate, i)));
                Expr index = mutate(simplify(extract_lane(op->index, i)));
                Expr lane = cast(UInt(t.bits()), w >> word_const(t, t.bits() * i));
                Stmt store = op->with(reinterpret(t.element_of(), lane), index, const_true(), ModulusRemainder());
                store = IfThenElse::make(p, store);
                stores = stores.defined() ? Block::make(stores, store) : store;
            }
            return LetStmt::make(name, to_word(op->value), stores);
        }
        Expr base;
        const auto offsets = lane_offsets(op->index, &base);
        bool dense = offsets.has_value();
        for (int i = 0; dense && i < t.lanes(); i++) {
            dense = (*offsets)[i] == i;
        }
        if (!dense) {
            fail(op->index, "a store to addresses that are not consecutive");
            return Stmt();
        }
        Expr value = Reinterpret::make(t, to_word(op->value));
        return op->with(value, Ramp::make(mutate(base), 1, t.lanes()), op->predicate, op->alignment);
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let(op, op->body, [&](const Stmt &b) { return mutate(b); });
    }

    Expr visit(const Let *op) override {
        return visit_let(op, op->body, [&](const Expr &b) { return mutate(b); });
    }

    Expr visit(const Reinterpret *op) override {
        if (op->value.type().is_vector()) {
            Expr w = to_word(op->value);
            return w.type() == op->type ? w : reinterpret(op->type, w);
        }
        return IRMutator::visit(op);
    }

    Expr visit(const Shuffle *op) override {
        internal_assert(op->indices.size() == 1);
        // Extracting a single lane.
        int lane = op->indices[0];
        for (const Expr &v : op->vectors) {
            if (lane >= v.type().lanes()) {
                lane -= v.type().lanes();
                continue;
            }
            if (v.type().is_scalar()) {
                return mutate(v);
            } else if (v.type().is_bool()) {
                return mutate(simplify(extract_lane(v, lane)));
            }
            Expr w = to_word(v);
            const int bits = v.type().bits();
            Expr bits_of_lane = cast(UInt(bits), w >> make_const(w.type(), lane * bits));
            return reinterpret(op->type, bits_of_lane);
        }
        internal_error << "Shuffle index out of range: " << Expr(op) << "\n";
        return Expr();
    }

    Expr visit(const VectorReduce *op) override {
        internal_assert(op->type.is_scalar());
        // Reduce the lanes one at a time.
        Expr result;
        for (int i = 0; i < op->value.type().lanes(); i++) {
            Expr lane = extract_lane(op->value, i);
            if (!result.defined()) {
                result = lane;
                continue;
            }
            switch (op->op) {
            case VectorReduce::Add:
                result = result + lane;
                break;
            case VectorReduce::SaturatingAdd:
                result = saturating_add(result, lane);
                break;
            case VectorReduce::Mul:
                result = result * lane;
                break;
            case VectorReduce::Min:
                result = min(result, lane);
                break;
            case VectorReduce::Max:
                result = max(result, lane);
                break;
            case VectorReduce::And:
                result = result && lane;
                break;
            case VectorReduce::Or:
                result = result || lane;
                break;
            }
        }
        return mutate(simplify(result));
    }
};

}  // namespace

Stmt lower_gpr_vectors(const Stmt &s, const string &loop_name, const Target &target) {
    Stmt result = simplify(s);
    result = LowerGPRVectors(loop_name, target.bits)(result);
    return result;
}

}  // namespace Internal
}  // namespace Halide
