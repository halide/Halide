#include "Halide.h"
#include "parser.h"
#include "expr_util.h"
#include "synthesize_predicate.h"
#include "reduction_order.h"

#include <fstream>

using namespace Halide;
using namespace Halide::Internal;

// Take a list of rewrite rules and classify them by root IR node, and
// what problems they might have that require further investigation.

struct Rule {
    Expr lhs, rhs, predicate;
    Expr orig;
};

using std::map;
using std::string;
using std::vector;

    class FindCommutativeOps : public IRVisitor {
        template<typename Op>
        void visit_commutative_op(const Op *op) {
            const Variable *var_a = op->a.template as<Variable>();
            const Variable *var_b = op->b.template as<Variable>();
            if (var_b && var_b->name[0] == 'c') return;
            if (is_const(op->b)) return;
            if (var_a || var_b) {
                commutative_ops.push_back(Expr(op));
            }
            IRVisitor::visit(op);
        }

        void visit(const Add *op) override {
            visit_commutative_op(op);
        }
        void visit(const Mul *op) override {
            visit_commutative_op(op);
        }
        void visit(const Min *op) override {
            visit_commutative_op(op);
        }
        void visit(const Max *op) override {
            visit_commutative_op(op);
        }
        void visit(const EQ *op) override {
            visit_commutative_op(op);
        }
        void visit(const NE *op) override {
            visit_commutative_op(op);
        }
        void visit(const And *op) override {
            visit_commutative_op(op);
        }
        void visit(const Or *op) override {
            visit_commutative_op(op);
        }

    public:
        vector<Expr> commutative_ops;
    };

    class Commute : public IRMutator {
        template<typename Op>
        Expr visit_commutative_op(const Op *op) {
            if (to_commute.same_as(op)) {
                return Op::make(op->b, op->a);
            } else {
                return IRMutator::visit(op);
            }
        }

        Expr visit(const Add *op) override {
            return visit_commutative_op(op);
        }
        Expr visit(const Mul *op) override {
            return visit_commutative_op(op);
        }
        Expr visit(const Min *op) override {
            return visit_commutative_op(op);
        }
        Expr visit(const Max *op) override {
            return visit_commutative_op(op);
        }
        Expr visit(const EQ *op) override {
            return visit_commutative_op(op);
        }
        Expr visit(const NE *op) override {
            return visit_commutative_op(op);
        }
        Expr visit(const And *op) override {
            return visit_commutative_op(op);
        }
        Expr visit(const Or *op) override {
            return visit_commutative_op(op);
        }

        Expr to_commute;
    public:
        Commute(Expr c) : to_commute(c) {}
    };


// Levenshtein distance algorithm copied from wikipedia
unsigned int edit_distance(const std::string& s1, const std::string& s2) {
    const std::size_t len1 = s1.size(), len2 = s2.size();
    std::vector<std::vector<unsigned int>> d(len1 + 1, std::vector<unsigned int>(len2 + 1));

    d[0][0] = 0;
    for (unsigned int i = 1; i <= len1; ++i) d[i][0] = i;
    for (unsigned int i = 1; i <= len2; ++i) d[0][i] = i;

    for (unsigned int i = 1; i <= len1; ++i)
        for (unsigned int j = 1; j <= len2; ++j)
            // note that std::min({arg1, arg2, arg3}) works only in C++11,
            // for C++98 use std::min(std::min(arg1, arg2), arg3)
            d[i][j] =
                std::min({ d[i - 1][j] + 1,
                           d[i][j - 1] + 1,
                           d[i - 1][j - 1] + (s1[i - 1] == s2[j - 1] ? 0 : 1) });
    return d[len1][len2];
}

vector<Expr> generate_commuted_variants(const Expr &expr) {
    FindCommutativeOps finder;
    expr.accept(&finder);

    vector<Expr> exprs;
    exprs.push_back(expr);

    for (const Expr &e : finder.commutative_ops) {
        Commute commuter(e);
        vector<Expr> new_exprs = exprs;
        for (const Expr &l : exprs) {
            new_exprs.push_back(commuter.mutate(l));
        }
        exprs.swap(new_exprs);
    }
    return exprs;
}

vector<Expr> generate_reassociated_variants(const Expr &e);

struct LinearTerm {
    bool positive;
    Expr e;
};

