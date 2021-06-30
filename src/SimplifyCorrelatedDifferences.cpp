#include "SimplifyCorrelatedDifferences.h"

#include "CSE.h"
#include "CompilerLogger.h"
#include "Error.h"
#include "ExprUsesVar.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Monotonic.h"
#include "RealizationOrder.h"
#include "Scope.h"
#include "Simplify.h"
#include "Solve.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {
namespace {

using std::string;
using std::vector;

class SimplifyCorrelatedDifferences : public IRMutator {
    using IRMutator::visit;

    string loop_var;

    Scope<ConstantInterval> monotonic;

    struct OuterLet {
        string name;
        Expr value;
        bool may_substitute;
    };
    vector<OuterLet> lets;

    template<typename LetStmtOrLet, typename StmtOrExpr>
    StmtOrExpr visit_let(const LetStmtOrLet *op) {
        // Visit an entire chain of lets in a single method to conserve stack space.
        struct Frame {
            const LetStmtOrLet *op;
            ScopedBinding<ConstantInterval> binding;
            Expr new_value;
            Frame(const LetStmtOrLet *op, const string &loop_var, Scope<ConstantInterval> &scope)
                : op(op),
                  binding(scope, op->name, derivative_bounds(op->value, loop_var, scope)) {
            }
            Frame(const LetStmtOrLet *op)
                : op(op) {
            }
        };
        std::vector<Frame> frames;
        StmtOrExpr result;

        // Note that we must add *everything* that depends on the loop
        // var to the monotonic scope and the list of lets, even
        // things which we can never substitute in (e.g. impure
        // things). This is for two reasons. First this pass could be
        // used at a time when we still have nested lets under the
        // same name. If we decide not to add an inner let, but do add
        // the outer one, then later references to it will be
        // incorrect. Second, if we don't add something that happens
        // to be non-monotonic, then derivative_bounds finds a variable
        // that references it in a later let, it will think it's a
        // constant, not an unknown.
        do {
            result = op->body;
            if (loop_var.empty()) {
                frames.emplace_back(op);
                continue;
            }

            bool pure = is_pure(op->value);
            if (!pure || expr_uses_vars(op->value, monotonic) || monotonic.contains(op->name)) {
                frames.emplace_back(op, loop_var, monotonic);
                Expr new_value = mutate(op->value);
                bool may_substitute_in = new_value.type() == Int(32) && pure;
                lets.emplace_back(OuterLet{op->name, new_value, may_substitute_in});
                frames.back().new_value = std::move(new_value);
            } else {
                // Pure and constant w.r.t the loop var. Doesn't
                // shadow any outer thing already in the monotonic
                // scope.
                frames.emplace_back(op);
            }
        } while ((op = result.template as<LetStmtOrLet>()));

        result = mutate(result);

        for (auto it = frames.rbegin(); it != frames.rend(); it++) {
            if (it->new_value.defined()) {
                result = LetStmtOrLet::make(it->op->name, it->new_value, result);
            } else {
                result = LetStmtOrLet::make(it->op->name, it->op->value, result);
            }
            if (it->binding.bound()) {
                lets.pop_back();
            }
        }

        return result;
    }

    Expr visit(const Let *op) override {
        return visit_let<Let, Expr>(op);
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let<LetStmt, Stmt>(op);
    }

    Stmt visit(const For *op) override {
        Stmt s = op;
        // This is unfortunately quadratic in maximum loop nesting depth
        if (loop_var.empty()) {
            decltype(monotonic) tmp_monotonic;
            decltype(lets) tmp_lets;
            tmp_monotonic.swap(monotonic);
            tmp_lets.swap(lets);
            loop_var = op->name;
            {
                ScopedBinding<ConstantInterval> bind(monotonic, loop_var, ConstantInterval::single_point(1));
                s = IRMutator::visit(op);
            }
            loop_var.clear();
            tmp_monotonic.swap(monotonic);
            tmp_lets.swap(lets);
        }
        s = IRMutator::visit(s.as<For>());
        return s;
    }

    class PartiallyCancelDifferences : public IRMutator {
        using IRMutator::visit;

