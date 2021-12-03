#include "HexagonOptimize.h"
#include "Bounds.h"
#include "CSE.h"
#include "CodeGen_Internal.h"
#include "ConciseCasts.h"
#include "ExprUsesVar.h"
#include "FindIntrinsics.h"
#include "HexagonAlignment.h"
#include "IREquality.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Lerp.h"
#include "Scope.h"
#include "Simplify.h"
#include "Substitute.h"
#include <unordered_map>
#include <utility>

namespace Halide {
namespace Internal {

using std::pair;
using std::set;
using std::string;
using std::vector;

using namespace Halide::ConciseCasts;

Expr native_interleave(const Expr &x) {
    string fn;
    switch (x.type().bits()) {
    case 8:
        fn = "halide.hexagon.interleave.vb";
        break;
    case 16:
        fn = "halide.hexagon.interleave.vh";
        break;
    case 32:
        fn = "halide.hexagon.interleave.vw";
        break;
    default:
        internal_error << "Cannot interleave native vectors of type " << x.type() << "\n";
    }
    return Call::make(x.type(), fn, {x}, Call::PureExtern);
}

Expr native_deinterleave(const Expr &x) {
    string fn;
    switch (x.type().bits()) {
    case 8:
        fn = "halide.hexagon.deinterleave.vb";
        break;
    case 16:
        fn = "halide.hexagon.deinterleave.vh";
        break;
    case 32:
        fn = "halide.hexagon.deinterleave.vw";
        break;
    default:
        internal_error << "Cannot deinterleave native vectors of type " << x.type() << "\n";
    }
    return Call::make(x.type(), fn, {x}, Call::PureExtern);
}

bool is_native_interleave_op(const Expr &x, const char *name) {
    const Call *c = x.as<Call>();
    if (!c || c->args.size() != 1) {
        return false;
    }
    return starts_with(c->name, name);
}

bool is_native_interleave(const Expr &x) {
    return is_native_interleave_op(x, "halide.hexagon.interleave");
}

bool is_native_deinterleave(const Expr &x) {
    return is_native_interleave_op(x, "halide.hexagon.deinterleave");
}

string type_suffix(Type type, bool signed_variants) {
    string prefix = type.is_vector() ? ".v" : ".";
    if (type.is_int() || !signed_variants) {
        switch (type.bits()) {
        case 8:
            return prefix + "b";
        case 16:
            return prefix + "h";
        case 32:
            return prefix + "w";
        }
    } else if (type.is_uint()) {
        switch (type.bits()) {
        case 8:
            return prefix + "ub";
        case 16:
            return prefix + "uh";
        case 32:
            return prefix + "uw";
        }
    }
    internal_error << "Unsupported HVX type: " << type << "\n";
    return "";
}

string type_suffix(const Expr &a, bool signed_variants) {
    return type_suffix(a.type(), signed_variants);
}

string type_suffix(const Expr &a, const Expr &b, bool signed_variants) {
    return type_suffix(a, signed_variants) + type_suffix(b, signed_variants);
}

string type_suffix(const vector<Expr> &ops, bool signed_variants) {
    if (ops.empty()) {
        return "";
    }
    string suffix = type_suffix(ops.front(), signed_variants);
    for (size_t i = 1; i < ops.size(); i++) {
        suffix = suffix + type_suffix(ops[i], signed_variants);
    }
    return suffix;
}

namespace {

// Helper to handle various forms of multiplication.
Expr as_mul(const Expr &a) {
    if (a.as<Mul>()) {
        return a;
    } else if (const Call *wm = Call::as_intrinsic(a, {Call::widening_mul})) {
        return simplify(Mul::make(cast(wm->type, wm->args[0]), cast(wm->type, wm->args[1])));
    } else if (const Call *s = Call::as_intrinsic(a, {Call::shift_left, Call::widening_shift_left})) {
        const uint64_t *log2_b = as_const_uint(s->args[1]);
        if (log2_b) {
            Expr b = make_one(s->type) << cast(UInt(s->type.bits()), (int)*log2_b);
            return simplify(Mul::make(cast(s->type, s->args[0]), b));
        }
    }
    return Expr();
}

// Helpers to generate horizontally reducing multiply operations.
Expr halide_hexagon_add_2mpy(Type result_type, const string &suffix, Expr v0, Expr v1, Expr c0, Expr c1) {
    Expr call = Call::make(result_type, "halide.hexagon.add_2mpy" + suffix,
                           {std::move(v0), std::move(v1), std::move(c0), std::move(c1)}, Call::PureExtern);
    return native_interleave(call);
}

Expr halide_hexagon_add_2mpy(Type result_type, const string &suffix, Expr v01, Expr c01) {
    return Call::make(result_type, "halide.hexagon.add_2mpy" + suffix,
                      {std::move(v01), std::move(c01)}, Call::PureExtern);
}

Expr halide_hexagon_add_3mpy(Type result_type, const string &suffix, Expr v01, Expr c01) {
    return Call::make(result_type, "halide.hexagon.add_3mpy" + suffix,
                      {std::move(v01), std::move(c01)}, Call::PureExtern);
}

Expr halide_hexagon_add_4mpy(Type result_type, const string &suffix, Expr v01, Expr c01) {
    return Call::make(result_type, "halide.hexagon.add_4mpy" + suffix,
                      {std::move(v01), std::move(c01)}, Call::PureExtern);
}

struct Pattern {
    enum Flags {
        InterleaveResult = 1 << 0,  // After evaluating the pattern, interleave native vectors of the result.
        SwapOps01 = 1 << 1,         // Swap operands 0 and 1 prior to substitution.
        SwapOps12 = 1 << 2,         // Swap operands 1 and 2 prior to substitution.

        DeinterleaveOp0 = 1 << 5,  // Prior to evaluating the pattern, deinterleave native vectors of operand 0.
        DeinterleaveOp1 = 1 << 6,  // Same as above, but for operand 1.
        DeinterleaveOp2 = 1 << 7,
        DeinterleaveOps = DeinterleaveOp0 | DeinterleaveOp1 | DeinterleaveOp2,

        BeginDeinterleaveOp = 0,  // BeginDeinterleaveOp and EndDeinterleaveOp ensure that we check only three
        EndDeinterleaveOp = 3,    // deinterleave Op0, 1 and 2.
        // Many patterns are instructions that widen only
        // operand 0, which need to both deinterleave operand 0, and then
        // re-interleave the result.
        ReinterleaveOp0 = InterleaveResult | DeinterleaveOp0,

        NarrowOp0 = 1 << 10,  // Replace operand 0 with its half-width equivalent.
        NarrowOp1 = 1 << 11,  // Same as above, but for operand 1.
        NarrowOp2 = 1 << 12,
        NarrowOps = NarrowOp0 | NarrowOp1 | NarrowOp2,

        NarrowUnsignedOp0 = 1 << 15,  // Similar to the above, but narrow to an unsigned half width type.
        NarrowUnsignedOp1 = 1 << 16,
        NarrowUnsignedOp2 = 1 << 17,
        NarrowUnsignedOps = NarrowUnsignedOp0 | NarrowUnsignedOp1 | NarrowUnsignedOp2,

        v65orLater = 1 << 21,  // Pattern should be matched only for v65 target or later
        v66orLater = 1 << 22,  // Pattern should be matched only for v66 target or later
    };

    string intrin;  // Name of the intrinsic
    Expr pattern;   // The pattern to match against
    int flags;

    Pattern() = default;
    Pattern(const string &intrin, Expr p, int flags = 0)
        : intrin(intrin), pattern(std::move(p)), flags(flags) {
    }
};

Expr wild_u8 = Variable::make(UInt(8), "*");
Expr wild_u16 = Variable::make(UInt(16), "*");
Expr wild_u32 = Variable::make(UInt(32), "*");
Expr wild_u64 = Variable::make(UInt(64), "*");
Expr wild_i8 = Variable::make(Int(8), "*");
Expr wild_i16 = Variable::make(Int(16), "*");
Expr wild_i32 = Variable::make(Int(32), "*");
Expr wild_i64 = Variable::make(Int(64), "*");

Expr wild_u8x = Variable::make(Type(Type::UInt, 8, 0), "*");
Expr wild_u16x = Variable::make(Type(Type::UInt, 16, 0), "*");
Expr wild_u32x = Variable::make(Type(Type::UInt, 32, 0), "*");
Expr wild_u64x = Variable::make(Type(Type::UInt, 64, 0), "*");
Expr wild_i8x = Variable::make(Type(Type::Int, 8, 0), "*");
Expr wild_i16x = Variable::make(Type(Type::Int, 16, 0), "*");
Expr wild_i32x = Variable::make(Type(Type::Int, 32, 0), "*");
Expr wild_i64x = Variable::make(Type(Type::Int, 64, 0), "*");

// Check if a pattern with flags 'flags' is supported on the target.
bool check_pattern_target(int flags, const Target &target) {
    if ((flags & (Pattern::v65orLater)) &&
        !target.features_any_of({Target::HVX_v65, Target::HVX_v66})) {
        return false;
    }
    if ((flags & (Pattern::v66orLater)) &&
        !target.features_any_of({Target::HVX_v66})) {
        return false;
    }
    return true;
}

// Check if the matches satisfy the given pattern flags, and mutate the matches
// as specified by the flags.
bool process_match_flags(vector<Expr> &matches, int flags) {
    // The Pattern::Narrow*Op* flags are ordered such that the operand
    // corresponds to the bit (with operand 0 corresponding to the least
    // significant bit), so we can check for them all in a loop.
    for (size_t i = 0; i < matches.size(); i++) {
        Type t = matches[i].type();
        if (flags & (Pattern::NarrowOp0 << i)) {
            matches[i] = lossless_cast(t.narrow(), matches[i]);
        } else if (flags & (Pattern::NarrowUnsignedOp0 << i)) {
            matches[i] = lossless_cast(t.narrow().with_code(Type::UInt), matches[i]);
        }
        if (!matches[i].defined()) {
            return false;
        }
    }

    for (size_t i = Pattern::BeginDeinterleaveOp; i < Pattern::EndDeinterleaveOp; i++) {
        if (flags & (Pattern::DeinterleaveOp0 << (i - Pattern::BeginDeinterleaveOp))) {
            internal_assert(matches[i].type().is_vector());
            matches[i] = native_deinterleave(matches[i]);
        }
    }
    if (flags & Pattern::SwapOps01) {
        internal_assert(matches.size() >= 2);
        std::swap(matches[0], matches[1]);
    }
    if (flags & Pattern::SwapOps12) {
        internal_assert(matches.size() >= 3);
        std::swap(matches[1], matches[2]);
    }
    return true;
}

// Replace an expression with the one specified by a pattern.
Expr replace_pattern(Expr x, const vector<Expr> &matches, const Pattern &p) {
    x = Call::make(x.type(), p.intrin, matches, Call::PureExtern);
    if (p.flags & Pattern::InterleaveResult) {
        // The pattern wants us to interleave the result.
        x = native_interleave(x);
    }
    return x;
}

bool is_double_vector(const Expr &x, const Target &target) {
    int native_vector_lanes = target.natural_vector_size(x.type());
    return x.type().lanes() % (2 * native_vector_lanes) == 0;
}

// Attempt to apply one of the patterns to x. If a match is
// successful, the expression is replaced with a call using the
// matched operands. Prior to substitution, the matches are mutated
// with op_mutator.
Expr apply_patterns(Expr x, const vector<Pattern> &patterns, const Target &target, IRMutator *op_mutator) {
    constexpr int debug_level = 3;
    debug(debug_level) << "apply_patterns " << x << "\n";
    vector<Expr> matches;
    for (const Pattern &p : patterns) {
        if (!check_pattern_target(p.flags, target)) {
            continue;
        }

        if (expr_match(p.pattern, x, matches)) {
            debug(debug_level) << "matched " << p.pattern << "\n";
            debug(debug_level) << "matches:\n";
            for (const Expr &i : matches) {
                debug(debug_level) << i << "\n";
            }

            if (!process_match_flags(matches, p.flags)) {
                continue;
            }

            // Don't apply pattern if it involves an interleave,
            // and is not a multiple of two vectors.
            // See https://github.com/halide/Halide/issues/1582
            if ((p.flags & Pattern::InterleaveResult) && !is_double_vector(x, target)) {
                continue;
            }
            // Mutate the operands with the given mutator.
            for (Expr &op : matches) {
                op = op_mutator->mutate(op);
            }

            x = replace_pattern(x, matches, p);
            debug(debug_level) << "rewrote to: " << x << "\n";
            return x;
        }
    }
    return x;
}

template<typename T>
Expr apply_commutative_patterns(const T *op, const vector<Pattern> &patterns, const Target &target, IRMutator *mutator) {
    Expr ret = apply_patterns(op, patterns, target, mutator);
    if (!ret.same_as(op)) {
        return ret;
    }

    // Try commuting the op
    Expr commuted = T::make(op->b, op->a);
    ret = apply_patterns(commuted, patterns, target, mutator);
    if (!ret.same_as(commuted)) {
        return ret;
    }

    return op;
}

typedef pair<Expr, Expr> MulExpr;

// If ty is scalar or a vector with different lanes count,
// and x is a vector, try to remove a broadcast or adjust
// the number of lanes in Broadcast or indices in a Shuffle
// to match the ty lanes before using lossless_cast on it.
Expr unbroadcast_lossless_cast(Type ty, Expr x) {
    if (x.type().is_vector()) {
        if (const Broadcast *bc = x.as<Broadcast>()) {
            if (ty.is_scalar()) {
                x = bc->value;
            } else {
                x = Broadcast::make(bc->value, ty.lanes());
            }
        }
        // Check if shuffle can be treated as a broadcast.
        if (const Shuffle *shuff = x.as<Shuffle>()) {
            int factor = x.type().lanes() / ty.lanes();
            if (shuff->is_broadcast() && shuff->broadcast_factor() % factor == 0) {
                x = Shuffle::make(shuff->vectors, std::vector<int>(shuff->indices.begin(),
                                                                   shuff->indices.begin() + ty.lanes()));
            }
        }
    }
    if (ty.lanes() != x.type().lanes()) {
        return Expr();
    }
    return lossless_cast(ty, x);
}

// Try to extract a list of multiplies of the form a_ty*b_ty added
// together, such that op is equivalent to the sum of the
// multiplies in 'mpys', added to 'rest'.
// Difference in mpys.size() - return indicates the number of
// expressions where we pretend the op to be multiplied by 1.
int find_mpy_ops(const Expr &op, Type a_ty, Type b_ty, int max_mpy_count,
                 vector<MulExpr> &mpys, Expr &rest) {
    if ((int)mpys.size() >= max_mpy_count) {
        rest = rest.defined() ? Add::make(rest, op) : op;
        return 0;
    }

    // If the add is also widening, remove the cast.
    int mpy_bits = std::max(a_ty.bits(), b_ty.bits()) * 2;
    Expr maybe_mul = op;
    if (op.type().bits() == mpy_bits * 2) {
        if (const Cast *cast = op.as<Cast>()) {
            if (cast->value.type().bits() == mpy_bits) {
                maybe_mul = cast->value;
            }
        }
    }
    maybe_mul = as_mul(maybe_mul);

    if (maybe_mul.defined()) {
        const Mul *mul = maybe_mul.as<Mul>();
        Expr a = unbroadcast_lossless_cast(a_ty, mul->a);
        Expr b = unbroadcast_lossless_cast(b_ty, mul->b);
        if (a.defined() && b.defined()) {
            mpys.emplace_back(a, b);
            return 1;
        } else {
            // Try to commute the op.
            a = unbroadcast_lossless_cast(a_ty, mul->b);
            b = unbroadcast_lossless_cast(b_ty, mul->a);
            if (a.defined() && b.defined()) {
                mpys.emplace_back(a, b);
                return 1;
            }
        }
    } else if (const Add *add = op.as<Add>()) {
        int mpy_count = 0;
        mpy_count += find_mpy_ops(add->a, a_ty, b_ty, max_mpy_count, mpys, rest);
        mpy_count += find_mpy_ops(add->b, a_ty, b_ty, max_mpy_count, mpys, rest);
        return mpy_count;
    } else if (const Call *add = Call::as_intrinsic(op, {Call::widening_add})) {
        int mpy_count = 0;
        mpy_count += find_mpy_ops(cast(op.type(), add->args[0]), a_ty, b_ty, max_mpy_count, mpys, rest);
        mpy_count += find_mpy_ops(cast(op.type(), add->args[1]), a_ty, b_ty, max_mpy_count, mpys, rest);
        return mpy_count;
    }

    // Attempt to pretend this op is multiplied by 1.
    Expr as_a = unbroadcast_lossless_cast(a_ty, op);
    Expr as_b = unbroadcast_lossless_cast(b_ty, op);

    if (as_a.defined()) {
        mpys.emplace_back(as_a, make_one(b_ty));
    } else if (as_b.defined()) {
        mpys.emplace_back(make_one(a_ty), as_b);
    } else {
        rest = rest.defined() ? Add::make(rest, op) : op;
    }
    return 0;
}

// Perform peephole optimizations on the IR, adding appropriate
// interleave and deinterleave calls.
class OptimizePatterns : public IRMutator {
private:
    using IRMutator::visit;

