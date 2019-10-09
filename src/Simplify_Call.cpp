#include "Simplify_Internal.h"

#include "Simplify.h"

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

namespace {

// Consider moving these to (say) Util.h if we need them elsewhere.
int popcount64(uint64_t x) {
#ifdef _MSC_VER
#if defined(_WIN64)
    return __popcnt64(x);
#else
    return __popcnt((uint32_t)(x >> 32)) + __popcnt((uint32_t)(x & 0xffffffff));
#endif
#else
    static_assert(sizeof(unsigned long long) >= sizeof(uint64_t), "");
    return __builtin_popcountll(x);
#endif
}

int clz64(uint64_t x) {
    internal_assert(x != 0);
#ifdef _MSC_VER
    unsigned long r = 0;
#if defined(_WIN64)
    return _BitScanReverse64(&r, x) ? (63 - r) : 64;
#else
    if (_BitScanReverse(&r, (uint32_t)(x >> 32))) {
        return (63 - (r + 32));
    } else if (_BitScanReverse(&r, (uint32_t)(x & 0xffffffff))) {
        return 63 - r;
    } else {
        return 64;
    }
#endif
#else
    static_assert(sizeof(unsigned long long) >= sizeof(uint64_t), "");
    constexpr int offset = (sizeof(unsigned long long) - sizeof(uint64_t)) * 8;
    return __builtin_clzll(x) + offset;
#endif
}

int ctz64(uint64_t x) {
    internal_assert(x != 0);
#ifdef _MSC_VER
    unsigned long r = 0;
#if defined(_WIN64)
    return _BitScanForward64(&r, x) ? r : 64;
#else
    if (_BitScanForward(&r, (uint32_t)(x & 0xffffffff))) {
        return r;
    } else if (_BitScanForward(&r, (uint32_t)(x >> 32))) {
        return r + 32;
    } else {
        return 64;
    }
#endif
#else
    static_assert(sizeof(unsigned long long) >= sizeof(uint64_t), "");
    return __builtin_ctzll(x);
#endif
}

}  // namespace

