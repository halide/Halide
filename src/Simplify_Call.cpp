#include "Simplify_Internal.h"

#include "FindIntrinsics.h"
#include "Simplify.h"

#ifdef _MSC_VER
#include <intrin.h>
#endif

#include <cfenv>
#include <functional>
#include <unordered_map>

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

namespace {

// Rewrite name(broadcast(args)) to broadcast(name(args)).
// Assumes that scalars are implicitly broadcast.
Expr lift_elementwise_broadcasts(Type type, const std::string &name, std::vector<Expr> args, Call::CallType call_type) {
    if (type.lanes() == 1) {
        return Expr();
    }
    int lanes = 0;
    for (Expr &i : args) {
        if (const Broadcast *b = i.as<Broadcast>()) {
            i = b->value;
            if (lanes == 0) {
                lanes = i.type().lanes();
            } else if (lanes != i.type().lanes()) {
                // This is a broadcast of another vector, and does not match another vector argument.
                return Expr();
            }
        } else if (!i.type().is_scalar()) {
            // This is not a scalar or broadcast scalar, we can't lift broadcasts.
            return Expr();
        }
    }
    if (lanes != type.lanes()) {
        return Broadcast::make(Call::make(type.with_lanes(lanes), name, args, call_type), type.lanes() / lanes);
    } else {
        return Expr();
    }
}

}  // namespace