        // Symbols used by rewrite rules
        IRMatcher::Wild<0> x;
        IRMatcher::Wild<1> y;
        IRMatcher::Wild<2> z;
        IRMatcher::WildConst<0> c0;
        IRMatcher::WildConst<1> c1;

        Expr visit(const Sub *op) override {

            Expr a = mutate(op->a), b = mutate(op->b);

            // Partially cancel terms in correlated differences of
            // various kinds to get tighter bounds.  We assume any
            // correlated term has already been pulled leftmost by
            // solve_expression.
            if (op->type == Int(32)) {
                auto rewrite = IRMatcher::rewriter(IRMatcher::sub(a, b), op->type);
                if (
                    // Differences of quasi-affine functions
                    rewrite((x + y) / c0 - (x + z) / c0, ((x % c0) + y) / c0 - ((x % c0) + z) / c0) ||
                    rewrite(x / c0 - (x + z) / c0, 0 - ((x % c0) + z) / c0) ||
                    rewrite((x + y) / c0 - x / c0, ((x % c0) + y) / c0) ||

                    // truncated cones have a constant upper or lower
                    // bound that isn't apparent when expressed in the
                    // form in the LHS below
                    rewrite(min(x, c0) - max(x, c1), min(min(c0 - x, x - c1), fold(min(0, c0 - c1)))) ||
                    rewrite(max(x, c0) - min(x, c1), max(max(c0 - x, x - c1), fold(max(0, c0 - c1)))) ||
                    rewrite(min(x, y) - max(x, z), min(min(x, y) - max(x, z), 0)) ||
                    rewrite(max(x, y) - min(x, z), max(max(x, y) - min(x, z), 0)) ||

                    false) {
                    return rewrite.result;
                }
            }
            return a - b;
        }
    };

    Expr cancel_correlated_subexpression(Expr e, const Expr &a, const Expr &b, bool correlated) {
        auto ma = is_monotonic(a, loop_var, monotonic);
        auto mb = is_monotonic(b, loop_var, monotonic);

        if ((ma == Monotonic::Increasing && mb == Monotonic::Increasing && correlated) ||
            (ma == Monotonic::Decreasing && mb == Monotonic::Decreasing && correlated) ||
            (ma == Monotonic::Increasing && mb == Monotonic::Decreasing && !correlated) ||
            (ma == Monotonic::Decreasing && mb == Monotonic::Increasing && !correlated)) {

            for (auto it = lets.rbegin(); it != lets.rend(); it++) {
                if (expr_uses_var(e, it->name)) {
                    if (!it->may_substitute) {
                        // We have to stop here. Can't continue
                        // because there might be an outer let with
                        // the same name that we *can* substitute in,
                        // and then inner uses will get the wrong
                        // value.
                        break;
                    }
                }
                e = Let::make(it->name, it->value, e);
            }
            e = common_subexpression_elimination(e);
            e = solve_expression(e, loop_var).result;
            e = PartiallyCancelDifferences().mutate(e);
            e = simplify(e);

            const bool check_non_monotonic = debug::debug_level() > 0 || get_compiler_logger() != nullptr;
            if (check_non_monotonic &&
                is_monotonic(e, loop_var) == Monotonic::Unknown) {
                // Might be a missed simplification opportunity. Log to help improve the simplifier.
                if (get_compiler_logger()) {
                    get_compiler_logger()->record_non_monotonic_loop_var(loop_var, e);
                }
                debug(1) << "Warning: expression is non-monotonic in loop variable "
                         << loop_var << ": " << e << "\n";
            }
        }
        return e;
    }

    template<typename T>
    Expr visit_binop(const T *op, bool correlated) {
        Expr e = IRMutator::visit(op);
        op = e.as<T>();
        if (op == nullptr ||
            op->a.type() != Int(32) ||
            loop_var.empty()) {
            return e;
        } else {
            // Bury the logic that doesn't depend on the template
            // parameter in a separate function to save code size and
            // reduce stack frame size in the recursive path.
            return cancel_correlated_subexpression(e, op->a, op->b, correlated);
        }
    }

