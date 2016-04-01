#include "HexagonOptimize.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRMatch.h"
#include "ExprUsesVar.h"
#include "Scope.h"
#include "Target.h"

namespace Halide {
namespace Internal {

using std::set;
using std::vector;
using std::string;

Expr deinterleave(Expr x, const Target &target) {
    int lanes = target.natural_vector_size(x.type());
    return Call::make(x.type(), "hexagon_deinterleave",
                      {x, lanes}, Call::PureExtern);
}

Expr interleave(Expr x, const Target &target) {
    int lanes = target.natural_vector_size(x.type());
    return Call::make(x.type(), "hexagon_interleave",
                      {x, lanes}, Call::PureExtern);
}

bool is_interleave_op(Expr x, const char *name, const Target &target) {
    const Call *c = x.as<Call>();
    if (!c || c->args.size() != 2) return false;
    if (c->name != name) return false;
    const int64_t *lanes = as_const_int(c->args[1]);
    if (!lanes) return false;
    return *lanes == target.natural_vector_size(x.type());
}

bool is_interleave(Expr x, const Target &target) {
    return is_interleave_op(x, "hexagon_interleave", target);
}

bool is_deinterleave(Expr x, const Target &target) {
    return is_interleave_op(x, "hexagon_deinterleave", target);
}

namespace {

Expr u8(Expr E) { return cast(UInt(8, E.type().lanes()), E); }
Expr i8(Expr E) { return cast(Int(8, E.type().lanes()), E); }
Expr u16(Expr E) { return cast(UInt(16, E.type().lanes()), E); }
Expr i16(Expr E) { return cast(Int(16, E.type().lanes()), E); }
Expr u32(Expr E) { return cast(UInt(32, E.type().lanes()), E); }
Expr i32(Expr E) { return cast(Int(32, E.type().lanes()), E); }
Expr u64(Expr E) { return cast(UInt(64, E.type().lanes()), E); }
Expr i64(Expr E) { return cast(Int(64, E.type().lanes()), E); }
Expr bc(Expr E) { return Broadcast::make(E, 0); }

Expr min_i8 = i8(Int(8).min());
Expr max_i8 = i8(Int(8).max());
Expr min_u8 = u8(UInt(8).min());
Expr max_u8 = u8(UInt(8).max());
Expr min_i16 = i16(Int(16).min());
Expr max_i16 = i16(Int(16).max());
Expr min_u16 = u16(UInt(16).min());
Expr max_u16 = u16(UInt(16).max());
Expr min_i32 = i32(Int(32).min());
Expr max_i32 = i32(Int(32).max());
Expr min_u32 = u32(UInt(32).min());
Expr max_u32 = u32(UInt(32).max());

// The simplifier eliminates max(x, 0) for unsigned x, so make sure
// our patterns reflect the same.
Expr simplified_clamp(Expr x, Expr min, Expr max) {
    if (x.type().is_uint() && is_zero(min)) {
        return Halide::min(x, max);
    } else {
        return clamp(x, min, max);
    }
}

Expr i32c(Expr e) { return i32(simplified_clamp(e, min_i32, max_i32)); }
Expr u32c(Expr e) { return u32(simplified_clamp(e, min_u32, max_u32)); }
Expr i16c(Expr e) { return i16(simplified_clamp(e, min_i16, max_i16)); }
Expr u16c(Expr e) { return u16(simplified_clamp(e, min_u16, max_u16)); }
Expr i8c(Expr e) { return i8(simplified_clamp(e, min_i8, max_i8)); }
Expr u8c(Expr e) { return u8(simplified_clamp(e, min_u8, max_u8)); }

struct Pattern {
    enum Flags {
        DeinterleaveOperands = 1 << 0,  ///< Prior to evaluating the pattern, deinterleave native vectors of the operands.
        InterleaveResult = 1 << 1,  ///< After evaluating the pattern, interleave native vectors of the result.
        SwapOperands = 1 << 2,  ///< Swap operands prior to substitution.
        LosslessCastOp0 = 1 << 3, // Proceed only if lossless_cast on the first operand succeeds with the type of the first operand.
        LosslessCastOp1 = 1 << 4,  // Proceed only if lossless_cast on second operand succeeds with the type of the first operand.
    };
    string intrin;        ///< Name of the intrinsic
    Expr pattern;         ///< The pattern to match against
    int flags;