// This function is very very exponential
void all_possible_exprs_that_compute_sum(const vector<LinearTerm> &terms, vector<Expr> *result) {
    if (terms.size() == 1) {
        if (terms[0].positive) {
            vector<Expr> variants = generate_reassociated_variants(terms[0].e);
            result->insert(result->end(), variants.begin(), variants.end());
        }
        return;
    }

    for (size_t i = 1; i < (1 << terms.size()) - 1; i++) {
        vector<LinearTerm> left, right;
        for (size_t j = 0; j < terms.size(); j++) {
            if (i & (1 << j)) {
                left.push_back(terms[j]);
            } else {
                right.push_back(terms[j]);
            }
        }
        vector<Expr> left_exprs, right_exprs, right_exprs_negated;
        all_possible_exprs_that_compute_sum(left, &left_exprs);
        all_possible_exprs_that_compute_sum(right, &right_exprs);
        for (auto &t : right) {
            t.positive = !t.positive;
        }
        all_possible_exprs_that_compute_sum(right, &right_exprs_negated);

        for (auto &l : left_exprs) {
            for (auto &r : right_exprs) {
                result->push_back(l + r);
            }
            for (auto &r : right_exprs_negated) {
                result->push_back(l - r);
            }
        }
    }
}

Expr make_binop(IRNodeType t, Expr l, Expr r) {
    if (t == IRNodeType::Min) {
        return min(l, r);
    } else if (t == IRNodeType::Max) {
        return max(l, r);
    } else {
        std::cerr << "Unsupported binop in make_binop: " << t << "\n";
        abort();
    }
}

template<typename Op>
void all_possible_exprs_that_compute_associative_op(const Expr &e,
                                                    vector<Expr> *result) {
    if (!e.as<Op>()) {
        vector<Expr> variants = generate_reassociated_variants(e);
        result->insert(result->end(), variants.begin(), variants.end());
        return;
    }

    vector<Expr> terms = unpack_binary_op<Op>(e);
    for (size_t i = 1; i < (1 << terms.size()) - 1; i++) {
        vector<Expr> left, right;
        for (size_t j = 0; j < terms.size(); j++) {
            if (i & (1 << j)) {
                left.push_back(terms[j]);
            } else {
                right.push_back(terms[j]);
            }
        }
        assert(left.size() < terms.size());
        assert(right.size() < terms.size());
        vector<Expr> left_exprs, right_exprs;
        all_possible_exprs_that_compute_associative_op<Op>(pack_binary_op<Op>(left), &left_exprs);
        all_possible_exprs_that_compute_associative_op<Op>(pack_binary_op<Op>(right), &right_exprs);
        for (auto &l : left_exprs) {
            for (auto &r : right_exprs) {
                result->push_back(Op::make(l, r));
            }
        }
    }
}

vector<Expr> generate_reassociated_variants(const Expr &e) {
    debug(0) << "Generating variants of " << e << "\n";
    if (e.as<Add>() || e.as<Sub>()) {
        vector<LinearTerm> terms, pending;
        pending.emplace_back(LinearTerm {true, e});
        while (!pending.empty()) {
            auto next = pending.back();
            pending.pop_back();
            if (const Add *add = next.e.as<Add>()) {
                pending.emplace_back(LinearTerm {next.positive, add->a});
                pending.emplace_back(LinearTerm {next.positive, add->b});
            } else if (const Sub *sub = next.e.as<Sub>()) {
                pending.emplace_back(LinearTerm {next.positive, sub->a});
                pending.emplace_back(LinearTerm {!next.positive, sub->b});
            } else {
                terms.push_back(next);
            }
        }

        // We now have a linear combination of terms and need to
        // generate all possible trees that compute it. We'll generate
        // all possible partitions, then generate all reassociated
        // variants of the left and right, then combine them.
        vector<Expr> result;
        all_possible_exprs_that_compute_sum(terms, &result);
        return result;
    } else if (const Min *m = e.as<Min>()) {
        vector<Expr> result;
        all_possible_exprs_that_compute_associative_op<Min>(e, &result);
        return result;
    } else if (const Max *m = e.as<Max>()) {
        vector<Expr> result;
        all_possible_exprs_that_compute_associative_op<Max>(e, &result);
        return result;
    }
    return {e};
}

std::string expr_to_rpn_string(const Expr &e) {
    std::ostringstream ss;
    ss << e;
    return ss.str();
    /*

    class RPNPrinter : public IRMutator {
    public:
        Expr mutate(const Expr &e) override {
            IRMutator::mutate(e);
            if (e.as<Variable>() || is_const(e)) {
                ss << e << ' ';
            } else if (e.as<Add>()) {
                ss << "+ ";
            } else if (e.as<Sub>()) {
                ss << "- ";
            } else if (e.as<Min>()) {
                ss << "_ ";
            } else if (e.as<Max>()) {
                ss << "^ ";
            } else if (e.as<Mul>()) {
                ss << "* ";
            } else if (e.as<Div>()) {
                ss << "/ ";
            } else if (e.as<Select>()) {
                ss << "s ";
            } else if (e.as<And>()) {
                ss << "& ";
            } else if (e.as<Or>()) {
                ss << "| ";
            } else if (e.as<EQ>()) {
                ss << "= ";
            } else if (e.as<NE>()) {
                ss << "! ";
            } else if (e.as<LT>()) {
                ss << "< ";
            } else if (e.as<LE>()) {
                ss << "> ";
            } else {
                ss << e.node_type() << ' ';
            }
            return e;
        };

        std::ostringstream ss;
    } rpn_printer;
    rpn_printer.mutate(e);
    return rpn_printer.ss.str();
    */
}