    // Binary ops where it pays to cancel a correlated term on both
    // sides. E.g. consider the x in:
    //
    // (x*3 + y)*2 - max(x*6, 0)))
    //
    // Both sides increase monotonically with x so interval arithmetic
    // will overestimate the bounds. If we subtract x*6 from both
    // sides we get:
    //
    // y*2 - max(0, x*-6)
    //
    // Now only one side depends on x and interval arithmetic becomes
    // exact.
    Expr visit(const Sub *op) override {
        return visit_binop(op, true);
    }

    Expr visit(const LT *op) override {
        return visit_binop(op, true);
    }

    Expr visit(const LE *op) override {
        return visit_binop(op, true);
    }

    Expr visit(const GT *op) override {
        return visit_binop(op, true);
    }

    Expr visit(const GE *op) override {
        return visit_binop(op, true);
    }

    Expr visit(const EQ *op) override {
        return visit_binop(op, true);
    }

    Expr visit(const NE *op) override {
        return visit_binop(op, true);
    }

    // For add you actually want to cancel any anti-correlated term
    // (e.g. x in (x*3 + y)*2 + max(x*-6, 0))
    Expr visit(const Add *op) override {
        return visit_binop(op, false);
    }
};

class RefactorCorrelatedDifferences : public IRMutator {
    using IRMutator::visit;

    // Symbols used by rewrite rules
    IRMatcher::Wild<0> x;
    IRMatcher::Wild<1> y;
    IRMatcher::Wild<2> z;
    IRMatcher::Wild<3> w;
    // IRMatcher::Wild<4> u;
    // IRMatcher::Wild<5> v;
    IRMatcher::WildConst<0> c0;
    IRMatcher::WildConst<1> c1;
    IRMatcher::WildConst<2> c2;
    IRMatcher::WildConst<3> c3;
    IRMatcher::WildConst<4> c4;
    // IRMatcher::WildConst<5> c5;