    Pattern() {}
    Pattern(const string &intrin, Expr p, int flags = 0)
        : intrin(intrin), pattern(p), flags(flags) {}
};

Expr wild_u8 = Variable::make(UInt(8), "*");
Expr wild_u16 = Variable::make(UInt(16), "*");
Expr wild_u32 = Variable::make(UInt(32), "*");
Expr wild_i8 = Variable::make(Int(8), "*");
Expr wild_i16 = Variable::make(Int(16), "*");
Expr wild_i32 = Variable::make(Int(32), "*");

Expr wild_u8x = Variable::make(Type(Type::UInt, 8, 0), "*");
Expr wild_u16x = Variable::make(Type(Type::UInt, 16, 0), "*");
Expr wild_u32x = Variable::make(Type(Type::UInt, 32, 0), "*");
Expr wild_i8x = Variable::make(Type(Type::Int, 8, 0), "*");
Expr wild_i16x = Variable::make(Type(Type::Int, 16, 0), "*");
Expr wild_i32x = Variable::make(Type(Type::Int, 32, 0), "*");

std::vector<Pattern> casts = {
    // Saturating add/subtract
    { "halide.hexagon.addsat.vub.vub", u8c(u16(wild_u8x) + u16(wild_u8x)) },
    { "halide.hexagon.addsat.vuh.vuh", u16c(u32(wild_u16x) + u32(wild_u16x)) },
    { "halide.hexagon.addsat.vh.vh", i16c(i32(wild_i16x) + i32(wild_i16x)) },
    { "halide.hexagon.addsat.vw.vw", i32c(i64(wild_i32x) + i64(wild_i32x)) },

    { "halide.hexagon.subsat.vub.vub", u8c(i16(wild_u8x) - i16(wild_u8x)) },
    { "halide.hexagon.subsat.vuh.vuh", u16c(i32(wild_u16x) - i32(wild_u16x)) },
    { "halide.hexagon.subsat.vh.vh", i16c(i32(wild_i16x) - i32(wild_i16x)) },
    { "halide.hexagon.subsat.vw.vw", i32c(i64(wild_i32x) - i64(wild_i32x)) },

    // Averaging
    { "halide.hexagon.avg.vub.vub", u8((u16(wild_u8x) + u16(wild_u8x))/2) },
    { "halide.hexagon.avg.vuh.vuh", u16((u32(wild_u16x) + u32(wild_u16x))/2) },
    { "halide.hexagon.avg.vh.vh", i16((i32(wild_i16x) + i32(wild_i16x))/2) },
    { "halide.hexagon.avg.vw.vw", i32((i64(wild_i32x) + i64(wild_i32x))/2) },

    { "halide.hexagon.avgrnd.vub.vub", u8((u16(wild_u8x) + u16(wild_u8x) + 1)/2) },
    { "halide.hexagon.avgrnd.vuh.vuh", u16((u32(wild_u16x) + u32(wild_u16x) + 1)/2) },
    { "halide.hexagon.avgrnd.vh.vh", i16((i32(wild_i16x) + i32(wild_i16x) + 1)/2) },
    { "halide.hexagon.avgrnd.vw.vw", i32((i64(wild_i32x) + i64(wild_i32x) + 1)/2) },

    { "halide.hexagon.navg.vub.vub", u8c((i16(wild_u8x) - i16(wild_u8x))/2) },
    { "halide.hexagon.navg.vh.vh", i16c((i32(wild_i16x) - i32(wild_i16x))/2) },
    { "halide.hexagon.navg.vw.vw", i32c((i64(wild_i32x) - i64(wild_i32x))/2) },
    // vnavg.uw doesn't exist.

    // Widening casts
    { "halide.hexagon.zxt.vub", u16(wild_u8x), Pattern::InterleaveResult },
    { "halide.hexagon.zxt.vuh", u32(wild_u16x), Pattern::InterleaveResult },
    { "halide.hexagon.sxt.vb", i16(wild_i8x), Pattern::InterleaveResult },
    { "halide.hexagon.sxt.vh", i32(wild_i16x), Pattern::InterleaveResult },

    // Saturating narrowing casts
    { "halide.hexagon.sat.vh", u8c(wild_i16x), Pattern::DeinterleaveOperands },
    { "halide.hexagon.sat.vuh", u8c(wild_u16x), Pattern::DeinterleaveOperands },
    { "halide.hexagon.sat.vuw", u16c(wild_u32x), Pattern::DeinterleaveOperands },
    { "halide.hexagon.sat.vw", i16c(wild_i32x), Pattern::DeinterleaveOperands },

    // Note the absence of deinterleaving the operands. This is a
    // problem because we can't simplify away the interleaving
    // resulting from widening if this is the later narrowing op. But,
    // we don't have vsat variants for all of the types we need.
    { "halide.hexagon.trunchi.sat.vuh", u8c(wild_i16x) },
    { "halide.hexagon.trunchi.sat.vuw", u16c(wild_i32x) },
    { "halide.hexagon.trunchi.sat.vh", i8c(wild_i16x) },

    // Narrowing casts
    { "halide.hexagon.trunclo.vh", u8(wild_u16x/256), Pattern::DeinterleaveOperands },
    { "halide.hexagon.trunclo.vw", u16(wild_u32x/65536), Pattern::DeinterleaveOperands },
    { "halide.hexagon.trunclo.vh", i8(wild_i16x/256), Pattern::DeinterleaveOperands },
    { "halide.hexagon.trunclo.vw", i16(wild_i32x/65536), Pattern::DeinterleaveOperands },
    { "halide.hexagon.trunchi.vh", u8(wild_u16x), Pattern::DeinterleaveOperands },
    { "halide.hexagon.trunchi.vw", u16(wild_u32x), Pattern::DeinterleaveOperands },
    { "halide.hexagon.trunchi.vh", i8(wild_i16x), Pattern::DeinterleaveOperands },
    { "halide.hexagon.trunchi.vw", i16(wild_i32x), Pattern::DeinterleaveOperands },
};

std::vector<Pattern> muls = {
    // Widening multiplication
    { "halide.hexagon.mpy.vub.vub", u16(wild_u8x)*u16(wild_u8x), Pattern::InterleaveResult },
    { "halide.hexagon.mpy.vuh.vuh", u32(wild_u16x)*u32(wild_u16x), Pattern::InterleaveResult },
    { "halide.hexagon.mpy.vb.vb", i16(wild_i8x)*i16(wild_i8x), Pattern::InterleaveResult },
    { "halide.hexagon.mpy.vh.vh", i32(wild_i16x)*i32(wild_i16x), Pattern::InterleaveResult },

    { "halide.hexagon.mpy.vub.vb", i16(wild_u8x)*i16(wild_i8x), Pattern::InterleaveResult },
    { "halide.hexagon.mpy.vh.vuh", i32(wild_i16x)*i32(wild_u16x), Pattern::InterleaveResult },
    { "halide.hexagon.mpy.vub.vb", i16(wild_i8x)*i16(wild_u8x), Pattern::InterleaveResult | Pattern::SwapOperands },
    { "halide.hexagon.mpy.vh.vuh", i32(wild_u16x)*i32(wild_i16x), Pattern::InterleaveResult | Pattern::SwapOperands },

    // Vector by scalar widening multiplies.
    { "halide.hexagon.mpy.vub.ub", u16(wild_u8x)*bc(wild_u16), Pattern::InterleaveResult | Pattern::LosslessCastOp1 },
    { "halide.hexagon.mpy.vub.b", i16(wild_u8x)*bc(wild_i16), Pattern::InterleaveResult | Pattern::LosslessCastOp1 },
    { "halide.hexagon.mpy.vuh.uh", u32(wild_u16x)*bc(wild_u32), Pattern::InterleaveResult | Pattern::LosslessCastOp1 },
    { "halide.hexagon.mpy.vh.h",  i32(wild_i16x)*bc(wild_i32), Pattern::InterleaveResult | Pattern::LosslessCastOp1 },
    // Commute the four above.
    { "halide.hexagon.mpy.vub.ub", bc(wild_u16)*u16(wild_u8x), Pattern::InterleaveResult | Pattern::LosslessCastOp0 | Pattern::SwapOperands },
    { "halide.hexagon.mpy.vub.b", bc(wild_i16)*i16(wild_u8x), Pattern::InterleaveResult | Pattern::LosslessCastOp0 | Pattern::SwapOperands },
    { "halide.hexagon.mpy.vuh.uh", bc(wild_u32)*u32(wild_u16x), Pattern::InterleaveResult | Pattern::LosslessCastOp0 | Pattern::SwapOperands },
    { "halide.hexagon.mpy.vh.h",  bc(wild_i32)*i32(wild_i16x), Pattern::InterleaveResult | Pattern::LosslessCastOp0 | Pattern::SwapOperands },
};

Expr apply_patterns(Expr x, const std::vector<Pattern> &patterns, IRMutator *op_mutator,
                    const Target &target, bool vectors_only = true) {
    if (!vectors_only || x.type().is_vector()) {
        std::vector<Expr> matches;
        for (const Pattern &p : patterns) {
            if (expr_match(p.pattern, x, matches)) {
                // Mutate the operands with the given mutator.
                for (Expr &op : matches) {
                    op = op_mutator->mutate(op);
                }
                if (p.flags & Pattern::DeinterleaveOperands) {
                    // The pattern wants us to deinterleave the operands.
                    for (Expr &op : matches) {
                        // Only deinterleave vector operands.
                        if (op.type().is_vector()) {
                            op = deinterleave(op, target);
                        }
                    }
                }
                if ((p.flags & Pattern::LosslessCastOp1) ||
                    (p.flags & Pattern::LosslessCastOp0)) {
                    internal_assert(matches.size() == 2);
                    // LosslessCastOpx is a way of knowing that operand x is a broadcast.
                    // Therefore, both *_should_not_* be set in the pattern flags because
                    // that'll mean we are operating on two broadcasts.
                    int op_num = (p.flags & Pattern::LosslessCastOp0) ? 0 : 1;
                    Type t = matches[op_num].type();
                    Expr b = lossless_cast(t.with_bits(t.bits()/2).with_lanes(1), matches[op_num]);
                    if (!b.defined())  continue;
                    else matches[op_num] = b;
                }
                if (p.flags & Pattern::SwapOperands) {
                    std::swap(matches[0], matches[1]);
                }
                x = Call::make(x.type(), p.intrin, matches, Call::PureExtern);
                if (p.flags & Pattern::InterleaveResult) {
                    // The pattern wants us to interleave the result.
                    x = interleave(x, target);
                }
                return x;
            }
        }
    }
    return x;
}

// Perform peephole optimizations on the IR, adding appropriate
// interleave and deinterleave calls.
class OptimizePatterns : public IRMutator {
private:
    Target target;