vector<Rule> generate_commuted_variants(const Rule &rule) {
    vector<Expr> lhs = generate_commuted_variants(rule.lhs);
    vector<Expr> rhs = generate_reassociated_variants(rule.rhs);

    vector<Rule> result;
    for (const Expr &l : lhs) {
        Rule r2 = rule;
        r2.lhs = l;
        // Pick the rhs that minimizes edit distance
        std::string lhs_str = expr_to_rpn_string(l);
        int best_edit_distance = -1;
        for (const Expr &r : rhs) {
            std::string rhs_str = expr_to_rpn_string(r);
            int d = edit_distance(lhs_str, rhs_str);
            if (best_edit_distance < 0 || d < best_edit_distance) {
                r2.rhs = r;
                best_edit_distance = d;
            }
        }
        result.push_back(r2);
    }
    return result;
}

Expr remove_folds(const Expr &e) {
    class RemoveFolds : public IRMutator {
        using IRMutator::visit;

        Expr visit(const Call *op) override {
            if (op->name == "fold") {
                return op->args[0];
            } else {
                return IRMutator::visit(op);
            }
        }
    };

    return RemoveFolds().mutate(e);
}

Expr inject_folds(const Expr &e) {

    class InjectFolds : public IRMutator {
        bool constant = false;

        using IRMutator::visit;

        Expr visit(const Variable *var) override {
            if (var->name[0] != 'c') {
                constant = false;
            }
            return var;
        }

    public:
        using IRMutator::mutate;

        Expr mutate(const Expr &e) override {
            bool old = constant;
            constant = true;
            Expr new_e = IRMutator::mutate(e);
            if (constant) {
                // Note we wrap a fold around the *unmutated* child,
                // to avoid nested folds.
                constant = constant && old;
                if (is_const(e) || e.as<Variable>()) {
                    return e;
                } else {
                    return Call::make(e.type(), "fold", {e}, Call::PureExtern);
                }
            } else {
                constant = constant && old;
                return new_e;
            }
        }
    };

    return InjectFolds().mutate(e);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cout << "Usage: ./filter_rewrite_rules rewrite_rules.txt\n";
        return 0;
    }

    vector<Expr> exprs_vec = parse_halide_exprs_from_file(argv[1]);

    // De-dup
    std::set<Expr, IRDeepCompare> exprs;
    exprs.insert(exprs_vec.begin(), exprs_vec.end());

    vector<Rule> rules;

    for (const Expr &e : exprs) {
        if (const Call *call = e.as<Call>()) {
            if (call->name != "rewrite") {
                std::cerr << "Expr is not a rewrite rule: " << e << "\n";
                return -1;
            }
            _halide_user_assert(call->args.size() == 3);
            rules.emplace_back(Rule{call->args[0], call->args[1], call->args[2], e});
        } else {
            std::cerr << "Expr is not a rewrite rule: " << e << "\n";
            return -1;
        }
    }

    // Re-synthesize the predicates if you don't currently trust them
    /*
    for (Rule &r : rules) {
        vector<map<string, Expr>> examples;
        map<string, Expr> binding;
        std::cout << "Re-synthesizing predicate for " << r.orig << "\n";
        r.predicate = synthesize_predicate(r.lhs, r.rhs, examples, &binding);
        r.lhs = substitute(binding, r.lhs);

        for (auto &it: binding) {
            it.second = Call::make(it.second.type(), "fold", {it.second}, Call::PureExtern);
        }
        r.rhs = substitute(binding, r.rhs);
    }
    */

    /*
    {
        Var x("x"), y("y"), z("z"), c0("c0"), c1("c1");
        map<string, Expr> binding;
        Expr la = ((min((min(x, c0) + y), z) + c1) <= y);
        Expr lb = (((x + y) + c0) <= y);
        std::cerr << more_general_than(lb, la, binding) << "\n";
        return 1;
    }
    */

    // Remove all fold operations
    for (Rule &r : rules) {
        r.rhs = remove_folds(r.rhs);
    }

    // Normalize LE rules to LT rules where it's possible to invert the RHS for free
    for (Rule &r : rules) {
        if (const LE *lhs = r.lhs.as<LE>()) {
            if (is_const(r.rhs)) {
                r.lhs = (lhs->b < lhs->a);
                r.rhs = simplify(!r.rhs);
            } else if (const LE *rhs = r.rhs.as<LE>()) {
                r.lhs = (lhs->b < lhs->a);
                r.rhs = (rhs->b < rhs->a);
            } else if (const LT *rhs = r.rhs.as<LT>()) {
                r.lhs = (lhs->b < lhs->a);
                r.rhs = (rhs->b <= rhs->a);
            } else if (const EQ *rhs = r.rhs.as<EQ>()) {
                r.lhs = (lhs->b < lhs->a);
                r.rhs = (rhs->a != rhs->b);
            } else if (const NE *rhs = r.rhs.as<NE>()) {
                r.lhs = (lhs->b < lhs->a);
                r.rhs = (rhs->a == rhs->b);
            } else if (const Not *rhs = r.rhs.as<Not>()) {
                r.lhs = (lhs->b < lhs->a);
                r.rhs = rhs->a;
            }
        }
    }

    // Reinject folds
    for (Rule &r : rules) {
        r.rhs = inject_folds(r.rhs);
    }

    // Generate all commutations
    vector<Rule> expanded;
    for (const Rule &r : rules) {
        auto e = generate_commuted_variants(r);
        assert(!e.empty());
        expanded.insert(expanded.end(), e.begin(), e.end());
    }
    rules.swap(expanded);


    std::map<IRNodeType, vector<Expr>> good_ones;

    // Sort the rules by LHS
    std::sort(rules.begin(), rules.end(),
              [](const Rule &r1, const Rule &r2) {
                  if (IRDeepCompare{}(r1.lhs, r2.lhs)) {
                      return true;
                  }
                  if (IRDeepCompare{}(r2.lhs, r1.lhs)) {
                      return false;
                  }
                  return IRDeepCompare{}(r1.predicate, r2.predicate);
              });

    Expr last_lhs, last_predicate;
    for (const Rule &r : rules) {
        bool bad = false;

        if (last_lhs.defined() &&
            equal(r.lhs, last_lhs) &&
            equal(r.predicate, last_predicate)) {
            continue;
        }
        last_lhs = r.lhs;
        last_predicate = r.predicate;

        if (!(valid_reduction_order(r.lhs, r.rhs))) {
            std::cout << "Rule doesn't obey reduction order: " << r.lhs << " -> " << r.rhs << "\n";
            continue;
        }

        // Check for failed predicate synthesis
        if (is_zero(r.predicate)) {
            std::cout << "False predicate: " << r.orig << "\n";
            continue;
        }

        // Check for implicit rules
        auto vars = find_vars(r.rhs);
        for (const auto &p : vars) {
            if (!expr_uses_var(r.lhs, p.first)) {
                std::cout << "Implicit rule: " << r.orig << "\n";
                bad = true;
                break;
            }
        }
        if (bad) continue;

        // Check if this rule is dominated by another rule
        for (const Rule &r2 : rules) {
            map<string, Expr> binding;
            if (more_general_than(r2.lhs, r.lhs, binding) &&
                can_prove(r2.predicate || substitute(binding, !r.predicate))) {
                std::cout << "Too specific: " << r.orig << "\n variant " << r.lhs << "\n vs " << r2.orig << "\n variant " << r2.lhs << "\n";;

                // Would they also annihilate in the other order?
                binding.clear();
                if (more_general_than(r.lhs, r2.lhs, binding) &&
                    can_prove(r.predicate || substitute(binding, !r2.predicate))) {
                    bad = &r < &r2; // Arbitrarily pick the one with the lower memory address.
                } else {
                    bad = true;
                }
                break;
            }
        }
        if (bad) continue;

        /*
        // Add the additional extreme constraint that at least one var is entirely eliminated
        if (find_vars(r.rhs).size() >= find_vars(r.lhs).size()) {
            std::cout << "Doesn't eliminate a var: " << r.lhs << " -> " << r.rhs << "\n";
            continue;
        }
        */

        // We have a reasonable rule
        std::cout << "Good rule: rewrite(" << r.lhs << ", " << r.rhs << ", " << r.predicate << ")\n";
        vector<Expr> args = {r.lhs, r.rhs};
        if (!is_one(r.predicate)) {
            args.push_back(r.predicate);
        }
        good_ones[r.lhs.node_type()].push_back(Call::make(Int(32), "rewrite", args, Call::Extern));
    }

    std::cout << "Generated rules:\n";
    for (auto it : good_ones) {
        std::ostringstream filename;
        filename << "Simplify_" << it.first << ".inc";
        std::cout << it.first << "\n";
        std::ofstream of;
        of.open(filename.str().c_str());
        for (auto r : it.second) {
            of << r << " ||\n";
            std::cout << " " << r << "\n";
        }
        of.close();
    }

    return 0;
}
