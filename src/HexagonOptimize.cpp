#include "HexagonOptimize.h"
#include "Bounds.h"
#include "CSE.h"
#include "ConciseCasts.h"
#include "ExprUsesVar.h"
#include "IREquality.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Lerp.h"
#include "Scope.h"
#include "Simplify.h"
#include "Substitute.h"
#include "HexagonAlignment.h"
#include <unordered_map>

namespace Halide {
namespace Internal {

using std::pair;
using std::set;
using std::string;
using std::vector;

using namespace Halide::ConciseCasts;

Expr native_interleave(Expr x) {
    string fn;
    switch (x.type().bits()) {
    case 8: fn = "halide.hexagon.interleave.vb"; break;
    case 16: fn = "halide.hexagon.interleave.vh"; break;
    case 32: fn = "halide.hexagon.interleave.vw"; break;
    default: internal_error << "Cannot interleave native vectors of type " << x.type() << "\n";
    }
    return Call::make(x.type(), fn, {x}, Call::PureExtern);
}

Expr native_deinterleave(Expr x) {
    string fn;
    switch (x.type().bits()) {
    case 8: fn = "halide.hexagon.deinterleave.vb"; break;
    case 16: fn = "halide.hexagon.deinterleave.vh"; break;
    case 32: fn = "halide.hexagon.deinterleave.vw"; break;
    default: internal_error << "Cannot deinterleave native vectors of type " << x.type() << "\n";
    }
    return Call::make(x.type(), fn, {x}, Call::PureExtern);
}

bool is_native_interleave_op(Expr x, const char *name) {
    const Call *c = x.as<Call>();
    if (!c || c->args.size() != 1) return false;
    return starts_with(c->name, name);
}

bool is_native_interleave(Expr x) {
    return is_native_interleave_op(x, "halide.hexagon.interleave");
}

bool is_native_deinterleave(Expr x) {
    return is_native_interleave_op(x, "halide.hexagon.deinterleave");
}

namespace {

// Broadcast to an unknown number of lanes, for making patterns.
Expr bc(Expr x) { return Broadcast::make(x, 0); }

// This mutator rewrites patterns with an unknown number of lanes to
// have the specified number of lanes.
class WithLanes : public IRMutator2 {
    using IRMutator2::visit;

    int lanes;

    Type with_lanes(Type t) { return t.with_lanes(lanes); }

    Expr visit(const Cast *op) override {
        if (op->type.lanes() != lanes) {
            return Cast::make(with_lanes(op->type), mutate(op->value));
        } else {
            return IRMutator2::visit(op);
        }
    }

    Expr visit(const Variable *op) override {
        if (op->type.lanes() != lanes) {
            return Variable::make(with_lanes(op->type), op->name);
        } else {
            return op;
        }
    }

    Expr visit(const Broadcast *op) override {
        if (op->type.lanes() != lanes) {
            return Broadcast::make(op->value, lanes);
        } else {
            return IRMutator2::visit(op);
        }
    }

public:
    WithLanes(int lanes) : lanes(lanes) {}
};

Expr with_lanes(Expr x, int lanes) {
    return WithLanes(lanes).mutate(x);
}

struct Pattern {
    enum Flags {
        InterleaveResult = 1 << 0,  // After evaluating the pattern, interleave native vectors of the result.
        SwapOps01 = 1 << 1,  // Swap operands 0 and 1 prior to substitution.
        SwapOps12 = 1 << 2,  // Swap operands 1 and 2 prior to substitution.
        ExactLog2Op1 = 1 << 3, // Replace operand 1 with its log base 2, if the log base 2 is exact.
        ExactLog2Op2 = 1 << 4, // Save as above, but for operand 2.

        BeginExactLog2Op = 1,   // BeginExactLog2Op and EndExactLog2Op ensure that we check only op1 and op2
        EndExactLog2Op = 3,     // for ExactLog2Op

        DeinterleaveOp0 = 1 << 5,  // Prior to evaluating the pattern, deinterleave native vectors of operand 0.
        DeinterleaveOp1 = 1 << 6,  // Same as above, but for operand 1.
        DeinterleaveOp2 = 1 << 7,
        DeinterleaveOps = DeinterleaveOp0 | DeinterleaveOp1 | DeinterleaveOp2,

        BeginDeinterleaveOp = 0, // BeginDeinterleaveOp and EndDeinterleaveOp ensure that we check only three
        EndDeinterleaveOp = 3,   // deinterleave Op0, 1 and 2.
        // Many patterns are instructions that widen only
        // operand 0, which need to both deinterleave operand 0, and then
        // re-interleave the result.
        ReinterleaveOp0 = InterleaveResult | DeinterleaveOp0,

        NarrowOp0 = 1 << 10,  // Replace operand 0 with its half-width equivalent.
        NarrowOp1 = 1 << 11,  // Same as above, but for operand 1.
        NarrowOp2 = 1 << 12,
        NarrowOp3 = 1 << 13,
        NarrowOps = NarrowOp0 | NarrowOp1 | NarrowOp2 | NarrowOp3,

        NarrowUnsignedOp0 = 1 << 15,  // Similar to the above, but narrow to an unsigned half width type.
        NarrowUnsignedOp1 = 1 << 16,
        NarrowUnsignedOp2 = 1 << 17,
        NarrowUnsignedOps = NarrowUnsignedOp0 | NarrowUnsignedOp1 | NarrowUnsignedOp2,

        v62orLater = 1 << 20,  // Pattern should be matched only for v62 target or later
        v65orLater = 1 << 21,  // Pattern should be matched only for v65 target or later
        v66orLater = 1 << 22,  // Pattern should be matched only for v66 target or later
   };

    string intrin;        // Name of the intrinsic
    Expr pattern;         // The pattern to match against
    int flags;

    Pattern() {}
    Pattern(const string &intrin, Expr p, int flags = 0)
        : intrin(intrin), pattern(p), flags(flags) {}
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
    if ((flags & (Pattern::v62orLater)) &&
        !target.features_any_of({Target::HVX_v62, Target::HVX_v65, Target::HVX_v66})) {
        return false;
    }
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
        Type target_t = t.with_bits(t.bits()/2);
        if (flags & (Pattern::NarrowOp0 << i)) {
            matches[i] = lossless_cast(target_t, matches[i]);
        } else if (flags & (Pattern::NarrowUnsignedOp0 << i)) {
            matches[i] = lossless_cast(target_t.with_code(Type::UInt), matches[i]);
        }
        if (!matches[i].defined()) return false;
    }