    Expr visit(const Sub *op) override {

        Expr a = mutate(op->a), b = mutate(op->b);

        if (op->type == Int(32)) {
            auto rewrite = IRMatcher::rewriter(IRMatcher::sub(a, b), op->type);

            auto synth149 = max(min((((((x - y) - c0)/c1)*c1) + y) + c1, x), z) - min(min(min(x - c1, y), z - c2), 0);
            auto synth149b = (max(min((((((x - y) + c0)/c1)*c1) + y) + c1, x), z) - min(min(x + c3, y), min(z, c2) + c4));
            auto synth149b_cond = (c4 == -c2);
                              // max(min((((((x - y) + -1)/16)*16) + y) + 16, x), z) - min(min(x + -16, y), min(z, 8) + -8)
                            //  (max(min((((((x - y) + -1)/16)*16) + y) + 16, x), z) - min(min(x + -16, y), min(z, 8) + -8))
            auto synth152 = (min((((((y - x) + c0)/c1)*c1) + x) + c1, y) - min(y + c2, x));
                            // (min((((((y - x) + -1)/8)*8) + x) + 8, y) - min(y + -8, x))
            auto synth152_cond = (c2 == -c1);
            auto bound152 = fold(min(-c0, c1) - ((c1*c1) - c0));

            auto synth157 = (min((((((x - y) + c0)/c1)*c1) + y) + c1, x) - min(x + c2, y));
            auto synth157_cond = (((c0 <= 0) && (0 <= c1)) && (c2 <= 0));
            auto bound157 = c0;

            auto synth158 = (max(z + y, x) - min(min(z, c0) + y, min(x, c1) + c1));
            auto synth158_cond = (0 <= min(c0, c1));
            auto bound158 = 0;

            auto synth159 = (((min(y + c0, x) + ((((min((((z + c1)/c2)*c2) + x, y) - min(y + c0, x)) + c3)/c2)*c2)) - min(min((((z + c1)/c2)*c2) + x, y) + c0, x)) + c2);
            auto bound159 = fold(min(c0, c3));
            auto synth159_cond = ((((c0 <= 0) && (0 <= c1)) && (0 <= c2)) && (c3 <= 0));

            auto synth161 = (min(((((x - y)/c0)*c0) + y) + c1, x) - min(x + c2, y));
            auto bound161 = fold(c2 - c0);
            auto synth161_cond = ((0 <= min(c0, c1)) && (c2 <= 0));

            auto synth162 = (max(x + y, z) - min(min(z, c0) + c1, min(x, c2) + y));
            auto bound162 = 0;
            // auto synth162_cond = (0 < min(c0, c1));

            // rewrite((min(c0, x) + min(c1*y, c2)), min(c0 + c2, min(c0, x) + min(c1*y, c2)), (0 < min(min(c0, c1), c2)))
            // min(min(x, 104) + (y*8), min(x, 104) + 16);
            auto synth164 = min(min(x, c0) + y, min(x, c0) + c1);
            auto bound164 = fold(c0 + c1);


            if (
                rewrite(x - (x / c0) * c0, x % c0, c0 != 0) ||
                rewrite(((x + y) - ((x / c0) * c0)), (x % c0) + y, c0 != 0) ||

                rewrite((x + y) - (z + y), x - z) ||
                rewrite(((x + y) - z) - y, x - z) ||

                rewrite(min(x + y, y + z) - (w + y), min(x, z) - w) ||

                // I think there's a much tighter bound if c0 = -1 and c0 >= 0... Is that worth it?
                rewrite((min((((((x - y) - c0)/c1)*c1) + y) + c1, x) - min(x - c1, y)) , max(min((((((x - y) - c0)/c1)*c1) + y) + c1, x) - min(x - c1, y), fold(0 - c0)), c0 >= 0 && c1 >= 0) ||
                rewrite((min((((((x - y) - c0)/c1)*c1) + y) + c1, x) - min(x - c1, y)), max(min((((((x - y) - c0)/c1)*c1) + y) + c1, x) - min(x - c1, y), fold(min(c1 - c0, 0) + ((0 - c1)*c1)))) ||

                rewrite(synth149, max(synth149, c2), c0 >= 0 && c1 >= 0 && c2 >= 0) ||
                rewrite(synth149b, max(synth149b, fold(min(c2, 0))), synth149b_cond) ||

                rewrite(synth152, max(synth152, bound152), synth152_cond) ||
                rewrite(synth157, max(synth157, bound157), synth157_cond) ||
                rewrite(synth158, max(synth158, bound158), synth158_cond) ||
                rewrite(synth159, max(synth159, bound159), synth159_cond) ||
                rewrite(synth161, max(synth161, bound161), synth161_cond) ||
                // rewrite(synth162, max(synth162, bound162), synth162_cond) ||
                rewrite(synth162, max(synth162, bound162)) ||
                // synth 163 + Andrew's help
                rewrite(min(x*c0, c1) - min(x, c2)*c0, min(c1 - min(x, c2)*c0, 0), c0 > 0 && c1 <= c2*c0) ||
                rewrite(synth164, min(synth164, bound164)) ||



                rewrite(IRMatcher::intrin(Call::likely_if_innermost, x) - x, IRMatcher::intrin(Call::likely_if_innermost, 0)) ||

                // truncated cones have a constant upper or lower
                // bound that isn't apparent when expressed in the
                // form in the LHS below
                rewrite(min(x, c0) - max(x, c1), min(min(c0 - x, x - c1), fold(min(0, c0 - c1)))) ||
                rewrite(max(x, c0) - min(x, c1), max(max(c0 - x, x - c1), fold(max(0, c0 - c1)))) ||
                rewrite(min(x, y) - max(x, z), min(min(x, y) - max(x, z), 0)) ||
                rewrite(max(x, y) - min(x, z), max(max(x, y) - min(x, z), 0)) ||

                false) {
                return rewrite.result;
            }
        }
        return a - b;
    }

