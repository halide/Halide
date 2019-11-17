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
    for (Rule &r : rules) {
        vector<map<string, Expr>> examples;
        map<string, Expr> binding;
        if (is_zero(r.predicate)) {
            std::cout << "Re-synthesizing predicate for " << r.orig << " with a larger beam size\n";
            int bs = 16;
            while (bs <= 64 && is_zero(r.predicate)) {
                binding.clear();
                r.predicate = synthesize_predicate(r.lhs, r.rhs, examples, &binding, bs);
                bs *= 2;
            }
            r.lhs = substitute(binding, r.lhs);

            for (auto &it: binding) {
                it.second = Call::make(it.second.type(), "fold", {it.second}, Call::PureExtern);
            }
            r.rhs = substitute(binding, r.rhs);
        }
    }

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


    std::map<IRNodeType, vector<Rule>> good_ones;

    class TopLevelNodeTypes : public IRMutator {
        int depth = 0;
    public:
        using IRMutator::mutate;
        Expr mutate(const Expr &e) {
            if (depth >= 2) {
                return e;
            }
            result.push_back(e.node_type());
            depth++;
            IRMutator::mutate(e);
            depth--;
            return e;
        }
        vector<IRNodeType> result;
    };

    // Sort the rules by LHS
    std::sort(rules.begin(), rules.end(),
              [](const Rule &r1, const Rule &r2) {
                  TopLevelNodeTypes t1, t2;
                  t1.mutate(r1.lhs);
                  t2.mutate(r2.lhs);
                  if (t1.result.size() < t2.result.size()) {
                      return true;
                  }
                  if (t2.result.size() < t1.result.size()) {
                      return false;
                  }
                  for (size_t i = 0; i < t1.result.size(); i++) {
                      if (t1.result[i] < t2.result[i]) {
                          return true;
                      }
                      if (t2.result[i] < t1.result[i]) {
                          return false;
                      }
                  }
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

        // Check for failed predicate synthesis
        if (is_zero(r.predicate)) {
            std::cout << "False predicate: " << r.orig << "\n";
            continue;
        }

        if (!(valid_reduction_order(r.lhs, r.rhs))) {
            std::cout << "Rule doesn't obey reduction order: " << r.lhs << " -> " << r.rhs << "\n";
            continue;
        }
        if (valid_reduction_order(r.rhs, r.lhs)) {
            std::cerr << "Rule would be valid reduction order in either direction. There must be a bug in the reduction order:\n"
                      << r.lhs << " -> " << r.rhs << "\n";
            abort();
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
        good_ones[r.lhs.node_type()].push_back(r);
    }

    std::cout << "Generated rules:\n";
    for (auto it : good_ones) {
        std::ostringstream os;
        IRNodeType last_a_type = IRNodeType::Variable, last_b_type = IRNodeType::Variable;
        bool first_line = true;
        (void)first_line;
        for (auto r : it.second) {
            TopLevelNodeTypes t;
            t.mutate(r.lhs);
            IRNodeType a_type = t.result.size() > 0 ? t.result[1] : IRNodeType::Variable;
            IRNodeType b_type = t.result.size() > 1 ? t.result[2] : IRNodeType::Variable;

            if (b_type != last_b_type && last_b_type != IRNodeType::Variable) {
                // close out the b type bucket
                os << "))";
            }

            if (a_type != last_a_type && last_a_type != IRNodeType::Variable) {
                os << "))";
            }

            if (!first_line) {
                os << " ||\n";
            }
            first_line = false;

            if (a_type != last_a_type && a_type != IRNodeType::Variable) {
                // Open a new a bucket
                os << "((a.node_type() == IRNodeType::" << a_type << ") && (\n";
            }

            if (b_type != last_b_type && b_type != IRNodeType::Variable) {
                // open a new b type bucket
                os << "((b.node_type() == IRNodeType::" << b_type << ") && (\n";
            }

            last_a_type = a_type;
            last_b_type = b_type;

            vector<Expr> args = {r.lhs, r.rhs};
            if (!is_one(r.predicate)) {
                args.push_back(r.predicate);
            }
            Expr rule_expr = Call::make(Int(32), "rewrite", args, Call::Extern);

            os << " " << rule_expr;
        }

        if (last_b_type != IRNodeType::Variable) {
            os << "))";
        }

        if (last_a_type != IRNodeType::Variable) {
            os << "))";
        }

        std::cout << os.str() << "\n";

        std::ostringstream filename;
        filename << "Simplify_" << it.first << ".inc";
        std::cout << it.first << "\n";
        std::ofstream of;
        of.open(filename.str().c_str());
        of << os.str();
        of.close();
    }

    return 0;
}
