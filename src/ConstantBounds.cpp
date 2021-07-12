#include "ConstantBounds.h"

#include "CSE.h"
#include "CompilerLogger.h"
#include "Error.h"
#include "Expr.h"
#include "ExprUsesVar.h"
#include "IR.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Scope.h"
#include "Simplify.h"
#include "Simplify_Internal.h"
#include "Solve.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {
namespace {

bool get_use_push_division_from_environment() {
    static std::string env_var_value = get_env_variable("HL_USE_PUSH_DIVISION");
    static bool enable = env_var_value == "1";
    static bool disable = env_var_value == "0";
    if (enable == disable) {
        // Nag people to set this one way or the other, to avoid
        // running the wrong experiment by mistake.
        user_warning << "HL_USE_PUSH_DIVISION unset\n";
    }
    return enable;
}

using std::string;
using std::vector;

class RefactorCorrelatedDifferences : public IRMutator {
    using IRMutator::visit;

    // Symbols used by rewrite rules
    IRMatcher::Wild<0> x;
    IRMatcher::Wild<1> y;
    IRMatcher::Wild<2> z;
    IRMatcher::Wild<3> w;
    IRMatcher::Wild<4> u;
    IRMatcher::Wild<5> v;
    IRMatcher::WildConst<0> c0;
    IRMatcher::WildConst<1> c1;
    IRMatcher::WildConst<2> c2;
    IRMatcher::WildConst<3> c3;
    IRMatcher::WildConst<4> c4;
    IRMatcher::WildConst<5> c5;

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

                #include "rules/Simplify_Sub.inc"
                ||

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
                
                #include "rules/Simplify_Max.inc"
                ||

