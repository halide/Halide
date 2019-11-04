#include "Halide.h"
#include "parser.h"
#include "expr_util.h"
#include "synthesize_predicate.h"
#include "reduction_order.h"

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

    for (Rule &r: rules) {
        if (!(valid_reduction_order(r.lhs, r.rhs))) {
            std::cout << "Rule doesn't obey reduction order: " << r.orig << "\n";
        } else {
            std::cout << "Rule is consistent with reduction order: " << r.orig << "\n";
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
        Var x("x"), y("y"), c0("c0"), c1("c1");
        map<string, Expr> binding;
        Expr la = ((x < (y/c0)) && ((y/c0) < (x + c1)));
        Expr lb = ((x < y) && (y < (x + c0)));
        std::cerr << more_general_than(lb, la, binding) << "\n";
        return 1;
    }
    */

    std::map<IRNodeType, vector<Expr>> good_ones;

    Expr last;
    for (const Rule &r : rules) {
        bool bad = false;

        // Ignore duplicates. We've already sorted the rules, so they'll be adjacent.
        if (last.defined() && equal(r.orig, last)) continue;
        last = r.orig;

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
            if (r.orig.same_as(r2.orig)) continue;

            map<string, Expr> binding;
            if (more_general_than(r2.lhs, r.lhs, binding) &&
                can_prove(r2.predicate || substitute(binding, !r.predicate))) {
                std::cout << "Too specific: " << r.orig << " vs " << r2.orig << "\n";
                bad = true;
                break;
            }
        }
        if (bad) continue;

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
        std::cout << it.first << "\n";
        for (auto r : it.second) {
            std::cout << " " << r << " ||\n";
        }
    }

    return 0;
}