    Scope<Interval> bounds;
    const Target &target;

    Expr visit(const Mul *op) override {
        static const vector<Pattern> scalar_muls = {
            // Non-widening scalar multiplication.
            {"halide.hexagon.mul.vh.b", wild_i16x * wild_i16, Pattern::NarrowOp1},
            {"halide.hexagon.mul.vw.h", wild_i32x * wild_i32, Pattern::NarrowOp1},
            // TODO: There's also mul.vw.b. We currently generate mul.vw.h
            // instead. I'm not sure mul.vw.b is faster, it might even be
            // slower due to the extra step in broadcasting the scalar up to
            // 32 bits.
        };

        static const vector<Pattern> muls = {
            // One operand widening multiplication.
            {"halide.hexagon.mul.vw.vh", wild_i32x * wild_i32x, Pattern::ReinterleaveOp0 | Pattern::NarrowOp1},
            {"halide.hexagon.mul.vw.vuh", wild_i32x * wild_i32x, Pattern::ReinterleaveOp0 | Pattern::NarrowUnsignedOp1},
            {"halide.hexagon.mul.vuw.vuh", wild_u32x * wild_u32x, Pattern::ReinterleaveOp0 | Pattern::NarrowUnsignedOp1},
        };

        if (op->type.is_vector()) {
            Expr new_expr = apply_commutative_patterns(op, scalar_muls, target, this);
            if (!new_expr.same_as(op)) {
                return new_expr;
            }

            new_expr = apply_commutative_patterns(op, muls, target, this);
            if (!new_expr.same_as(op)) {
                return new_expr;
            }
        }
        return IRMutator::visit(op);
    }

    // We'll try to sort the mpys based my mpys.first.
    // But, for this all the mpy.first exprs should either be
    // all loads or all slice_vectors.
    static void sort_mpy_exprs(vector<MulExpr> &mpys) {
        struct LoadCompare {
            bool operator()(const MulExpr &m1, const MulExpr &m2) const {
                if (!m1.first.defined() || !m2.first.defined()) {
                    return false;
                }
                const Load *m1_load = m1.first.as<Load>();
                const Load *m2_load = m2.first.as<Load>();
                internal_assert(m1_load && m2_load);
                const Ramp *m1_ramp = m1_load->index.as<Ramp>();
                const Ramp *m2_ramp = m2_load->index.as<Ramp>();
                internal_assert(m1_ramp && m2_ramp);
                return can_prove(m1_ramp->base < m2_ramp->base);
            }
        };
        const Shuffle *first_shuffle = mpys[0].first.as<Shuffle>();
        if (first_shuffle) {
            for (MulExpr &m : mpys) {
                const Shuffle *shuffle = m.first.as<Shuffle>();
                if (!shuffle || !shuffle->is_slice()) {
                    return;
                }
            }
            std::stable_sort(mpys.begin(), mpys.end(),
                             [](const MulExpr &m1, const MulExpr &m2) {
                                 return m1.first.as<Shuffle>()->slice_begin() < m2.first.as<Shuffle>()->slice_begin();
                             });
            return;
        } else if (const Load *first_load = mpys[0].first.as<Load>()) {
            const Ramp *first_ramp = first_load->index.as<Ramp>();
            if (!first_ramp) {
                return;
            }
            for (MulExpr &m : mpys) {
                const Load *load = m.first.as<Load>();
                if (!load ||
                    load->name != first_load->name ||
                    !load->index.as<Ramp>()) {
                    return;
                }
            }
            std::stable_sort(mpys.begin(), mpys.end(), LoadCompare());
        }
    }

    // Look for adds in an Add expression. This is factored out of visit(const Add*) to
    // enable look in widening_adds too.
    Expr find_mpyadds(const Expr &op_add) {
        const Add *op = op_add.as<Add>();
        internal_assert(op);

        // vmpa, vdmpy, and vrmpy instructions are hard to match with
        // patterns, do it manually here.
        // Try to find vrmpy opportunities first, which consume 4 operands.
        if (op->type.is_vector() && (op->type.bits() == 16 || op->type.bits() == 32)) {
            int lanes = op->type.lanes();
            vector<MulExpr> mpys;
            Expr rest;
            string suffix;
            int mpy_count = 0;

            // Try to find a vector*scalar multiply first, which will
            // match a subset of the expressions that vector*vector
            // matches.
            if (op->type.is_uint()) {
                mpy_count = find_mpy_ops(op, UInt(8, lanes), UInt(8), 4, mpys, rest);
                suffix = ".vub.ub";
            } else {
                mpy_count = find_mpy_ops(op, UInt(8, lanes), Int(8), 4, mpys, rest);
                suffix = ".vub.b";
            }

            if (mpy_count > 0 && mpys.size() == 4) {
                // It's possible that permuting the order of the
                // multiply operands can simplify the shuffle away.
                // So, give yourself a fighting chance by ordering the
                // mpys in the ascending order of their start lanes (if all
                // are slice_vectors) or in the ascending order of their
                // load indices if all are loads from the same buffer.
                sort_mpy_exprs(mpys);
                Expr a0123 = Shuffle::make_interleave({mpys[0].first, mpys[1].first, mpys[2].first, mpys[3].first});
                a0123 = simplify(a0123);

                // We can generate this op for 16 bits, but, it's only
                // faster to do so if the interleave simplifies away.
                if (op->type.bits() == 32 || !a0123.as<Shuffle>()) {
                    Expr b0123 = Shuffle::make_interleave({mpys[0].second, mpys[1].second, mpys[2].second, mpys[3].second});
                    b0123 = simplify(b0123);
                    b0123 = reinterpret(Type(b0123.type().code(), 32, 1), b0123);
                    Expr new_expr = halide_hexagon_add_4mpy(op->type.with_bits(32), suffix, a0123, b0123);
                    if (op->type.bits() == 16) {
                        // It's actually safe to use this op on 16 bit
                        // results, we just need to narrow the
                        // result. Overflow can occur, but will still
                        // produce the same result thanks to 2's
                        // complement arithmetic.
                        new_expr = Call::make(op->type, "halide.hexagon.pack.vw", {new_expr}, Call::PureExtern);
                    }
                    if (rest.defined()) {
                        new_expr = Add::make(new_expr, rest);
                    }
                    return mutate(new_expr);
                }
            }

            // Now try to match vector*vector vrmpy expressions.
            mpys.clear();
            rest = Expr();
            if (op->type.is_uint()) {
                mpy_count = find_mpy_ops(op, UInt(8, lanes), UInt(8, lanes), 4, mpys, rest);
                suffix = ".vub.vub";
            } else {
                mpy_count = find_mpy_ops(op, Int(8, lanes), Int(8, lanes), 4, mpys, rest);
                suffix = ".vb.vb";
            }

            // TODO: suffix = ".vub.vb"
            if (mpy_count > 0 && mpys.size() == 4) {
                // It's possible that permuting the order of the
                // multiply operands can simplify the shuffle away.
                // So, give yourself a fighting chance by ordering the
                // mpys in the ascending order of their start lanes (if all
                // are slice_vectors) or in the ascending order of their
                // load indices if all are loads from the same buffer.
                sort_mpy_exprs(mpys);
                Expr a0123 = Shuffle::make_interleave({mpys[0].first, mpys[1].first, mpys[2].first, mpys[3].first});
                Expr b0123 = Shuffle::make_interleave({mpys[0].second, mpys[1].second, mpys[2].second, mpys[3].second});
                a0123 = simplify(a0123);
                b0123 = simplify(b0123);
                // We can generate this op for 16 bits, but, it's only
                // faster to do so if the interleave simplifies away.
                if (op->type.bits() == 32 || (!a0123.as<Shuffle>() && !b0123.as<Shuffle>())) {
                    Expr new_expr = halide_hexagon_add_4mpy(op->type.with_bits(32), suffix, a0123, b0123);
                    if (op->type.bits() == 16) {
                        // It's actually safe to use this op on 16 bit
                        // results, we just need to narrow the
                        // result. Overflow can occur, but will still
                        // produce the same result thanks to 2's
                        // complement arithmetic.
                        new_expr = Call::make(op->type, "halide.hexagon.pack.vw", {new_expr}, Call::PureExtern);
                    }
                    if (rest.defined()) {
                        new_expr = Add::make(new_expr, rest);
                    }
                    return mutate(new_expr);
                }
            }
        }

        // Find opportunities vdmpy or vmpa.
        if (op->type.is_vector() && (op->type.bits() == 16 || op->type.bits() == 32)) {
            int lanes = op->type.lanes();

            vector<MulExpr> mpys;
            Expr rest;
            string vmpa_suffix;
            string vdmpy_suffix;
            int mpy_count = 0;

            // Try to find vector*scalar multiplies.
            if (op->type.bits() == 16) {
                mpy_count = find_mpy_ops(op, UInt(8, lanes), Int(8), 2, mpys, rest);
                vmpa_suffix = ".vub.vub.b.b";
                vdmpy_suffix = ".vub.b";
            } else if (op->type.bits() == 32) {
                mpy_count = find_mpy_ops(op, Int(16, lanes), Int(8), 2, mpys, rest);
                vmpa_suffix = ".vh.vh.b.b";
                vdmpy_suffix = ".vh.b";
            }
            if (mpy_count > 0 && mpys.size() == 2) {
                // It's possible that permuting the order of the
                // multiply operands can simplify the shuffle away.
                // So, give yourself a fighting chance by ordering the
                // mpys in the ascending order of their start lanes (if all
                // are slice_vectors) or in the ascending order of their
                // load indices if all are loads from the same buffer.
                sort_mpy_exprs(mpys);
                Expr a01 = Shuffle::make_interleave({mpys[0].first, mpys[1].first});
                a01 = simplify(a01);
                // TODO: This requires the operands to be in a
                // particular order. It should be more robust... but
                // this is pretty tough to do, other than simply
                // trying all permutations.
                Expr new_expr;
                if (!a01.as<Shuffle>() || vmpa_suffix.empty()) {
                    Expr b01 = Shuffle::make_interleave({mpys[0].second, mpys[1].second, mpys[0].second, mpys[1].second});
                    b01 = simplify(b01);
                    b01 = reinterpret(Type(b01.type().code(), 32, 1), b01);
                    new_expr = halide_hexagon_add_2mpy(op->type, vdmpy_suffix, a01, b01);
                } else {
                    new_expr = halide_hexagon_add_2mpy(op->type, vmpa_suffix, mpys[0].first, mpys[1].first, mpys[0].second, mpys[1].second);
                }
                if (rest.defined()) {
                    new_expr = Add::make(new_expr, rest);
                }
                return mutate(new_expr);
            }
        }
        return Expr();
    }