    Expr visit(const Max *op) override {
        Expr a = mutate(op->a), b = mutate(op->b);

        if (op->type == Int(32)) {
            auto rewrite = IRMatcher::rewriter(IRMatcher::max(a, b), op->type);

            auto quantized_sub = (((x + -1)/c0)*c0) + c0;

            if (
                rewrite(max(x, quantized_sub), quantized_sub, c0 > 0) ||
                rewrite(max(x, quantized_sub), x, c0 < 0) ||

                false) {
                return rewrite.result;
            }
        }
        return max(a, b);
    }

    // Expr visit(const Mul *op) override {
    //     Expr a = mutate(op->a), b = mutate(op->b);

    //     if (op->type == Int(32)) {
    //         auto rewrite = IRMatcher::rewriter(IRMatcher::mul(a, b), op->type);

    //         if (
    //             rewrite((x / c0) * c0, x - (x % c0), c0 != 0) ||

    //             false) {
    //             return rewrite.result;
    //         }
    //     }
    //     return a * b;
    // }

    Expr visit(const Add *op) override {
        Expr a = mutate(op->a), b = mutate(op->b);

        if (op->type == Int(32)) {
            auto rewrite = IRMatcher::rewriter(IRMatcher::add(a, b), op->type);

            if (
                // rewrite(min(x, c0) + c1, min(x + c1, 0), c0 == -c1) ||

                false) {
                return rewrite.result;
            }
        }
        return a + b;
    }

};


class PossiblyCorrelatedChecker : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Variable *op) override {
        if (variables.count(op->name) > 0) {
            possibly_correlated = true;
        } else {
            variables.insert(op->name);
        }
    }

public:
    std::set<std::string> variables;
    bool possibly_correlated = false;
};

class SimplerNameMutator : public IRMutator {
    using IRMutator::visit;

    size_t unique_vars_used = 0;
    std::vector<std::string> unique_names = {
        "x",
        "y",
        "z",
        "w",
        "u",
        "v",
    };

    Expr visit(const Variable *op) override {
        auto it = variables.find(op->name);
        if (it == variables.end()) {
            // internal_assert(unique_vars_used < unique_names.size()) << "Expression used too many variables!\n";
            if (unique_vars_used < unique_names.size()) {
                std::string new_name = unique_names[unique_vars_used];
                unique_vars_used++;
                Expr new_var = Variable::make(op->type, new_name);
                variables[op->name] = new_var;
                return new_var;
            } else {
                std::string new_name = "v" + std::to_string(unique_vars_used);
                unique_vars_used++;
                Expr new_var = Variable::make(op->type, new_name);
                variables[op->name] = new_var;
                return new_var;
            }
        } else {
            return it->second;
        }
    }

    Expr visit(const Let *op) override {
        // Don't let these be replaced
        Expr value = mutate(op->value);
        Expr variable = Variable::make(op->value.type(), op->name);
        bool exists = variables.find(op->name) != variables.end();
        Expr prev;
        if (exists) {
            prev = variables.at(op->name);
        }
        variables[op->name] = variable;
        Expr body = mutate(op->body);
        if (exists) {
            variables[op->name] = prev;
        } else {
            variables.erase(op->name);
        }
        return Let::make(op->name, value, body);
    }

public:
    std::map<std::string, Expr> variables;
};

class ReorderTerms : public IRMutator {
    using IRMutator::visit;

    // Directly taken from Simplify_Internal.h
    HALIDE_ALWAYS_INLINE
    bool should_commute(const Expr &a, const Expr &b) {
        if (a.node_type() < b.node_type()) {
            return true;
        }
        if (a.node_type() > b.node_type()) {
            return false;
        }

        if (a.node_type() == IRNodeType::Variable) {
            const Variable *va = a.as<Variable>();
            const Variable *vb = b.as<Variable>();
            return va->name.compare(vb->name) > 0;
        }

        return false;
    }

    // This is very similar to code in LICM, but we don't care about depth.
    struct AffineTerm {
        Expr expr;
        int coefficient;
    };