    using IRMutator::visit;

    void visit(const Mul *op) {
        expr = apply_patterns(op, muls, this, target);
        if (expr.same_as(op)) IRMutator::visit(op);
    }

    void visit(const Cast *op) {
        expr = apply_patterns(op, casts, this, target);
        if (expr.same_as(op)) IRMutator::visit(op);
    }

public:
    OptimizePatterns(const Target &target) : target(target) {}
};

// Attempt to cancel out redundant interleave/deinterleave pairs. The
// basic strategy is to push interleavings toward the end of the
// program, using the fact that interleaves can pass through pointwise
// IR operations. When an interleave collides with a deinterleave,
// they cancel out.
class EliminateInterleaves : public IRMutator {
private:
    Target target;

    Scope<bool> vars;

    Expr interleave(Expr x) { return Halide::Internal::interleave(x, target);}
    Expr deinterleave(Expr x) { return Halide::Internal::deinterleave(x, target);}
    bool is_interleave(Expr x) { return Halide::Internal::is_interleave(x, target);}
    bool is_deinterleave(Expr x) { return Halide::Internal::is_deinterleave(x, target);}

    // Check if x is an expression that is either an interleave, or
    // can pretend to be one (is a scalar or a broadcast).
    bool yields_interleave(Expr x) {
        if (is_interleave(x)) {
            return true;
        } else if (x.type().is_scalar() || x.as<Broadcast>()) {
            return true;
        }
        const Variable *var = x.as<Variable>();
        if (var && vars.contains(var->name + ".deinterleaved")) {
            return true;
        }
        return false;
    }