    Expr visit(const Add *op) override {
        Expr mpyadd = find_mpyadds(op);
        if (mpyadd.defined()) {
            return mpyadd;
        }
        static const vector<Pattern> adds = {
            // Use accumulating versions of vmpa, vdmpy, vrmpy instructions when possible.
            {"halide.hexagon.acc_add_2mpy.vh.vub.vub.b.b", wild_i16x + halide_hexagon_add_2mpy(Int(16, 0), ".vub.vub.b.b", wild_u8x, wild_u8x, wild_i8, wild_i8), Pattern::ReinterleaveOp0},
            {"halide.hexagon.acc_add_2mpy.vw.vh.vh.b.b", wild_i32x + halide_hexagon_add_2mpy(Int(32, 0), ".vh.vh.b.b", wild_i16x, wild_i16x, wild_i8, wild_i8), Pattern::ReinterleaveOp0},
            {"halide.hexagon.acc_add_2mpy.vh.vub.b", wild_i16x + halide_hexagon_add_2mpy(Int(16, 0), ".vub.b", wild_u8x, wild_i32)},
            {"halide.hexagon.acc_add_2mpy.vw.vh.b", wild_i32x + halide_hexagon_add_2mpy(Int(32, 0), ".vh.b", wild_i16x, wild_i32)},
            {"halide.hexagon.acc_add_3mpy.vh.vub.b", wild_i16x + native_interleave(halide_hexagon_add_3mpy(Int(16, 0), ".vub.b", wild_u8x, wild_i32)), Pattern::ReinterleaveOp0},
            {"halide.hexagon.acc_add_3mpy.vh.vb.b", wild_i16x + native_interleave(halide_hexagon_add_3mpy(Int(16, 0), ".vb.b", wild_i8x, wild_i32)), Pattern::ReinterleaveOp0},
            {"halide.hexagon.acc_add_3mpy.vw.vh.b", wild_i32x + native_interleave(halide_hexagon_add_3mpy(Int(32, 0), ".vh.b", wild_i16x, wild_i32)), Pattern::ReinterleaveOp0},
            {"halide.hexagon.acc_add_4mpy.vw.vub.b", wild_i32x + halide_hexagon_add_4mpy(Int(32, 0), ".vub.b", wild_u8x, wild_i32)},
            {"halide.hexagon.acc_add_4mpy.vuw.vub.ub", wild_u32x + halide_hexagon_add_4mpy(UInt(32, 0), ".vub.ub", wild_u8x, wild_u32)},
            {"halide.hexagon.acc_add_4mpy.vuw.vub.ub", wild_i32x + halide_hexagon_add_4mpy(Int(32, 0), ".vub.ub", wild_u8x, wild_u32)},
            {"halide.hexagon.acc_add_4mpy.vuw.vub.vub", wild_u32x + halide_hexagon_add_4mpy(UInt(32, 0), ".vub.vub", wild_u8x, wild_u8x)},
            {"halide.hexagon.acc_add_4mpy.vuw.vub.vub", wild_i32x + halide_hexagon_add_4mpy(Int(32, 0), ".vub.vub", wild_u8x, wild_u8x)},
            {"halide.hexagon.acc_add_4mpy.vw.vub.vb", wild_i32x + halide_hexagon_add_4mpy(Int(32, 0), ".vub.vb", wild_u8x, wild_i8x)},
            {"halide.hexagon.acc_add_4mpy.vw.vb.vb", wild_i32x + halide_hexagon_add_4mpy(Int(32, 0), ".vb.vb", wild_i8x, wild_i8x)},

            // Widening multiply-accumulates with a scalar.
            {"halide.hexagon.add_mpy.vuh.vub.ub", wild_u16x + widening_mul(wild_u8x, wild_u8), Pattern::ReinterleaveOp0},
            {"halide.hexagon.add_mpy.vh.vub.b", wild_i16x + widening_mul(wild_u8x, wild_i8), Pattern::ReinterleaveOp0},
            {"halide.hexagon.add_mpy.vuw.vuh.uh", wild_u32x + widening_mul(wild_u16x, wild_u16), Pattern::ReinterleaveOp0},

            // These patterns aren't exactly right because the instruction
            // saturates the result. However, this is really the instruction
            // that we want to use in most cases, and we can exploit the fact
            // that 32 bit signed arithmetic overflow is undefined to argue
            // that these patterns are not completely incorrect.
            {"halide.hexagon.satw_add_mpy.vw.vh.h", wild_i32x + widening_mul(wild_i16x, wild_i16), Pattern::ReinterleaveOp0},

            // Widening multiply-accumulates.
            {"halide.hexagon.add_mpy.vuh.vub.vub", wild_u16x + widening_mul(wild_u8x, wild_u8x), Pattern::ReinterleaveOp0},
            {"halide.hexagon.add_mpy.vuw.vuh.vuh", wild_u32x + widening_mul(wild_u16x, wild_u16x), Pattern::ReinterleaveOp0},
            {"halide.hexagon.add_mpy.vh.vb.vb", wild_i16x + widening_mul(wild_i8x, wild_i8x), Pattern::ReinterleaveOp0},
            {"halide.hexagon.add_mpy.vw.vh.vh", wild_i32x + widening_mul(wild_i16x, wild_i16x), Pattern::ReinterleaveOp0},

            {"halide.hexagon.add_mpy.vh.vub.vb", wild_i16x + widening_mul(wild_u8x, wild_i8x), Pattern::ReinterleaveOp0},
            {"halide.hexagon.add_mpy.vw.vh.vuh", wild_i32x + widening_mul(wild_i16x, wild_u16x), Pattern::ReinterleaveOp0},
            {"halide.hexagon.add_mpy.vh.vub.vb", wild_i16x + widening_mul(wild_i8x, wild_u8x), Pattern::ReinterleaveOp0 | Pattern::SwapOps12},
            {"halide.hexagon.add_mpy.vw.vh.vuh", wild_i32x + widening_mul(wild_u16x, wild_i16x), Pattern::ReinterleaveOp0 | Pattern::SwapOps12},

            // Shift-accumulates.
            {"halide.hexagon.add_shr.vw.vw.uw", wild_i32x + (wild_i32x >> wild_u32)},
            {"halide.hexagon.add_shl.vw.vw.uw", wild_i32x + (wild_i32x << wild_u32)},
            {"halide.hexagon.add_shl.vw.vw.uw", wild_u32x + (wild_u32x << wild_u32)},
            {"halide.hexagon.add_shl.vh.vh.uh", wild_i16x + (wild_i16x << wild_u16), Pattern::v65orLater},
            {"halide.hexagon.add_shl.vh.vh.uh", wild_u16x + (wild_u16x << wild_u16), Pattern::v65orLater},
            {"halide.hexagon.add_shr.vh.vh.uh", wild_i16x + (wild_i16x >> wild_u16), Pattern::v65orLater},
            {"halide.hexagon.add_shl.vh.vh.uh", wild_i16x + (wild_i16x << wild_i16), Pattern::v65orLater},
            {"halide.hexagon.add_shl.vh.vh.uh", wild_u16x + (wild_u16x << wild_u16), Pattern::v65orLater},

            // Non-widening multiply-accumulates with a scalar.
            {"halide.hexagon.add_mul.vh.vh.b", wild_i16x + wild_i16x * wild_i16, Pattern::NarrowOp2},
            {"halide.hexagon.add_mul.vw.vw.h", wild_i32x + wild_i32x * wild_i32, Pattern::NarrowOp2},
            // TODO: There's also a add_mul.vw.vw.b

            // This pattern is very general, so it must come last.
            {"halide.hexagon.add_mul.vh.vh.vh", wild_i16x + wild_i16x * wild_i16x},
        };

        if (op->type.is_vector()) {
            Expr new_expr = apply_commutative_patterns(op, adds, target, this);
            if (!new_expr.same_as(op)) {
                return new_expr;
            }
        }
        return IRMutator::visit(op);
    }

    Expr visit(const Sub *op) override {
        if (op->type.is_vector()) {
            // Try negating op->b, using an add pattern if successful.
            Expr neg_b = lossless_negate(op->b);
            if (neg_b.defined()) {
                return mutate(op->a + neg_b);
            }
        }
        return IRMutator::visit(op);
    }

    Expr visit(const Max *op) override {
        Expr expr = IRMutator::visit(op);

        if (op->type.is_vector()) {
            // This pattern is weird (two operands must match, result
            // needs 1 added) and we're unlikely to need another
            // pattern for max, so just match it directly.
            static const pair<string, Expr> cl[] = {
                {"halide.hexagon.cls.vh", max(count_leading_zeros(wild_i16x), count_leading_zeros(~wild_i16x))},
                {"halide.hexagon.cls.vw", max(count_leading_zeros(wild_i32x), count_leading_zeros(~wild_i32x))},
            };
            vector<Expr> matches;
            for (const auto &i : cl) {
                if (expr_match(i.second, expr, matches) && equal(matches[0], matches[1])) {
                    return Call::make(op->type, i.first, {matches[0]}, Call::PureExtern) + 1;
                }
            }
        }
        return expr;
    }