    vector<AffineTerm> extract_summation(const Expr &e) {
        vector<AffineTerm> pending, terms;
        pending.push_back({e, 1});
        while (!pending.empty()) {
            AffineTerm next = pending.back();
            pending.pop_back();
            const Add *add = next.expr.as<Add>();
            const Sub *sub = next.expr.as<Sub>();
            if (add) {
                pending.push_back({add->a, next.coefficient});
                pending.push_back({add->b, next.coefficient});
            } else if (sub) {
                pending.push_back({sub->a, next.coefficient});
                pending.push_back({sub->b, -next.coefficient});
            } else {
                next.expr = mutate(next.expr);
                if (next.expr.as<Add>() || next.expr.as<Sub>()) {
                    // After mutation it became an add or sub, throw it back on the pending queue.
                    pending.push_back(next);
                } else {
                    terms.push_back(next);
                }
            }
        }

        // Sort the terms by commutativty.
        std::stable_sort(terms.begin(), terms.end(),
                         [&](const AffineTerm &a, const AffineTerm &b) {
                             return should_commute(a.expr, b.expr);
                         });

        return terms;
    }

    // Two-finger O(n) algorithm for simplifying sums
    vector<AffineTerm> simplify_summation(const vector<AffineTerm> &terms) {
        if (terms.empty()) {
            return terms;
        }

        vector<AffineTerm> simplified = { terms[0] };

        int i_simpl = 0;
        int j_terms = 1;

        const int n = terms.size();

        while (j_terms < n) {
            AffineTerm current_term = terms[j_terms];
            if (graph_equal(simplified[i_simpl].expr, current_term.expr)) {
                simplified[i_simpl].coefficient += current_term.coefficient;
            } else {
                simplified.push_back(current_term);
                i_simpl++;
            }
            j_terms++;
        }
        return simplified;
    }

    Expr construct_summation(vector<AffineTerm> &terms) {
        Expr result;
        while (!terms.empty()) {
            AffineTerm next = terms.back();
            terms.pop_back();
            if (next.coefficient == 0) {
                continue;
            } else if (!result.defined()) {
                if (next.coefficient == 1) {
                    result = next.expr;
                } else if (next.coefficient == -1) {
                    result = -next.expr;
                } else {
                    result += (next.coefficient * next.expr);
                }
            } else {
                if (next.coefficient == 1) {
                    result += next.expr;
                } else if (next.coefficient == -1) {
                    result -= next.expr;
                } else {
                    result += (next.coefficient * next.expr);
                }
            }
        }
        return result;
    }

    Expr reassociate_summation(const Expr &e) {
        vector<AffineTerm> terms = extract_summation(e);
        terms = simplify_summation(terms);
        return construct_summation(terms);
    }

    Expr visit(const Add *op) override {
        if (op->type == Int(32)) {
            return reassociate_summation(op);
        } else {
            return IRMutator::visit(op);
        }
    }

    Expr visit(const Sub *op) override {
        if (op->type == Int(32)) {
            return reassociate_summation(op);
        } else {
            return IRMutator::visit(op);
        }
    }

    struct AffineTermsGather {
        vector<AffineTerm> a;
        vector<AffineTerm> b;
        vector<AffineTerm> like_terms;
    };

    // Return terms will be simplified.
    // Assumes terms are simplified already.
    AffineTermsGather extract_like_terms(const vector<AffineTerm> &a, const vector<AffineTerm> &b) {
        internal_assert(!a.empty() && !b.empty()) << "Terms to extract should not be empty\n";

        AffineTermsGather gatherer;

        size_t i = 0, j = 0;

        while (i < a.size() && j < b.size()) {
            // TODO: do we need matching coefficients? probably, but this feels weak...
            if (a[i].coefficient == b[j].coefficient && graph_equal(a[i].expr, b[j].expr)) {
                gatherer.like_terms.push_back(a[i]);
                i++;
                j++;
            } else if (should_commute(a[i].expr, b[j].expr)) {
                // i term is earlier than j term
                gatherer.a.push_back(a[i]);
                i++;
            } else {
                gatherer.b.push_back(b[j]);
                j++;
            }
        }

        // Wrap up tail conditions
        while (i < a.size()) {
            gatherer.a.push_back(a[i]);
            i++;
        }

        while (j < b.size()) {
            gatherer.b.push_back(b[j]);
            j++;
        }

        return gatherer;
    }