    // Check that at least one of exprs is an interleave, and that all
    // of the exprs can yield an interleave.
    bool yields_removable_interleave(const std::vector<Expr> &exprs) {
        bool any_is_interleave = false;
        for (const Expr &i : exprs) {
            if (is_interleave(i)) {
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
        if (is_interleave(x)) {
            return x.as<Call>()->args[0];
        } else if (x.type().is_scalar() || x.as<Broadcast>()) {
            return x;
        }
        const Variable *var = x.as<Variable>();
        if (var) {
            internal_assert(vars.contains(var->name + ".deinterleaved"));
            return Variable::make(var->type, var->name + ".deinterleaved");
        }
        internal_error << "Expression '" << x << "' does not yield an interleave.\n";
        return x;
    }

    template <typename T>
    void visit_binary(const T* op) {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);
        // We only want to pull out an interleave if at least one of
        // the operands is an actual interleave.
        if (yields_removable_interleave({a, b})) {
            a = remove_interleave(a);
            b = remove_interleave(b);
            expr = interleave(T::make(a, b));
        } else if (!a.same_as(op->a) || !b.same_as(op->b)) {
            expr = T::make(a, b);
        } else {
            expr = op;
        }
    }

    void visit(const Add *op) { visit_binary(op); }
    void visit(const Sub *op) { visit_binary(op); }
    void visit(const Mul *op) { visit_binary(op); }
    void visit(const Div *op) { visit_binary(op); }
    void visit(const Mod *op) { visit_binary(op); }
    void visit(const Min *op) { visit_binary(op); }
    void visit(const Max *op) { visit_binary(op); }
    void visit(const EQ *op) { visit_binary(op); }
    void visit(const NE *op) { visit_binary(op); }
    void visit(const LT *op) { visit_binary(op); }
    void visit(const LE *op) { visit_binary(op); }
    void visit(const GT *op) { visit_binary(op); }
    void visit(const GE *op) { visit_binary(op); }
    void visit(const And *op) { visit_binary(op); }
    void visit(const Or *op) { visit_binary(op); }

    void visit(const Not *op) {
        Expr a = mutate(op->a);
        if (is_interleave(a)) {
            a = remove_interleave(a);
            expr = interleave(Not::make(a));
        } else if (!a.same_as(op->a)) {
            expr = Not::make(a);
        } else {
            expr = op;
        }
    }

    void visit(const Select *op) {
        Expr cond = mutate(op->condition);
        Expr true_value = mutate(op->true_value);
        Expr false_value = mutate(op->false_value);
        if (yields_removable_interleave({cond, true_value, false_value})) {
            cond = remove_interleave(cond);
            true_value = remove_interleave(true_value);
            false_value = remove_interleave(false_value);
            expr = interleave(Select::make(cond, true_value, false_value));
        } else if (!cond.same_as(op->condition) ||
                   !true_value.same_as(op->true_value) ||
                   !false_value.same_as(op->false_value)) {
            expr = Select::make(cond, true_value, false_value);
        } else {
            expr = op;
        }
    }

    // Make overloads of stmt/expr uses var so we can use it in a template.
    static bool uses_var(Stmt s, const std::string &var) {
        return stmt_uses_var(s, var);
    }
    static bool uses_var(Expr e, const std::string &var) {
        return expr_uses_var(e, var);
    }

    template <typename NodeType, typename LetType>
    void visit_let(NodeType &result, const LetType *op) {
        Expr value = mutate(op->value);
        string deinterleaved_name = op->name + ".deinterleaved";
        NodeType body;
        if (is_interleave(value)) {
            // We can provide a deinterleaved version of this let value.
            vars.push(deinterleaved_name, true);
            body = mutate(op->body);
            vars.pop(deinterleaved_name);
        } else {
            body = mutate(op->body);
        }
        if (value.same_as(op->value) && body.same_as(op->body)) {
            result = op;
        } else if (body.same_as(op->body)) {
            // If the body didn't change, we must not have used the deinterleaved value.
            result = LetType::make(op->name, value, body);
        } else {
            // We need to rewrap the body with new lets.
            result = body;
            bool deinterleaved_used = uses_var(result, deinterleaved_name);
            bool interleaved_used = uses_var(result, op->name);
            if (deinterleaved_used && interleaved_used) {
                // The body uses both the interleaved and
                // deinterleaved version of this let. Generate both
                // lets, using the deinterleaved one to generate the
                // interleaved one.
                result = LetType::make(op->name, interleave(deinterleaved_name), result);
                result = LetType::make(deinterleaved_name, remove_interleave(value), result);
            } else if (deinterleaved_used) {
                // Only the deinterleaved value is used, we can eliminate the interleave.
                result = LetType::make(deinterleaved_name, remove_interleave(value), result);
            } else if (interleaved_used) {
                // Only the original value is used, regenerate the let.
                result = LetType::make(op->name, value, result);
            } else {
                // This is probably a bug? We killed the usage of the let?
                internal_error << "EliminateInterleaves eliminated use of '" << op->name << "'\n";
            }
        }
    }

    void visit(const Let *op) { visit_let(expr, op); }
    void visit(const LetStmt *op) { visit_let(stmt, op); }

    void visit(const Cast *op) {
        if (op->type.bits() == op->value.type().bits()) {
            // We can move interleaves through casts of the same size.
            Expr value = mutate(op->value);
            if (is_interleave(value)) {
                value = remove_interleave(value);
                expr = interleave(Cast::make(op->type, value));
            } else if (!value.same_as(op->value)) {
                expr = Cast::make(op->type, value);
            } else {
                expr = op;
            }
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const Call *op) {
        // These calls can have interleaves moved from operands to the
        // result.
        static set<string> interleavable = {
            Call::bitwise_and,
            Call::bitwise_not,
            Call::bitwise_xor,
            Call::bitwise_or,
            Call::shift_left,
            Call::shift_right,
            Call::abs,
            Call::absd,
        };

        if (is_deinterleave(op)) {
            expr = mutate(op->args[0]);
            if (yields_interleave(expr)) {
                // This is a deinterleave of an interleave! Remove them both.
                expr = remove_interleave(expr);
            } else if (!expr.same_as(op->args[0])) {
                expr = deinterleave(expr);
            } else {
                expr = op;
            }
            // TODO: Need to change interleave(deinterleave(x)) ?
        } else if (starts_with(op->name, "halide.hexagon.") ||
                   interleavable.count(op->name)) {
            // This function can move interleaves.
            vector<Expr> args(op->args);

            // mutate all the args.
            bool changed = false;
            for (Expr &i : args) {
                Expr new_i = mutate(i);
                changed = changed || !new_i.same_as(i);
                i = new_i;
            }

            if (yields_removable_interleave(args)) {
                // All the arguments yield interleaves (and one of
                // them is an interleave), create a new call with the
                // interleave removed from the arguments.
                for (Expr &i : args) {
                    i = remove_interleave(i);
                }
                expr = Call::make(op->type, op->name, args, op->call_type,
                                  op->func, op->value_index, op->image, op->param);
                // Add the interleave back to the result of the call.
                expr = interleave(expr);
            } else if (changed) {
                expr = Call::make(op->type, op->name, args, op->call_type,
                                  op->func, op->value_index, op->image, op->param);
            } else {
                expr = op;
            }
        } else {
            // TODO: Treat interleave_vectors(a, b) where a and b are
            // native vectors as an interleave?
            IRMutator::visit(op);
        }
    }

    using IRMutator::visit;

public:
    EliminateInterleaves(const Target &target) : target(target) {}
};

}  // namespace

Stmt optimize_hexagon(Stmt s, const Target &target) {
    // Peephole optimize for Hexagon instructions. These can generate
    // interleaves and deinterleaves alongside the HVX intrinsics.
    s = OptimizePatterns(target).mutate(s);

    // Try to eliminate any redundant interleave/deinterleave pairs.
    s = EliminateInterleaves(target).mutate(s);

    // TODO: If all of the stores to a buffer are interleaved, and all
    // of the loads are immediately deinterleaved, then we can remove
    // all of the interleave/deinterleaves, and just let the storage
    // be deinterleaved.

    return s;
}

}  // namespace Internal
}  // namespace Halide