    Expr visit(const Cast *op) override {
        static const vector<Pattern> casts = {
            // Halving unsigned subtract.
            {"halide.hexagon.navg.vub.vub", i8(widening_sub(wild_u8x, wild_u8x) >> 1)},

            // Saturating narrowing casts with rounding
            {"halide.hexagon.trunc_satub_rnd.vh", u8_sat(rounding_shift_right(wild_i16x, 8)), Pattern::DeinterleaveOp0},
            {"halide.hexagon.trunc_satb_rnd.vh", i8_sat(rounding_shift_right(wild_i16x, 8)), Pattern::DeinterleaveOp0},
            {"halide.hexagon.trunc_satub_rnd.vuh", u8_sat(rounding_shift_right(wild_u16x, 8)), Pattern::DeinterleaveOp0},
            {"halide.hexagon.trunc_satuh_rnd.vw", u16_sat(rounding_shift_right(wild_i32x, 16)), Pattern::DeinterleaveOp0},
            {"halide.hexagon.trunc_sath_rnd.vw", i16_sat(rounding_shift_right(wild_i32x, 16)), Pattern::DeinterleaveOp0},
            {"halide.hexagon.trunc_satuh_rnd.vuw", u16_sat(rounding_shift_right(wild_u32x, 16)), Pattern::DeinterleaveOp0},

            // Saturating narrowing casts with rounding
            {"halide.hexagon.trunc_satub_shr_rnd.vh", u8_sat(rounding_shift_right(wild_i16x, wild_u16)), Pattern::DeinterleaveOp0},
            {"halide.hexagon.trunc_satb_shr_rnd.vh", i8_sat(rounding_shift_right(wild_i16x, wild_u16)), Pattern::DeinterleaveOp0},
            {"halide.hexagon.trunc_satub_shr_rnd.vuh", u8_sat(rounding_shift_right(wild_u16x, wild_u16)), Pattern::DeinterleaveOp0 | Pattern::v65orLater},
            {"halide.hexagon.trunc_satuh_shr_rnd.vw", u16_sat(rounding_shift_right(wild_i32x, wild_u32)), Pattern::DeinterleaveOp0},
            {"halide.hexagon.trunc_sath_shr_rnd.vw", i16_sat(rounding_shift_right(wild_i32x, wild_u32)), Pattern::DeinterleaveOp0},
            {"halide.hexagon.trunc_satuh_shr_rnd.vuw", u16_sat(rounding_shift_right(wild_u32x, wild_u32)), Pattern::DeinterleaveOp0},

            // Saturating narrowing casts
            {"halide.hexagon.trunc_satub_shr.vh.uh", u8_sat(wild_i16x >> wild_u16), Pattern::DeinterleaveOp0},
            {"halide.hexagon.trunc_satuh_shr.vw.uw", u16_sat(wild_i32x >> wild_u32), Pattern::DeinterleaveOp0},
            {"halide.hexagon.trunc_sath_shr.vw.uw", i16_sat(wild_i32x >> wild_u32), Pattern::DeinterleaveOp0},

            // For some of the following narrowing casts, we have the choice of
            // non-interleaving or interleaving instructions. Because we don't
            // know which one we prefer during pattern matching, we match the
            // non-interleaving versions for now and replace them with the
            // instructions that interleave later if it makes sense.

            // Saturating narrowing casts. These may interleave later with trunc_sat.
            {"halide.hexagon.pack_satub.vh", u8_sat(wild_i16x)},
            {"halide.hexagon.pack_satuh.vw", u16_sat(wild_i32x)},
            {"halide.hexagon.pack_satb.vh", i8_sat(wild_i16x)},
            {"halide.hexagon.pack_sath.vw", i16_sat(wild_i32x)},

            // We don't have a vpack equivalent to this one, so we match it directly.
            {"halide.hexagon.trunc_satuh.vuw", u16_sat(wild_u32x), Pattern::DeinterleaveOp0},

            // Narrowing casts. These may interleave later with trunclo.
            {"halide.hexagon.packhi.vh", u8(wild_u16x >> 8)},
            {"halide.hexagon.packhi.vh", u8(wild_i16x >> 8)},
            {"halide.hexagon.packhi.vh", i8(wild_u16x >> 8)},
            {"halide.hexagon.packhi.vh", i8(wild_i16x >> 8)},
            {"halide.hexagon.packhi.vw", u16(wild_u32x >> 16)},
            {"halide.hexagon.packhi.vw", u16(wild_i32x >> 16)},
            {"halide.hexagon.packhi.vw", i16(wild_u32x >> 16)},
            {"halide.hexagon.packhi.vw", i16(wild_i32x >> 16)},

            // Narrowing with shifting.
            {"halide.hexagon.trunc_shr.vw.uw", i16(wild_i32x >> wild_u32), Pattern::DeinterleaveOp0},

            // Narrowing casts. These may interleave later with trunc.
            {"halide.hexagon.pack.vh", u8(wild_u16x)},
            {"halide.hexagon.pack.vh", u8(wild_i16x)},
            {"halide.hexagon.pack.vh", i8(wild_u16x)},
            {"halide.hexagon.pack.vh", i8(wild_i16x)},
            {"halide.hexagon.pack.vw", u16(wild_u32x)},
            {"halide.hexagon.pack.vw", u16(wild_i32x)},
            {"halide.hexagon.pack.vw", i16(wild_u32x)},
            {"halide.hexagon.pack.vw", i16(wild_i32x)},

            // Widening casts
            {"halide.hexagon.zxt.vub", u16(wild_u8x), Pattern::InterleaveResult},
            {"halide.hexagon.zxt.vub", i16(wild_u8x), Pattern::InterleaveResult},
            {"halide.hexagon.zxt.vuh", u32(wild_u16x), Pattern::InterleaveResult},
            {"halide.hexagon.zxt.vuh", i32(wild_u16x), Pattern::InterleaveResult},
            {"halide.hexagon.sxt.vb", u16(wild_i8x), Pattern::InterleaveResult},
            {"halide.hexagon.sxt.vb", i16(wild_i8x), Pattern::InterleaveResult},
            {"halide.hexagon.sxt.vh", u32(wild_i16x), Pattern::InterleaveResult},
            {"halide.hexagon.sxt.vh", i32(wild_i16x), Pattern::InterleaveResult},
        };

        // To hit more of the patterns we want, rewrite "double casts"
        // as two stage casts. This also avoids letting vector casts
        // fall through to LLVM, which will generate large unoptimized
        // shuffles.
        static const vector<pair<Expr, Expr>> cast_rewrites = {
            // Saturating narrowing
            {u8_sat(wild_u32x), u8_sat(u16_sat(wild_u32x))},
            {u8_sat(wild_i32x), u8_sat(i16_sat(wild_i32x))},
            {i8_sat(wild_u32x), i8_sat(u16_sat(wild_u32x))},
            {i8_sat(wild_i32x), i8_sat(i16_sat(wild_i32x))},

            // Narrowing
            {u8(wild_u32x), u8(u16(wild_u32x))},
            {u8(wild_i32x), u8(i16(wild_i32x))},
            {i8(wild_u32x), i8(u16(wild_u32x))},
            {i8(wild_i32x), i8(i16(wild_i32x))},

            // Widening
            {u32(wild_u8x), u32(u16(wild_u8x))},
            {u32(wild_i8x), u32(i16(wild_i8x))},
            {i32(wild_u8x), i32(u16(wild_u8x))},
            {i32(wild_i8x), i32(i16(wild_i8x))},
        };

        if (op->type.is_vector()) {
            Expr cast = op;

            Expr new_expr = apply_patterns(cast, casts, target, this);
            if (!new_expr.same_as(cast)) {
                return new_expr;
            }

            // If we didn't find a pattern, try using one of the
            // rewrites above.
            vector<Expr> matches;
            for (const auto &i : cast_rewrites) {
                if (expr_match(i.first, cast, matches)) {
                    Expr replacement = substitute("*", matches[0], with_lanes(i.second, op->type.lanes()));
                    debug(3) << "rewriting cast to: " << replacement << " from " << cast << "\n";
                    return mutate(replacement);
                }
            }
        }
        return IRMutator::visit(op);
    }

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::if_then_else) && op->args[0].type().is_vector()) {
            const Broadcast *b = op->args[0].as<Broadcast>();
            if (!b || b->value.type().is_vector()) {
                return op;
            }
        }
        if (op->is_intrinsic(Call::widening_add)) {
            Expr mpyadds = find_mpyadds(Add::make(cast(op->type, op->args[0]), cast(op->type, op->args[1])));
            if (mpyadds.defined()) {
                return mpyadds;
            }
        }

        // These intrinsics should get the default lowering, and we need to recursively mutate the
        // result. We don't want to let these fall through to CodeGen_Hexagon and CodeGen_LLVM,
        // because they might generate interleaeves or deinterleaves we can simplify.
        static const vector<Call::IntrinsicOp> default_lower = {
            // TODO: Maybe there are widening shift instructions on Hexagon?
            Call::widening_shift_left,
        };

        for (Call::IntrinsicOp i : default_lower) {
            if (op->is_intrinsic(i)) {
                return mutate(lower_intrinsic(op));
            }
        }

        static const vector<Pattern> calls = {
            // Multiply keep high half.
            {"halide.hexagon.trunc_mpy.vw.vw", mul_shift_right(wild_i32x, wild_i32x, 32)},

            // Scalar multiply keep high half, with multiplication by 2.
            {"halide.hexagon.trunc_satw_mpy2.vh.h", mul_shift_right(wild_i16x, wild_i16, 15)},
            {"halide.hexagon.trunc_satw_mpy2.vh.h", mul_shift_right(wild_i16, wild_i16x, 15), Pattern::SwapOps01},
            {"halide.hexagon.trunc_satdw_mpy2.vw.vw", mul_shift_right(wild_i32x, wild_i32x, 31)},

            // Scalar and vector multiply keep high half, with multiplication by 2, and rounding.
            {"halide.hexagon.trunc_satw_mpy2_rnd.vh.h", rounding_mul_shift_right(wild_i16x, wild_i16, 15)},
            {"halide.hexagon.trunc_satw_mpy2_rnd.vh.h", rounding_mul_shift_right(wild_i16, wild_i16x, 15), Pattern::SwapOps01},
            {"halide.hexagon.trunc_satw_mpy2_rnd.vh.vh", rounding_mul_shift_right(wild_i16x, wild_i16x, 15)},
            {"halide.hexagon.trunc_satdw_mpy2_rnd.vw.vw", rounding_mul_shift_right(wild_i32x, wild_i32x, 31)},

            // Vector by scalar widening multiplies. These need to happen before the ones below, to avoid
            // using vector versions when scalar versions would suffice.
            {"halide.hexagon.mpy.vub.ub", widening_mul(wild_u8x, wild_u8), Pattern::InterleaveResult},
            {"halide.hexagon.mpy.vub.b", widening_mul(wild_u8x, wild_i8), Pattern::InterleaveResult},
            {"halide.hexagon.mpy.vuh.uh", widening_mul(wild_u16x, wild_u16), Pattern::InterleaveResult},
            {"halide.hexagon.mpy.vh.h", widening_mul(wild_i16x, wild_i16), Pattern::InterleaveResult},

            // These are calls that are almost trivial, but they differ due to interleaving.
            {"halide.hexagon.add_vuh.vub.vub", widening_add(wild_u8x, wild_u8x), Pattern::InterleaveResult},
            {"halide.hexagon.add_vuw.vuh.vuh", widening_add(wild_u16x, wild_u16x), Pattern::InterleaveResult},
            {"halide.hexagon.add_vw.vh.vh", widening_add(wild_i16x, wild_i16x), Pattern::InterleaveResult},
            {"halide.hexagon.sub_vh.vub.vub", widening_sub(wild_u8x, wild_u8x), Pattern::InterleaveResult},
            {"halide.hexagon.sub_vw.vuh.vuh", widening_sub(wild_u16x, wild_u16x), Pattern::InterleaveResult},
            {"halide.hexagon.sub_vw.vh.vh", widening_sub(wild_i16x, wild_i16x), Pattern::InterleaveResult},
            {"halide.hexagon.mpy.vub.vub", widening_mul(wild_u8x, wild_u8x), Pattern::InterleaveResult},
            {"halide.hexagon.mpy.vub.vb", widening_mul(wild_u8x, wild_i8x), Pattern::InterleaveResult},
            {"halide.hexagon.mpy.vub.vb", widening_mul(wild_i8x, wild_u8x), Pattern::InterleaveResult | Pattern::SwapOps01},
            {"halide.hexagon.mpy.vb.vb", widening_mul(wild_i8x, wild_i8x), Pattern::InterleaveResult},
            {"halide.hexagon.mpy.vuh.vuh", widening_mul(wild_u16x, wild_u16x), Pattern::InterleaveResult},
            {"halide.hexagon.mpy.vh.vh", widening_mul(wild_i16x, wild_i16x), Pattern::InterleaveResult},
            {"halide.hexagon.mpy.vh.vuh", widening_mul(wild_i16x, wild_u16x), Pattern::InterleaveResult},
            {"halide.hexagon.mpy.vh.vuh", widening_mul(wild_u16x, wild_i16x), Pattern::InterleaveResult | Pattern::SwapOps01},
        };

        if (op->type.is_vector()) {
            Expr new_expr = apply_patterns(op, calls, target, this);
            if (!new_expr.same_as(op)) {
                return new_expr;
            }
        }

        if (op->is_intrinsic(Call::lerp)) {
            // We need to lower lerps now to optimize the arithmetic
            // that they generate.
            internal_assert(op->args.size() == 3);
            return mutate(lower_lerp(op->args[0], op->args[1], op->args[2], target));
        } else if ((op->is_intrinsic(Call::div_round_to_zero) ||
                    op->is_intrinsic(Call::mod_round_to_zero)) &&
                   !op->type.is_float() && op->type.is_vector()) {
            internal_assert(op->args.size() == 2);
            Expr a = op->args[0];
            Expr b = op->args[1];
            // Run bounds analysis to estimate the range of result.
            Expr abs_result = op->type.is_int() ? abs(a / b) : a / b;
            Expr extent_upper = find_constant_bound(abs_result, Direction::Upper, bounds);
            const uint64_t *upper_bound = as_const_uint(extent_upper);
            a = mutate(a);
            b = mutate(b);
            std::pair<Expr, Expr> div_mod = long_div_mod_round_to_zero(a, b, upper_bound);
            if (op->is_intrinsic(Call::div_round_to_zero)) {
                return div_mod.first;
            }
            return div_mod.second;
        } else if (op->is_intrinsic(Call::mul_shift_right) ||
                   op->is_intrinsic(Call::rounding_mul_shift_right)) {
            // Lower these now, we might be able to use other patterns on the result.
            return mutate(lower_intrinsic(op));
        } else {
            return IRMutator::visit(op);
        }
    }

    template<typename NodeType, typename T>
    NodeType visit_let(const T *op) {
        bounds.push(op->name, bounds_of_expr_in_scope(op->value, bounds));
        NodeType node = IRMutator::visit(op);
        bounds.pop(op->name);
        return node;
    }

    Expr visit(const Let *op) override {
        return visit_let<Expr>(op);
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let<Stmt>(op);
    }

    Expr visit(const Div *op) override {
        if (!op->type.is_float() && op->type.is_vector()) {
            return mutate(lower_int_uint_div(op->a, op->b));
        }
        return IRMutator::visit(op);
    }

    Expr visit(const Mod *op) override {
        if (!op->type.is_float() && op->type.is_vector()) {
            return mutate(lower_int_uint_mod(op->a, op->b));
        }
        return IRMutator::visit(op);
    }

public:
    OptimizePatterns(const Target &t)
        : target(t) {
    }
};

class VectorReducePatterns : public IRMutator {
    using IRMutator::visit;