    // construct_summation but handles the empty term case
    Expr reconstruct_terms(vector<AffineTerm> &terms, Type t) {
        if (terms.empty()) {
            return make_zero(t);
        } else {
            return construct_summation(terms);
        }
    }

    vector<AffineTerm> extract_and_simplify(const Expr &expr) {
        auto extract = extract_summation(expr);
        return simplify_summation(extract);
    }

    template<class BinOp>
    Expr visit_binary_op(const BinOp *op) {
        if (op->type == Int(32)) {
          vector<AffineTerm> a_terms = extract_and_simplify(op->a);
          vector<AffineTerm> b_terms = extract_and_simplify(op->b);
          AffineTermsGather gathered = extract_like_terms(a_terms, b_terms);
          if (gathered.like_terms.empty()) {
              Expr a = construct_summation(a_terms);
              Expr b = construct_summation(b_terms);
              return BinOp::make(a, b);
          } else {
              Expr like_terms = construct_summation(gathered.like_terms);
              Expr a = reconstruct_terms(gathered.a, op->type);
              Expr b = reconstruct_terms(gathered.b, op->type);
              return BinOp::make(a, b) + like_terms;
          }
        } else {
            return IRMutator::visit(op);
        }
    }

    Expr visit(const Min *op) override {
        return visit_binary_op<Min>(op);
    }

    Expr visit(const Max *op) override {
        return visit_binary_op<Max>(op);
    }

    // TODO: do Select in the same way

};

class SubstituteSomeLets : public IRMutator {
    using IRMutator::visit;

    Scope<Expr> scope;
    size_t count;

    Expr visit(const Let *op) override {
        Expr value = mutate(op->value);
        ScopedBinding<Expr> bind(scope, op->name, value);
        Expr body = mutate(op->body);
        // Let simplify() handle the case that this var was removed.
        return Let::make(op->name, value, body);
    }

    Expr visit(const Variable *op) override {
        if (count > 0 && scope.contains(op->name)) {
            count--;
            return mutate(scope.get(op->name));
        } else {
            return op;
        }
    }

public:
    SubstituteSomeLets(size_t _count) : count(_count) {
    }
};

class FindVars : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Variable *op) override {
        vars.insert(op->name);
    }

public:
    std::set<std::string> vars;

};

}  // namespace

Stmt simplify_correlated_differences(const Stmt &stmt) {
    return SimplifyCorrelatedDifferences().mutate(stmt);
}

Expr refactor_correlated_differences(const Expr &expr) {
    // std::cerr << "refactor before: " << expr << "\n";
    Expr repl = simplify(expr);
    // std::cerr << "refactor simpl: " << repl << "\n";
    // repl = substitute_in_all_lets(repl);
    // repl = simplify(repl);
    std::cerr << "AJ LOOK HERE: " << SimplerNameMutator().mutate(repl) << "\n";
    repl = ReorderTerms().mutate(repl);
    // std::cerr << "refactor reordered: " << repl << "\n";
    repl = simplify(repl);
    // std::cerr << "refactor simpl: " << repl << "\n";
    repl = RefactorCorrelatedDifferences().mutate(repl);
    // std::cerr << "refactor after: " << repl << "\n";
    return repl;
}

bool possibly_correlated(const Expr &expr) {
    // For now, this is a really weak check, but it's easy and probably catches a lot of stuff
    PossiblyCorrelatedChecker checker;
    expr.accept(&checker);
    return checker.possibly_correlated;
}

Expr substitute_some_lets(const Expr &expr, size_t count) {
    return SubstituteSomeLets(count).mutate(expr);
}

Expr reorder_terms(const Expr &expr) {
    return ReorderTerms().mutate(expr);
}

void print_relevant_scope(const Expr &expr, const Scope<Interval> &scope, std::ostream &stream) {
        FindVars finder;
        expr.accept(&finder);

        stream << "{\n";
        for (const auto &var : finder.vars) {
            if (scope.contains(var)) {
                stream << "  " << var << " : " << scope.get(var) << "\n";
            }
        }
        stream << "}";
}

}  // namespace Internal
}  // namespace Halide