                false) {
                return rewrite.result;
            }
        }
        return max(a, b);
    }

    Expr visit(const Min *op) override {
        Expr a = mutate(op->a), b = mutate(op->b);

        if (op->type == Int(32)) {
            auto rewrite = IRMatcher::rewriter(IRMatcher::min(a, b), op->type);

            if (

                #include "rules/Simplify_Min.inc"
                ||

                false) {
                return rewrite.result;
            }
        }
        return min(a, b);
    }

    Expr visit(const Mul *op) override {
        Expr a = mutate(op->a), b = mutate(op->b);

        if (op->type == Int(32)) {
            auto rewrite = IRMatcher::rewriter(IRMatcher::mul(a, b), op->type);

            if (
                #include "rules/Simplify_Mul.inc"
                ||

                false) {
                return rewrite.result;
            }
        }
        return a * b;
    }

    Expr visit(const Add *op) override {
        Expr a = mutate(op->a), b = mutate(op->b);

        if (op->type == Int(32)) {
            auto rewrite = IRMatcher::rewriter(IRMatcher::add(a, b), op->type);

            if (
                #include "rules/Simplify_Add.inc" 
                ||

                false) {
                return rewrite.result;
            }
        }
        return a + b;
    }

    Expr visit(const Select *op) override {
        Expr a = mutate(op->condition);
        Expr b = mutate(op->true_value);
        Expr c = mutate(op->false_value);

        if (op->type == Int(32)) {
            auto rewrite = IRMatcher::rewriter(IRMatcher::select(a, b, c), op->type);

            if (

                #include "rules/Simplify_Select.inc"
                ||

                false) {
                return rewrite.result;
            }
        }
        return select(a, b, c);
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

class ReorderTerms : public IRGraphMutator {
    using IRGraphMutator::visit;

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

    bool is_mul_by_int_const(const Expr &expr, AffineTerm &term) {
        const Mul *mul = expr.as<Mul>();
        if (mul) {
            // TODO: can we always expect the imm to be on the right?
            const IntImm *b = mul->b.as<IntImm>();
            if (b) {
                // TODO: check for overflow...
                term.coefficient = b->value;
                term.expr = mul->a;
                return true;
            }
        }
        return false;
    }

    vector<AffineTerm> extract_summation(const Expr &e) {
        vector<AffineTerm> pending, terms;
        pending.push_back({e, 1});

        // In case this is not a sum
        const Add *add = e.as<Add>();
        const Sub *sub = e.as<Sub>();
        if (!(add || sub)) {
            return pending;
        }

        // std::cerr << "Extracting summation from: " << e << "\n";

        while (!pending.empty()) {
            AffineTerm next = pending.back();
            pending.pop_back();
            const Add *add = next.expr.as<Add>();
            const Sub *sub = next.expr.as<Sub>();
            if (add) {
                // std::cerr << next.expr << " : Add\n";
                pending.push_back({add->a, next.coefficient});
                pending.push_back({add->b, next.coefficient});
            } else if (sub) {
                // std::cerr << next.expr << " : Sub\n";
                pending.push_back({sub->a, next.coefficient});
                pending.push_back({sub->b, -next.coefficient});
            } else {
                // std::cerr << "Next: " << next.expr << "\n";
                // Expr old = next.expr;
                next.expr = mutate(next.expr);
                // std::cerr << "Became: " << old << " -> " << next.expr << "\n";
                if (next.expr.as<Add>() || next.expr.as<Sub>()) {
                    // After mutation it became an add or sub, throw it back on the pending queue.
                    // std::cerr << next.expr << " : Bac\n";
                    pending.push_back(next);
                } else {
                    // std::cerr << next.expr << " : Term\n";
                    AffineTerm term;
                    if (is_mul_by_int_const(next.expr, term)) {
                        internal_assert(term.expr.defined());
                        // TODO: check for overflow...
                        term.coefficient *= next.coefficient;
                        pending.push_back(term);
                    } else {
                        terms.push_back(next);
                    }
                }
            }
        }

        // Sort the terms by commutativty.
        std::stable_sort(terms.begin(), terms.end(),
                         [&](const AffineTerm &a, const AffineTerm &b) {
                             return should_commute(a.expr, b.expr);
                         });

        // std::cerr << "Extracted summation\n";
        // for (auto &term : terms) {
        //     std::cerr << term.expr << " x " << term.coefficient << "\n";
        // }
        // std::cerr << "\n";

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
        // std::cerr << "Constructing summation\n";
        while (!terms.empty()) {
            AffineTerm next = terms.back();
            internal_assert(next.expr.defined()) << "Expr with coefficient: " << next.coefficient << " is undefined for partial result: " << result << "\n";
            terms.pop_back();
            if (next.coefficient == 0) {
                continue;
            } else if (!result.defined()) {
                if (next.coefficient == 1) {
                    result = next.expr;
                } else if (next.coefficient == -1) {
                    result = -next.expr;
                } else {
                    result = (next.expr * next.coefficient);
                }
            } else {
                if (next.coefficient == 1) {
                    result += next.expr;
                } else if (next.coefficient == -1) {
                    result -= next.expr;
                } else {
                    result += (next.expr * next.coefficient);
                }
            }
        }

        // std::cerr << "Constructed summation: " << result << "\n";

        return result;
    }

    Expr reassociate_summation(const Expr &e) {
        vector<AffineTerm> terms = extract_summation(e);
        terms = simplify_summation(terms);
        return construct_summation(terms);
    }

    Expr visit(const Add *op) override {
        // internal_error << "I don't think we should see this?? " << Expr(op) << "\n";
        if (op->type == Int(32)) {
            return reassociate_summation(op);
        } else {
            return IRGraphMutator::visit(op);
        }
    }

    Expr visit(const Sub *op) override {
        // internal_error << "I don't think we should see this?? " << Expr(op) << "\n";
        if (op->type == Int(32)) {
            return reassociate_summation(op);
        } else {
            return IRGraphMutator::visit(op);
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
            if (a[i].coefficient == b[j].coefficient && graph_equal(a[i].expr, b[j].expr) && !is_const_zero(a[i].expr)) {
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
            return IRGraphMutator::visit(op);
        }
    }

    Expr visit(const Min *op) override {
        return visit_binary_op<Min>(op);
    }

    Expr visit(const Max *op) override {
        return visit_binary_op<Max>(op);
    }

    // TODO: do Select in the same way
    Expr visit(const Select *op) override {
        if (op->type == Int(32)) {
            vector<AffineTerm> a_terms = extract_and_simplify(op->true_value);
            vector<AffineTerm> b_terms = extract_and_simplify(op->false_value);
            AffineTermsGather gathered = extract_like_terms(a_terms, b_terms);
            if (gathered.like_terms.empty()) {
                Expr a = construct_summation(a_terms);
                Expr b = construct_summation(b_terms);
                return Select::make(op->condition, a, b);
            } else {
                Expr like_terms = construct_summation(gathered.like_terms);
                Expr a = reconstruct_terms(gathered.a, op->type);
                Expr b = reconstruct_terms(gathered.b, op->type);
                return Select::make(op->condition, a, b) + like_terms;
            }
        } else {
            return IRGraphMutator::visit(op);
        }
    }
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

class PushDivisions : public IRMutator {
    using IRMutator::visit;

    Expr visit(const Div *op) override {
        const IntImm *denominator = op->b.as<IntImm>();
        internal_assert(denominator) << "Encountered division by non-const or non-int\n" << Expr(op) << "\n";
        internal_assert(denominator->value > 0) << "Encountered division by non-positive constant\n";
        // TODO: catch overflow issues
        Expr ret;
        if (denom > 0) {
            // Already in a division.
            denom *= denominator->value;
            ret = mutate(op->a);
            denom /= denominator->value;
        } else {
            // Not in a division
            denom = denominator->value;
            ret = mutate(op->a);
            denom = -1;
        }
        if (lower) {
            return ret;
        } else {
            return ret + 1;
        }
    }

    Expr visit(const IntImm *op) override {
        if (denom > 0) {
            int64_t value = div_imp(op->value, denom);
            return IntImm::make(op->type, value);
        } else {
            return op;
        }
    }

    Expr visit(const UIntImm *op) override {
        internal_error << "Not implemented yet\n";
        return Expr();
    }

    Expr visit(const FloatImm *op) override {
        internal_error << "Not implemented yet\n";
        return Expr();
    }

    Expr visit(const StringImm *op) override {
        internal_error << "Not implemented yet\n";
        return Expr();
    }

    Expr visit(const Cast *op) override {
        internal_error << "Not implemented yet\n";
        return Expr();
    }

    Expr visit(const Variable *op) override {
        if (denom > 0) {
            return Expr(op) / IntImm::make(op->type, denom);
        } else {
            return op;
        }
    }

    // Add's default mutate method is fine
    Expr visit(const Sub *op) override {
        Expr a = mutate(op->a);
        lower = !lower;
        Expr b = mutate(op->b);
        lower = !lower;
        return a - b;
    }

    Expr visit(const Mul *op) override {
        if (denom > 0) {
            return Expr(op) / IntImm::make(op->type, denom);
        } else {
            return op;
        }
        // TODO: can we push inside of multiplication?
        // const IntImm *a_int = op->a.as<IntImm>();
        // const IntImm *b_int = op->b.as<IntImm>();
        // internal_assert(a_int || b_int) << "Multiplication by non-constant: " << Expr(op) << "\n";
        // // Assume already simplified
        // Expr ret;
        // if (a_int) {
        //     if (a_int->value < 0) {
        //         lower = !lower;
        //         ret = mutate(op->b);
        //         lower = !lower;
        //         ret *= op->a;
        //     } else {
        //         ret = mutate(op->b);
        //         ret *= op->a;
        //     }
        // } else {
        //     if (b_int->value < 0) {
        //         lower = !lower;
        //         ret = mutate(op->a);
        //         lower = !lower;
        //         ret *= op->b;
        //     } else {
        //         ret = mutate(op->a);
        //         ret *= op->b;
        //     }
        // }
        // return ret;
    }

    Expr visit(const Mod *op) override {
        internal_error << "Not implemented yet\n";
        return Expr();
    }

    // Min and Max pushing is fine

    Expr visit(const EQ *op) override {
        internal_error << "Not implemented yet\n";
        return Expr();
    }

    Expr visit(const NE *op) override {
        internal_error << "Not implemented yet\n";
        return Expr();
    }

    Expr visit(const LT *op) override {
        internal_error << "Not implemented yet\n";
        return Expr();
    }

    Expr visit(const LE *op) override {
        internal_error << "Not implemented yet\n";
        return Expr();
    }

    Expr visit(const GT *op) override {
        internal_error << "Not implemented yet\n";
        return Expr();
    }

    Expr visit(const GE *op) override {
        internal_error << "Not implemented yet\n";
        return Expr();
    }

    Expr visit(const And *op) override {
        internal_error << "Not implemented yet\n";
        return Expr();
    }

    Expr visit(const Or *op) override {
        internal_error << "Not implemented yet\n";
        return Expr();
    }

    Expr visit(const Not *op) override {
        internal_error << "Not implemented yet\n";
        return Expr();
    }

    Expr visit(const Select *op) override {
        Expr t  = mutate(op->true_value);
        Expr f = mutate(op->false_value);
        return Select::make(op->condition, t, f);
    }

    Expr visit(const Load *op) override {
        internal_error << "Not implemented yet\n";
        return Expr();
    }

    Expr visit(const Ramp *op) override {
        internal_error << "Not implemented yet\n";
        return Expr();
    }

    Expr visit(const Broadcast *op) override {
        internal_error << "Not implemented yet\n";
        return Expr();
    }

    Expr visit(const Call *op) override {
        internal_error << "Not implemented yet\n";
        return Expr();
    }

    // Let defualt is fine for now.

    Expr visit(const Shuffle *op) override {
        internal_error << "Not implemented yet\n";
        return Expr();
    }

    Expr visit(const VectorReduce *op) override {
        internal_error << "Not implemented yet\n";
        return Expr();
    }

    int64_t denom = -1;
    bool lower;
public:
    PushDivisions(bool _lower) : lower(_lower) {
    }
};

class CheckAffineDivision : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Mul *op) override {
        op->a.accept(this);
        op->b.accept(this);
        const IntImm *a_int = op->a.as<IntImm>();
        const IntImm *b_int = op->b.as<IntImm>();
        affine = affine && (a_int || b_int);
    }

    void visit(const Div *op) override {
        op->a.accept(this);
        // std::cerr << "Checking affine div on: " << Expr(op) << "\n";
        const IntImm *denom = op->b.as<IntImm>();
        const Add *numer_add = op->a.as<Add>();
        const Sub *numer_sub = op->a.as<Sub>();
        const Select *numer_sel = op->a.as<Select>();
        const Min *numer_min = op->a.as<Min>();
        const Max *numer_max = op->a.as<Max>();
        
        if (denom) {
            div_by_posconst_only = div_by_posconst_only && (denom->value > 0);

            if (numer_add || numer_sub || numer_sel || numer_max || numer_min) {
                has_pushable_div = true;
            }
        } else {
            div_by_posconst_only = false;
        }
    }

    void visit(const Select *op) override {
        op->true_value.accept(this);
        op->false_value.accept(this);
    }

    void visit(const UIntImm *op) override {
        contains_invalid_instruction = true;
    }

    void visit(const FloatImm *op) override {
        contains_invalid_instruction = true;
    }

    void visit(const StringImm *op) override {
        contains_invalid_instruction = true;
    }
    
    void visit(const Cast *op) override {
        contains_invalid_instruction = true;
    }

    void visit(const Mod *op) override {
        contains_invalid_instruction = true;
    }
    
    void visit(const Load *op) override {
        contains_invalid_instruction = true;
    }

    void visit(const Ramp *op) override {
        contains_invalid_instruction = true;
    }

    void visit(const Broadcast *op) override {
        contains_invalid_instruction = true;
    }

    void visit(const Call *op) override {
        contains_invalid_instruction = true;
    }

    void visit(const Shuffle *op) override {
        contains_invalid_instruction = true;
    }
    void visit(const VectorReduce *op) override {
        contains_invalid_instruction = true;
    }
    
    // Nix boolean constructors
    void visit(const EQ *op) override {
        contains_invalid_instruction = true;
    }
    void visit(const NE *op) override {
        contains_invalid_instruction = true;
    }
    void visit(const LT *op) override {
        contains_invalid_instruction = true;
    }
    void visit(const LE *op) override {
        contains_invalid_instruction = true;
    }
    void visit(const GT *op) override {
        contains_invalid_instruction = true;
    }
    void visit(const GE *op) override {
        contains_invalid_instruction = true;
    }
    void visit(const And *op) override {
        contains_invalid_instruction = true;
    }
    void visit(const Or *op) override {
        contains_invalid_instruction = true;
    }
public:
    bool affine = true;
    bool div_by_posconst_only = true;
    bool contains_invalid_instruction = false;
    bool has_pushable_div = false;
};

}  // namespace