    // Check for interleaves of vectors with stride 1 like shuffle with indices:
    // 0, 1, 2,..., window_size - 1,
    // 1, 2, 3,..., window_size,
    // 2, 3, 4,..., window_size + 1,
    // .....
    // window_size != lanes
    // TODO: Their could be other patterns as well which we should match
    static int is_stencil_interleave(const Expr &op, int window_size) {
        int lanes = op.type().lanes();
        internal_assert(lanes > window_size);
        if (const Shuffle *shuff = op.as<Shuffle>()) {
            for (int i = window_size; i < lanes; i++) {
                if ((i % window_size != window_size - 1) &&
                    (shuff->indices[i - window_size + 1] != shuff->indices[i])) {
                    return false;
                }
            }
            return true;
        }
        return false;
    }

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::if_then_else) && op->args[0].type().is_vector()) {
            const Broadcast *b = op->args[0].as<Broadcast>();
            if (!b || b->value.type().is_vector()) {
                return op;
            }
        }
        return IRMutator::visit(op);
    }

    Expr visit(const VectorReduce *op) override {
        if (!op->type.is_vector() || op->type.is_float() || op->op != VectorReduce::Add) {
            return IRMutator::visit(op);
        }

        struct Signature {
            enum Flags {
                SlidingWindow = 1,
                ScalarB = 1 << 1,
                NarrowB = 1 << 2,
                SwapOps = 1 << 3,  // Swapping ops is done before matching B to scalars.
            };
            int factor;
            int native_return_bits;
            Expr pattern;
            int flags;
        };

        int in_lanes = op->value.type().lanes();
        int out_lanes = op->type.lanes();
        int factor = in_lanes / out_lanes;

        // Map of instruction signatures
        // clang-format off
        static const vector<Signature> sigs = {
            // --------- vrmpy ---------
            // Sliding window
            {4, 32, widening_mul(wild_u8x, wild_u8x), Signature::SlidingWindow | Signature::ScalarB},
            {4, 32, widening_mul(wild_u8x, wild_i8x), Signature::SlidingWindow | Signature::ScalarB},
            {4, 32, widening_mul(wild_i8x, wild_u8x), Signature::SlidingWindow | Signature::ScalarB | Signature::SwapOps},
            // Vector * Scalar
            {4, 32, widening_mul(wild_u8x, wild_u8x), Signature::ScalarB},
            {4, 32, widening_mul(wild_i8x, wild_u8x), Signature::ScalarB},
            {4, 32, widening_mul(wild_u8x, wild_i8x), Signature::ScalarB},
            {4, 32, widening_mul(wild_i8x, wild_u8x), Signature::ScalarB | Signature::SwapOps},
            // Vector * Vector
            {4, 32, widening_mul(wild_u8x, wild_u8x)},
            {4, 32, widening_mul(wild_u8x, wild_i8x)},
            {4, 32, widening_mul(wild_i8x, wild_u8x), Signature::SwapOps},
            {4, 32, widening_mul(wild_i8x, wild_i8x)},
            // Sum
            {4, 32, wild_u8x, Signature::SlidingWindow},
            {4, 32, wild_i8x, Signature::SlidingWindow},
            {4, 32, wild_u8x},
            {4, 32, wild_i8x},

            // --------- vtmpy ---------
            // Vtmpy has additional requirement that third coefficient b[2]
            // needs to be 1.
            // Sliding window
            {3, 16, widening_mul(wild_i8x, wild_i8x), Signature::SlidingWindow | Signature::ScalarB},
            {3, 16, widening_mul(wild_u8x, wild_i8x), Signature::SlidingWindow | Signature::ScalarB},
            {3, 16, widening_mul(wild_i8x, wild_u8x), Signature::SlidingWindow | Signature::ScalarB | Signature::SwapOps},
            {3, 32, widening_mul(wild_i16x, wild_i16x), Signature::SlidingWindow | Signature::ScalarB},
            // Sum
            {3, 16, wild_i8x, Signature::SlidingWindow},
            {3, 16, wild_u8x, Signature::SlidingWindow},
            {3, 32, wild_i16x, Signature::SlidingWindow},

            // --------- vdmpy ---------
            // Sliding window
            {2, 16, widening_mul(wild_u8x, wild_i8x), Signature::SlidingWindow | Signature::ScalarB},
            {2, 16, widening_mul(wild_i8x, wild_u8x), Signature::SlidingWindow | Signature::ScalarB | Signature::SwapOps},
            {2, 32, widening_mul(wild_i16x, wild_i16x), Signature::SlidingWindow | Signature::ScalarB},
            // Vector * Scalar
            {2, 16, widening_mul(wild_u8x, wild_i8x), Signature::ScalarB},
            {2, 16, widening_mul(wild_i8x, wild_u8x), Signature::ScalarB | Signature::SwapOps},
            {2, 32, widening_mul(wild_i16x, wild_i16x), Signature::ScalarB | Signature::NarrowB},
            {2, 32, widening_mul(wild_i16x, wild_u16x), Signature::ScalarB},                       // Saturates
            {2, 32, widening_mul(wild_u16x, wild_i16x), Signature::ScalarB | Signature::SwapOps},  // Saturates
            {2, 32, widening_mul(wild_i16x, wild_i16x), Signature::ScalarB},                       // Saturates
            // Vector * Vector
            {2, 32, widening_mul(wild_i16x, wild_i16x)},  // Saturates
            // Sum
            {2, 16, wild_u8x, Signature::SlidingWindow},
            {2, 32, wild_i16x, Signature::SlidingWindow},
            {2, 16, wild_u8x},
            {2, 32, wild_i16x},
        };
        // clang-format on
        std::vector<Expr> matches;
        for (const Signature &sig : sigs) {
            if (factor != sig.factor) {
                continue;
            }
            // Try matching the pattern with any number of bits between the pattern type and the native result.
            for (int bits = sig.pattern.type().bits(); bits <= sig.native_return_bits; bits *= 2) {
                matches.clear();
                Expr pattern = sig.pattern;
                if (bits != pattern.type().bits()) {
                    // Allow the widening cast to cast to the type of the result, which may
                    // differ from the pattern.
                    pattern = Cast::make(op->type.with_bits(bits).with_lanes(0), pattern);
                }
                if (expr_match(pattern, op->value, matches)) {
                    break;
                }
            }
            if (matches.empty()) {
                continue;
            }

            Expr a = matches[0];
            Expr b = matches.size() > 1 ? matches[1] : make_const(Type(op->type.code(), 8, factor), 1);
            if (sig.flags & Signature::SwapOps) {
                std::swap(a, b);
            }

            if (sig.flags & Signature::ScalarB) {
                if (const Shuffle *shuff = b.as<Shuffle>()) {
                    if (shuff->is_broadcast() && shuff->broadcast_factor() % factor == 0) {
                        internal_assert(shuff->vectors.size() == 1);
                        b = Shuffle::make_slice(shuff->vectors[0], 0, 1, factor);
                    }
                } else if (const Shuffle *shuff = a.as<Shuffle>()) {
                    // If the types are equal, we can commute the ops.
                    if (a.type().element_of() == b.type().element_of() &&
                        shuff->is_broadcast() && shuff->broadcast_factor() % factor == 0) {
                        internal_assert(shuff->vectors.size() == 1);
                        a = Shuffle::make_slice(shuff->vectors[0], 0, 1, factor);
                        std::swap(a, b);
                    }
                }
                if (b.type().lanes() != factor) {
                    // This isn't a scalar, it doesn't match the pattern.
                    continue;
                }
            }

            if (sig.flags & Signature::NarrowB) {
                b = lossless_cast(b.type().narrow(), b);
                if (!b.defined()) {
                    continue;
                }
            }

            Expr a0, a1;
            if (sig.flags & Signature::SlidingWindow) {
                if (!is_stencil_interleave(a, factor)) {
                    continue;
                }
                // Split a into a0, a1 to get the correct vector args
                // for sliding window reduction instructions. Below are
                // required shuffle indices for a0 and a1:
                // For factor == 2:
                // If a  -> shuff[0, 1,...., out_lanes]
                //    a0 -> shuff[0, 1,...., out_lanes - 1]
                //    a1 -> shuff[2, 3,...., out_lanes + 1]
                //          Last index of a1 is don't care
                // For factor == 3:
                // If a  -> shuff[0, 1,...., out_lanes + 1]
                //    a0 -> shuff[0, 1,...., out_lanes - 1]
                //    a1 -> shuff[2, 3,...., out_lanes + 1]
                // For factor == 4:
                // If a  -> shuff[0, 1,...., out_lanes + 3]
                //    a0 -> shuff[0, 1,...., out_lanes - 1]
                //    a1 -> shuff[4, 5,...., out_lanes + 4]
                //          Last index of a1 is don't care
                // TODO: Why does this require a to be a shuffle? Why isn't this just:
                // a0 = Shuffle::make_slice(a, 0, factor, out_lanes);
                // a1 = Shuffle::make_slice(a, factor - 1, factor, out_lanes);
                // The current code probably also generates messier shuffles the backend
                // may not recognize.
                if (const Shuffle *shuff = a.as<Shuffle>()) {
                    vector<int> a0_indices(out_lanes), a1_indices(out_lanes);
                    for (int i = 0; i < out_lanes; i++) {
                        a0_indices[i] = shuff->indices[i * factor];
                        a1_indices[i] = shuff->indices[(i + 1) * factor - 1];
                    }
                    a0 = Shuffle::make(shuff->vectors, a0_indices);
                    a1 = Shuffle::make(shuff->vectors, a1_indices);
                    if (factor == 2 || factor == 4) {
                        // We'll need to rotate the indices by one element
                        // to get the correct order.
                        Type ty = UInt(8).with_lanes(a1.type().lanes() * a1.type().bytes());
                        a1 = reinterpret(a1.type(),
                                         Call::make(ty, "halide.hexagon.vror",
                                                    {reinterpret(ty, a1), a1.type().bytes()},
                                                    Call::PureExtern));
                    } else {
                        // Vtmpy has additional requirement that third
                        // coefficient b[2] needs to be 1.
                        if (!can_prove(Shuffle::make_extract_element(b, 2) == 1)) {
                            continue;
                        }
                        b = Shuffle::make_slice(b, 0, 1, 2);
                    }
                    a = Shuffle::make_concat({a0, a1});
                } else {
                    continue;
                }
            }

            std::string suffix = type_suffix(a);
            if (b.type().lanes() <= factor) {
                suffix += type_suffix(b.type().element_of());
                if (b.type().lanes() * b.type().bits() <= 16) {
                    b = Shuffle::make({b}, {0, 1, 0, 1});
                }
                // Reinterpret scalar b arg to get correct type.
                b = simplify(reinterpret(Type(b.type().code(), b.type().lanes() * b.type().bits(), 1), b));
            } else {
                suffix += type_suffix(b);
            }

            Type result_type = op->type.with_bits(sig.native_return_bits);

            Expr result;
            if (factor == 4) {
                if (sig.flags & Signature::SlidingWindow) {
                    result = halide_hexagon_add_4mpy(result_type, suffix + ".stencil", a, b);
                } else {
                    result = halide_hexagon_add_4mpy(result_type, suffix, a, b);
                }
            } else {
                if (sig.flags & Signature::SlidingWindow) {
                    string name = "halide.hexagon.add_" + std::to_string(factor) + "mpy" + suffix;
                    result = native_interleave(Call::make(result_type, name, {a, b}, Call::PureExtern));
                } else {
                    // factor == 3 has only sliding window reductions.
                    result = halide_hexagon_add_2mpy(result_type, suffix, a, b);
                }
            }
            if (result.type() != op->type) {
                result = Cast::make(op->type, result);
            }
            return result;
        }
        return IRMutator::visit(op);
    }
};

// Attempt to cancel out redundant interleave/deinterleave pairs. The
// basic strategy is to push interleavings toward the end of the
// program, using the fact that interleaves can pass through pointwise
// IR operations. When an interleave collides with a deinterleave,
// they cancel out.
class EliminateInterleaves : public IRMutator {
    Scope<bool> vars;

    // We need to know when loads are a multiple of 2 native vectors.
    int native_vector_bits;

    // Alignment analyzer for loads and stores
    HexagonAlignmentAnalyzer alignment_analyzer;

    // Check if x is an expression that is either an interleave, or
    // transitively is an interleave.
    bool yields_removable_interleave(const Expr &x) {
        if (is_native_interleave(x)) {
            return true;
        }

        if (const Let *let = x.as<Let>()) {
            return yields_removable_interleave(let->body);
        }

        const Variable *var = x.as<Variable>();
        if (var && vars.contains(var->name + ".deinterleaved")) {
            return true;
        }

        if (const Load *load = x.as<Load>()) {
            if (buffers.contains(load->name)) {
                return buffers.get(load->name) != BufferState::NotInterleaved;
            }
        }

        if (const Add *op = x.as<Add>()) {
            return yields_removable_interleave(op->a) || yields_removable_interleave(op->b);
        } else if (const Sub *op = x.as<Sub>()) {
            return yields_removable_interleave(op->a) || yields_removable_interleave(op->b);
        }

        return false;
    }

    // Check if x either has a removable interleave, or it can pretend
    // to be an interleave at no cost (a scalar or a broadcast).
    bool yields_interleave(const Expr &x) {
        if (yields_removable_interleave(x)) {
            return true;
        }

        // These yield an interleave, but we shouldn't
        // deinterleave them if we want to remove an actual
        // interleave.
        if (x.type().is_scalar() || x.as<Broadcast>()) {
            return true;
        }

        if (const Let *let = x.as<Let>()) {
            return yields_interleave(let->body);
        }

        // This is different from the deinterleaved lets handled in
        // yields_removable_interleave. These are lets that can be
        // deinterleaved freely, but are not actually interleaves.
        const Variable *var = x.as<Variable>();
        if (var && vars.contains(var->name + ".weak_deinterleaved")) {
            return true;
        }

        if (const Load *load = x.as<Load>()) {
            if (buffers.contains(load->name)) {
                return buffers.get(load->name) != BufferState::NotInterleaved;
            }
        }

        if (const Add *op = x.as<Add>()) {
            return yields_interleave(op->a) || yields_interleave(op->b);
        } else if (const Sub *op = x.as<Sub>()) {
            return yields_interleave(op->a) || yields_interleave(op->b);
        }

        return false;
    }

