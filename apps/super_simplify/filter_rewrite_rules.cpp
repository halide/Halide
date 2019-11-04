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



vector<Rule> generate_commuted_variants(const Rule &r) {
    FindCommutativeOps finder;
    r.lhs.accept(&finder);

    vector<Expr> lhs;
    lhs.push_back(r.lhs);

    for (const Expr &e : finder.commutative_ops) {
        Commute commuter(e);
        vector<Expr> new_lhs = lhs;
        for (const Expr &l : lhs) {
            new_lhs.push_back(commuter.mutate(l));
        }
        lhs.swap(new_lhs);
    }

    vector<Rule> result;
    for (const Expr &l : lhs) {
        Rule r2 = r;
        r2.lhs = l;
        result.push_back(r2);
    }
    return result;
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
        Var x("x"), y("y"), z("z"), c0("c0"), c1("c1");
        map<string, Expr> binding;
        Expr la = ((min((min(x, c0) + y), z) + c1) <= y);
        Expr lb = (((x + y) + c0) <= y);
        std::cerr << more_general_than(lb, la, binding) << "\n";
        return 1;
    }
    */

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

    // Generate all commutations
    vector<Rule> expanded;
    for (const Rule &r : rules) {
        auto e = generate_commuted_variants(r);
        assert(!e.empty());
        expanded.insert(expanded.end(), e.begin(), e.end());
    }
    rules.swap(expanded);

    // Sort the rules by LHS
    std::sort(rules.begin(), rules.end(),
              [](const Rule &r1, const Rule &r2) {
                  return IRDeepCompare{}(r1.lhs, r2.lhs);
              });

    std::map<IRNodeType, vector<Expr>> good_ones;

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