Expr refactor_correlated_differences(const Expr &expr) {
    return RefactorCorrelatedDifferences().mutate(expr);
    // // std::cerr << "refactor before: " << expr << "\n";
    // Expr repl = simplify(expr);
    // // std::cerr << "refactor simpl: " << repl << "\n";
    // // repl = substitute_in_all_lets(repl);
    // // repl = simplify(repl);
    // // std::cerr << "AJ LOOK HERE: " << SimplerNameMutator().mutate(repl) << "\n";
    // // repl = ReorderTerms().mutate(repl);
    // // std::cerr << "refactor reordered: " << repl << "\n";
    // // repl = simplify(repl);
    // // std::cerr << "refactor simpl: " << repl << "\n";
    // repl = RefactorCorrelatedDifferences().mutate(repl);
    // // std::cerr << "refactor after: " << repl << "\n";
    // return repl;
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
        stream << "}\n";
}

bool is_affine_division(const Expr &expr) {
    CheckAffineDivision check;
    expr.accept(&check);
    return check.has_pushable_div && check.affine && check.div_by_posconst_only && !check.contains_invalid_instruction;
}

Interval get_division_interval(const Expr &expr) {
    internal_assert(is_affine_division(expr)) << "get_division_interval called on invalid Expr: " << expr << "\n";
    // std::cerr << "Affine div: " << expr << "\n";
    Expr upper = PushDivisions(false).mutate(expr);
    Expr lower = PushDivisions(true).mutate(expr);
    Interval interval(lower, upper);
    // TODO: should we be doing simplification here? Seems like we need a GraphMutator...
    // std::cerr << "Before reorder: " << interval << "\n";
    // TODO: make these reorder
    interval.min = simplify(lower);
    interval.max = simplify(upper);
    // interval.min = reorder_terms(simplify(lower));
    // interval.max = reorder_terms(simplify(upper));
    // std::cerr << "After reorder: " << interval << "\n";
    return interval;
}