    // Check that if we were to remove interleaves from exprs, that
    // we would remove more interleaves than we added deinterleaves.
    bool yields_removable_interleave(const vector<Expr> &exprs) {
        int removable = 0;
        int does_not_yield = 0;
        for (const Expr &i : exprs) {
            if (yields_removable_interleave(i)) {
                removable++;
            } else if (!yields_interleave(i)) {
                does_not_yield++;
            }
        }
        return removable > 0 && removable >= does_not_yield;
    }

    // Asserting that x is an expression that can yield an interleave
    // operation, return the expression being interleaved.
    Expr remove_interleave(Expr x) {
        if (is_native_interleave(x)) {
            return x.as<Call>()->args[0];
        } else if (x.type().is_scalar() || x.as<Broadcast>()) {
            return x;
        }

        if (const Variable *var = x.as<Variable>()) {
            if (vars.contains(var->name + ".deinterleaved")) {
                return Variable::make(var->type, var->name + ".deinterleaved");
            } else if (vars.contains(var->name + ".weak_deinterleaved")) {
                return Variable::make(var->type, var->name + ".weak_deinterleaved");
            }
        }

        if (const Let *let = x.as<Let>()) {
            Expr body = remove_interleave(let->body);
            if (!body.same_as(let->body)) {
                return Let::make(let->name, let->value, body);
            } else {
                return x;
            }
        }

        if (const Load *load = x.as<Load>()) {
            if (buffers.contains(load->name)) {
                BufferState &state = buffers.ref(load->name);
                if (state != BufferState::NotInterleaved) {
                    state = BufferState::Interleaved;
                    return x;
                }
            }
        }

        if (const Add *op = x.as<Add>()) {
            return Add::make(remove_interleave(op->a), remove_interleave(op->b));
        } else if (const Sub *op = x.as<Sub>()) {
            return Sub::make(remove_interleave(op->a), remove_interleave(op->b));
        }

        // If we rewrite x as interleave(deinterleave(x)), we can remove the interleave.
        return native_deinterleave(x);
    }

    template<typename T>
    Expr visit_binary(const T *op) {
        Expr expr;
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);
        if (yields_removable_interleave({a, b})) {
            expr = T::make(remove_interleave(a), remove_interleave(b));
            expr = native_interleave(expr);
        } else if (!a.same_as(op->a) || !b.same_as(op->b)) {
            expr = T::make(a, b);
        } else {
            expr = op;
        }
        return expr;
    }

    Expr visit(const Add *op) override {
        return visit_binary(op);
    }
    Expr visit(const Sub *op) override {
        return visit_binary(op);
    }
    Expr visit(const Mul *op) override {
        return visit_binary(op);
    }
    Expr visit(const Div *op) override {
        return visit_binary(op);
    }
    Expr visit(const Mod *op) override {
        return visit_binary(op);
    }
    Expr visit(const Min *op) override {
        return visit_binary(op);
    }
    Expr visit(const Max *op) override {
        return visit_binary(op);
    }

    Expr visit(const Select *op) override {
        Expr true_value = mutate(op->true_value);
        Expr false_value = mutate(op->false_value);
        Expr cond = mutate(op->condition);

        // The condition isn't a vector, so we can just check if we
        // should move an interleave from the true/false values.
        if (cond.type().is_scalar() && yields_removable_interleave({true_value, false_value})) {
            true_value = remove_interleave(true_value);
            false_value = remove_interleave(false_value);
            return native_interleave(Select::make(cond, true_value, false_value));
        } else if (!cond.same_as(op->condition) ||
                   !true_value.same_as(op->true_value) ||
                   !false_value.same_as(op->false_value)) {
            return Select::make(cond, true_value, false_value);
        } else {
            return op;
        }
    }

    template<typename NodeType, typename LetType>
    NodeType visit_let(const LetType *op) {

        Expr value = mutate(op->value);
        string deinterleaved_name;
        NodeType body;
        // Other code in this mutator needs to be able to tell the
        // difference between a Let that yields a deinterleave, and a
        // let that has a removable deinterleave. Lets that can
        // pretend to be deinterleaved at no cost are given an
        // alternative let labelled "weak_deinterleaved", while lets
        // that have a removable interleave are given an alternative
        // let labelled "deinterleaved".
        if (yields_removable_interleave(value)) {
            // We can provide a deinterleaved version of this let value.
            deinterleaved_name = op->name + ".deinterleaved";
            vars.push(deinterleaved_name, true);
            body = mutate(op->body);
            vars.pop(deinterleaved_name);
        } else if (yields_interleave(value)) {
            // We have a soft deinterleaved version of this let value.
            deinterleaved_name = op->name + ".weak_deinterleaved";
            vars.push(deinterleaved_name, true);
            body = mutate(op->body);
            vars.pop(deinterleaved_name);
        } else {
            body = mutate(op->body);
        }

        if (value.same_as(op->value) && body.same_as(op->body)) {
            return op;
        } else if (body.same_as(op->body)) {
            // If the body didn't change, we must not have used the deinterleaved value.
            return LetType::make(op->name, value, body);
        } else {
            // We need to rewrap the body with new lets.
            NodeType result = body;
            bool deinterleaved_used = stmt_or_expr_uses_var(result, deinterleaved_name);
            bool interleaved_used = stmt_or_expr_uses_var(result, op->name);
            if (deinterleaved_used && interleaved_used) {
                // The body uses both the interleaved and
                // deinterleaved version of this let. Generate both
                // lets, using the deinterleaved one to generate the
                // interleaved one.
                Expr deinterleaved = remove_interleave(value);

                // If we actually removed an interleave from the
                // value, re-interleave it to get the interleaved let
                // value.
                Expr interleaved = Variable::make(deinterleaved.type(), deinterleaved_name);
                if (!deinterleaved.same_as(value)) {
                    interleaved = native_interleave(interleaved);
                }

                result = LetType::make(op->name, interleaved, result);
                return LetType::make(deinterleaved_name, deinterleaved, result);
            } else if (deinterleaved_used) {
                // Only the deinterleaved value is used, we can eliminate the interleave.
                return LetType::make(deinterleaved_name, remove_interleave(value), result);
            } else if (interleaved_used) {
                // Only the original value is used, regenerate the let.
                return LetType::make(op->name, value, result);
            } else {
                // The let must have been dead.
                internal_assert(!stmt_or_expr_uses_var(op->body, op->name))
                    << "EliminateInterleaves eliminated a non-dead let.\n";
                return op->body;
            }
        }
    }

    Expr visit(const Let *op) override {
        Expr expr = visit_let<Expr>(op);

        // Lift interleaves out of Let expression bodies.
        const Let *let = expr.as<Let>();
        if (let && yields_removable_interleave(let->body)) {
            expr = native_interleave(Let::make(let->name, let->value, remove_interleave(let->body)));
        }
        return expr;
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let<Stmt>(op);
    }

    Expr visit(const Cast *op) override {
        if (op->type.bits() == op->value.type().bits()) {
            // We can only move interleaves through casts of the same size.
            Expr value = mutate(op->value);

            if (yields_removable_interleave(value)) {
                value = remove_interleave(value);
                return native_interleave(Cast::make(op->type, value));
            } else if (!value.same_as(op->value)) {
                return Cast::make(op->type, value);
            } else {
                return op;
            }
        } else {
            return IRMutator::visit(op);
        }
    }

    static bool is_interleavable(const Call *op) {
        // These calls can have interleaves moved from operands to the
        // result...
        static const set<string> interleavable = {
            Call::get_intrinsic_name(Call::bitwise_and),
            Call::get_intrinsic_name(Call::bitwise_not),
            Call::get_intrinsic_name(Call::bitwise_xor),
            Call::get_intrinsic_name(Call::bitwise_or),
            Call::get_intrinsic_name(Call::shift_left),
            Call::get_intrinsic_name(Call::shift_right),
            Call::get_intrinsic_name(Call::abs),
            Call::get_intrinsic_name(Call::absd)};
        if (interleavable.count(op->name) != 0) {
            return true;
        }

        // ...these calls cannot. Furthermore, these calls have the
        // same return type as the arguments, which means our test
        // below will be inaccurate.
        static const set<string> not_interleavable = {
            "halide.hexagon.interleave.vb",
            "halide.hexagon.interleave.vh",
            "halide.hexagon.interleave.vw",
            "halide.hexagon.deinterleave.vb",
            "halide.hexagon.deinterleave.vh",
            "halide.hexagon.deinterleave.vw",
            Call::get_intrinsic_name(Call::hvx_gather),
            Call::get_intrinsic_name(Call::hvx_scatter),
            Call::get_intrinsic_name(Call::hvx_scatter_acc),
        };
        if (not_interleavable.count(op->name) != 0) {
            return false;
        }

        if (starts_with(op->name, "halide.hexagon.")) {
            // We assume that any hexagon intrinsic is interleavable
            // as long as all of the vector operands have the same
            // number of lanes and lane width as the return type.
            for (const Expr &i : op->args) {
                if (i.type().is_scalar()) {
                    continue;
                }
                if (i.type().bits() != op->type.bits() || i.type().lanes() != op->type.lanes()) {
                    return false;
                }
            }
        }
        return true;
    }

    Expr visit(const Call *op) override {
        vector<Expr> args(op->args);

        // mutate all the args.
        bool changed = false;
        for (Expr &i : args) {
            Expr new_i = mutate(i);
            changed = changed || !new_i.same_as(i);
            i = new_i;
        }

        // For a few operations, we have a choice of several
        // instructions, an interleaving or a non-inerleaving
        // variant. We handle this by generating the instruction that
        // does not deinterleave, and then opportunistically select
        // the interleaving alternative when we can cancel out to the
        // interleave.
        static std::map<string, string> deinterleaving_alts = {
            {"halide.hexagon.pack.vh", "halide.hexagon.trunc.vh"},
            {"halide.hexagon.pack.vw", "halide.hexagon.trunc.vw"},
            {"halide.hexagon.packhi.vh", "halide.hexagon.trunclo.vh"},
            {"halide.hexagon.packhi.vw", "halide.hexagon.trunclo.vw"},
            {"halide.hexagon.pack_satub.vh", "halide.hexagon.trunc_satub.vh"},
            {"halide.hexagon.pack_sath.vw", "halide.hexagon.trunc_sath.vw"},
            {"halide.hexagon.pack_satuh.vw", "halide.hexagon.trunc_satuh.vw"},
        };

        // The reverse mapping of the above.
        static std::map<string, string> interleaving_alts = {
            {"halide.hexagon.trunc.vh", "halide.hexagon.pack.vh"},
            {"halide.hexagon.trunc.vw", "halide.hexagon.pack.vw"},
            {"halide.hexagon.trunclo.vh", "halide.hexagon.packhi.vh"},
            {"halide.hexagon.trunclo.vw", "halide.hexagon.packhi.vw"},
            {"halide.hexagon.trunc_satub.vh", "halide.hexagon.pack_satub.vh"},
            {"halide.hexagon.trunc_sath.vw", "halide.hexagon.pack_sath.vw"},
            {"halide.hexagon.trunc_satuh.vw", "halide.hexagon.pack_satuh.vw"},
        };

        if (is_native_deinterleave(op) && yields_interleave(args[0])) {
            // This is a deinterleave of an interleave! Remove them both.
            return remove_interleave(args[0]);
        } else if (is_interleavable(op) && yields_removable_interleave(args)) {
            // We can reduce the total number of interleave and deinterleave
            // operations by removing interleaves from the arguments.
            for (Expr &i : args) {
                i = remove_interleave(i);
            }
            Expr expr = Call::make(op->type, op->name, args, op->call_type,
                                   op->func, op->value_index, op->image, op->param);
            // Add the interleave back to the result of the call.
            return native_interleave(expr);
        } else if (deinterleaving_alts.find(op->name) != deinterleaving_alts.end() &&
                   yields_removable_interleave(args)) {
            // This call has a deinterleaving alternative, and the
            // arguments are interleaved, so we should use the
            // alternative instead.
            for (Expr &i : args) {
                i = remove_interleave(i);
            }
            return Call::make(op->type, deinterleaving_alts[op->name], args, op->call_type);
        } else if (interleaving_alts.count(op->name) && is_native_deinterleave(args[0])) {
            // This is an interleaving alternative with a
            // deinterleave, which can be generated when we
            // deinterleave storage. Revert back to the interleaving
            // op so we can remove the deinterleave.
            Expr arg = args[0].as<Call>()->args[0];
            return Call::make(op->type, interleaving_alts[op->name], {arg}, op->call_type,
                              op->func, op->value_index, op->image, op->param);
        } else if (changed) {
            return Call::make(op->type, op->name, args, op->call_type,
                              op->func, op->value_index, op->image, op->param);
        } else {
            return op;
        }
    }

    // Track whether buffers are interleaved or not.
    enum class BufferState {
        Unknown,         // We don't know if this buffer is interleaved or not.
        Interleaved,     // We know the buffer is interleaved.
        NotInterleaved,  // We know the buffer is not interleaved.
    };
    Scope<BufferState> buffers;

    // False for buffers that have any loads or stores that are unaligned
    Scope<bool> aligned_buffer_access;

    // Buffers we should deinterleave the storage of.
    Scope<bool> deinterleave_buffers;

    Stmt visit(const Allocate *op) override {
        Expr condition = mutate(op->condition);

        // First, we need to mutate the op, to pull native interleaves
        // down, and to gather information about the loads and stores.
        buffers.push(op->name, BufferState::Unknown);

        // Assume buffers are accessed by aligned loads and stores by default.
        aligned_buffer_access.push(op->name, true);

        Stmt body = mutate(op->body);
        bool deinterleave = (buffers.get(op->name) == BufferState::Interleaved) &&
                            (aligned_buffer_access.get(op->name) == true);
        buffers.pop(op->name);

        // Second, if we decided it would be useful to deinterleave
        // the storage of this buffer, do so now.
        if (deinterleave) {
            deinterleave_buffers.push(op->name, true);
            body = mutate(op->body);
            deinterleave_buffers.pop(op->name);
        }

        aligned_buffer_access.pop(op->name);

        if (!body.same_as(op->body) || !condition.same_as(op->condition)) {
            return Allocate::make(op->name, op->type, op->memory_type,
                                  op->extents, condition, body,
                                  op->new_expr, op->free_function);
        } else {
            return op;
        }
    }

    Stmt visit(const Store *op) override {
        Expr predicate = mutate(op->predicate);
        Expr value = mutate(op->value);
        Expr index = mutate(op->index);

        if (buffers.contains(op->name)) {
            // When inspecting the stores to a buffer, update the state.
            BufferState &state = buffers.ref(op->name);
            if (!is_const_one(predicate) || !op->value.type().is_vector()) {
                // TODO(psuriana): This store is predicated. Mark the buffer as
                // not interleaved for now.
                state = BufferState::NotInterleaved;
            } else if (yields_removable_interleave(value)) {
                // The value yields a removable interleave. If we aren't tracking
                // this buffer, mark it as interleaved.
                if (state == BufferState::Unknown) {
                    state = BufferState::Interleaved;
                }
            } else if (!yields_interleave(value)) {
                // The value does not yield an interleave. Mark the
                // buffer as not interleaved.
                state = BufferState::NotInterleaved;
            } else {
                // If the buffer yields an interleave, but is not an
                // interleave itself, we don't want to change the
                // buffer state.
            }
            internal_assert(aligned_buffer_access.contains(op->name) && "Buffer not found in scope");
            bool &aligned_accesses = aligned_buffer_access.ref(op->name);
            int64_t aligned_offset = 0;

            if (!alignment_analyzer.is_aligned(op, &aligned_offset)) {
                aligned_accesses = false;
            }
        }
        if (deinterleave_buffers.contains(op->name)) {
            // We're deinterleaving this buffer, remove the interleave
            // from the store.
            internal_assert(is_const_one(predicate)) << "The store shouldn't have been predicated.\n";
            value = remove_interleave(value);
        }

        if (predicate.same_as(op->predicate) && value.same_as(op->value) && index.same_as(op->index)) {
            return op;
        } else {
            return Store::make(op->name, value, index, op->param, predicate, op->alignment);
        }
    }

    Expr visit(const Load *op) override {
        if (buffers.contains(op->name)) {
            if ((op->type.lanes() * op->type.bits()) % (native_vector_bits * 2) == 0) {
                // This is a double vector load, we might be able to
                // deinterleave the storage of this buffer.
                // We don't want to actually do anything to the buffer
                // state here. We know we can interleave the load if
                // necessary, but we don't want to cause it to be
                // interleaved unless it is a useful improvement,
                // which is only true if any of the stores are
                // actually interleaved (and don't just yield an
                // interleave).
                internal_assert(aligned_buffer_access.contains(op->name) && "Buffer not found in scope");
                bool &aligned_accesses = aligned_buffer_access.ref(op->name);
                int64_t aligned_offset = 0;

                if (!alignment_analyzer.is_aligned(op, &aligned_offset)) {
                    aligned_accesses = false;
                }
            } else {
                // This is not a double vector load, so we can't
                // deinterleave the storage of this buffer.
                BufferState &state = buffers.ref(op->name);
                state = BufferState::NotInterleaved;
            }
        }
        Expr expr = IRMutator::visit(op);
        if (deinterleave_buffers.contains(op->name)) {
            expr = native_interleave(expr);
        }
        return expr;
    }

    using IRMutator::visit;