    for (size_t i = Pattern::BeginExactLog2Op; i < Pattern::EndExactLog2Op; i++) {
        // This flag is mainly to capture shifts. When the operand of a div or
        // mul is a power of 2, we can use a shift instead.
        if (flags & (Pattern::ExactLog2Op1 << (i - Pattern::BeginExactLog2Op))) {
            int pow;
            if (is_const_power_of_two_integer(matches[i], &pow)) {
                matches[i] = cast(matches[i].type().with_lanes(1), pow);
            } else {
                return false;
            }
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

// Attempt to apply one of the patterns to x. If a match is
// successful, the expression is replaced with a call using the
// matched operands. Prior to substitution, the matches are mutated
// with op_mutator.
Expr apply_patterns(Expr x, const vector<Pattern> &patterns, const Target &target, IRMutator2 *op_mutator) {
    debug(3) << "apply_patterns " << x << "\n";
    vector<Expr> matches;
    for (const Pattern &p : patterns) {
        if (!check_pattern_target(p.flags, target)) {
            continue;
        }

        if (expr_match(p.pattern, x, matches)) {
            debug(3) << "matched " << p.pattern << "\n";
            debug(3) << "matches:\n";
            for (Expr i : matches) {
                debug(3) << i << "\n";
            }

            if (!process_match_flags(matches, p.flags)) {
                continue;
            }

            // Mutate the operands with the given mutator.
            for (Expr &op : matches) {
                op = op_mutator->mutate(op);
            }

            x = replace_pattern(x, matches, p);
            debug(3) << "rewrote to: " << x << "\n";
            return x;
        }
    }
    return x;
}

// Replace x with a negated version of x, if it can be done without
// overflow.
Expr lossless_negate(Expr x) {
    const Mul *m = x.as<Mul>();
    if (m) {
        Expr a = lossless_negate(m->a);
        if (a.defined()) {
            return Mul::make(a, m->b);
        }
        Expr b = lossless_negate(m->b);
        if (b.defined()) {
            return Mul::make(m->a, b);
        }
    }
    if (is_negative_negatable_const(x) || is_positive_const(x)) {
        return simplify(-x);
    }
    return Expr();
}

template <typename T>
Expr apply_commutative_patterns(const T *op, const vector<Pattern> &patterns, const Target &target, IRMutator2 *mutator) {
    Expr ret = apply_patterns(op, patterns, target, mutator);
    if (!ret.same_as(op)) return ret;

    // Try commuting the op
    Expr commuted = T::make(op->b, op->a);
    ret = apply_patterns(commuted, patterns, target, mutator);
    if (!ret.same_as(commuted)) return ret;

    return op;
}

typedef pair<Expr, Expr> MulExpr;

// If ty is scalar, and x is a vector, try to remove a broadcast
// from x prior to using lossless_cast on it.
Expr unbroadcast_lossless_cast(Type ty, Expr x) {
    if (ty.lanes() == 1 && x.type().lanes() > 1) {
        if (const Broadcast *bc = x.as<Broadcast>()) {
            x = bc->value;
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
int find_mpy_ops(Expr op, Type a_ty, Type b_ty, int max_mpy_count,
                        vector<MulExpr> &mpys, Expr &rest) {
    if ((int)mpys.size() >= max_mpy_count) {
        rest = rest.defined() ? Add::make(rest, op) : op;
        return 0;
    }

    // If the add is also widening, remove the cast.
    int mpy_bits = std::max(a_ty.bits(), b_ty.bits())*2;
    Expr maybe_mul = op;
    if (op.type().bits() == mpy_bits*2) {
        if (const Cast *cast = op.as<Cast>()) {
            if (cast->value.type().bits() == mpy_bits) {
                maybe_mul = cast->value;
            }
        }
    }

    if (const Mul *mul = maybe_mul.as<Mul>()) {
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
    } else if (const Sub *sub = op.as<Sub>()) {
        // Try to rewrite subs as adds.
        if (const Mul *mul_b = sub->b.as<Mul>()) {
            if (is_positive_const(mul_b->a) || is_negative_negatable_const(mul_b->a)) {
                Expr add_b = Mul::make(simplify(-mul_b->a), mul_b->b);
                int mpy_count = 0;
                mpy_count += find_mpy_ops(sub->a, a_ty, b_ty, max_mpy_count, mpys, rest);
                mpy_count += find_mpy_ops(add_b, a_ty, b_ty, max_mpy_count, mpys, rest);
                return mpy_count;
            } else if (is_positive_const(mul_b->b) || is_negative_negatable_const(mul_b->b)) {
                Expr add_b = Mul::make(mul_b->a, simplify(-mul_b->b));
                int mpy_count = 0;
                mpy_count += find_mpy_ops(sub->a, a_ty, b_ty, max_mpy_count, mpys, rest);
                mpy_count += find_mpy_ops(add_b, a_ty, b_ty, max_mpy_count, mpys, rest);
                return mpy_count;
            }
        }
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
class OptimizePatterns : public IRMutator2 {
private:
    using IRMutator2::visit;

    Target target;

    Expr visit(const Mul *op) override {
        static const vector<Pattern> scalar_muls = {
            // Vector by scalar widening multiplies.
            { "halide.hexagon.mpy.vub.ub", wild_u16x*bc(wild_u16), Pattern::InterleaveResult | Pattern::NarrowOps },
            { "halide.hexagon.mpy.vub.b",  wild_i16x*bc(wild_i16), Pattern::InterleaveResult | Pattern::NarrowUnsignedOp0 | Pattern::NarrowOp1 },
            { "halide.hexagon.mpy.vuh.uh", wild_u32x*bc(wild_u32), Pattern::InterleaveResult | Pattern::NarrowOps },
            { "halide.hexagon.mpy.vh.h",   wild_i32x*bc(wild_i32), Pattern::InterleaveResult | Pattern::NarrowOps },

            // Multiplication by powers of 2.
            { "halide.hexagon.shl.vub.ub", wild_u8x*bc(wild_u8), Pattern::ExactLog2Op1 },
            { "halide.hexagon.shl.vuh.uh", wild_u16x*bc(wild_u16), Pattern::ExactLog2Op1 },
            { "halide.hexagon.shl.vuw.uw", wild_u32x*bc(wild_u32), Pattern::ExactLog2Op1 },
            { "halide.hexagon.shl.vb.b", wild_i8x*bc(wild_i8), Pattern::ExactLog2Op1 },
            { "halide.hexagon.shl.vh.h", wild_i16x*bc(wild_i16), Pattern::ExactLog2Op1 },
            { "halide.hexagon.shl.vw.w", wild_i32x*bc(wild_i32), Pattern::ExactLog2Op1 },

            // Non-widening scalar multiplication.
            { "halide.hexagon.mul.vh.b", wild_i16x*bc(wild_i16), Pattern::NarrowOp1 },
            { "halide.hexagon.mul.vw.h", wild_i32x*bc(wild_i32), Pattern::NarrowOp1 },
            // TODO: There's also mul.vw.b. We currently generate mul.vw.h
            // instead. I'm not sure mul.vw.b is faster, it might even be
            // slower due to the extra step in broadcasting the scalar up to
            // 32 bits.
        };

        static const vector<Pattern> muls = {
            // Widening multiplication
            { "halide.hexagon.mpy.vub.vub", wild_u16x*wild_u16x, Pattern::InterleaveResult | Pattern::NarrowOps },
            { "halide.hexagon.mpy.vuh.vuh", wild_u32x*wild_u32x, Pattern::InterleaveResult | Pattern::NarrowOps },
            { "halide.hexagon.mpy.vb.vb",   wild_i16x*wild_i16x, Pattern::InterleaveResult | Pattern::NarrowOps },
            { "halide.hexagon.mpy.vh.vh",   wild_i32x*wild_i32x, Pattern::InterleaveResult | Pattern::NarrowOps },

            { "halide.hexagon.mpy.vub.vb",  wild_i16x*wild_i16x, Pattern::InterleaveResult | Pattern::NarrowUnsignedOp0 | Pattern::NarrowOp1 },
            { "halide.hexagon.mpy.vh.vuh",  wild_i32x*wild_i32x, Pattern::InterleaveResult | Pattern::NarrowOp0 | Pattern::NarrowUnsignedOp1 },
            // We need to check for the commuted versions of these patterns
            // before the more general patterns below catch these ops. The
            // other fix for this would be to break this into a third group of
            // multiply patterns, so the commuted versions of these would get
            // matched first.
            { "halide.hexagon.mpy.vub.vb",  wild_i16x*wild_i16x, Pattern::InterleaveResult | Pattern::NarrowOp0 | Pattern::NarrowUnsignedOp1 | Pattern::SwapOps01 },
            { "halide.hexagon.mpy.vh.vuh",  wild_i32x*wild_i32x, Pattern::InterleaveResult | Pattern::NarrowUnsignedOp0 | Pattern::NarrowOp1 | Pattern::SwapOps01 },

            // One operand widening multiplication.
            { "halide.hexagon.mul.vw.vh", wild_i32x*wild_i32x, Pattern::ReinterleaveOp0 | Pattern::NarrowOp1 },
            { "halide.hexagon.mul.vw.vuh", wild_i32x*wild_i32x, Pattern::ReinterleaveOp0 | Pattern::NarrowUnsignedOp1 },
            { "halide.hexagon.mul.vuw.vuh", wild_u32x*wild_u32x, Pattern::ReinterleaveOp0 | Pattern::NarrowUnsignedOp1 },
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
        return IRMutator2::visit(op);
    }

    // Helpers to generate horizontally reducing multiply operations.
    static Expr halide_hexagon_add_2mpy(Type result_type, string suffix, Expr v0, Expr v1, Expr c0, Expr c1) {
        Expr call = Call::make(result_type, "halide.hexagon.add_2mpy" + suffix, {v0, v1, c0, c1}, Call::PureExtern);
        return native_interleave(call);
    }

    static Expr halide_hexagon_add_2mpy(Type result_type, string suffix, Expr v01, Expr c01) {
        return Call::make(result_type, "halide.hexagon.add_2mpy" + suffix, {v01, c01}, Call::PureExtern);
    }

    static Expr halide_hexagon_add_4mpy(Type result_type, string suffix, Expr v01, Expr c01) {
        return Call::make(result_type, "halide.hexagon.add_4mpy" + suffix, {v01, c01}, Call::PureExtern);
    }
    // We'll try to sort the mpys based my mpys.first.
    // But, for this all the mpy.first exprs should either be
    // all loads or all slice_vectors.
    static void sort_mpy_exprs(vector<MulExpr> &mpys) {
        struct LoadCompare {
            bool operator()(const MulExpr &m1, const MulExpr &m2) {
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
    Expr visit(const Add *op) override {
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
                    Expr new_expr = halide_hexagon_add_4mpy(op->type, suffix, a0123, b0123);
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
                    Expr new_expr = halide_hexagon_add_4mpy(op->type, suffix, a0123, b0123);
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
                    Expr b01 = Shuffle::make_interleave({mpys[0].second, mpys[1].second});
                    b01 = simplify(b01);
                    b01 = reinterpret(Type(b01.type().code(), 16, 1), b01);
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

        static const vector<Pattern> adds = {
            // Use accumulating versions of vmpa, vdmpy, vrmpy instructions when possible.
            { "halide.hexagon.acc_add_2mpy.vh.vub.vub.b.b", wild_i16x + halide_hexagon_add_2mpy(Int(16, 0),  ".vub.vub.b.b", wild_u8x, wild_u8x, wild_i8, wild_i8), Pattern::ReinterleaveOp0 },
            { "halide.hexagon.acc_add_2mpy.vw.vh.vh.b.b",   wild_i32x + halide_hexagon_add_2mpy(Int(32, 0),  ".vh.vh.b.b", wild_i16x, wild_i16x, wild_i8, wild_i8), Pattern::ReinterleaveOp0 },
            { "halide.hexagon.acc_add_2mpy.vh.vub.b",       wild_i16x + halide_hexagon_add_2mpy(Int(16, 0),  ".vub.b", wild_u8x, wild_i16) },
            { "halide.hexagon.acc_add_2mpy.vw.vh.b",        wild_i32x + halide_hexagon_add_2mpy(Int(32, 0),  ".vh.b", wild_i16x, wild_i16) },
            { "halide.hexagon.acc_add_4mpy.vw.vub.b",       wild_i32x + halide_hexagon_add_4mpy(Int(32, 0),  ".vub.b", wild_u8x, wild_i32) },
            { "halide.hexagon.acc_add_4mpy.vuw.vub.ub",     wild_u32x + halide_hexagon_add_4mpy(UInt(32, 0), ".vub.ub", wild_u8x, wild_u32) },
            { "halide.hexagon.acc_add_4mpy.vuw.vub.vub",    wild_u32x + halide_hexagon_add_4mpy(UInt(32, 0), ".vub.vub", wild_u8x, wild_u8x) },
            { "halide.hexagon.acc_add_4mpy.vw.vub.vb",      wild_i32x + halide_hexagon_add_4mpy(Int(32, 0),  ".vub.vb", wild_u8x, wild_i8x) },
            { "halide.hexagon.acc_add_4mpy.vw.vb.vb",       wild_i32x + halide_hexagon_add_4mpy(Int(32, 0),  ".vb.vb", wild_i8x, wild_i8x) },

            // Widening adds. There are other instructions that add two vub and two vuh but do not widen.
            // To differentiate those from the widening ones, we encode the return type in the name here.
            { "halide.hexagon.add_vuh.vub.vub", wild_u16x + wild_u16x, Pattern::InterleaveResult | Pattern::NarrowOps },
            { "halide.hexagon.add_vuw.vuh.vuh", wild_u32x + wild_u32x, Pattern::InterleaveResult | Pattern::NarrowOps },
            { "halide.hexagon.add_vw.vh.vh", wild_i32x + wild_i32x, Pattern::InterleaveResult | Pattern::NarrowOps },

            // Widening multiply-accumulates with a scalar.
            { "halide.hexagon.add_mpy.vuh.vub.ub", wild_u16x + wild_u16x*bc(wild_u16), Pattern::ReinterleaveOp0 | Pattern::NarrowOp1 | Pattern::NarrowOp2 },
            { "halide.hexagon.add_mpy.vh.vub.b",   wild_i16x + wild_i16x*bc(wild_i16), Pattern::ReinterleaveOp0 | Pattern::NarrowUnsignedOp1 | Pattern::NarrowOp2 },
            { "halide.hexagon.add_mpy.vuw.vuh.uh", wild_u32x + wild_u32x*bc(wild_u32), Pattern::ReinterleaveOp0 | Pattern::NarrowOp1 | Pattern::NarrowOp2 },
            { "halide.hexagon.add_mpy.vuh.vub.ub", wild_u16x + bc(wild_u16)*wild_u16x, Pattern::ReinterleaveOp0 | Pattern::NarrowOp1 | Pattern::NarrowOp2 | Pattern::SwapOps12 },
            { "halide.hexagon.add_mpy.vh.vub.b",   wild_i16x + bc(wild_i16)*wild_i16x, Pattern::ReinterleaveOp0 | Pattern::NarrowOp1 | Pattern::NarrowUnsignedOp2 | Pattern::SwapOps12 },
            { "halide.hexagon.add_mpy.vuw.vuh.uh", wild_u32x + bc(wild_u32)*wild_u32x, Pattern::ReinterleaveOp0 | Pattern::NarrowOp1 | Pattern::NarrowOp2 | Pattern::SwapOps12 },

            // These patterns aren't exactly right because the instruction
            // saturates the result. However, this is really the instruction
            // that we want to use in most cases, and we can exploit the fact
            // that 32 bit signed arithmetic overflow is undefined to argue
            // that these patterns are not completely incorrect.
            { "halide.hexagon.satw_add_mpy.vw.vh.h", wild_i32x + wild_i32x*bc(wild_i32), Pattern::ReinterleaveOp0 | Pattern::NarrowOp1 | Pattern::NarrowOp2 },
            { "halide.hexagon.satw_add_mpy.vw.vh.h", wild_i32x + bc(wild_i32)*wild_i32x, Pattern::ReinterleaveOp0 | Pattern::NarrowOp1 | Pattern::NarrowOp2 | Pattern::SwapOps12 },

            // Widening multiply-accumulates.
            { "halide.hexagon.add_mpy.vuh.vub.vub", wild_u16x + wild_u16x*wild_u16x, Pattern::ReinterleaveOp0 | Pattern::NarrowOp1 | Pattern::NarrowOp2 },
            { "halide.hexagon.add_mpy.vuw.vuh.vuh", wild_u32x + wild_u32x*wild_u32x, Pattern::ReinterleaveOp0 | Pattern::NarrowOp1 | Pattern::NarrowOp2 },
            { "halide.hexagon.add_mpy.vh.vb.vb",    wild_i16x + wild_i16x*wild_i16x, Pattern::ReinterleaveOp0 | Pattern::NarrowOp1 | Pattern::NarrowOp2 },
            { "halide.hexagon.add_mpy.vw.vh.vh",    wild_i32x + wild_i32x*wild_i32x, Pattern::ReinterleaveOp0 | Pattern::NarrowOp1 | Pattern::NarrowOp2 },

            { "halide.hexagon.add_mpy.vh.vub.vb",   wild_i16x + wild_i16x*wild_i16x, Pattern::ReinterleaveOp0 | Pattern::NarrowUnsignedOp1 | Pattern::NarrowOp2 },
            { "halide.hexagon.add_mpy.vw.vh.vuh",   wild_i32x + wild_i32x*wild_i32x, Pattern::ReinterleaveOp0 | Pattern::NarrowOp1 | Pattern::NarrowUnsignedOp2 },
            { "halide.hexagon.add_mpy.vh.vub.vb",   wild_i16x + wild_i16x*wild_i16x, Pattern::ReinterleaveOp0 | Pattern::NarrowOp1 | Pattern::NarrowUnsignedOp2 | Pattern::SwapOps12 },
            { "halide.hexagon.add_mpy.vw.vh.vuh",   wild_i32x + wild_i32x*wild_i32x, Pattern::ReinterleaveOp0 | Pattern::NarrowUnsignedOp1 | Pattern::NarrowOp2 | Pattern::SwapOps12 },

            // Shift-accumulates.
            { "halide.hexagon.add_shr.vw.vw.w", wild_i32x + (wild_i32x >> bc(wild_i32)) },
            { "halide.hexagon.add_shl.vw.vw.w", wild_i32x + (wild_i32x << bc(wild_i32)) },
            { "halide.hexagon.add_shl.vw.vw.w", wild_u32x + (wild_u32x << bc(wild_u32)) },
            { "halide.hexagon.add_shr.vw.vw.w", wild_i32x + (wild_i32x/bc(wild_i32)), Pattern::ExactLog2Op2 },
            { "halide.hexagon.add_shl.vw.vw.w", wild_i32x + (wild_i32x*bc(wild_i32)), Pattern::ExactLog2Op2 },
            { "halide.hexagon.add_shl.vw.vw.w", wild_u32x + (wild_u32x*bc(wild_u32)), Pattern::ExactLog2Op2 },
            { "halide.hexagon.add_shl.vw.vw.w", wild_i32x + (bc(wild_i32)*wild_i32x), Pattern::ExactLog2Op1 | Pattern::SwapOps12 },
            { "halide.hexagon.add_shl.vw.vw.w", wild_u32x + (bc(wild_u32)*wild_u32x), Pattern::ExactLog2Op1 | Pattern::SwapOps12 },
            { "halide.hexagon.add_shl.vh.vh.h", wild_i16x + (wild_i16x << bc(wild_i16)), Pattern::v65orLater },
            { "halide.hexagon.add_shl.vh.vh.h", wild_u16x + (wild_u16x << bc(wild_u16)), Pattern::v65orLater },
            { "halide.hexagon.add_shl.vh.vh.h", wild_i16x + (bc(wild_i16) << wild_i16x), Pattern::SwapOps12 | Pattern::v65orLater },
            { "halide.hexagon.add_shl.vh.vh.h", wild_u16x + (bc(wild_u16) << wild_u16x), Pattern::SwapOps12 | Pattern::v65orLater },
            { "halide.hexagon.add_shr.vh.vh.h", wild_i16x + (wild_i16x >> bc(wild_i16)), Pattern::v65orLater },
            { "halide.hexagon.add_shr.vh.vh.h", wild_i16x + (wild_i16x/bc(wild_i16)), Pattern::ExactLog2Op2 | Pattern::v65orLater },
            { "halide.hexagon.add_shl.vh.vh.h", wild_i16x + (wild_i16x*bc(wild_i16)), Pattern::ExactLog2Op2 | Pattern::v65orLater },
            { "halide.hexagon.add_shl.vh.vh.h", wild_u16x + (wild_u16x*bc(wild_u16)), Pattern::ExactLog2Op2 | Pattern::v65orLater },
            { "halide.hexagon.add_shl.vh.vh.h", wild_i16x + (bc(wild_i16)*wild_i16x), Pattern::ExactLog2Op1 | Pattern::SwapOps12 | Pattern::v65orLater },
            { "halide.hexagon.add_shl.vh.vh.h", wild_u16x + (bc(wild_u16)*wild_u16x), Pattern::ExactLog2Op1 | Pattern::SwapOps12 | Pattern::v65orLater },

            // Non-widening multiply-accumulates with a scalar.
            { "halide.hexagon.add_mul.vh.vh.b", wild_i16x + wild_i16x*bc(wild_i16), Pattern::NarrowOp2 },
            { "halide.hexagon.add_mul.vw.vw.h", wild_i32x + wild_i32x*bc(wild_i32), Pattern::NarrowOp2 },
            { "halide.hexagon.add_mul.vh.vh.b", wild_i16x + bc(wild_i16)*wild_i16x, Pattern::NarrowOp1 | Pattern::SwapOps12 },
            { "halide.hexagon.add_mul.vw.vw.h", wild_i32x + bc(wild_i32)*wild_i32x, Pattern::NarrowOp1 | Pattern::SwapOps12 },
            // TODO: There's also a add_mul.vw.vw.b

            // This pattern is very general, so it must come last.
            { "halide.hexagon.add_mul.vh.vh.vh", wild_i16x + wild_i16x*wild_i16x },
        };

        if (op->type.is_vector()) {
            Expr new_expr = apply_commutative_patterns(op, adds, target, this);
            if (!new_expr.same_as(op)) {
                return new_expr;
            }
        }
        return IRMutator2::visit(op);
    }

    Expr visit(const Sub *op) override {
        if (op->type.is_vector()) {
            // Try negating op->b, using an add pattern if successful.
            Expr neg_b = lossless_negate(op->b);
            if (neg_b.defined()) {
                return mutate(op->a + neg_b);
            } else {
                static const vector<Pattern> subs = {
                    // Widening subtracts. There are other instructions that subtact two vub and two vuh but do not widen.
                    // To differentiate those from the widening ones, we encode the return type in the name here.
                    { "halide.hexagon.sub_vuh.vub.vub", wild_u16x - wild_u16x, Pattern::InterleaveResult | Pattern::NarrowOps },
                    { "halide.hexagon.sub_vh.vub.vub", wild_i16x - wild_i16x, Pattern::InterleaveResult | Pattern::NarrowUnsignedOps },
                    { "halide.hexagon.sub_vuw.vuh.vuh", wild_u32x - wild_u32x, Pattern::InterleaveResult | Pattern::NarrowOps },
                    { "halide.hexagon.sub_vw.vuh.vuh", wild_i32x - wild_i32x, Pattern::InterleaveResult | Pattern::NarrowUnsignedOps },
                    { "halide.hexagon.sub_vw.vh.vh", wild_i32x - wild_i32x, Pattern::InterleaveResult | Pattern::NarrowOps },
                };

                Expr new_expr = apply_patterns(op, subs, target, this);
                if (!new_expr.same_as(op)) {
                    return new_expr;
                }
            }
        }
        return IRMutator2::visit(op);
    }

    Expr visit(const Max *op) override {
        Expr expr = IRMutator2::visit(op);

        if (op->type.is_vector()) {
            // This pattern is weird (two operands must match, result
            // needs 1 added) and we're unlikely to need another
            // pattern for max, so just match it directly.
            static const pair<string, Expr> cl[] = {
                { "halide.hexagon.cls.vh", max(count_leading_zeros(wild_i16x), count_leading_zeros(~wild_i16x)) },
                { "halide.hexagon.cls.vw", max(count_leading_zeros(wild_i32x), count_leading_zeros(~wild_i32x)) },
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
        // Separate these so we can do some special handling below.
        static const vector<Pattern> trunc_mpy = {
            // Multiply keep high half
            { "halide.hexagon.trunc_mpy.vw.vw", i32((wild_i64x*wild_i64x)/wild_i64), Pattern::NarrowOps },

            // Scalar multiply keep high half, with multiplication by 2.
            { "halide.hexagon.trunc_satw_mpy2.vh.h", i16_sat((wild_i32x*bc(wild_i32))/wild_i32), Pattern::NarrowOps },
            { "halide.hexagon.trunc_satw_mpy2.vh.h", i16_sat((bc(wild_i32)*wild_i32x)/wild_i32), Pattern::NarrowOps | Pattern::SwapOps01 },

            // Scalar and vector multiply keep high half, with multiplication by 2, and rounding.
            { "halide.hexagon.trunc_satw_mpy2_rnd.vh.h", i16_sat((wild_i32x*bc(wild_i32) + wild_i32)/wild_i32), Pattern::NarrowOps },
            { "halide.hexagon.trunc_satw_mpy2_rnd.vh.h", i16_sat((bc(wild_i32)*wild_i32x + wild_i32)/wild_i32), Pattern::NarrowOps | Pattern::SwapOps01 },
            { "halide.hexagon.trunc_satw_mpy2_rnd.vh.vh", i16_sat((wild_i32x*wild_i32x + wild_i32)/wild_i32), Pattern::NarrowOps },
            { "halide.hexagon.trunc_satdw_mpy2_rnd.vw.vw", i32_sat((wild_i64x*wild_i64x + wild_i64)/wild_i64), Pattern::NarrowOps },

            // Vector multiply keep high half, with multiplicatoin by 2.
            { "halide.hexagon.trunc_satdw_mpy2.vw.vw", i32_sat((wild_i64x*wild_i64x)/wild_i64), Pattern::NarrowOps },
        };

        static const vector<Pattern> casts = {
            // Averaging
            { "halide.hexagon.avg.vub.vub", u8((wild_u16x + wild_u16x)/2), Pattern::NarrowOps },
            { "halide.hexagon.avg.vuh.vuh", u16((wild_u32x + wild_u32x)/2), Pattern::NarrowOps },
            { "halide.hexagon.avg.vh.vh", i16((wild_i32x + wild_i32x)/2), Pattern::NarrowOps },
            { "halide.hexagon.avg.vw.vw", i32((wild_i64x + wild_i64x)/2), Pattern::NarrowOps },
            { "halide.hexagon.avg.vb.vb", i8((wild_i16x + wild_i16x)/2), Pattern::NarrowOps | Pattern::v65orLater },
            { "halide.hexagon.avg.vuw.vuw", u32((wild_u64x + wild_u64x)/2), Pattern::NarrowOps | Pattern::v65orLater },

            { "halide.hexagon.avg_rnd.vub.vub", u8((wild_u16x + wild_u16x + 1)/2), Pattern::NarrowOps },
            { "halide.hexagon.avg_rnd.vuh.vuh", u16((wild_u32x + wild_u32x + 1)/2), Pattern::NarrowOps },
            { "halide.hexagon.avg_rnd.vh.vh", i16((wild_i32x + wild_i32x + 1)/2), Pattern::NarrowOps },
            { "halide.hexagon.avg_rnd.vw.vw", i32((wild_i64x + wild_i64x + 1)/2), Pattern::NarrowOps },

            { "halide.hexagon.navg.vub.vub", i8_sat((wild_i16x - wild_i16x)/2), Pattern::NarrowUnsignedOps },
            { "halide.hexagon.navg.vh.vh", i16_sat((wild_i32x - wild_i32x)/2), Pattern::NarrowOps },
            { "halide.hexagon.navg.vw.vw", i32_sat((wild_i64x - wild_i64x)/2), Pattern::NarrowOps },
            // vnavg.uw doesn't exist.

            // Saturating add/subtract
            { "halide.hexagon.satub_add.vub.vub", u8_sat(wild_u16x + wild_u16x), Pattern::NarrowOps },
            { "halide.hexagon.satuh_add.vuh.vuh", u16_sat(wild_u32x + wild_u32x), Pattern::NarrowOps },
            { "halide.hexagon.satuw_add.vuw.vuw", u32_sat(wild_u64x + wild_u64x), Pattern::NarrowOps | Pattern::v62orLater },
            { "halide.hexagon.sath_add.vh.vh", i16_sat(wild_i32x + wild_i32x), Pattern::NarrowOps },
            { "halide.hexagon.satw_add.vw.vw", i32_sat(wild_i64x + wild_i64x), Pattern::NarrowOps },

            { "halide.hexagon.satub_sub.vub.vub", u8_sat(wild_i16x - wild_i16x), Pattern::NarrowUnsignedOps },
            { "halide.hexagon.satuh_sub.vuh.vuh", u16_sat(wild_i32x - wild_i32x), Pattern::NarrowUnsignedOps },
            { "halide.hexagon.sath_sub.vh.vh", i16_sat(wild_i32x - wild_i32x), Pattern::NarrowOps },
            { "halide.hexagon.satw_sub.vw.vw", i32_sat(wild_i64x - wild_i64x), Pattern::NarrowOps },

            // Saturating narrowing casts with rounding
            { "halide.hexagon.trunc_satub_rnd.vh", u8_sat((wild_i32x + 128)/256), Pattern::DeinterleaveOp0 | Pattern::NarrowOp0 },
            { "halide.hexagon.trunc_satb_rnd.vh",  i8_sat((wild_i32x + 128)/256), Pattern::DeinterleaveOp0 | Pattern::NarrowOp0 },
            { "halide.hexagon.trunc_satuh_rnd.vw", u16_sat((wild_i64x + 32768)/65536), Pattern::DeinterleaveOp0 | Pattern::NarrowOp0 },
            { "halide.hexagon.trunc_sath_rnd.vw",  i16_sat((wild_i64x + 32768)/65536), Pattern::DeinterleaveOp0 | Pattern::NarrowOp0 },

            // Saturating narrowing casts
            { "halide.hexagon.trunc_satub_shr.vh.h", u8_sat(wild_i16x >> wild_i16), Pattern::DeinterleaveOp0 },
            { "halide.hexagon.trunc_satuh_shr.vw.w", u16_sat(wild_i32x >> wild_i32), Pattern::DeinterleaveOp0 },
            { "halide.hexagon.trunc_sath_shr.vw.w",  i16_sat(wild_i32x >> wild_i32), Pattern::DeinterleaveOp0 },
            { "halide.hexagon.trunc_satub_shr.vh.h", u8_sat(wild_i16x/wild_i16), Pattern::DeinterleaveOp0 | Pattern::ExactLog2Op1 },
            { "halide.hexagon.trunc_satuh_shr.vw.w", u16_sat(wild_i32x/wild_i32), Pattern::DeinterleaveOp0 | Pattern::ExactLog2Op1 },
            { "halide.hexagon.trunc_sath_shr.vw.w",  i16_sat(wild_i32x/wild_i32), Pattern::DeinterleaveOp0 | Pattern::ExactLog2Op1 },

            // For some of the following narrowing casts, we have the choice of
            // non-interleaving or interleaving instructions. Because we don't
            // know which one we prefer during pattern matching, we match the
            // non-interleaving versions for now and replace them with the
            // instructions that interleave later if it makes sense.

            // Saturating narrowing casts. These may interleave later with trunc_sat.
            { "halide.hexagon.pack_satub.vh", u8_sat(wild_i16x) },
            { "halide.hexagon.pack_satuh.vw", u16_sat(wild_i32x) },
            { "halide.hexagon.pack_satb.vh", i8_sat(wild_i16x) },
            { "halide.hexagon.pack_sath.vw", i16_sat(wild_i32x) },

            // We don't have a vpack equivalent to this one, so we match it directly.
            { "halide.hexagon.trunc_satuh.vuw", u16_sat(wild_u32x), Pattern::DeinterleaveOp0 | Pattern::v62orLater },

            // Narrowing casts. These may interleave later with trunclo.
            { "halide.hexagon.packhi.vh", u8(wild_u16x/256) },
            { "halide.hexagon.packhi.vh", u8(wild_i16x/256) },
            { "halide.hexagon.packhi.vh", i8(wild_u16x/256) },
            { "halide.hexagon.packhi.vh", i8(wild_i16x/256) },
            { "halide.hexagon.packhi.vw", u16(wild_u32x/65536) },
            { "halide.hexagon.packhi.vw", u16(wild_i32x/65536) },
            { "halide.hexagon.packhi.vw", i16(wild_u32x/65536) },
            { "halide.hexagon.packhi.vw", i16(wild_i32x/65536) },

            // Narrowing with shifting.
            { "halide.hexagon.trunc_shr.vw.w",  i16(wild_i32x >> wild_i32), Pattern::DeinterleaveOp0 },
            { "halide.hexagon.trunc_shr.vw.w",  i16(wild_i32x/wild_i32), Pattern::DeinterleaveOp0 | Pattern::ExactLog2Op1 },

            // Narrowing casts. These may interleave later with trunc.
            { "halide.hexagon.pack.vh", u8(wild_u16x) },
            { "halide.hexagon.pack.vh", u8(wild_i16x) },
            { "halide.hexagon.pack.vh", i8(wild_u16x) },
            { "halide.hexagon.pack.vh", i8(wild_i16x) },
            { "halide.hexagon.pack.vw", u16(wild_u32x) },
            { "halide.hexagon.pack.vw", u16(wild_i32x) },
            { "halide.hexagon.pack.vw", i16(wild_u32x) },
            { "halide.hexagon.pack.vw", i16(wild_i32x) },

            // Widening casts
            { "halide.hexagon.zxt.vub", u16(wild_u8x), Pattern::InterleaveResult },
            { "halide.hexagon.zxt.vub", i16(wild_u8x), Pattern::InterleaveResult },
            { "halide.hexagon.zxt.vuh", u32(wild_u16x), Pattern::InterleaveResult },
            { "halide.hexagon.zxt.vuh", i32(wild_u16x), Pattern::InterleaveResult },
            { "halide.hexagon.sxt.vb", u16(wild_i8x), Pattern::InterleaveResult },
            { "halide.hexagon.sxt.vb", i16(wild_i8x), Pattern::InterleaveResult },
            { "halide.hexagon.sxt.vh", u32(wild_i16x), Pattern::InterleaveResult },
            { "halide.hexagon.sxt.vh", i32(wild_i16x), Pattern::InterleaveResult },
        };


        // To hit more of the patterns we want, rewrite "double casts"
        // as two stage casts. This also avoids letting vector casts
        // fall through to LLVM, which will generate large unoptimized
        // shuffles.
        static const vector<pair<Expr, Expr>> cast_rewrites = {
            // Saturating narrowing
            { u8_sat(wild_u32x), u8_sat(u16_sat(wild_u32x)) },
            { u8_sat(wild_i32x), u8_sat(i16_sat(wild_i32x)) },
            { i8_sat(wild_u32x), i8_sat(u16_sat(wild_u32x)) },
            { i8_sat(wild_i32x), i8_sat(i16_sat(wild_i32x)) },

            // Narrowing
            { u8(wild_u32x), u8(u16(wild_u32x)) },
            { u8(wild_i32x), u8(i16(wild_i32x)) },
            { i8(wild_u32x), i8(u16(wild_u32x)) },
            { i8(wild_i32x), i8(i16(wild_i32x)) },

            // Widening
            { u32(wild_u8x), u32(u16(wild_u8x)) },
            { u32(wild_i8x), u32(i16(wild_i8x)) },
            { i32(wild_u8x), i32(u16(wild_u8x)) },
            { i32(wild_i8x), i32(i16(wild_i8x)) },
        };

        if (op->type.is_vector()) {
            Expr cast = op;

            // Truncating multiplies require special care, because the
            // simplifier can cause them to have denominators we do not expect.
            // If the simplifier cancels a factor out of these patterns, we can
            // still use them, but we have to inject the factor back into the
            // expression.
            vector<Expr> matches;
            for (const Pattern &p : trunc_mpy) {
                if (!check_pattern_target(p.flags, target)) {
                    continue;
                }

                if (expr_match(p.pattern, cast, matches)) {
                    int log2_denominator = 0;
                    if (matches.size() == 4) {
                        // Rounding patterns have 4 operands, with the rounding
                        // in the 3rd operand and the denominator in the 4th.
                        if (!is_const_power_of_two_integer(matches[3], &log2_denominator)) {
                            continue;
                        }
                        if (!can_prove(matches[2] * 2 == i64(1) << log2_denominator)) {
                            continue;
                        }
                    } else {
                        // The non-rounding patterns have the denominator in the 3rd operand.
                        if (!is_const_power_of_two_integer(matches[2], &log2_denominator)) {
                            continue;
                        }
                    }
                    // Drop the divisor and the rounding.
                    matches.resize(2);

                    // If the power of 2 is not exactly 2^(bits of the result
                    // type), we need to scale up the operand accordingly.
                    int shift = cast.type().bits() - log2_denominator;
                    if (p.intrin.find("mpy2") != std::string::npos) {
                        // Account for a built-in factor of 2.
                        shift -= 1;
                    }

                    // We need to build the scale into one of the operands,
                    // which must be a constant.
                    if (is_const(matches[0])) {
                        matches[0] = simplify(matches[0] * (1 << shift));
                    } else {
                        // Just assume this is a constant. If it is, this will
                        // work correctly (will simplify only if it doesn't
                        // overflow the narrower type). If not, we will probably
                        // fail to satisfy the match flags, but it also might
                        // work...
                        matches[1] = simplify(matches[1] * (1 << shift));
                    }

                    if (!process_match_flags(matches, p.flags)) {
                        continue;
                    }

                    // Mutate the operands with the given mutator.
                    for (Expr &op : matches) {
                        op = mutate(op);
                    }

                    cast = replace_pattern(cast, matches, p);
                    return cast;
                }
            }

            Expr new_expr = apply_patterns(cast, casts, target, this);
            if (!new_expr.same_as(cast)) {
                return new_expr;
            }

            // If we didn't find a pattern, try using one of the
            // rewrites above.
            for (auto i : cast_rewrites) {
                if (expr_match(i.first, cast, matches)) {
                    debug(3) << "rewriting cast to: " << i.first << " from " << cast << "\n";
                    Expr replacement = with_lanes(i.second, op->type.lanes());
                    Expr expr = substitute("*", matches[0], replacement);
                    return mutate(expr);
                }
            }
        }
        return IRMutator2::visit(op);
    }

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::lerp)) {
            // We need to lower lerps now to optimize the arithmetic
            // that they generate.
            internal_assert(op->args.size() == 3);
            return mutate(lower_lerp(op->args[0], op->args[1], op->args[2]));
        } else if (op->is_intrinsic(Call::cast_mask)) {
            internal_assert(op->args.size() == 1);
            Type src_type = op->args[0].type();
            Type dst_type = op->type;
            if (dst_type.bits() < src_type.bits()) {
                // For narrowing, we can truncate
                return mutate(Cast::make(dst_type, op->args[0]));
            } else {
                // Hexagon masks only use the bottom bit in each byte,
                // so duplicate each lane until we're wide enough.
                Expr e = op->args[0];
                while (src_type.bits() < dst_type.bits()) {
                    e = Shuffle::make_interleave({e, e});
                    src_type = src_type.with_bits(src_type.bits()*2);
                    e = reinterpret(src_type, e);
                }
                return mutate(e);
            }
        } else {
            return IRMutator2::visit(op);
        }
    }

public:
    OptimizePatterns(Target t) {
        target = t;
    }
};

// Attempt to cancel out redundant interleave/deinterleave pairs. The
// basic strategy is to push interleavings toward the end of the
// program, using the fact that interleaves can pass through pointwise
// IR operations. When an interleave collides with a deinterleave,
// they cancel out.
class EliminateInterleaves : public IRMutator2 {
    Scope<bool> vars;


    // We need to know when loads are a multiple of 2 native vectors.
    int native_vector_bits;

    // Alignment analyzer for loads and stores
    HexagonAlignmentAnalyzer alignment_analyzer;

    // We can't interleave booleans, so we handle them specially.
    bool in_bool_to_mask = false;
    bool interleave_mask = false;

    // Check if x is an expression that is either an interleave, or
    // transitively is an interleave.
    bool yields_removable_interleave(Expr x) {
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

        return false;
    }

    // Check if x either has a removable interleave, or it can pretend
    // to be an interleave at no cost (a scalar or a broadcast).
    bool yields_interleave(Expr x) {
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

        return false;
    }

    // Check that at least one of exprs is an interleave that should
    // be removed, and that all of the exprs can yield an interleave.
    bool yields_removable_interleave(const vector<Expr> &exprs) {
        bool any_is_interleave = false;
        for (const Expr &i : exprs) {
            if (yields_removable_interleave(i)) {
                any_is_interleave = true;
            } else if (!yields_interleave(i)) {
                return false;
            }
        }
        return any_is_interleave;
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
                return Let::make(let->name, let->value, remove_interleave(let->body));
            } else {
                return x;
            }
        }

        internal_error << "Expression '" << x << "' does not yield an interleave.\n";
        return x;
    }

    template <typename T>
    Expr visit_binary(const T* op) {
        Expr expr;
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);
        if (yields_removable_interleave({a, b})) {
            a = remove_interleave(a);
            b = remove_interleave(b);
            expr = T::make(a, b);
            if (expr.type().bits() == 1) {
                internal_assert(!interleave_mask);
                interleave_mask = true;
            } else {
                expr = native_interleave(expr);
            }
        } else if (!a.same_as(op->a) || !b.same_as(op->b)) {
            expr = T::make(a, b);
        } else {
            expr = op;
        }
        return expr;
    }

    Expr visit(const Add *op) override { return visit_binary(op); }
    Expr visit(const Sub *op) override { return visit_binary(op); }
    Expr visit(const Mul *op) override { return visit_binary(op); }
    Expr visit(const Div *op) override { return visit_binary(op); }
    Expr visit(const Mod *op) override { return visit_binary(op); }
    Expr visit(const Min *op) override { return visit_binary(op); }
    Expr visit(const Max *op) override { return visit_binary(op); }
    Expr visit(const EQ *op) override { return visit_binary(op); }
    Expr visit(const NE *op) override { return visit_binary(op); }
    Expr visit(const LT *op) override { return visit_binary(op); }
    Expr visit(const LE *op) override { return visit_binary(op); }
    Expr visit(const GT *op) override { return visit_binary(op); }
    Expr visit(const GE *op) override { return visit_binary(op); }

    // These next 3 nodes should not exist if we're vectorized, they
    // should have been replaced with bitwise operations.
    Expr visit(const And *op) override {
        internal_assert(op->type.is_scalar());
        return IRMutator2::visit(op);
    }
    Expr visit(const Or *op) override {
        internal_assert(op->type.is_scalar());
        return IRMutator2::visit(op);
    }
    Expr visit(const Not *op) override {
        internal_assert(op->type.is_scalar());
        return IRMutator2::visit(op);
    }

    Expr visit(const Select *op) override {
        Expr true_value = mutate(op->true_value);
        Expr false_value = mutate(op->false_value);

        internal_assert(op->condition.type().is_scalar());

        Expr cond = mutate(op->condition);

        // The condition isn't a vector, so we can just check if we
        // should move an interleave from the true/false values.
        if (yields_removable_interleave({true_value, false_value})) {
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

    // Make overloads of stmt/expr uses var so we can use it in a template.
    static bool uses_var(Stmt s, const string &var) {
        return stmt_uses_var(s, var);
    }
    static bool uses_var(Expr e, const string &var) {
        return expr_uses_var(e, var);
    }

    template <typename NodeType, typename LetType>
    NodeType visit_let(const LetType *op) {
        // Push alignment info on the stack
        if (op->value.type() == Int(32)) {
            alignment_analyzer.push(op->name, op->value);
        }

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

        // Pop alignment info from the scope stack
        if (op->value.type() == Int(32)) {
            alignment_analyzer.pop(op->name);
        }

        if (value.same_as(op->value) && body.same_as(op->body)) {
            return op;
        } else if (body.same_as(op->body)) {
            // If the body didn't change, we must not have used the deinterleaved value.
            return LetType::make(op->name, value, body);
        } else {
            // We need to rewrap the body with new lets.
            NodeType result = body;
            bool deinterleaved_used = uses_var(result, deinterleaved_name);
            bool interleaved_used = uses_var(result, op->name);
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
                internal_assert(!uses_var(op->body, op->name)) << "EliminateInterleaves eliminated a non-dead let.\n";
                return NodeType();
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

    Stmt visit(const LetStmt *op) override { return visit_let<Stmt>(op); }

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
            return IRMutator2::visit(op);
        }
    }

    static bool is_interleavable(const Call *op) {
        // These calls can have interleaves moved from operands to the
        // result...
        static const set<string> interleavable = {
            Call::bitwise_and,
            Call::bitwise_not,
            Call::bitwise_xor,
            Call::bitwise_or,
            Call::shift_left,
            Call::shift_right,
            Call::abs,
            Call::absd,
            Call::select_mask
        };
        if (interleavable.count(op->name) != 0) return true;

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
            "gather",
            "scatter",
            "scatter_acc",
        };
        if (not_interleavable.count(op->name) != 0) return false;

        if (starts_with(op->name, "halide.hexagon.")) {
            // We assume that any hexagon intrinsic is interleavable
            // as long as all of the vector operands have the same
            // number of lanes and lane width as the return type.
            for (Expr i : op->args) {
                if (i.type().is_scalar()) continue;
                if (i.type().bits() != op->type.bits() || i.type().lanes() != op->type.lanes()) {
                    return false;
                }
            }
        }
        return true;
    }

    Expr visit_bool_to_mask(const Call *op) {
        Expr expr;
        ScopedValue<bool> old_in_bool_to_mask(in_bool_to_mask, true);

        Expr arg = mutate(op->args[0]);
        if (!arg.same_as(op->args[0]) || interleave_mask) {
            expr = Call::make(op->type, Call::bool_to_mask, {arg}, Call::PureIntrinsic);
            if (interleave_mask) {
                expr = native_interleave(expr);
                interleave_mask = false;
            }
        } else {
            expr = op;
        }
        return expr;
    }

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::bool_to_mask)) {
            return visit_bool_to_mask(op);
        }

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
            { "halide.hexagon.pack.vh", "halide.hexagon.trunc.vh" },
            { "halide.hexagon.pack.vw", "halide.hexagon.trunc.vw" },
            { "halide.hexagon.packhi.vh", "halide.hexagon.trunclo.vh" },
            { "halide.hexagon.packhi.vw", "halide.hexagon.trunclo.vw" },
            { "halide.hexagon.pack_satub.vh", "halide.hexagon.trunc_satub.vh" },
            { "halide.hexagon.pack_sath.vw", "halide.hexagon.trunc_sath.vw" },
            { "halide.hexagon.pack_satuh.vw", "halide.hexagon.trunc_satuh.vw" },
        };

        // The reverse mapping of the above.
        static std::map<string, string> interleaving_alts = {
            { "halide.hexagon.trunc.vh", "halide.hexagon.pack.vh" },
            { "halide.hexagon.trunc.vw", "halide.hexagon.pack.vw" },
            { "halide.hexagon.trunclo.vh", "halide.hexagon.packhi.vh" },
            { "halide.hexagon.trunclo.vw", "halide.hexagon.packhi.vw" },
            { "halide.hexagon.trunc_satub.vh", "halide.hexagon.pack_satub.vh" },
            { "halide.hexagon.trunc_sath.vw", "halide.hexagon.pack_sath.vw" },
            { "halide.hexagon.trunc_satuh.vw", "halide.hexagon.pack_satuh.vw" },
        };

        if (is_native_deinterleave(op) && yields_interleave(args[0])) {
            // This is a deinterleave of an interleave! Remove them both.
            return remove_interleave(args[0]);
        } else if (is_interleavable(op) && yields_removable_interleave(args)) {
            // All the arguments yield interleaves (and one of
            // them is an interleave), create a new call with the
            // interleave removed from the arguments.
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
            return Call::make(op->type, interleaving_alts[op->name], { arg }, op->call_type,
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
            if (!is_one(predicate) || !op->value.type().is_vector()) {
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
            int aligned_offset = 0;

            if (!alignment_analyzer.is_aligned(op, &aligned_offset)) {
                aligned_accesses = false;
            }
        }
        if (deinterleave_buffers.contains(op->name)) {
            // We're deinterleaving this buffer, remove the interleave
            // from the store.
            internal_assert(is_one(predicate)) << "The store shouldn't have been predicated.\n";
            value = remove_interleave(value);
        }

        if (predicate.same_as(op->predicate) && value.same_as(op->value) && index.same_as(op->index)) {
            return op;
        } else {
            return Store::make(op->name, value, index, op->param, predicate);
        }
    }

    Expr visit(const Load *op) override {
        if (buffers.contains(op->name)) {
            if ((op->type.lanes()*op->type.bits()) % (native_vector_bits*2) == 0) {
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
                int aligned_offset = 0;

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
        Expr expr = IRMutator2::visit(op);
        if (deinterleave_buffers.contains(op->name)) {
            expr = native_interleave(expr);
        }
        return expr;
    }

    using IRMutator2::visit;

public:
    EliminateInterleaves(int native_vector_bytes, Scope<ModulusRemainder>& alignment_info) :
        native_vector_bits(native_vector_bytes * 8), alignment_analyzer(native_vector_bytes, alignment_info) {}
};

// After eliminating interleaves, there may be some that remain. This
// mutator attempts to replace interleaves paired with other
// operations that do not require an interleave. It's important to do
// this after all other efforts to eliminate the interleaves,
// otherwise this might eat some interleaves that could have cancelled
// with other operations.
class FuseInterleaves : public IRMutator2 {
    Expr visit(const Call *op) override {
        // This is a list of {f, g} pairs that if the first operation
        // is interleaved, interleave(f(x)) is equivalent to g(x).
        static const std::vector<std::pair<string, string>> non_deinterleaving_alts = {
            { "halide.hexagon.zxt.vub", "halide.hexagon.unpack.vub" },
            { "halide.hexagon.sxt.vb", "halide.hexagon.unpack.vb" },
            { "halide.hexagon.zxt.vuh", "halide.hexagon.unpack.vuh" },
            { "halide.hexagon.sxt.vh", "halide.hexagon.unpack.vh" },
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

        return IRMutator2::visit(op);
    }

    using IRMutator2::visit;
};

// Find an upper bound of bounds.max - bounds.min.
Expr span_of_bounds(Interval bounds) {
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
class OptimizeShuffles : public IRMutator2 {
    int lut_alignment;
    Scope<Interval> bounds;
    std::vector<std::pair<string, Expr>> lets;

    using IRMutator2::visit;

    template <typename NodeType, typename T>
    NodeType visit_let(const T *op) {
        // We only care about vector lets.
        if (op->value.type().is_vector()) {
            bounds.push(op->name, bounds_of_expr_in_scope(op->value, bounds));
        }
        NodeType node = IRMutator2::visit(op);
        if (op->value.type().is_vector()) {
            bounds.pop(op->name);
        }
        return node;
    }

    Expr visit(const Let *op) override {
        lets.push_back({op->name, op->value});
        Expr expr = visit_let<Expr>(op);
        lets.pop_back();
        return expr;
    }
    Stmt visit(const LetStmt *op) override { return visit_let<Stmt>(op); }

    Expr visit(const Load *op) override {
        if (!is_one(op->predicate)) {
            // TODO(psuriana): We shouldn't mess with predicated load for now.
            return IRMutator2::visit(op);
        }
        if (!op->type.is_vector() || op->index.as<Ramp>()) {
            // Don't handle scalar or simple vector loads.
            return IRMutator2::visit(op);
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
                ((unaligned_index_bounds.max + align) / align) * align - 1
            };

            for (Interval index_bounds : {aligned_index_bounds, unaligned_index_bounds}) {
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
                                          op->image, op->param, const_true(const_extent));

                    // We know the size of the LUT is not more than 256, so we
                    // can safely cast the index to 8 bit, which
                    // dynamic_shuffle requires.
                    index = simplify(cast(UInt(8).with_lanes(op->type.lanes()), index - base));

                    return Call::make(op->type, "dynamic_shuffle", {lut, index, 0, const_extent - 1}, Call::PureIntrinsic);
                }
            }
        }
        if (!index.same_as(op->index)) {
            return Load::make(op->type, op->name, index, op->image, op->param, op->predicate);
        } else {
            return op;
        }
    }

public:
    OptimizeShuffles(int lut_alignment) : lut_alignment(lut_alignment) {}
};

// Attempt to generate vtmpy instructions. This requires that all lets
// be substituted prior to running, and so must be an IRGraphMutator2.
class VtmpyGenerator : public IRGraphMutator2 {
private:
    using IRMutator2::visit;
    typedef pair<Expr, size_t> LoadIndex;

    // Check if vectors a and b point to the same buffer with the base of a
    // shifted by diff i.e. base(a) = base(b) + diff.
    bool is_base_shifted(const Expr &a, const Expr &b, int diff) {
        Expr maybe_load_a = calc_load(a);
        Expr maybe_load_b = calc_load(b);

        if (maybe_load_a.defined() && maybe_load_b.defined()) {
            const Load* load_a = maybe_load_a.as<Load>();
            const Load* load_b = maybe_load_b.as<Load>();
            if (load_a->name == load_b->name) {
                Expr base_diff = simplify(load_a->index - load_b->index - diff);
                if (is_const(base_diff, 0)) {
                    return true;
                }
            }
        }
        return false;
    }

    // Return the load expression of first vector if all vector in exprs are
    // contiguous vectors pointing to the same buffer.
    Expr are_contiguous_vectors(const vector<Expr> exprs) {
        if (exprs.empty()) {
            return Expr();
        }
        // If the shuffle simplifies then the vectors are contiguous.
        // If not, check if the bases of adjacent vectors differ by
        // vector size.
        Expr concat = simplify(Shuffle::make_concat(exprs));
        const Shuffle *maybe_shuffle = concat.as<Shuffle>();
        if(!maybe_shuffle || !maybe_shuffle->is_concat()) {
            return calc_load(exprs[0]);
        }
        return Expr();
    }

    // Returns the load indicating vector start index. If the vector is sliced
    // return load with shifted ramp by slice_begin expr.
    Expr calc_load(const Expr &e) {
        if (const Cast *maybe_cast = e.as<Cast>()) {
            return calc_load(maybe_cast->value);
        }
        if (const Shuffle *maybe_shuffle = e.as<Shuffle>()) {
            if (maybe_shuffle->is_slice() && maybe_shuffle->slice_stride() == 1) {
                Expr maybe_load = calc_load(maybe_shuffle->vectors[0]);
                if (!maybe_load.defined()) {
                    return Expr();
                }
                const Load *res = maybe_load.as<Load>();
                Expr shifted_load = Load::make(res->type, res->name, res->index + maybe_shuffle->slice_begin(),
                                                res->image, res->param, res->predicate);
                return shifted_load;
            } else if (maybe_shuffle->is_concat()) {
                return are_contiguous_vectors(maybe_shuffle->vectors);
            }
        }
        if (const Load *maybe_load = e.as<Load>()) {
            const Ramp *maybe_ramp = maybe_load->index.as<Ramp>();
            if (maybe_ramp && is_const(maybe_ramp->stride, 1)) {
                return maybe_load;
            }
        }
        return Expr();
    }

    // Loads comparator for sorting Load Expr of the same buffer.
    static bool loads_comparator(LoadIndex a, LoadIndex b) {
        if (a.first.defined() && b.first.defined()) {
            const Load* load_a = a.first.as<Load>();
            const Load* load_b = b.first.as<Load>();
            if (load_a->name == load_b->name) {
                Expr base_diff = simplify(load_b->index - load_a->index);
                if (is_positive_const(base_diff)) {
                    return true;
                }
            } else {
                return load_a->name < load_b->name;
            }
        }
        return false;
    }

    // Vtmpy helps in sliding window ops of the form a*v0 + b*v1 + v2.
    // Conditions required:
    //      v0, v1 and v2 start indices differ by vector stride
    // Current supported value of stride is 1.
    // TODO: Add support for any stride.
    Expr visit(const Add *op) override {
        // Find opportunities vtmpy
        if (op && op->type.is_vector() && (op->type.bits() == 16 || op->type.bits() == 32)) {
            int lanes = op->type.lanes();
            vector<MulExpr> mpys;
            Expr rest;
            string vtmpy_suffix;

            // Finding more than 100 such expresssions is rare.
            // Setting it to 100 makes sure we dont miss anything
            // in most cases and also dont spend unreasonable time while
            // just looking for vtmpy patterns.
            const int max_mpy_ops = 100;
            if (op->type.bits() == 16) {
                find_mpy_ops(op, UInt(8, lanes), Int(8), max_mpy_ops, mpys, rest);
                vtmpy_suffix = ".vub.vub.b.b";
                if (mpys.size() < 3) {
                    mpys.clear();
                    rest = Expr();
                    find_mpy_ops(op, Int(8, lanes), Int(8), max_mpy_ops, mpys, rest);
                    vtmpy_suffix = ".vb.vb.b.b";
                }
            } else if (op->type.bits() == 32) {
                find_mpy_ops(op, Int(16, lanes), Int(8), max_mpy_ops, mpys, rest);
                vtmpy_suffix = ".vh.vh.b.b";
            }

            if (mpys.size() >= 3) {
                const size_t mpy_size = mpys.size();
                // Used to put loads with different buffers in different buckets.
                std::unordered_map<string, vector<LoadIndex> > loads;
                // To keep track of indices selected for vtmpy.
                std::unordered_map<size_t, bool> vtmpy_indices;
                vector<Expr> vtmpy_exprs;
                Expr new_expr;

                for(size_t i = 0; i < mpy_size; i++) {
                    Expr curr_load = calc_load(mpys[i].first);
                    if (curr_load.defined()) {
                        loads[curr_load.as<Load>()->name].emplace_back(curr_load, i);
                    } else {
                        new_expr = new_expr.defined() ? new_expr + curr_load : curr_load;
                    }
                }

                for (auto iter = loads.begin(); iter != loads.end(); iter++) {
                    // Sort the bucket and compare bases of 3 adjacent vectors
                    // at a time. If they differ by vector stride, we've
                    // found a vtmpy
                    std::sort(iter->second.begin(), iter->second.end(), loads_comparator);
                    size_t vec_size = iter->second.size();
                    for(size_t i = 0; i + 2 < vec_size; i++) {
                        Expr v0 = iter->second[i].first;
                        Expr v1 = iter->second[i+1].first;
                        Expr v2 = iter->second[i+2].first;
                        size_t v0_idx = iter->second[i].second;
                        size_t v1_idx = iter->second[i+1].second;
                        size_t v2_idx = iter->second[i+2].second;
                        if (is_const(mpys[v2_idx].second, 1) &&
                            is_base_shifted(v2, v1, 1) &&
                            is_base_shifted(v1, v0, 1)) {

                            vtmpy_indices[v0_idx] = true;
                            vtmpy_indices[v1_idx] = true;
                            vtmpy_indices[v2_idx] = true;

                            vtmpy_exprs.emplace_back(native_interleave(Call::make(op->type,
                                "halide.hexagon.vtmpy" + vtmpy_suffix,
                                { mpys[v0_idx].first, mpys[v2_idx].first,
                                  mpys[v0_idx].second, mpys[v1_idx].second },
                                Call::PureExtern)));
                            // As we cannot test the same indices again
                            i = i+2;
                        }
                    }
                }
                // If we found any vtmpy's then recombine Expr using
                // vtmpy_expr, non_vtmpy_exprs and rest.
                if (vtmpy_exprs.size() > 0) {
                    for (size_t i = 0; i < mpy_size; i++) {
                        if (vtmpy_indices[i]) {
                            continue;
                        }
                        Expr mpy_a = lossless_cast(op->type, mpys[i].first);
                        Expr mpy_b = lossless_cast(op->type, mpys[i].second);
                        Expr mpy_res = mpy_a * mpy_b;
                        new_expr = new_expr.defined() ? new_expr + mpy_res : mpy_res;
                    }
                    for (size_t i = 0; i < vtmpy_exprs.size(); i++) {
                        new_expr = new_expr.defined() ? new_expr + vtmpy_exprs[i] : vtmpy_exprs[i];
                    }
                    if (rest.defined()) {
                        new_expr = new_expr + rest;
                    }
                    return mutate(new_expr);
                }
            }
        }
        return IRMutator2::visit(op);
    }
};

// Convert some expressions to an equivalent form which could get better
// optimized in later stages for hexagon
class RearrangeExpressions : public IRMutator2 {
private:
    using IRMutator2::visit;

    Expr visit(const Mul *op) override {
        if (!op->type.is_vector()) {
            // Only do this for vectors (where we have vmpa).
            return IRMutator2::visit(op);
        }

        if (op->a.as<Broadcast>() && !op->b.as<Broadcast>()) {
            // Ensures broadcasts always occurs as op1 not op0
            return mutate(op->b * op->a);
        }

        if (op->b.as<Broadcast>() && op->a.type().is_int() &&
            (op->a.type().bits() == 16 || op->a.type().bits() == 32)) {
            // Distributing broadcasts helps creating more vmpa
            // because of more adds of muls. Since muls are
            // generally widening ops we need not check if op->a
            // is a sum of widening casts.
            if (const Add *add = op->a.as<Add>()) {
                // simplify() ensures that if add->a is also
                // a scalar multiplications, then we combine the two
                // broadcasts produced in add->a * op->b. For eg:
                // add->a = bc * i16, then simplify combines
                // bc * op->b into a single expression.
                // Since the simplifier is used on the individual operands
                // after distributing the broadcast, the mutation does not
                // compete with the simplifier [the next mutation always occurs
                // on the simplified operands]. For eg:
                // Consider initial expression:
                //      ((v0 * bc(x) + v1 * bc(y)) + v2) * bc(z)
                // Mutation sequence:
                // Step 1:
                //      mutate((v0 * bc(x) + v1 * bc(y)) * bc(z) + v2 * bc(z))
                // Step 2:
                //      mutate((v0 * bc(x) + v1 * bc(y)) * bc(z)) + mutate(v2 * bc(z))
                // Step 3 [Result]:
                //      ((v0 * bc(x * z) + v1 * bc(y *z)) + v2 * bc(z))
                return mutate(simplify(add->a * op->b) + simplify(add->b * op->b));
            } else if (const Sub *sub = op->a.as<Sub>()) {
                return mutate(simplify(sub->a * op->b) - simplify(sub->b * op->b));
            }
        }
        return IRMutator2::visit(op);
    }
};

// Try generating vgathers instead of shuffles.
// At present, we request VTCM memory with single page allocation flag for all
// store_in allocations. So it's always safe to generate a vgather.
// Expressions which generate vgathers are of the form:
//     out(x) = lut(foo(x))
// For vgathers out and lut should be in VTCM in a single page.
class ScatterGatherGenerator : public IRMutator2 {
    Scope<Interval> bounds;
    std::unordered_map<string, const Allocate *> allocations;

    using IRMutator2::visit;

    template <typename NodeType, typename T>
    NodeType visit_let(const T *op) {
        // We only care about vector lets.
        if (op->value.type().is_vector()) {
            bounds.push(op->name, bounds_of_expr_in_scope(op->value, bounds));
        }
        NodeType node = IRMutator2::visit(op);
        if (op->value.type().is_vector()) {
            bounds.pop(op->name);
        }
        return node;
    }

    Expr visit(const Let *op) override { return visit_let<Expr>(op); }

    Stmt visit(const LetStmt *op) override { return visit_let<Stmt>(op); }

    Stmt visit(const Allocate *op) override {
        // Create a map of the allocation
        allocations[op->name] = op;
        return IRMutator2::visit(op);
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
        if (op->index.as<Ramp>() || !is_one(op->predicate) || !ty.is_vector() ||
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
        for (size_t i = 0; i < alloc->extents.size(); i++) {
            size *= alloc->extents[i];
        }
        Expr src = Variable::make(Handle(), op->name);
        Expr new_index = mutate(cast(ty.with_code(Type::Int), index));
        dst_index = mutate(dst_index);

        return Call::make(ty, "gather", {dst_base, dst_index, src, size-1, new_index},
                          Call::Intrinsic);
    }

    // Checks if the Store node can be replaced with a scatter_accumulate.
    // If yes, return new_value to be used for scatter-accumulate, else return
    // the input parameter value.
    Expr is_scatter_acc(const Store *op) {
        Expr lhs = Load::make(op->value.type(), op->name, op->index, Buffer<>(),
                              Parameter(), const_true(op->value.type().lanes()));
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
        if (!is_one(op->predicate) || !ty.is_vector() || ty.bits() == 8) {
            return IRMutator2::visit(op);
        }
        // To use vgathers, the destination address must be VTCM memory.
        const Allocate *alloc = allocations[op->name];
        if (!alloc || alloc->memory_type != MemoryType::VTCM) {
            return IRMutator2::visit(op);
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
            return IRMutator2::visit(op);
        }
        // Calculate the size of the buffer in bytes.
        Expr size = ty.bytes();
        for (size_t i = 0; i < alloc->extents.size(); i++) {
            size *= alloc->extents[i];
        }
        // Check for scatter-acc.
        Expr value = is_scatter_acc(op);
        string intrinsic = "scatter";
        if (!value.same_as(op->value)) {
            // It's a scatter-accumulate
            intrinsic = "scatter_acc";
        }
        Expr buffer = Variable::make(Handle(), op->name);
        Expr index = mutate(cast(ty.with_code(Type::Int), ty.bytes() * op->index));
        value = mutate(value);
        Stmt scatter = Evaluate::make(Call::make(ty, intrinsic,
                              {buffer, size-1, index, value}, Call::Intrinsic));
        return scatter;
    }
};

// Scatter-Gather instructions on Hexagon are asynchronous and hence require a
// scatter-release store followed by a vector load from the same address. This
// stalls the pipeline untill all previous scatter-gather operations have
// finished. The operations are not ordered with respect to load and store
// operations as well.
class SyncronizationBarriers : public IRMutator2 {
    // Keep track of all scatter-gather operations in flight which could cause
    // a hazard in the future.
    std::map<string, vector<const Stmt *>> in_flight;
    // Trail of For Blocks to reach a stmt.
    vector<const Stmt *> curr_path;
    // Current Stmt being mutated.
    const Stmt *curr = NULL;
    // Track where the Stmt generated a scatter-release.
    std::map<const Stmt *, Expr> sync;

    using IRMutator2::visit;

    Expr visit(const Call *op) override {
        if (op->name == "scatter" || op->name == "scatter_acc" || op->name == "gather") {
            string name = op->args[0].as<Variable>()->name;
            // Check if the scatter-gather encountered conflicts with any
            // previous operation. If yes, insert a scatter-release.
            check_hazard(name);
            in_flight[name] = curr_path;
        }
        return IRMutator2::visit(op);
    }

    Stmt visit(const For *op) override {
        // Keep trail of the For blocks encoutered.
        curr_path.push_back(curr);
        Stmt s = IRMutator2::visit(op);
        curr_path.pop_back();
        return s;
    }

    // Creates entry in sync map for the stmt requiring a
    // scatter-release instruction before it.
    void check_hazard(string name) {
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
        return IRMutator2::visit(op);
    }

    Stmt visit(const Store *op) override {
        // Resolve scatter-store and gather-store hazards.
        check_hazard(op->name);
        return IRMutator2::visit(op);
    }

public:
    using IRMutator2::mutate;

    Stmt mutate(const Stmt &s) override {
        curr = &s;
        Stmt new_s = IRMutator2::mutate(s);
        // Wrap the stmt with scatter-release if any hazard was detected.
        if (sync.find(&s) != sync.end()) {
            Stmt scatter_sync = Evaluate::make(Call::make(Int(32), "scatter_release",
                                               {sync[&s]}, Call::Intrinsic));
            return Block::make(scatter_sync, new_s);
        }
        return new_s;
    }
};

}  // namespace

Stmt optimize_hexagon_shuffles(Stmt s, int lut_alignment) {
    // Replace indirect and other complicated loads with
    // dynamic_shuffle (vlut) calls.
    return OptimizeShuffles(lut_alignment).mutate(s);
}

Stmt vtmpy_generator(Stmt s) {
    // Generate vtmpy instruction if possible
    s = substitute_in_all_lets(s);
    s = VtmpyGenerator().mutate(s);
    s = common_subexpression_elimination(s);
    return s;
}

Stmt scatter_gather_generator(Stmt s) {
    // Generate vscatter-vgather instruction if target >= v65
    s = ScatterGatherGenerator().mutate(s);
    s = SyncronizationBarriers().mutate(s);
    return s;
}

Stmt optimize_hexagon_instructions(Stmt s, Target t, Scope<ModulusRemainder> &alignment_info) {
    // Convert some expressions to an equivalent form which get better
    // optimized in later stages for hexagon
    s = RearrangeExpressions().mutate(s);

    // Peephole optimize for Hexagon instructions. These can generate
    // interleaves and deinterleaves alongside the HVX intrinsics.
    s = OptimizePatterns(t).mutate(s);

    // Try to eliminate any redundant interleave/deinterleave pairs.
    s = EliminateInterleaves(t.natural_vector_size(Int(8)), alignment_info).mutate(s);

    // There may be interleaves left over that we can fuse with other
    // operations.
    s = FuseInterleaves().mutate(s);

    return s;
}

}  // namespace Internal
}  // namespace Halide