void simplify(Interval &interval) {
    interval.min = simplify(interval.min);
    interval.max = simplify(interval.max);
}

Interval default_cbounds(const Expr &expr, const Scope<Interval> &scope) {
    Interval interval = bounds_of_expr_in_scope(expr, scope, FuncValueBounds(), true);
    simplify(interval);

    // Note that we can get non-const but well-defined results (e.g. signed_integer_overflow);
    // for our purposes here, treat anything non-const as no-bound.
    if (!is_const(interval.min)) {
        interval.min = Interval::neg_inf();
    }
    if (!is_const(interval.max)) {
        interval.max = Interval::pos_inf();
    }
    return interval;
}

Interval find_constant_bounds_v2(const Expr &expr, const Scope<Interval> &scope) {
return default_cbounds(expr, scope);

    if (!is_const(expr) && possibly_correlated(expr)) {
        Expr subst = simplify(substitute_some_lets(expr));

        Interval interval;

        if (is_affine_division(subst)) {
            // TODO: should we be refactoring here?
            Interval div_interval = get_division_interval(subst);
            interval.min = bounds_of_expr_in_scope(div_interval.min, scope, FuncValueBounds(), true).min;
            interval.max = bounds_of_expr_in_scope(div_interval.max, scope, FuncValueBounds(), true).max;
        } else {
            // Todo: more possible pre-processing.
            subst = refactor_correlated_differences(subst);
            interval = bounds_of_expr_in_scope(subst, scope, FuncValueBounds(), true);
        }
        simplify(interval);
        return interval;

        // std::cerr << "\nTerm:" << expr << "\n";
        // Expr full_simp = simplify(substitute_in_all_lets(expr));
        // Interval full_i= bounds_of_expr_in_scope(full_simp, scope, FuncValueBounds(), true);
        // simplify(full_i);
        // Expr part_simp = simplify(substitute_some_lets(expr));
        // Interval part_i= bounds_of_expr_in_scope(part_simp, scope, FuncValueBounds(), true);
        // simplify(part_i);
        // Expr refactor_simp = refactor_correlated_differences(expr);
        // Interval refactor_i= bounds_of_expr_in_scope(refactor_simp, scope, FuncValueBounds(), true);
        // simplify(refactor_i);
        // Expr refactor_sub = refactor_correlated_differences(simplify(substitute_some_lets(expr)));
        // Interval refactors_i= bounds_of_expr_in_scope(refactor_sub, scope, FuncValueBounds(), true);
        // simplify(refactors_i);

        // std::cerr << "\tOrig:\n\t" << interval << "\n";
        // std::cerr << "\tFull: " << full_simp << "\n\t" << full_i << "\n";
        // std::cerr << "\tPart: " << part_simp << "\n\t" << part_i << "\n";
        // std::cerr << "\tRefc: " << refactor_simp << "\n\t" << refactor_i << "\n";
        // std::cerr << "\tBoth:" << refactor_sub << "\n\t" << refactors_i << "\n";
        // std::cerr << "Relevant scope:\n";
        // std::cerr << "\n";
        // Expr expr = reorder_terms(simplify(remove_likelies(e)));
        // Expr expr = simplify(substitute_some_lets(remove_likelies(e)));
        // Interval interval_s = bounds_of_expr_in_scope(expr, scope, FuncValueBounds(), true);
        // interval_s.min = simplify(interval_s.min);
        // interval_s.max = simplify(interval_s.max);

        // if ((is_const(interval_s.min) && !is_const(interval.min)) || (is_const(interval_s.max) && !is_const(interval.max))) {
        //     std::cerr << "\n\ncomparing methods\nfor: " << e << "\n\t-> " << expr << "\n\t" << interval << "\n\tvs\n\t" << interval_s << "\n";
        //     // interval = interval_s;
        //     scope.print(std::cerr);
        //     std::cerr << "\n\n";
        // } else if ((!is_const(interval_s.min) && is_const(interval.min)) || (!is_const(interval_s.max) && is_const(interval.max))) {
        //     std::cerr << "\n\nfailing method\nfor: " << e << "\n\t-> " << expr << "\n\t" << interval << "\n\tvs\n\t" << interval_s << "\n";
        //     scope.print(std::cerr);
        //     std::cerr << "\n\n";
        // } else if ((is_const(interval_s.min) && is_const(interval.min) && can_prove(interval_s.min != interval.min)) ||
        //             (is_const(interval_s.max) && is_const(interval.max) && can_prove(interval_s.max != interval.max))) {
        //     std::cerr << "\n\ndiffering methods\nfor: " << e << "\n\t-> " << expr << "\n\t" << interval << "\n\tvs\n\t" << interval_s << "\n";
        //     scope.print(std::cerr);
        //     std::cerr << "\n\n";
        // }
    } else {
        return default_cbounds(expr, scope);
    }
}