Expr Simplify::visit(const Call *op, ExprInfo *bounds) {
    // Calls implicitly depend on host, dev, mins, and strides of the buffer referenced
    if (op->call_type == Call::Image || op->call_type == Call::Halide) {
        found_buffer_reference(op->name, op->args.size());
    }

    if (op->is_intrinsic(Call::strict_float)) {
        ScopedValue<bool> save_no_float_simplify(no_float_simplify, true);
        Expr arg = mutate(op->args[0], nullptr);
        if (arg.same_as(op->args[0])) {
            return op;
        } else {
            return strict_float(arg);
        }
    } else if (op->is_intrinsic(Call::popcount) ||
               op->is_intrinsic(Call::count_leading_zeros) ||
               op->is_intrinsic(Call::count_trailing_zeros)) {
        Expr a = mutate(op->args[0], nullptr);

        uint64_t ua = 0;
        if (const_int(a, (int64_t *)(&ua)) || const_uint(a, &ua)) {
            const int bits = op->type.bits();
            const uint64_t mask = std::numeric_limits<uint64_t>::max() >> (64 - bits);
            ua &= mask;
            static_assert(sizeof(unsigned long long) >= sizeof(uint64_t), "");
            int r = 0;
            if (op->is_intrinsic(Call::popcount)) {
                // popcount *is* well-defined for ua = 0
                r = popcount64(ua);
            } else if (op->is_intrinsic(Call::count_leading_zeros)) {
                // clz64() is undefined for 0, but Halide's count_leading_zeros defines clz(0) = bits
                r = ua == 0 ? bits : (clz64(ua) - (64 - bits));
            } else /* if (op->is_intrinsic(Call::count_trailing_zeros)) */ {
                // ctz64() is undefined for 0, but Halide's count_trailing_zeros defines clz(0) = bits
                r = ua == 0 ? bits : (ctz64(ua));
            }
            return make_const(op->type, r);
        }

        if (a.same_as(op->args[0])) {
            return op;
        } else {
            return Call::make(op->type, op->name, {std::move(a)}, Internal::Call::PureIntrinsic);
        }

    } else if (op->is_intrinsic(Call::shift_left) ||
               op->is_intrinsic(Call::shift_right)) {
        Expr a = mutate(op->args[0], nullptr);
        Expr b = mutate(op->args[1], nullptr);

        if (is_zero(b)) {
            return a;
        }

        const Type t = op->type;

        uint64_t ub = 0;
        int64_t sb = 0;
        bool b_is_const_uint = const_uint(b, &ub);
        bool b_is_const_int = const_int(b, &sb);
        if (b_is_const_uint || b_is_const_int) {
            if (b_is_const_int) {
                ub = std::abs(sb);
            }

            // Determine which direction to shift.
            const bool b_is_pos = b_is_const_uint || (b_is_const_int && sb >= 0);
            const bool b_is_neg = b_is_const_int && sb < 0;
            const bool shift_left = ((op->is_intrinsic(Call::shift_left) && b_is_pos) ||
                                     (op->is_intrinsic(Call::shift_right) && b_is_neg));
            const bool shift_right = ((op->is_intrinsic(Call::shift_right) && b_is_pos) ||
                                      (op->is_intrinsic(Call::shift_left) && b_is_neg));

            // LLVM shl and shr instructions produce poison for
            // shifts >= typesize, so we will follow suit in our simplifier.
            user_assert(ub < (uint64_t)t.bits()) << "bitshift by a constant amount >= the type size is not legal in Halide.";
            if (a.type().is_uint() || ub < ((uint64_t)t.bits() - 1)) {
                b = make_const(t, ((int64_t)1LL) << ub);
                if (shift_left) {
                    return mutate(Mul::make(a, b), bounds);
                } else if (shift_right) {
                    return mutate(Div::make(a, b), bounds);
                }
            } else {
                // For signed types, (1 << ub) will overflow into the sign bit while
                // (-32768 >> ub) propagates the sign bit, making decomposition
                // into mul or div problematic, so just special-case them here.
                if (shift_left) {
                    return mutate(select((a & 1) != 0, make_const(t, ((int64_t)1LL) << ub), make_zero(t)), bounds);
                } else if (shift_right) {
                    return mutate(select(a < 0, make_const(t, -1), make_zero(t)), bounds);
                }
            }
        }

        if (a.same_as(op->args[0]) && b.same_as(op->args[1])) {
            return op;
        } else if (op->is_intrinsic(Call::shift_left)) {
            return a << b;
        } else {
            return a >> b;
        }
    } else if (op->is_intrinsic(Call::bitwise_and)) {
        Expr a = mutate(op->args[0], nullptr);
        Expr b = mutate(op->args[1], nullptr);

        int64_t ia, ib = 0;
        uint64_t ua, ub = 0;
        int bits;

        if (const_int(a, &ia) &&
            const_int(b, &ib)) {
            return make_const(op->type, ia & ib);
        } else if (const_uint(a, &ua) &&
                   const_uint(b, &ub)) {
            return make_const(op->type, ua & ub);
        } else if (const_int(b, &ib) &&
                   !b.type().is_max(ib) &&
                   is_const_power_of_two_integer(make_const(a.type(), ib + 1), &bits)) {
            return Mod::make(a, make_const(a.type(), ib + 1));
        } else if (const_uint(b, &ub) &&
                   b.type().is_max(ub)) {
            return a;
        } else if (const_uint(b, &ub) &&
                   is_const_power_of_two_integer(make_const(a.type(), ub + 1), &bits)) {
            return Mod::make(a, make_const(a.type(), ub + 1));
        } else if (a.same_as(op->args[0]) && b.same_as(op->args[1])) {
            return op;
        } else {
            return a & b;
        }
    } else if (op->is_intrinsic(Call::bitwise_or)) {
        Expr a = mutate(op->args[0], nullptr);
        Expr b = mutate(op->args[1], nullptr);

        int64_t ia, ib;
        uint64_t ua, ub;
        if (const_int(a, &ia) &&
            const_int(b, &ib)) {
            return make_const(op->type, ia | ib);
        } else if (const_uint(a, &ua) &&
                   const_uint(b, &ub)) {
            return make_const(op->type, ua | ub);
        } else if (a.same_as(op->args[0]) && b.same_as(op->args[1])) {
            return op;
        } else {
            return a | b;
        }
    } else if (op->is_intrinsic(Call::bitwise_not)) {
        Expr a = mutate(op->args[0], nullptr);

        int64_t ia;
        uint64_t ua;
        if (const_int(a, &ia)) {
            return make_const(op->type, ~ia);
        } else if (const_uint(a, &ua)) {
            return make_const(op->type, ~ua);
        } else if (a.same_as(op->args[0])) {
            return op;
        } else {
            return ~a;
        }
    } else if (op->is_intrinsic(Call::bitwise_xor)) {
        Expr a = mutate(op->args[0], nullptr);
        Expr b = mutate(op->args[1], nullptr);

        int64_t ia, ib;
        uint64_t ua, ub;
        if (const_int(a, &ia) &&
            const_int(b, &ib)) {
            return make_const(op->type, ia ^ ib);
        } else if (const_uint(a, &ua) &&
                   const_uint(b, &ub)) {
            return make_const(op->type, ua ^ ub);
        } else if (a.same_as(op->args[0]) && b.same_as(op->args[1])) {
            return op;
        } else {
            return a ^ b;
        }
    } else if (op->is_intrinsic(Call::reinterpret)) {
        Expr a = mutate(op->args[0], nullptr);

        int64_t ia;
        uint64_t ua;
        bool vector = op->type.is_vector() || a.type().is_vector();
        if (op->type == a.type()) {
            return a;
        } else if (const_int(a, &ia) && op->type.is_uint() && !vector) {
            // int -> uint
            return make_const(op->type, (uint64_t)ia);
        } else if (const_uint(a, &ua) && op->type.is_int() && !vector) {
            // uint -> int
            return make_const(op->type, (int64_t)ua);
        } else if (a.same_as(op->args[0])) {
            return op;
        } else {
            return reinterpret(op->type, a);
        }
    } else if (op->is_intrinsic(Call::abs)) {
        // Constant evaluate abs(x).
        ExprInfo a_bounds;
        Expr a = mutate(op->args[0], &a_bounds);

        Type ta = a.type();
        int64_t ia = 0;
        double fa = 0;
        if (ta.is_int() && const_int(a, &ia)) {
            if (ia < 0 && !(Int(64).is_min(ia))) {
                ia = -ia;
            }
            return make_const(op->type, ia);
        } else if (ta.is_uint()) {
            // abs(uint) is a no-op.
            return a;
        } else if (const_float(a, &fa)) {
            if (fa < 0) {
                fa = -fa;
            }
            return make_const(a.type(), fa);
        } else if (a.type().is_int() && a_bounds.min_defined && a_bounds.min >= 0) {
            return cast(op->type, a);
        } else if (a.type().is_int() && a_bounds.max_defined && a_bounds.max <= 0) {
            return cast(op->type, -a);
        } else if (a.same_as(op->args[0])) {
            return op;
        } else {
            return abs(a);
        }
    } else if (op->is_intrinsic(Call::absd)) {
        // Constant evaluate absd(a, b).
        ExprInfo a_bounds, b_bounds;
        Expr a = mutate(op->args[0], &a_bounds);
        Expr b = mutate(op->args[1], &b_bounds);

        Type ta = a.type();
        // absd() should enforce identical types for a and b when the node is created
        internal_assert(ta == b.type());

        int64_t ia = 0, ib = 0;
        uint64_t ua = 0, ub = 0;
        double fa = 0, fb = 0;
        if (ta.is_int() && const_int(a, &ia) && const_int(b, &ib)) {
            // Note that absd(int, int) always produces a uint result
            internal_assert(op->type.is_uint());
            const uint64_t d = ia > ib ? (uint64_t)(ia - ib) : (uint64_t)(ib - ia);
            return make_const(op->type, d);
        } else if (ta.is_uint() && const_uint(a, &ua) && const_uint(b, &ub)) {
            const uint64_t d = ua > ub ? ua - ub : ub - ua;
            return make_const(op->type, d);
        } else if (const_float(a, &fa) && const_float(b, &fb)) {
            const double d = fa > fb ? fa - fb : fb - fa;
            return make_const(op->type, d);
        } else if (a.same_as(op->args[0]) && b.same_as(op->args[1])) {
            return op;
        } else {
            return absd(a, b);
        }
    } else if (op->call_type == Call::PureExtern &&
               (op->name == "is_nan_f16" || op->name == "is_nan_f32" || op->name == "is_nan_f64")) {
        Expr arg = mutate(op->args[0], nullptr);
        double f = 0.0;
        if (const_float(arg, &f)) {
            return make_bool(std::isnan(f));
        } else if (arg.same_as(op->args[0])) {
            return op;
        } else {
            return Call::make(op->type, op->name, {arg}, op->call_type);
        }
    } else if (op->call_type == Call::PureExtern &&
               (op->name == "is_inf_f16" || op->name == "is_inf_f32" || op->name == "is_inf_f64")) {
        Expr arg = mutate(op->args[0], nullptr);
        double f = 0.0;
        if (const_float(arg, &f)) {
            return make_bool(std::isinf(f));
        } else if (arg.same_as(op->args[0])) {
            return op;
        } else {
            return Call::make(op->type, op->name, {arg}, op->call_type);
        }
    } else if (op->call_type == Call::PureExtern &&
               (op->name == "is_finite_f16" || op->name == "is_finite_f32" || op->name == "is_finite_f64")) {
        Expr arg = mutate(op->args[0], nullptr);
        double f = 0.0;
        if (const_float(arg, &f)) {
            return make_bool(std::isfinite(f));
        } else if (arg.same_as(op->args[0])) {
            return op;
        } else {
            return Call::make(op->type, op->name, {arg}, op->call_type);
        }
    } else if (op->is_intrinsic(Call::stringify)) {
        // Eagerly concat constant arguments to a stringify.
        bool changed = false;
        vector<Expr> new_args;
        const StringImm *last = nullptr;
        for (size_t i = 0; i < op->args.size(); i++) {
            Expr arg = mutate(op->args[i], nullptr);
            if (!arg.same_as(op->args[i])) {
                changed = true;
            }
            const StringImm *string_imm = arg.as<StringImm>();
            const IntImm *int_imm = arg.as<IntImm>();
            const FloatImm *float_imm = arg.as<FloatImm>();
            // We use snprintf here rather than stringstreams,
            // because the runtime's float printing is guaranteed
            // to match snprintf.
            char buf[64];  // Large enough to hold the biggest float literal.
            if (last && string_imm) {
                new_args.back() = last->value + string_imm->value;
                changed = true;
            } else if (int_imm) {
                snprintf(buf, sizeof(buf), "%lld", (long long)int_imm->value);
                if (last) {
                    new_args.back() = last->value + buf;
                } else {
                    new_args.push_back(string(buf));
                }
                changed = true;
            } else if (last && float_imm) {
                snprintf(buf, sizeof(buf), "%f", float_imm->value);
                if (last) {
                    new_args.back() = last->value + buf;
                } else {
                    new_args.push_back(string(buf));
                }
                changed = true;
            } else {
                new_args.push_back(arg);
            }
            last = new_args.back().as<StringImm>();
        }

        if (new_args.size() == 1 && new_args[0].as<StringImm>()) {
            // stringify of a string constant is just the string constant
            return new_args[0];
        } else if (changed) {
            return Call::make(op->type, op->name, new_args, op->call_type);
        } else {
            return op;
        }
    } else if (op->call_type == Call::PureExtern &&
               op->name == "sqrt_f32") {
        Expr arg = mutate(op->args[0], nullptr);

        if (const double *f = as_const_float(arg)) {
            return make_const(arg.type(), std::sqrt(*f));
        } else if (!arg.same_as(op->args[0])) {
            return Call::make(op->type, op->name, {arg}, op->call_type);
        } else {
            return op;
        }
    } else if (op->call_type == Call::PureExtern &&
               op->name == "log_f32") {
        Expr arg = mutate(op->args[0], nullptr);

        if (const double *f = as_const_float(arg)) {
            return make_const(arg.type(), std::log(*f));
        } else if (!arg.same_as(op->args[0])) {
            return Call::make(op->type, op->name, {arg}, op->call_type);
        } else {
            return op;
        }
    } else if (op->call_type == Call::PureExtern &&
               op->name == "exp_f32") {
        Expr arg = mutate(op->args[0], nullptr);

        if (const double *f = as_const_float(arg)) {
            return make_const(arg.type(), std::exp(*f));
        } else if (!arg.same_as(op->args[0])) {
            return Call::make(op->type, op->name, {arg}, op->call_type);
        } else {
            return op;
        }
    } else if (op->call_type == Call::PureExtern &&
               op->name == "pow_f32") {
        Expr arg0 = mutate(op->args[0], nullptr);
        Expr arg1 = mutate(op->args[1], nullptr);

        const double *f0 = as_const_float(arg0);
        const double *f1 = as_const_float(arg1);
        if (f0 && f1) {
            return make_const(arg0.type(), std::pow(*f0, *f1));
        } else if (!arg0.same_as(op->args[0]) || !arg1.same_as(op->args[1])) {
            return Call::make(op->type, op->name, {arg0, arg1}, op->call_type);
        } else {
            return op;
        }
    } else if (op->call_type == Call::PureExtern &&
               (op->name == "floor_f32" || op->name == "ceil_f32" ||
                op->name == "round_f32" || op->name == "trunc_f32")) {
        internal_assert(op->args.size() == 1);
        Expr arg = mutate(op->args[0], nullptr);

        const Call *call = arg.as<Call>();
        if (const double *f = as_const_float(arg)) {
            if (op->name == "floor_f32") {
                return make_const(arg.type(), std::floor(*f));
            } else if (op->name == "ceil_f32") {
                return make_const(arg.type(), std::ceil(*f));
            } else if (op->name == "round_f32") {
                return make_const(arg.type(), std::nearbyint(*f));
            } else if (op->name == "trunc_f32") {
                return make_const(arg.type(), (*f < 0 ? std::ceil(*f) : std::floor(*f)));
            } else {
                return op;
            }
        } else if (call && call->call_type == Call::PureExtern &&
                   (call->name == "floor_f32" || call->name == "ceil_f32" ||
                    call->name == "round_f32" || call->name == "trunc_f32")) {
            // For any combination of these integer-valued functions, we can
            // discard the outer function. For example, floor(ceil(x)) == ceil(x).
            return call;
        } else if (!arg.same_as(op->args[0])) {
            return Call::make(op->type, op->name, {arg}, op->call_type);
        } else {
            return op;
        }
    } else if (op->is_intrinsic(Call::prefetch)) {
        // Collapse the prefetched region into lower dimension whenever is possible.
        // TODO(psuriana): Deal with negative strides and overlaps.

        internal_assert(op->args.size() % 2 == 0);  // Format: {base, offset, extent0, min0, ...}

        vector<Expr> args(op->args);
        bool changed = false;
        for (size_t i = 0; i < op->args.size(); ++i) {
            args[i] = mutate(op->args[i], nullptr);
            if (!args[i].same_as(op->args[i])) {
                changed = true;
            }
        }

        // The {extent, stride} args in the prefetch call are sorted
        // based on the storage dimension in ascending order (i.e. innermost
        // first and outermost last), so, it is enough to check for the upper
        // triangular pairs to see if any contiguous addresses exist.
        for (size_t i = 2; i < args.size(); i += 2) {
            Expr extent_0 = args[i];
            Expr stride_0 = args[i + 1];
            for (size_t j = i + 2; j < args.size(); j += 2) {
                Expr extent_1 = args[j];
                Expr stride_1 = args[j + 1];

                if (is_one(mutate(extent_0 * stride_0 == stride_1, nullptr))) {
                    Expr new_extent = mutate(extent_0 * extent_1, nullptr);
                    Expr new_stride = stride_0;
                    args.erase(args.begin() + j, args.begin() + j + 2);
                    args[i] = new_extent;
                    args[i + 1] = new_stride;
                    i -= 2;
                    break;
                }
            }
        }
        internal_assert(args.size() <= op->args.size());

        if (changed || (args.size() != op->args.size())) {
            return Call::make(op->type, Call::prefetch, args, Call::Intrinsic);
        } else {
            return op;
        }
    } else if (op->is_intrinsic(Call::require)) {
        Expr cond = mutate(op->args[0], nullptr);
        // likely(const-bool) is deliberately not reduced
        // by the simplify(), but for our purposes here, we want
        // to ignore the likely() wrapper. (Note that this is
        // equivalent to calling can_prove() without needing to
        // create a new Simplifier instance.)
        if (const Call *c = cond.as<Call>()) {
            if (c->is_intrinsic(Call::likely)) {
                cond = c->args[0];
            }
        }

        if (is_zero(cond)) {
            // (We could simplify this to avoid evaluating the provably-false
            // expression, but since this is a degenerate condition, don't bother.)
            user_warning << "This pipeline is guaranteed to fail a require() expression at runtime: \n"
                         << Expr(op) << "\n";
        }

        Expr result;
        {
            // Can assume the condition is true when evaluating the value.
            auto t = scoped_truth(cond);
            result = mutate(op->args[1], bounds);
        }

        if (is_one(cond)) {
            return result;
        }

        Expr message = mutate(op->args[2], nullptr);

        if (cond.same_as(op->args[0]) &&
            result.same_as(op->args[1]) &&
            message.same_as(op->args[2])) {
            return op;
        } else {
            return Internal::Call::make(op->type,
                                        Internal::Call::require,
                                        {std::move(cond), std::move(result), std::move(message)},
                                        Internal::Call::PureIntrinsic);
        }
    } else {
        vector<Expr> new_args(op->args.size());
        bool changed = false;

        // Mutate the args
        for (size_t i = 0; i < op->args.size(); i++) {
            const Expr &old_arg = op->args[i];
            Expr new_arg = mutate(old_arg, nullptr);
            if (!new_arg.same_as(old_arg)) changed = true;
            new_args[i] = std::move(new_arg);
        }

        if (!changed) {
            return op;
        } else {
            return Call::make(op->type, op->name, new_args, op->call_type,
                              op->func, op->value_index, op->image, op->param);
        }
    }
}

}  // namespace Internal
}  // namespace Halide