public:
    EliminateInterleaves(int native_vector_bytes)
        : native_vector_bits(native_vector_bytes * 8), alignment_analyzer(native_vector_bytes) {
    }
};

// After eliminating interleaves, there may be some that remain. This
// mutator attempts to replace interleaves paired with other
// operations that do not require an interleave. It's important to do
// this after all other efforts to eliminate the interleaves,
// otherwise this might eat some interleaves that could have cancelled
// with other operations.
class FuseInterleaves : public IRMutator {
    Expr visit(const Call *op) override {
        // This is a list of {f, g} pairs that if the first operation
        // is interleaved, interleave(f(x)) is equivalent to g(x).
        static const std::vector<std::pair<string, string>> non_deinterleaving_alts = {
            {"halide.hexagon.zxt.vub", "halide.hexagon.unpack.vub"},
            {"halide.hexagon.sxt.vb", "halide.hexagon.unpack.vb"},
            {"halide.hexagon.zxt.vuh", "halide.hexagon.unpack.vuh"},
            {"halide.hexagon.sxt.vh", "halide.hexagon.unpack.vh"},
        };

        if (is_native_interleave(op)) {
            if (const Call *arg = op->args[0].as<Call>()) {
                for (const auto &i : non_deinterleaving_alts) {
                    if (arg->name == i.first) {
                        std::vector<Expr> args = arg->args;
                        for (Expr &j : args) {
                            j = mutate(j);
                        }
                        return Call::make(op->type, i.second, args, Call::PureExtern);
                    }
                }
            }
        }

        return IRMutator::visit(op);
    }

    using IRMutator::visit;
};

// Find an upper bound of bounds.max - bounds.min.
Expr span_of_bounds(const Interval &bounds) {
    internal_assert(bounds.is_bounded());

    const Min *min_min = bounds.min.as<Min>();
    const Max *min_max = bounds.min.as<Max>();
    const Min *max_min = bounds.max.as<Min>();
    const Max *max_max = bounds.max.as<Max>();
    const Add *min_add = bounds.min.as<Add>();
    const Add *max_add = bounds.max.as<Add>();
    const Sub *min_sub = bounds.min.as<Sub>();
    const Sub *max_sub = bounds.max.as<Sub>();

    if (min_min && max_min && equal(min_min->b, max_min->b)) {
        return span_of_bounds({min_min->a, max_min->a});
    } else if (min_max && max_max && equal(min_max->b, max_max->b)) {
        return span_of_bounds({min_max->a, max_max->a});
    } else if (min_add && max_add && equal(min_add->b, max_add->b)) {
        return span_of_bounds({min_add->a, max_add->a});
    } else if (min_sub && max_sub && equal(min_sub->b, max_sub->b)) {
        return span_of_bounds({min_sub->a, max_sub->a});
    } else {
        return bounds.max - bounds.min;
    }
}

// Replace indirect loads with dynamic_shuffle intrinsics where
// possible.
class OptimizeShuffles : public IRMutator {
    int lut_alignment;
    Scope<Interval> bounds;
    std::vector<std::pair<string, Expr>> lets;

    using IRMutator::visit;

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::if_then_else) && op->args[0].type().is_vector()) {
            const Broadcast *b = op->args[0].as<Broadcast>();
            if (!b || b->value.type().is_vector()) {
                return op;
            }
        }
        return IRMutator::visit(op);
    }

    template<typename NodeType, typename T>
    NodeType visit_let(const T *op) {
        // We only care about vector lets.
        if (op->value.type().is_vector()) {
            bounds.push(op->name, bounds_of_expr_in_scope(op->value, bounds));
        }
        NodeType node = IRMutator::visit(op);
        if (op->value.type().is_vector()) {
            bounds.pop(op->name);
        }
        return node;
    }

    Expr visit(const Let *op) override {
        lets.emplace_back(op->name, op->value);
        Expr expr = visit_let<Expr>(op);
        lets.pop_back();
        return expr;
    }
    Stmt visit(const LetStmt *op) override {
        return visit_let<Stmt>(op);
    }

    Expr visit(const Load *op) override {
        if (!is_const_one(op->predicate)) {
            // TODO(psuriana): We shouldn't mess with predicated load for now.
            return IRMutator::visit(op);
        }
        if (!op->type.is_vector() || op->index.as<Ramp>()) {
            // Don't handle scalar or simple vector loads.
            return IRMutator::visit(op);
        }

        Expr index = mutate(op->index);
        Interval unaligned_index_bounds = bounds_of_expr_in_scope(index, bounds);
        if (unaligned_index_bounds.is_bounded()) {
            // We want to try both the unaligned and aligned
            // bounds. The unaligned bounds might fit in 256 elements,
            // while the aligned bounds do not.
            int align = lut_alignment / op->type.bytes();
            Interval aligned_index_bounds = {
                (unaligned_index_bounds.min / align) * align,
                ((unaligned_index_bounds.max + align) / align) * align - 1};
            ModulusRemainder alignment(align, 0);

            for (const Interval &index_bounds : {aligned_index_bounds, unaligned_index_bounds}) {
                Expr index_span = span_of_bounds(index_bounds);
                index_span = common_subexpression_elimination(index_span);
                index_span = simplify(index_span);

                if (can_prove(index_span < 256)) {
                    // This is a lookup within an up to 256 element array. We
                    // can use dynamic_shuffle for this.
                    int const_extent = as_const_int(index_span) ? *as_const_int(index_span) + 1 : 256;
                    Expr base = simplify(index_bounds.min);

                    // Load all of the possible indices loaded from the
                    // LUT. Note that for clamped ramps, this loads up to 1
                    // vector past the max. CodeGen_Hexagon::allocation_padding
                    // returns a native vector size to account for this.
                    Expr lut = Load::make(op->type.with_lanes(const_extent), op->name,
                                          Ramp::make(base, 1, const_extent),
                                          op->image, op->param, const_true(const_extent), alignment);

                    // We know the size of the LUT is not more than 256, so we
                    // can safely cast the index to 8 bit, which
                    // dynamic_shuffle requires.
                    index = simplify(cast(UInt(8).with_lanes(op->type.lanes()), index - base));
                    return Call::make(op->type, "dynamic_shuffle", {lut, index, 0, const_extent - 1}, Call::PureIntrinsic);
                }
                // Only the first iteration of this loop is aligned.
                alignment = ModulusRemainder();
            }
        }
        if (!index.same_as(op->index)) {
            return Load::make(op->type, op->name, index, op->image, op->param, op->predicate, op->alignment);
        } else {
            return op;
        }
    }

public:
    OptimizeShuffles(int lut_alignment)
        : lut_alignment(lut_alignment) {
    }
};

// Distribute constant RHS widening shift lefts as multiplies.
// TODO: This is an extremely unfortunate mess. I think the better
// solution is for the simplifier to distribute constant multiplications
// instead of factoring them, and then this logic is unnecessary (find_mpy_ops
// would need to handle shifts, but that's easy).
// Another possibility would be adding a widening_mul_add intrinsic that takes
// a list of pairs of operands, and computes a widening sum of widening multiplies
// of these pairs. FindIntrinsics could aggressively rewrite shifts as
// widening_mul_add operands.
class DistributeShiftsAsMuls : public IRMutator {
private:
    static bool is_cast(const Expr &e, Type value_t) {
        if (const Cast *cast = e.as<Cast>()) {
            return cast->value.type() == value_t;
        }
        return false;
    }

    static Expr distribute(const Expr &a, const Expr &b) {
        if (const Add *add = a.as<Add>()) {
            return Add::make(distribute(add->a, b), distribute(add->b, b));
        } else if (const Sub *sub = a.as<Sub>()) {
            Expr sub_a = distribute(sub->a, b);
            Expr sub_b = distribute(sub->b, b);
            Expr negative_sub_b = lossless_negate(sub_b);
            if (negative_sub_b.defined()) {
                return Add::make(sub_a, negative_sub_b);
            } else {
                return Sub::make(sub_a, sub_b);
            }
        } else if (const Cast *cast = a.as<Cast>()) {
            Expr cast_b = lossless_cast(b.type().with_bits(cast->value.type().bits()), b);
            if (cast_b.defined()) {
                Expr mul = widening_mul(cast->value, cast_b);
                if (mul.type().bits() <= cast->type.bits()) {
                    if (mul.type() != cast->type) {
                        mul = Cast::make(cast->type, mul);
                    }
                    return mul;
                }
            }
        } else if (const Call *add = Call::as_intrinsic(a, {Call::widening_add})) {
            Expr add_a = Cast::make(add->type, add->args[0]);
            Expr add_b = Cast::make(add->type, add->args[1]);
            add_a = distribute(add_a, b);
            add_b = distribute(add_b, b);
            // If add_a and add_b are the same kind of cast, we should remake a widening add.
            const Cast *add_a_cast = add_a.as<Cast>();
            const Cast *add_b_cast = add_b.as<Cast>();
            if (add_a_cast && add_b_cast &&
                add_a_cast->value.type() == add->args[0].type() &&
                add_b_cast->value.type() == add->args[1].type()) {
                return widening_add(add_a_cast->value, add_b_cast->value);
            } else {
                return Add::make(add_a, add_b);
            }
        } else if (const Call *sub = Call::as_intrinsic(a, {Call::widening_sub})) {
            Expr sub_a = Cast::make(sub->type, sub->args[0]);
            Expr sub_b = Cast::make(sub->type, sub->args[1]);
            sub_a = distribute(sub_a, b);
            sub_b = distribute(sub_b, b);
            Expr negative_sub_b = lossless_negate(sub_b);
            if (negative_sub_b.defined()) {
                sub_b = negative_sub_b;
            }
            // If sub_a and sub_b are the same kind of cast, we should remake a widening sub.
            const Cast *sub_a_cast = sub_a.as<Cast>();
            const Cast *sub_b_cast = sub_b.as<Cast>();
            if (sub_a_cast && sub_b_cast &&
                sub_a_cast->value.type() == sub->args[0].type() &&
                sub_b_cast->value.type() == sub->args[1].type()) {
                if (negative_sub_b.defined()) {
                    return widening_add(sub_a_cast->value, sub_b_cast->value);
                } else {
                    return widening_sub(sub_a_cast->value, sub_b_cast->value);
                }
            } else {
                if (negative_sub_b.defined()) {
                    return Add::make(sub_a, sub_b);
                } else {
                    return Sub::make(sub_a, sub_b);
                }
            }
        } else if (const Call *mul = Call::as_intrinsic(a, {Call::widening_mul})) {
            Expr mul_a = Cast::make(mul->type, mul->args[0]);
            Expr mul_b = Cast::make(mul->type, mul->args[1]);
            mul_a = distribute(mul_a, b);
            if (const Cast *mul_a_cast = mul_a.as<Cast>()) {
                if (mul_a_cast->value.type() == mul->args[0].type()) {
                    return widening_mul(mul_a_cast->value, mul->args[1]);
                }
            }
            mul_b = distribute(mul_b, b);
            if (const Cast *mul_b_cast = mul_b.as<Cast>()) {
                if (mul_b_cast->value.type() == mul->args[1].type()) {
                    return widening_mul(mul->args[0], mul_b_cast->value);
                }
            }
        }
        return simplify(Mul::make(a, b));
    }