void make_const_interval(Interval &interval) {
    // Note that we can get non-const but well-defined results (e.g. signed_integer_overflow);
    // for our purposes here, treat anything non-const as no-bound.
    if (!is_const(interval.min)) {
        interval.min = Interval::neg_inf();
    }
    if (!is_const(interval.max)) {
        interval.max = Interval::pos_inf();
    }
}

Interval get_cbounds(const Expr &expr, const Scope<Interval> &scope) {
    Interval interval = bounds_of_expr_in_scope(expr, scope, FuncValueBounds(), true);
    simplify(interval);
    make_const_interval(interval);
    return interval;
}

Interval all_opt(const Expr &expr, const Scope<Interval> &scope) {
    Expr e = simplify(substitute_some_lets(expr));
    e = refactor_correlated_differences(e);
    // TODO: don't reorder for some reason...
    // e = simplify(reorder_terms(e));
    e = simplify(e);

    Interval interval;

    if (is_affine_division(e)) {
        Interval div_interval = get_division_interval(e);
        interval.min = bounds_of_expr_in_scope(div_interval.min, scope, FuncValueBounds(), true).min;
        interval.max = bounds_of_expr_in_scope(div_interval.max, scope, FuncValueBounds(), true).max;
    } else {
        interval = get_cbounds(e, scope);
    }
    simplify(interval);
    make_const_interval(interval);
    return interval;
}