Expr Simplify::visit(const Call *op, ExprInfo *info) {
    // Calls implicitly depend on host, dev, mins, and strides of the buffer referenced
    if (op->call_type == Call::Image || op->call_type == Call::Halide) {
        found_buffer_reference(op->name, op->args.size());
    }

    if (info) {
        info->cast_to(op->type);
    }

    if (op->is_intrinsic(Call::unreachable)) {
        in_unreachable = true;
        return op;
    } else if (op->is_intrinsic(Call::popcount) ||
               op->is_intrinsic(Call::count_leading_zeros) ||
               op->is_intrinsic(Call::count_trailing_zeros)) {
        Expr a = mutate(op->args[0], nullptr);

        Expr unbroadcast = lift_elementwise_broadcasts(op->type, op->name, {a}, op->call_type);
        if (unbroadcast.defined()) {
            return mutate(unbroadcast, info);
        }

        auto ia = as_const_int(a);
        auto ua = as_const_uint(a);
        uint64_t u = ua.value_or(reinterpret_bits<uint64_t>(ia.value_or(0)));
        if (ia || ua) {
            const int bits = op->type.bits();
            const uint64_t mask = std::numeric_limits<uint64_t>::max() >> (64 - bits);
            u &= mask;
            static_assert(sizeof(unsigned long long) >= sizeof(uint64_t), "");
            int64_t r = 0;
            if (op->is_intrinsic(Call::popcount)) {
                // popcount *is* well-defined for ua = 0
                r = popcount64(u);
            } else if (op->is_intrinsic(Call::count_leading_zeros)) {
                // clz64() is undefined for 0, but Halide's count_leading_zeros defines clz(0) = bits
                r = u == 0 ? bits : (clz64(u) - (64 - bits));
            } else /* if (op->is_intrinsic(Call::count_trailing_zeros)) */ {
                // ctz64() is undefined for 0, but Halide's count_trailing_zeros defines clz(0) = bits
                r = u == 0 ? bits : (ctz64(u));
            }
            return make_const(op->type, r, info);
        }

        if (a.same_as(op->args[0])) {
            return op;
        } else {
            return Call::make(op->type, op->name, {std::move(a)}, Internal::Call::PureIntrinsic);
        }

    } else if (op->is_intrinsic(Call::shift_left) ||
               op->is_intrinsic(Call::shift_right)) {
        ExprInfo a_info, b_info;
        Expr a = mutate(op->args[0], &a_info);
        // TODO: When simplifying b, it would be nice to specify the min/max useful bounds, so
        // stronger simplifications could occur. For example, x >> min(-i8, 0) should be simplified
        // to x >> -max(i8, 0) and then x << max(i8, 0). This isn't safe because -i8 can overflow.
        Expr b = mutate(op->args[1], &b_info);

        if (is_const_zero(b)) {
            if (info) {
                *info = a_info;
            }
            return a;
        }

        Expr unbroadcast = lift_elementwise_broadcasts(op->type, op->name, {a, b}, op->call_type);
        if (unbroadcast.defined()) {
            return mutate(unbroadcast, info);
        }

        const Type t = op->type;

        // We might swap from a right to left shift or the reverse.
        std::string result_op = op->name;

        // If we know the sign of this shift, change it to an unsigned shift.
        if (b_info.bounds >= 0) {
            b = mutate(cast(b.type().with_code(halide_type_uint), b), nullptr);
        } else if (b.type().is_int() && b_info.bounds <= 0) {
            result_op = Call::get_intrinsic_name(op->is_intrinsic(Call::shift_right) ? Call::shift_left : Call::shift_right);
            b = mutate(cast(b.type().with_code(halide_type_uint), -b), nullptr);
        }

        // If the shift is by a constant, it should now be unsigned.
        if (auto ub = as_const_uint(b)) {
            // LLVM shl and shr instructions produce poison for
            // shifts >= typesize, so we will follow suit in our simplifier.
            if (*ub >= (uint64_t)(t.bits())) {
                clear_expr_info(info);
                return make_signed_integer_overflow(t);
            }
            if (a.type().is_uint() || *ub < ((uint64_t)t.bits() - 1)) {
                b = make_const(t, ((int64_t)1) << *ub, nullptr);
                if (result_op == Call::get_intrinsic_name(Call::shift_left)) {
                    return mutate(Mul::make(a, b), info);
                } else {
                    return mutate(Div::make(a, b), info);
                }
            } else {
                // For signed types, (1 << (t.bits() - 1)) will overflow into the sign bit while
                // (-32768 >> (t.bits() - 1)) propagates the sign bit, making decomposition
                // into mul or div problematic, so just special-case them here.
                if (result_op == Call::get_intrinsic_name(Call::shift_left)) {
                    return mutate(select((a & 1) != 0, make_const(t, ((int64_t)1) << *ub, nullptr), make_zero(t)), info);
                } else {
                    return mutate(select(a < 0, make_const(t, (int64_t)(-1), nullptr), make_zero(t)), info);
                }
            }
        }

        // Rewrite shifts with signed negated RHSes as shifts of the other direction.
        if (b.type().is_int()) {
            if (const Sub *sub = b.as<Sub>()) {
                if (is_const_zero(sub->a)) {
                    result_op = Call::get_intrinsic_name(op->is_intrinsic(Call::shift_right) ? Call::shift_left : Call::shift_right);
                    b = sub->b;
                    return mutate(Call::make(op->type, result_op, {a, b}, Call::PureIntrinsic), info);
                }
            }
        }

        if (a.same_as(op->args[0]) && b.same_as(op->args[1])) {
            internal_assert(result_op == op->name);
            return op;
        } else {
            return Call::make(op->type, result_op, {a, b}, Call::PureIntrinsic);
        }
    } else if (op->is_intrinsic(Call::bitwise_and)) {
        ExprInfo a_info, b_info;
        Expr a = mutate(op->args[0], &a_info);
        Expr b = mutate(op->args[1], &b_info);

        if (should_commute(a, b)) {
            std::swap(a, b);
            std::swap(a_info, b_info);
        }

        Expr unbroadcast = lift_elementwise_broadcasts(op->type, op->name, {a, b}, op->call_type);
        if (unbroadcast.defined()) {
            return mutate(unbroadcast, info);
        }

        if (info && op->type.is_int_or_uint()) {
            auto bits_known = a_info.to_bits_known(op->type) & b_info.to_bits_known(op->type);
            info->from_bits_known(bits_known, op->type);
            if (bits_known.all_bits_known()) {
                // All bits are known, so this must be a constant
                return make_const(op->type, bits_known.value, nullptr);
            }
        }

        auto ia = as_const_int(a), ib = as_const_int(b);
        auto ua = as_const_uint(a), ub = as_const_uint(b);

        if (ia && ib) {
            return make_const(op->type, *ia & *ib, info);
        } else if (ua && ub) {
            return make_const(op->type, *ua & *ub, info);
        } else if (ib &&
                   !b.type().is_max(*ib) &&
                   is_const_power_of_two_integer(*ib + 1)) {
            return mutate(Mod::make(a, make_const(a.type(), *ib + 1, nullptr)), info);
        } else if ((ub && b.type().is_max(*ub)) ||
                   (ib && *ib == -1)) {
            if (info) {
                *info = a_info;
            }
            return a;
        } else if (ub &&
                   is_const_power_of_two_integer(*ub + 1)) {
            return mutate(Mod::make(a, make_const(a.type(), *ub + 1, nullptr)), info);
        } else if (a.same_as(op->args[0]) && b.same_as(op->args[1])) {
            return op;
        } else {
            return a & b;
        }
    } else if (op->is_intrinsic(Call::bitwise_or)) {
        ExprInfo a_info, b_info;
        Expr a = mutate(op->args[0], &a_info);
        Expr b = mutate(op->args[1], &b_info);

        if (should_commute(a, b)) {
            std::swap(a, b);
            std::swap(a_info, b_info);
        }

        Expr unbroadcast = lift_elementwise_broadcasts(op->type, op->name, {a, b}, op->call_type);
        if (unbroadcast.defined()) {
            return mutate(unbroadcast, info);
        }

        if (info && op->type.is_int_or_uint()) {
            auto bits_known = a_info.to_bits_known(op->type) | b_info.to_bits_known(op->type);
            info->from_bits_known(bits_known, op->type);
            if (bits_known.all_bits_known()) {
                return make_const(op->type, bits_known.value, nullptr);
            }
        }

        auto ia = as_const_int(a), ib = as_const_int(b);
        auto ua = as_const_uint(a), ub = as_const_uint(b);
        if (ia && ib) {
            return make_const(op->type, *ia | *ib, info);
        } else if (ua && ub) {
            return make_const(op->type, *ua | *ub, info);
        } else if ((ub && *ub == 0) ||
                   (ib && *ib == 0)) {
            if (info) {
                *info = a_info;
            }
            return a;
        } else if (a.same_as(op->args[0]) && b.same_as(op->args[1])) {
            return op;
        } else {
            return a | b;
        }
    } else if (op->is_intrinsic(Call::bitwise_not)) {
        ExprInfo a_info;
        Expr a = mutate(op->args[0], &a_info);

        Expr unbroadcast = lift_elementwise_broadcasts(op->type, op->name, {a}, op->call_type);
        if (unbroadcast.defined()) {
            return mutate(unbroadcast, info);
        }

        if (info && op->type.is_int_or_uint()) {
            // We could compute bits known here, but for the purpose of bounds
            // and alignment, it's more precise to treat ~x as an all-ones bit
            // pattern minus x. We get more information that way than just
            // counting the leading zeros or ones.
            Expr e = mutate(make_const(op->type, (int64_t)(-1), nullptr) - a, info);
            // If the result of this happens to be a constant, we may as well
            // return it. This is redundant with the constant folding below, but
            // the constant folding below still needs to happen when info is
            // nullptr.
            if (info->bounds.is_single_point()) {
                return e;
            }
        }

        if (auto ia = as_const_int(a)) {
            return make_const(op->type, ~(*ia), info);
        } else if (auto ua = as_const_uint(a)) {
            return make_const(op->type, ~(*ua), info);
        } else if (a.same_as(op->args[0])) {
            return op;
        } else {
            return ~a;
        }
    } else if (op->is_intrinsic(Call::bitwise_xor)) {
        ExprInfo a_info, b_info;
        Expr a = mutate(op->args[0], &a_info);
        Expr b = mutate(op->args[1], &b_info);

        if (should_commute(a, b)) {
            std::swap(a, b);
            std::swap(a_info, b_info);
        }

        Expr unbroadcast = lift_elementwise_broadcasts(op->type, op->name, {a, b}, op->call_type);
        if (unbroadcast.defined()) {
            return mutate(unbroadcast, info);
        }

        if (info && op->type.is_int_or_uint()) {
            auto bits_known = a_info.to_bits_known(op->type) ^ b_info.to_bits_known(op->type);
            info->from_bits_known(bits_known, op->type);
        }

        auto ia = as_const_int(a), ib = as_const_int(b);
        auto ua = as_const_uint(a), ub = as_const_uint(b);
        if (ia && ib) {
            return make_const(op->type, *ia ^ *ib, info);
        } else if (ua && ub) {
            return make_const(op->type, *ua ^ *ub, info);
        } else if ((ub && *ub == 0) ||
                   (ib && *ib == 0)) {
            if (info) {
                *info = a_info;
            }
            return a;
        } else if (a.same_as(op->args[0]) && b.same_as(op->args[1])) {
            return op;
        } else {
            return a ^ b;
        }
    } else if (op->is_intrinsic(Call::abs)) {
        // Constant evaluate abs(x).
        ExprInfo a_info;
        Expr a = mutate(op->args[0], &a_info);
        ModulusRemainder negative_alignment = ModulusRemainder{0, 0} - a_info.alignment;

        Expr unbroadcast = lift_elementwise_broadcasts(op->type, op->name, {a}, op->call_type);
        if (unbroadcast.defined()) {
            return mutate(unbroadcast, info);
        }

        ExprInfo abs_info;
        abs_info.bounds = abs(a_info.bounds);
        abs_info.alignment = ModulusRemainder::unify(a_info.alignment, negative_alignment);
        abs_info.cast_to(op->type);
        abs_info.trim_bounds_using_alignment();

        if (info) {
            *info = abs_info;
        }

        if (abs_info.bounds.is_single_point()) {
            // The arg could have been something like select(x, -30, 30), or ramp(-30, 60, 2)
            return make_const(op->type, abs_info.bounds.min, info);
        }

        Type ta = a.type();
        if (auto ia = as_const_int(a)) {
            if (*ia < 0 && !(Int(64).is_min(*ia))) {
                *ia = -(*ia);
            }
            return make_const(op->type, *ia, info);
        } else if (ta.is_uint()) {
            // abs(uint) is a no-op.
            if (info) {
                *info = a_info;
            }
            return a;
        } else if (auto fa = as_const_float(a)) {
            if (*fa < 0) {
                *fa = -(*fa);
            }
            return make_const(a.type(), *fa, info);
        } else if (a.type().is_int() && a_info.bounds >= 0) {
            return mutate(cast(op->type, a), info);
        } else if (a.type().is_int() && a_info.bounds <= 0) {
            if (info) {
                // Preserve alignment info
                info->alignment = negative_alignment;
            }
            return cast(op->type, -a);
        } else if (a.same_as(op->args[0])) {
            return op;
        } else {
            return abs(a);
        }
    } else if (op->is_intrinsic(Call::absd)) {
        // Constant evaluate absd(a, b).
        ExprInfo a_info, b_info;
        Expr a = mutate(op->args[0], &a_info);
        Expr b = mutate(op->args[1], &b_info);

        Expr unbroadcast = lift_elementwise_broadcasts(op->type, op->name, {a, b}, op->call_type);
        if (unbroadcast.defined()) {
            return mutate(unbroadcast, info);
        }

        Type ta = a.type();
        // absd() should enforce identical types for a and b when the node is created
        internal_assert(ta == b.type());

        auto ia = as_const_int(a), ib = as_const_int(b);
        auto ua = as_const_uint(a), ub = as_const_uint(b);
        auto fa = as_const_float(a), fb = as_const_float(b);
        if (ta.is_int() && ia && ib) {
            // Note that absd(int, int) always produces a uint result
            internal_assert(op->type.is_uint());
            const uint64_t d = *ia > *ib ? (uint64_t)(*ia - *ib) : (uint64_t)(*ib - *ia);
            return make_const(op->type, d, info);
        } else if (ta.is_uint() && ua && ub) {
            const uint64_t d = *ua > *ub ? *ua - *ub : *ub - *ua;
            return make_const(op->type, d, info);
        } else if (fa && fb) {
            const double d = *fa > *fb ? *fa - *fb : *fb - *fa;
            return make_const(op->type, d, info);
        } else if (a.same_as(op->args[0]) && b.same_as(op->args[1])) {
            return op;
        } else {
            return absd(a, b);
        }
    } else if (op->is_intrinsic(Call::saturating_cast)) {
        internal_assert(op->args.size() == 1);
        ExprInfo a_info;
        Expr a = mutate(op->args[0], &a_info);

        // In principle we could use constant bounds here to convert saturating
        // casts to casts, but it's probably a bad idea. Saturating casts only
        // show up if the user asks for them, and they're faster than a cast on
        // some platforms. We should leave them be.

        if (is_const(a)) {
            a = lower_saturating_cast(op->type, a);
            return mutate(a, info);
        } else if (!a.same_as(op->args[0])) {
            return saturating_cast(op->type, a);
        } else {
            return op;
        }
    } else if (op->is_intrinsic(Call::stringify)) {
        // Eagerly concat constant arguments to a stringify.
        bool changed = false;
        vector<Expr> new_args;
        const StringImm *last = nullptr;
        for (const auto &a : op->args) {
            Expr arg = mutate(a, nullptr);
            if (!arg.same_as(a)) {
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
                    new_args.emplace_back(string(buf));
                }
                changed = true;
            } else if (last && float_imm) {
                snprintf(buf, sizeof(buf), "%f", float_imm->value);
                if (last) {
                    new_args.back() = last->value + buf;
                } else {
                    new_args.emplace_back(string(buf));
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
    } else if (op->is_intrinsic(Call::prefetch)) {
        // Collapse the prefetched region into lower dimension whenever is possible.
        // TODO(psuriana): Deal with negative strides and overlaps.

        internal_assert(op->args.size() % 2 == 0);  // Prefetch: {base, offset, extent0, stride0, ...}

        auto [args, changed] = mutate_with_changes(op->args);

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

                if (is_const_one(mutate(extent_0 * stride_0 == stride_1, nullptr))) {
                    Expr new_extent = mutate(extent_0 * extent_1, nullptr);
                    args.erase(args.begin() + j, args.begin() + j + 2);
                    args[i] = new_extent;
                    args[i + 1] = stride_0;
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

        if (is_const_zero(cond)) {
            // (We could simplify this to avoid evaluating the provably-false
            // expression, but since this is a degenerate condition, don't bother.)
            user_warning << "This pipeline is guaranteed to fail a require() expression at runtime: \n"
                         << Expr(op) << "\n";
        }

        Expr result;
        {
            // Can assume the condition is true when evaluating the value.
            auto t = scoped_truth(cond);
            result = mutate(op->args[1], info);
        }

        if (is_const_one(cond)) {
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
    } else if (op->is_intrinsic(Call::promise_clamped) ||
               op->is_intrinsic(Call::unsafe_promise_clamped)) {
        // If the simplifier can infer that the clamp is unnecessary,
        // we should be good to discard the promise.
        internal_assert(op->args.size() == 3);
        ExprInfo arg_info, lower_info, upper_info;
        Expr arg = mutate(op->args[0], &arg_info);
        Expr lower = mutate(op->args[1], &lower_info);
        Expr upper = mutate(op->args[2], &upper_info);

        const Broadcast *b_arg = arg.as<Broadcast>();
        const Broadcast *b_lower = lower.as<Broadcast>();
        const Broadcast *b_upper = upper.as<Broadcast>();

        if (arg_info.bounds >= lower_info.bounds &&
            arg_info.bounds <= upper_info.bounds) {
            if (info) {
                *info = arg_info;
            }
            return arg;
        } else if (b_arg && b_lower && b_upper) {
            // Move broadcasts outwards
            return Broadcast::make(Call::make(b_arg->value.type(), op->name,
                                              {b_arg->value, b_lower->value, b_upper->value},
                                              Call::Intrinsic),
                                   b_arg->lanes);
        } else if (arg.same_as(op->args[0]) &&
                   lower.same_as(op->args[1]) &&
                   upper.same_as(op->args[2])) {
            return op;

        } else {
            return Call::make(op->type, op->name,
                              {arg, lower, upper},
                              Call::Intrinsic);
        }
    } else if (Call::as_tag(op)) {
        // The bounds of the result are the bounds of the arg
        internal_assert(op->args.size() == 1);
        Expr arg = mutate(op->args[0], info);
        if (arg.same_as(op->args[0])) {
            return op;
        } else {
            return Call::make(op->type, op->name, {arg}, op->call_type);
        }
    } else if (op->is_intrinsic(Call::if_then_else)) {
        // Note that this call promises to evaluate exactly one of the conditions,
        // so this optimization should be safe.

        internal_assert(op->args.size() == 2 || op->args.size() == 3);
        Expr cond_value = mutate(op->args[0], nullptr);

        // Ignore tags for our purposes here
        Expr cond = unwrap_tags(cond_value);
        if (in_unreachable) {
            return op;
        }

        if (is_const_one(cond)) {
            return mutate(op->args[1], info);
        } else if (is_const_zero(cond)) {
            if (op->args.size() == 3) {
                return mutate(op->args[2], info);
            } else {
                return mutate(make_zero(op->type), info);
            }
        } else {
            Expr true_value = mutate(op->args[1], nullptr);
            bool true_unreachable = in_unreachable;
            in_unreachable = false;
            Expr false_value = op->args.size() == 3 ? mutate(op->args[2], nullptr) : Expr();
            bool false_unreachable = in_unreachable;

            if (true_unreachable && false_unreachable) {
                return true_value;
            }
            in_unreachable = false;
            if (true_unreachable) {
                return false_value;
            } else if (false_unreachable) {
                return true_value;
            }

            if (cond_value.same_as(op->args[0]) &&
                true_value.same_as(op->args[1]) &&
                (op->args.size() == 2 || false_value.same_as(op->args[2]))) {
                return op;
            } else {
                vector<Expr> args = {std::move(cond_value), std::move(true_value)};
                if (op->args.size() == 3) {
                    args.push_back(std::move(false_value));
                }
                return Internal::Call::make(op->type, Call::if_then_else, args, op->call_type);
            }
        }
    } else if (op->is_intrinsic(Call::mux)) {
        internal_assert(op->args.size() >= 2);
        int num_values = (int)op->args.size() - 1;
        if (num_values == 1) {
            // Mux of a single value
            return mutate(op->args[1], info);
        }
        ExprInfo index_info;
        Expr index = mutate(op->args[0], &index_info);

        // Check if the mux has statically resolved
        if (index_info.bounds.is_single_point()) {
            int64_t v = index_info.bounds.min;
            if (v >= 0 && v < num_values) {
                // In-range, return the (simplified) corresponding value.
                return mutate(op->args[v + 1], info);
            } else {
                // It's out-of-range, so return the last value.
                return mutate(op->args.back(), info);
            }
        }

        // The logic above could be extended to also truncate the
        // range of values in the case where the mux index has a
        // constant bound. This seems unlikely to ever come up though.

        bool unchanged = index.same_as(op->args[0]);
        vector<Expr> mutated_args(op->args.size());
        mutated_args[0] = index;
        for (size_t i = 1; i < op->args.size(); ++i) {
            mutated_args[i] = mutate(op->args[i], nullptr);
            unchanged &= mutated_args[i].same_as(op->args[i]);
        }

        if (unchanged) {
            return op;
        } else {
            return Call::make(op->type, Call::mux, mutated_args, Call::PureIntrinsic);
        }
    } else if (op->call_type == Call::PureExtern || op->call_type == Call::PureIntrinsic) {
        // TODO: This could probably be simplified into a single map-lookup
        // with a bit more cleverness; not sure if the reduced lookup time
        // would pay for itself (in comparison with the possible lost code clarity).

        // Handle all the PureExtern cases of float -> bool
        {
            using FnType = bool (*)(double);
            // Some GCC versions are unable to resolve std::isnan (etc) directly, so
            // wrap them in lambdas.
            const FnType is_finite = [](double a) -> bool { return std::isfinite(a); };
            const FnType is_inf = [](double a) -> bool { return std::isinf(a); };
            const FnType is_nan = [](double a) -> bool { return std::isnan(a); };
            static const std::unordered_map<std::string, FnType>
                pure_externs_f1b = {
                    {"is_finite_f16", is_finite},
                    {"is_finite_f32", is_finite},
                    {"is_finite_f64", is_finite},
                    {"is_inf_f16", is_inf},
                    {"is_inf_f32", is_inf},
                    {"is_inf_f64", is_inf},
                    {"is_nan_f16", is_nan},
                    {"is_nan_f32", is_nan},
                    {"is_nan_f64", is_nan},
                };
            auto it = pure_externs_f1b.find(op->name);
            if (it != pure_externs_f1b.end()) {
                Expr arg = mutate(op->args[0], nullptr);
                if (auto f = as_const_float(arg)) {
                    auto fn = it->second;
                    return make_bool(fn(*f));
                } else if (arg.same_as(op->args[0])) {
                    return op;
                } else {
                    return Call::make(op->type, op->name, {arg}, op->call_type);
                }
            }
            // else fall thru
        }

        // Handle all the PureExtern cases of float -> float
        // TODO: should we handle the f16 and f64 cases here? (We never did before.)
        // TODO: should we handle fast_inverse and/or fast_inverse_sqrt here?
        {
            using FnType = double (*)(double);
            static const std::unordered_map<std::string, FnType>
                pure_externs_f1 = {
                    {"acos_f32", std::acos},
                    {"acosh_f32", std::acosh},
                    {"asin_f32", std::asin},
                    {"asinh_f32", std::asinh},
                    {"atan_f32", std::atan},
                    {"atanh_f32", std::atanh},
                    {"cos_f32", std::cos},
                    {"cosh_f32", std::cosh},
                    {"exp_f32", std::exp},
                    {"log_f32", std::log},
                    {"sin_f32", std::sin},
                    {"sinh_f32", std::sinh},
                    {"sqrt_f32", std::sqrt},
                    {"tan_f32", std::tan},
                    {"tanh_f32", std::tanh},
                };
            auto it = pure_externs_f1.find(op->name);
            if (it != pure_externs_f1.end()) {
                Expr arg = mutate(op->args[0], nullptr);
                if (auto f = as_const_float(arg)) {
                    auto fn = it->second;
                    return make_const(arg.type(), fn(*f), info);
                } else if (arg.same_as(op->args[0])) {
                    return op;
                } else {
                    return Call::make(op->type, op->name, {arg}, op->call_type);
                }
            }
            // else fall thru
        }

        // Handle all the PureExtern/PureIntrinsic cases of float -> integerized-float
        {
            using FnType = double (*)(double);
            static const std::unordered_map<std::string, FnType>
                pure_externs_truncation = {
                    {"ceil_f32", std::ceil},
                    {"floor_f32", std::floor},
                    {Call::get_intrinsic_name(Call::round), [](double a) -> double {
                         std::fesetround(FE_TONEAREST);
                         a = std::nearbyint(a);
                         return a;
                     }},
                    {"trunc_f32", std::trunc},
                };
            auto it = pure_externs_truncation.find(op->name);
            if (it != pure_externs_truncation.end()) {
                internal_assert(op->args.size() == 1);
                Expr arg = mutate(op->args[0], nullptr);

                const Call *call = arg.as<Call>();
                if (auto f = as_const_float(arg)) {
                    auto fn = it->second;
                    return make_const(arg.type(), fn(*f), info);
                } else if (call && (call->call_type == Call::PureExtern || call->call_type == Call::PureIntrinsic) &&
                           (it = pure_externs_truncation.find(call->name)) != pure_externs_truncation.end()) {
                    // For any combination of these integer-valued functions, we can
                    // discard the outer function. For example, floor(ceil(x)) == ceil(x).
                    return call;
                } else if (!arg.same_as(op->args[0])) {
                    return Call::make(op->type, op->name, {arg}, op->call_type);
                } else {
                    return op;
                }
            }
            // else fall thru
        }

        // Handle all the PureExtern cases of (float, float) -> integerized-float
        {
            using FnType = double (*)(double, double);
            static const std::unordered_map<std::string, FnType>
                pure_externs_f2 = {
                    {"atan2_f32", std::atan2},
                    {"pow_f32", std::pow},
                };
            auto it = pure_externs_f2.find(op->name);
            if (it != pure_externs_f2.end()) {
                Expr arg0 = mutate(op->args[0], nullptr);
                Expr arg1 = mutate(op->args[1], nullptr);

                auto f0 = as_const_float(arg0);
                auto f1 = as_const_float(arg1);
                if (f0 && f1) {
                    auto fn = it->second;
                    return make_const(arg0.type(), fn(*f0, *f1), info);
                } else if (!arg0.same_as(op->args[0]) || !arg1.same_as(op->args[1])) {
                    return Call::make(op->type, op->name, {arg0, arg1}, op->call_type);
                } else {
                    return op;
                }
            }
            // else fall thru
        }

        // There are other PureExterns we don't bother with (e.g. fast_inverse_f32)...
        // just fall thru and take the general case.
        debug(2) << "Simplifier: unhandled PureExtern: " << op->name << "\n";
    } else if (op->is_intrinsic(Call::signed_integer_overflow)) {
        clear_expr_info(info);
    } else if (op->is_intrinsic(Call::concat_bits) && op->args.size() == 1) {
        return mutate(op->args[0], info);
    }

    // No else: we want to fall thru from the PureExtern clause.
    {
        auto [new_args, changed] = mutate_with_changes(op->args);
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