    using IRMutator::visit;

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::shift_left)) {
            if (const uint64_t *const_b = as_const_uint(op->args[1])) {
                Expr a = op->args[0];
                // Only rewrite widening shifts.
                const Cast *cast_a = a.as<Cast>();
                bool is_widening_cast = cast_a && cast_a->type.bits() >= cast_a->value.type().bits() * 2;
                if (is_widening_cast || Call::as_intrinsic(a, {Call::widening_add, Call::widening_mul, Call::widening_sub})) {
                    return mutate(distribute(a, make_one(a.type()) << *const_b));
                }
            }
        } else if (op->is_intrinsic(Call::widening_shift_left)) {
            if (const uint64_t *const_b = as_const_uint(op->args[1])) {
                Expr a = Cast::make(op->type, op->args[0]);
                return mutate(distribute(a, make_one(a.type()) << *const_b));
            }
        }
        return IRMutator::visit(op);
    }
};

// Try generating vgathers instead of shuffles.
// At present, we request VTCM memory with single page allocation flag for all
// store_in allocations. So it's always safe to generate a vgather.
// Expressions which generate vgathers are of the form:
//     out(x) = lut(foo(x))
// For vgathers out and lut should be in VTCM in a single page.
class ScatterGatherGenerator : public IRMutator {
    Scope<Interval> bounds;
    std::unordered_map<string, const Allocate *> allocations;

    using IRMutator::visit;

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::if_then_else) && op->args[0].type().is_vector()) {
            const Broadcast *b = op->args[0].as<Broadcast>();
            if (!b || b->value.type().is_vector()) {
                return op;
            }
        }
        return IRMutator::visit(op);
    }

    template<typename NodeType, typename T>
    NodeType visit_let(const T *op) {
        // We only care about vector lets.
        if (op->value.type().is_vector()) {
            bounds.push(op->name, bounds_of_expr_in_scope(op->value, bounds));
        }
        NodeType node = IRMutator::visit(op);
        if (op->value.type().is_vector()) {
            bounds.pop(op->name);
        }
        return node;
    }

    Expr visit(const Let *op) override {
        return visit_let<Expr>(op);
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let<Stmt>(op);
    }

    Stmt visit(const Allocate *op) override {
        // Create a map of the allocation
        allocations[op->name] = op;
        return IRMutator::visit(op);
    }

    // Try to match expressions of the form:
    //     out(x) = lut(foo(x))
    // to generate vgathers. Here, out and lut should have
    // store_in(MemoryType::VTCM) directive. If a vgather is found return Call
    // Expr to vgather, otherwise Expr().
    Expr make_gather(const Load *op, Expr dst_base, Expr dst_index) {
        Type ty = op->type;
        const Allocate *alloc = allocations[op->name];
        // The lut should be in VTCM.
        if (!alloc || alloc->memory_type != MemoryType::VTCM) {
            return Expr();
        }
        // HVX has only 16 or 32-bit gathers. Predicated vgathers are not
        // supported yet.
        if (op->index.as<Ramp>() || !is_const_one(op->predicate) || !ty.is_vector() ||
            ty.bits() == 8) {
            return Expr();
        }
        Expr index = mutate(ty.bytes() * op->index);
        Interval index_bounds = bounds_of_expr_in_scope(index, bounds);
        if (ty.bits() == 16 && index_bounds.is_bounded()) {
            Expr index_span = span_of_bounds(index_bounds);
            index_span = common_subexpression_elimination(index_span);
            index_span = simplify(index_span);
            // We need to downcast the index values to 16 bit signed. So all the
            // the indices must be less than 1 << 15.
            if (!can_prove(index_span < std::numeric_limits<int16_t>::max())) {
                return Expr();
            }
        }
        // Calculate the size of the buffer lut in bytes.
        Expr size = ty.bytes();
        for (const auto &extent : alloc->extents) {
            size *= extent;
        }
        Expr src = Variable::make(Handle(), op->name);
        Expr new_index = mutate(cast(ty.with_code(Type::Int), index));
        dst_index = mutate(dst_index);

        return Call::make(ty, Call::hvx_gather, {std::move(dst_base), dst_index, src, size - 1, new_index},
                          Call::Intrinsic);
    }

    // Checks if the Store node can be replaced with a scatter_accumulate.
    // If yes, return new_value to be used for scatter-accumulate, else return
    // the input parameter value.
    Expr is_scatter_acc(const Store *op) {
        Expr lhs = Load::make(op->value.type(), op->name, op->index, Buffer<>(),
                              Parameter(), const_true(op->value.type().lanes()), op->alignment);
        Expr wild = Variable::make(op->value.type(), "*");
        vector<Expr> matches;
        if (expr_match(lhs + wild, op->value, matches) ||
            expr_match(wild + lhs, op->value, matches)) {
            // Scatter accumulate found.
            return matches[0];
        }
        return op->value;
    }

    Stmt visit(const Store *op) override {
        // HVX has only 16 or 32-bit gathers. Predicated vgathers are not
        // supported yet.
        Type ty = op->value.type();
        if (!is_const_one(op->predicate) || !ty.is_vector() || ty.bits() == 8) {
            return IRMutator::visit(op);
        }
        // To use vgathers, the destination address must be VTCM memory.
        const Allocate *alloc = allocations[op->name];
        if (!alloc || alloc->memory_type != MemoryType::VTCM) {
            return IRMutator::visit(op);
        }
        // The source for a gather must also be a buffer in VTCM.
        if (op->index.as<Ramp>() && op->value.as<Load>()) {
            // Check for vgathers
            Expr dst_base = Variable::make(Handle(), op->name);
            Expr dst_index = op->index.as<Ramp>()->base;
            Expr value = make_gather(op->value.as<Load>(), dst_base, dst_index);
            if (value.defined()) {
                // Found a vgather.
                // Function make_gather already mutates all the call arguements,
                // so no need to mutate again.
                return Evaluate::make(value);
            }
        }
        // Check for scatter/scatter-accumulate.
        if (op->index.as<Ramp>()) {
            return IRMutator::visit(op);
        }
        // Calculate the size of the buffer in bytes.
        Expr size = ty.bytes();
        for (const auto &extent : alloc->extents) {
            size *= extent;
        }
        // Check for scatter-acc.
        Expr value = is_scatter_acc(op);
        Call::IntrinsicOp intrinsic = Call::hvx_scatter;
        if (!value.same_as(op->value)) {
            // It's a scatter-accumulate
            intrinsic = Call::hvx_scatter_acc;
        }
        Expr buffer = Variable::make(Handle(), op->name);
        Expr index = mutate(cast(ty.with_code(Type::Int), ty.bytes() * op->index));
        value = mutate(value);
        Stmt scatter = Evaluate::make(Call::make(ty, intrinsic,
                                                 {buffer, size - 1, index, value}, Call::Intrinsic));
        return scatter;
    }
};

// Scatter-Gather instructions on Hexagon are asynchronous and hence require a
// scatter-release store followed by a vector load from the same address. This
// stalls the pipeline untill all previous scatter-gather operations have
// finished. The operations are not ordered with respect to load and store
// operations as well.
class SyncronizationBarriers : public IRMutator {
    // Keep track of all scatter-gather operations in flight which could cause
    // a hazard in the future.
    std::map<string, vector<const Stmt *>> in_flight;
    // Trail of For Blocks to reach a stmt.
    vector<const Stmt *> curr_path;
    // Current Stmt being mutated.
    const Stmt *curr = nullptr;
    // Track where the Stmt generated a scatter-release.
    std::map<const Stmt *, Expr> sync;

    using IRMutator::visit;

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::hvx_scatter) ||
            op->is_intrinsic(Call::hvx_scatter_acc) ||
            op->is_intrinsic(Call::hvx_gather)) {
            string name = op->args[0].as<Variable>()->name;
            // Check if the scatter-gather encountered conflicts with any
            // previous operation. If yes, insert a scatter-release.
            check_hazard(name);
            in_flight[name] = curr_path;
        }
        return IRMutator::visit(op);
    }

    Stmt visit(const For *op) override {
        // Keep trail of the For blocks encoutered.
        curr_path.push_back(curr);
        Stmt s = IRMutator::visit(op);
        curr_path.pop_back();
        return s;
    }

    // Creates entry in sync map for the stmt requiring a
    // scatter-release instruction before it.
    void check_hazard(const string &name) {
        if (in_flight.find(name) == in_flight.end()) {
            return;
        }
        // Sync Needed. Add the scatter-release before the first different For
        // loop lock between the curr_path and the hazard src location.
        size_t min_size = std::min(in_flight[name].size(), curr_path.size());
        size_t i = 0;
        // Find the first different For loop block.
        for (; i < min_size; i++) {
            if (in_flight[name][i] != curr_path[i]) {
                break;
            }
        }
        if (i < curr_path.size()) {
            // Place scatter-release before the first different For loop block.
            sync[curr_path[i]] = Variable::make(Handle(), name);
        } else {
            // Need to add the scatter-release before the curr stmt.
            sync[curr] = Variable::make(Handle(), name);
        }
        in_flight.clear();
    }

    Expr visit(const Load *op) override {
        // Resolve scatter-load hazard.
        check_hazard(op->name);
        return IRMutator::visit(op);
    }

    Stmt visit(const Store *op) override {
        // Resolve scatter-store and gather-store hazards.
        check_hazard(op->name);
        return IRMutator::visit(op);
    }

public:
    using IRMutator::mutate;

    Stmt mutate(const Stmt &s) override {
        curr = &s;
        Stmt new_s = IRMutator::mutate(s);
        // Wrap the stmt with scatter-release if any hazard was detected.
        if (sync.find(&s) != sync.end()) {
            Stmt scatter_sync =
                Evaluate::make(Call::make(Int(32), Call::hvx_scatter_release, {sync[&s]}, Call::Intrinsic));
            return Block::make(scatter_sync, new_s);
        }
        return new_s;
    }
};

}  // namespace

Stmt optimize_hexagon_shuffles(const Stmt &s, int lut_alignment) {
    // Replace indirect and other complicated loads with
    // dynamic_shuffle (vlut) calls.
    return OptimizeShuffles(lut_alignment).mutate(s);
}

Stmt scatter_gather_generator(Stmt s) {
    // Generate vscatter-vgather instruction if target >= v65
    s = substitute_in_all_lets(s);
    s = ScatterGatherGenerator().mutate(s);
    s = SyncronizationBarriers().mutate(s);
    s = common_subexpression_elimination(s);
    return s;
}

Stmt optimize_hexagon_instructions(Stmt s, const Target &t) {
    // We need to redo intrinsic matching due to simplification that has
    // happened after the end of target independent lowering.
    s = find_intrinsics(s);

    // Hexagon prefers widening shifts to be expressed as multiplies to
    // hopefully hit compound widening multiplies.
    s = DistributeShiftsAsMuls().mutate(s);

    // Pattern match VectorReduce IR node. Handle vector reduce instructions
    // before OptimizePatterns to prevent being mutated by patterns like
    // (v0 + v1 * c) -> add_mpy
    s = VectorReducePatterns().mutate(s);

    // Peephole optimize for Hexagon instructions. These can generate
    // interleaves and deinterleaves alongside the HVX intrinsics.
    s = OptimizePatterns(t).mutate(s);

    // Try to eliminate any redundant interleave/deinterleave pairs.
    s = EliminateInterleaves(t.natural_vector_size(Int(8))).mutate(s);

    // There may be interleaves left over that we can fuse with other
    // operations.
    s = FuseInterleaves().mutate(s);
    return s;
}

}  // namespace Internal
}  // namespace Halide