Interval perform_push_division(const Expr &expr, const Scope<Interval> &scope) {
    internal_assert(is_affine_division(expr)) << "Expr is not affine division\n" << expr << "\n";
    Interval div_interval = get_division_interval(expr);
    Expr min_val = bounds_of_expr_in_scope(div_interval.min, scope, FuncValueBounds(), true).min;
    Expr max_val = bounds_of_expr_in_scope(div_interval.max, scope, FuncValueBounds(), true).max;
    Interval interval(min_val, max_val);
    simplify(interval);
    make_const_interval(interval);
    return interval;
}

Interval try_constant_bounds_methods(const Expr &expr, const Scope<Interval> &scope) {
    /*
    Interval original = get_cbounds(expr, scope);

    if (is_const(expr) || !possibly_correlated(expr) || expr.type().is_float()) {
        return original;
    }

    std::cerr << "Expr: " << expr << "\n";
    std::cerr << "Renamed: " << SimplerNameMutator().mutate(expr) << "\n";

    Expr subst = simplify(substitute_some_lets(expr));
    // std::cerr << "Subst: " << subst << "\n";
    Expr synth = simplify(refactor_correlated_differences(expr));
    // std::cerr << "Synth: " << synth << "\n";
    Expr reorder = simplify(reorder_terms(expr));
    // std::cerr << "Reorder: " << reorder << "\n";

    Interval subst_i = get_cbounds(subst, scope);
    Interval synth_i = get_cbounds(synth, scope);
    Interval reorder_i = get_cbounds(reorder, scope);

    // std::cerr << "Subst: " << subst_i << "\n";
    // std::cerr << "Synth: " << synth_i << "\n";
    // std::cerr << "Reorder: " << reorder_i << "\n";

    bool log_div = is_affine_division(expr);
    Interval div_i;
    if (log_div) {
        Interval div_interval = get_division_interval(expr);
        div_i.min = bounds_of_expr_in_scope(div_interval.min, scope, FuncValueBounds(), true).min;
        div_i.max = bounds_of_expr_in_scope(div_interval.max, scope, FuncValueBounds(), true).max;
        simplify(div_i);
        make_const_interval(div_i);
    }

    Interval opt_i = all_opt(expr, scope);
    compare_intervals(subst_i, original, expr, scope, "subst vs original");
    compare_intervals(synth_i, original, expr, scope, "synth vs original");
    compare_intervals(reorder_i, original, expr, scope, "reorder vs original");
    if (log_div) {
        compare_intervals(div_i, original, expr, scope, "div vs original");
    }
    Interval ret = compare_intervals(opt_i, original, expr, scope, "all vs original");

    return ret;
    */

    Interval original = get_cbounds(expr, scope);
    static bool use_push_div = get_use_push_division_from_environment();

    if (use_push_div && is_affine_division(expr)) {
        Interval div_interval = perform_push_division(expr, scope);
        original.min = Interval::make_max(original.min, div_interval.min);
        original.max = Interval::make_max(original.max, div_interval.max);
    }
    return original;
}




}  // namespace Internal
}  // namespace Halide
