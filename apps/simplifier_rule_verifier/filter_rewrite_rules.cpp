#include "Halide.h"
#include "debug.h"
#include "expr_util.h"
#include "halide_thread_pool.h"
#include "parser.h"
#include "reduction_order.h"
#include "z3.h"

#include <atomic>
#include <fstream>
#include <future>
#include <limits>
#include <mutex>

using namespace Halide;
using namespace Halide::Internal;
using Halide::Tools::ThreadPool;

// Take a list of rewrite rules and classify them by root IR node, and
// what problems they might have that require further investigation.

struct Rule {
    Expr lhs, rhs, predicate;
    Expr orig;
    // Set if z3 found a counterexample to this rule.
    bool incorrect = false;
};

using std::map;
using std::set;
using std::string;
using std::vector;

// Canonicalize ordering of children in commutative ops
class Canonicalizer : public IRMutator {
    using IRMutator::visit;

public:
    using IRMutator::mutate;

private:
    template<typename Op>
    Expr visit_commutative_op(const Op *op) {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);
        IRNodeType a_node_type = a.node_type();
        IRNodeType b_node_type = b.node_type();
        const Call *call_a = a.as<Call>();
        const Call *call_b = b.as<Call>();
        const Variable *var_a = a.as<Variable>();
        const Variable *var_b = b.as<Variable>();
        bool a_is_const = (is_const(a) ||
                           (var_a && var_a->name[0] == 'c') ||
                           (call_a && call_a->name == "fold"));
        if (a_is_const) {
            a_node_type = IRNodeType::IntImm;
        }
        bool b_is_const = (is_const(b) ||
                           (var_b && var_b->name[0] == 'c') ||
                           (call_b && call_b->name == "fold"));
        if (b_is_const) {
            b_node_type = IRNodeType::IntImm;
        }
        bool should_commute = ((a_is_const && !b_is_const) ||
                               (!var_a && !var_b && a_node_type < b_node_type));
        if (should_commute) {
            return Op::make(b, a);
        } else {
            return Op::make(a, b);
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
    Expr visit(const And *op) override {
        return visit_commutative_op(op);
    }
    Expr visit(const Or *op) override {
        return visit_commutative_op(op);
    }
    Expr visit(const EQ *op) override {
        return visit_commutative_op(op);
    }
};

// The Levenshtein distance between two strings.
size_t edit_distance(const string &a, const string &b) {
    // d[i][j] is the distance between the first i characters of a and the
    // first j characters of b.
    vector<vector<size_t>> d(a.size() + 1, vector<size_t>(b.size() + 1));
    for (size_t i = 0; i <= a.size(); i++) {
        d[i][0] = i;
    }
    for (size_t j = 0; j <= b.size(); j++) {
        d[0][j] = j;
    }
    for (size_t i = 1; i <= a.size(); i++) {
        for (size_t j = 1; j <= b.size(); j++) {
            d[i][j] = std::min({d[i - 1][j] + 1,
                                d[i][j - 1] + 1,
                                d[i - 1][j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        }
    }
    return d[a.size()][b.size()];
}

string expr_to_rpn_string(const Expr &e) {
    class VisitLeaves : public IRMutator {
        using IRMutator::visit;
        Expr visit(const Variable *op) override {
            ss << op->name;
            return op;
        }

    public:
        using IRMutator::mutate;
        Expr mutate(const Expr &e) override {
            if (is_const(e) || e.as<Variable>()) {
                IRMutator::mutate(e);
            } else {
                ss << "(";
                IRMutator::mutate(e);
                ss << ")";
            }
            return e;
        }

        std::stringstream ss;
    } visit_leaves;
    visit_leaves.mutate(e);
    return visit_leaves.ss.str();
}

vector<Rule> generate_commuted_variants(const Rule &rule) {
    vector<Expr> lhs = generate_commuted_variants(rule.lhs);
    vector<Expr> rhs = generate_reassociated_variants(rule.rhs);

    vector<Rule> result;
    for (Expr l : lhs) {
        l = Canonicalizer().mutate(l);
        Rule r2 = rule;
        r2.lhs = l;
        // Pick the rhs that minimizes edit distance
        string lhs_str = expr_to_rpn_string(l);
        size_t best_edit_distance = std::numeric_limits<size_t>::max();
        for (Expr r : rhs) {
            r = Canonicalizer().mutate(r);
            size_t d = edit_distance(lhs_str, expr_to_rpn_string(r));
            if (d < best_edit_distance) {
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

    public:
        using IRMutator::mutate;

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

class ImplicitPredicate : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Div *op) {
        const Variable *v = op->b.as<Variable>();
        if (v && v->name[0] == 'c') {
            // Legal, but would have folded
            result = result && (op->b != 0) && (op->b != 1) && (op->b != -1);
        }
        IRVisitor::visit(op);
    }

    void visit(const Mul *op) {
        const Variable *v = op->b.as<Variable>();
        if (v && v->name[0] == 'c') {
            // Would have folded
            result = result && (op->b != 0) && (op->b != 1);
        }
        IRVisitor::visit(op);
    }

    void visit(const Mod *op) {
        const Variable *v = op->b.as<Variable>();
        if (v && v->name[0] == 'c') {
            // Would have folded
            result = result && (op->b != 0) && (op->b != 1) && (op->b != -1);
        }
        IRVisitor::visit(op);
    }

public:
    ImplicitPredicate()
        : result(const_true()) {
    }
    Expr result;
};

class MoveNegationInnermost : public IRMutator {
    using IRMutator::visit;

public:
    using IRMutator::mutate;

private:
    Expr visit(const Not *op) override {
        if (const And *and_a = op->a.as<And>()) {
            return mutate(!and_a->a) || mutate(!and_a->b);
        } else if (const Or *or_a = op->a.as<Or>()) {
            return mutate(!or_a->a) && mutate(!or_a->b);
        } else if (const Not *not_a = op->a.as<Not>()) {
            return mutate(not_a->a);
        } else if (const LT *lt = op->a.as<LT>()) {
            return mutate(lt->b <= lt->a);
        } else if (const LE *le = op->a.as<LE>()) {
            return mutate(le->b < le->a);
        } else if (const EQ *eq = op->a.as<EQ>()) {
            return mutate(eq->a != eq->b);
        } else if (const NE *ne = op->a.as<NE>()) {
            return mutate(ne->a == ne->b);
        } else {
            return IRMutator::visit(op);
        }
    }
};

class ToDNF : public IRMutator {
    using IRMutator::visit;

public:
    using IRMutator::mutate;

private:
    Expr visit(const And *op) override {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);
        vector<Expr> as = unpack_binary_op<Or>(a);
        vector<Expr> bs = unpack_binary_op<Or>(b);
        set<Expr, IRDeepCompare> result;
        for (const Expr &a1 : as) {
            for (const Expr &b1 : bs) {
                auto a_clauses = unpack_binary_op<And>(a1);
                auto b_clauses = unpack_binary_op<And>(b1);
                set<Expr, IRDeepCompare> both;
                both.insert(a_clauses.begin(), a_clauses.end());
                both.insert(b_clauses.begin(), b_clauses.end());
                result.insert(pack_binary_op<And>(both));
            }
        }
        return pack_binary_op<Or>(result);
    }

    Expr visit(const EQ *op) override {
        if (op->a.type().is_bool()) {
            return mutate((op->a && op->b) || (!op->a && !op->b));
        } else {
            return IRMutator::visit(op);
        }
    }

    Expr visit(const LE *op) override {
        if (const Min *min_a = op->a.as<Min>()) {
            return mutate(min_a->a <= op->b || min_a->b <= op->b);
        } else if (const Max *max_a = op->a.as<Max>()) {
            return mutate(max_a->a <= op->b && max_a->b <= op->b);
        } else if (const Min *min_b = op->b.as<Min>()) {
            return mutate(op->a <= min_b->a && op->a <= min_b->b);
        } else if (const Max *max_b = op->b.as<Max>()) {
            return mutate(op->a <= max_b->a || op->a <= max_b->b);
        } else {
            return IRMutator::visit(op);
        }
    }

    Expr visit(const LT *op) override {
        if (const Min *min_a = op->a.as<Min>()) {
            return mutate(min_a->a < op->b || min_a->b < op->b);
        } else if (const Max *max_a = op->a.as<Max>()) {
            return mutate(max_a->a < op->b && max_a->b < op->b);
        } else if (const Min *min_b = op->b.as<Min>()) {
            return mutate(op->a < min_b->a && op->a < min_b->b);
        } else if (const Max *max_b = op->b.as<Max>()) {
            return mutate(op->a < max_b->a || op->a < max_b->b);
        } else {
            return IRMutator::visit(op);
        }
    }
};

// Make the first wildcard found x, the second y, etc.
class CanonicalizeVariableNames : public IRMutator {
public:
    using IRMutator::mutate;

private:
    map<string, string> remapping;
    const char *wild_names[6] = {"x", "y", "z", "w", "u", "v"};
    int next_wild = 0;
    int next_constant = 0;

    using IRMutator::visit;

    Expr visit(const Variable *op) override {
        auto it = remapping.find(op->name);
        if (it != remapping.end()) {
            return Variable::make(op->type, it->second);
        } else {
            string n;
            if (op->name[0] == 'c') {
                n = "c" + std::to_string(next_constant);
                next_constant++;
            } else {
                assert(next_wild < 6);
                n = wild_names[next_wild];
                next_wild++;
            }
            remapping[op->name] = n;
            return Variable::make(op->type, n);
        }
    }
};

// Rules that z3 disproved, and rules that z3 couldn't decide either way.
std::atomic<int> incorrect_rules{0}, unverifiable_rules{0};

// Flushes a buffer of a single rule's output to stdout in one piece on
// destruction, so that concurrent checks don't interleave.
struct ScopedFlush {
    std::ostringstream &out;

    explicit ScopedFlush(std::ostringstream &out)
        : out(out) {
    }

    ~ScopedFlush() {
        static std::mutex mutex;
        std::lock_guard<std::mutex> lock(mutex);
        std::cout << out.str() << std::flush;
    }
};

void check_rule(Rule &r) {
    std::ostringstream out;
    ScopedFlush flush_out(out);

    // Check the rule with Z3
    map<string, Expr> mapping;
    ImplicitPredicate imp;
    r.lhs.accept(&imp);
    if (!is_const_zero(r.predicate)) {
        auto result = satisfy(r.predicate && imp.result && r.lhs != r.rhs, &mapping);
        if (result == Z3Result::Unsat) {
            out << "Verified with SMT: rewrite("
                << r.lhs << ", " << r.rhs << ", " << r.predicate << ")\n";
            return;
        } else if (result == Z3Result::Sat) {
            incorrect_rules++;
            r.incorrect = true;
            out << "Incorrect rule: rewrite("
                << r.lhs << ", " << r.rhs << ", " << r.predicate << ")\n";
            if (mapping.empty()) {
                // The simplifier proved the two sides differ without z3's help,
                // so there's no model to report.
                out << "The two sides are never equal\n";
            } else {
                out << "Counterexample is:\n";
                for (const auto &p : mapping) {
                    out << " " << p.first << " = " << p.second << "\n";
                }
                out << "For which LHS = " << simplify(substitute(mapping, r.lhs))
                    << " and RHS = " << simplify(substitute(mapping, r.rhs)) << "\n";
            }
        } else if (result == Z3Result::Unknown) {
            unverifiable_rules++;
            out << "Z3 returned unknown/timeout for: rewrite("
                << r.lhs << ", " << r.rhs << ", " << r.predicate << ")\n";
        }
    }

    if (is_const_zero(r.predicate)) {
        out << "Synthesizing a predicate for " << r.orig << "\n";
        Expr new_predicate = const_true();
        Expr rule_holds = simplify(r.lhs == r.rhs);
        debug(1) << "Rule holds: " << rule_holds << "\n";

        // We can substitute in any old values for the
        // non-constant variables to get a candidate
        // constraint. Let's start with 0/1

        auto vars = find_vars(rule_holds);

        map<string, Expr> all_vars_zero;
        for (const auto &p : vars) {
            if (p.first[0] == 'c') {
                continue;
            }
            all_vars_zero.emplace(p.first, cast(p.second.first.type(), 0));
        }
        vector<Expr> terms;
        terms.push_back(substitute(all_vars_zero, rule_holds));
        for (const auto &p : vars) {
            if (p.first[0] == 'c') {
                continue;
            }
            all_vars_zero[p.first] = cast(p.second.first.type(), 1);
            terms.push_back(simplify(substitute(all_vars_zero, rule_holds)));
            all_vars_zero[p.first] = cast(p.second.first.type(), 0);
        }

        new_predicate = pack_binary_op<And>(terms);

        // Exploit the implicit predicate to clean some terms up
        {
            for (const auto &p : find_vars(new_predicate)) {
                if (p.first[0] == 'c') {
                    Expr v = p.second.first;
                    if (is_const_zero(simplify(v == -1 || v == 0 || v == 1, {}, {}, {imp.result}))) {
                        // This var appears on the RHS of a div or mod
                        new_predicate = substitute(1 % v, 1, new_predicate);
                        new_predicate = substitute(1 / v, 0, new_predicate);
                    }
                    new_predicate = substitute(-1 / v == 0, v == 0, new_predicate);
                    new_predicate = substitute(-1 / v == -1, 0 < v, new_predicate);
                    new_predicate = substitute(-1 / v == 1, v < 0, new_predicate);
                }
            }
            new_predicate = simplify(new_predicate, {}, {}, {imp.result});
        }

        auto lhs_vars = find_vars(r.lhs);
        for (int terms = 0;; terms++) {
            // Try to eliminate constant vars that only occur on the RHS
            {
                for (const auto &v : find_vars(r.rhs)) {
                    if (lhs_vars.count(v.first)) {
                        continue;
                    }
                    for (const Expr &t : unpack_binary_op<And>(new_predicate)) {
                        auto result = solve_expression(t, v.first);
                        if (result.fully_solved) {
                            if (const EQ *eq = result.result.as<EQ>()) {
                                if (equal(eq->a, Variable::make(Int(32), v.first))) {
                                    Expr replacement = simplify(eq->b);
                                    new_predicate = simplify(substitute(v.first, replacement, new_predicate));
                                    r.rhs = substitute(v.first, replacement, r.rhs);
                                    rule_holds = simplify(substitute(v.first, replacement, rule_holds));
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            if (terms > 4) {
                // May be trying to handle an infinite number of cases one term at a time
                debug(1) << "Giving up. Accumulating too many terms\n";

                new_predicate = MoveNegationInnermost().mutate(new_predicate);
                new_predicate = ToDNF().mutate(new_predicate);
                set<Expr, IRDeepCompare> clauses;
                for (auto clause : unpack_binary_op<Or>(new_predicate)) {
                    clause = simplify(clause);
                    if (is_const_zero(clause)) {
                        continue;
                    }
                    clauses.insert(clause);
                }

                debug(1) << "Predicate in DNF form:\n";
                for (const auto &c : clauses) {
                    debug(1) << " " << c << "\n";
                }

                // Right now we have a necessary condition which
                // is a disjunction (i.e. union) of a bunch of
                // clauses. Try to find a subset of the clauses
                // which we can prove are sufficient conditions,
                // and just keep those.
                set<Expr, IRDeepCompare> trimmed_clauses;
                bool any_timeouts = false;
                for (auto c : clauses) {

                    // Aggressively simplify the clause
                    auto terms = unpack_binary_op<And>(c);
                    for (size_t i = 0; i < terms.size(); i++) {
                        for (size_t j = 0; j < terms.size(); j++) {
                            if (i == j) {
                                continue;
                            }
                            terms[j] = simplify(terms[j], {}, {}, {imp.result, terms[i]});
                        }
                    }
                    c = pack_binary_op<And>(terms);

                    map<string, Expr> binding;
                    auto z3_result = satisfy(imp.result && c && !rule_holds, &binding, "checking one clause in DNF", 30);
                    if (z3_result == Z3Result::Sat) {
                        continue;
                    }
                    any_timeouts |= (z3_result != Z3Result::Unsat);
                    trimmed_clauses.insert(c);
                }
                trimmed_clauses.insert(const_false());

                new_predicate = simplify(pack_binary_op<Or>(trimmed_clauses));
                if (any_timeouts && !is_const_zero(new_predicate)) {
                    new_predicate = Call::make(Bool(), "prove_me", {new_predicate}, Call::Extern);
                }
                break;
            }
            Expr there_is_a_failure = simplify(imp.result && new_predicate && !rule_holds);
            map<string, Expr> binding;
            auto z3_result = satisfy(there_is_a_failure, &binding, "checking a predicate for failures", 30);
            if (z3_result == Z3Result::Unsat) {
                // Woo. No failures exist.
                break;
            } else if (z3_result == Z3Result::Sat) {
                Expr new_term = rule_holds;
                for (const auto &p : binding) {
                    if (p.first[0] != 'c') {
                        new_term = substitute(p.first, p.second, new_term);
                    }
                }
                debug(1) << "new_term: " << new_term << "\n";
                new_term = simplify(new_term);
                new_predicate = new_predicate && new_term;
                debug(1) << "new_predicate: " << new_predicate << "\n";
                new_predicate = simplify(new_predicate);
            } else {
                // Couldn't find a failure, so hopefully
                // there aren't any. Would Require human
                // checking though.
                debug(1) << "Z3 Timeout\n";
                // A human will have to prove this by hand
                new_predicate = Call::make(Bool(), "prove_me", {new_predicate}, Call::Extern);
                break;
            }
        }
        debug(1) << "Synthesized predicate: " << new_predicate << "\n";

        // Eliminate constant vars that occur on the LHS
        {
            for (const auto &v : find_vars(r.lhs)) {
                for (const Expr &t : unpack_binary_op<And>(new_predicate)) {
                    auto result = solve_expression(t, v.first);
                    if (result.fully_solved) {
                        if (const EQ *eq = result.result.as<EQ>()) {
                            if (equal(eq->a, Variable::make(Int(32), v.first))) {
                                Expr replacement = simplify(eq->b);
                                const Variable *r_var = replacement.as<Variable>();
                                bool lower_numbered_constant_var =
                                    r_var && (r_var->name[0] == 'c' && (r_var->name[1] < v.first[1]));
                                if (lower_numbered_constant_var || is_const(replacement)) {
                                    new_predicate = simplify(substitute(v.first, replacement, new_predicate));
                                    r.lhs = substitute(v.first, replacement, r.lhs);
                                    r.rhs = substitute(v.first, replacement, r.rhs);
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }

        // Save human attention for things small enough to be tractable - one clause only please.
        if (const Call *c = new_predicate.as<Call>()) {
            if (c->name == "prove_me") {
                if (c->args[0].as<And>()) {
                    new_predicate = const_false();
                }
            }
        }

        if (!can_prove(r.predicate == new_predicate)) {
            out << "Rewrote predicate: " << r.predicate << " -> " << new_predicate << "\n";
            r.predicate = new_predicate;
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cout << "Usage: ./filter_rewrite_rules rewrite_rules.txt [output_dir]\n";
        return 0;
    }

    const string rewrite_rules_path = argv[1];
    // If given, the surviving rules are also written out as one
    // Simplify_<node type>.inc file per root IR node type.
    string output_dir_path = argc >= 3 ? argv[2] : "";
    if (!output_dir_path.empty() && output_dir_path.back() != '/') {
        output_dir_path += "/";
    }

    vector<Expr> exprs_vec = parse_halide_exprs_from_file(rewrite_rules_path);

    // De-dup
    set<Expr, IRDeepCompare> exprs;
    exprs.insert(exprs_vec.begin(), exprs_vec.end());

    vector<Rule> rules;

    for (const Expr &e : exprs) {
        const Call *call = e.as<Call>();
        if (!call || call->name != "rewrite" ||
            call->args.size() < 2 || call->args.size() > 3) {
            std::cerr << "Expr is not a rewrite rule: " << e << "\n";
            return -1;
        }
        // A rule with no predicate is unconditional. A rule with a predicate
        // of false is a request to synthesize one.
        Expr predicate = call->args.size() == 3 ? call->args[2] : const_true();
        rules.emplace_back(Rule{call->args[0], call->args[1], predicate, e});
    }

    // Check the rules, and synthesize predicates for any that ask for one. Each
    // check shells out to z3, so run a few at a time.
    {
        ThreadPool<void> pool;
        vector<std::future<void>> futures;
        for (Rule &r : rules) {
            futures.emplace_back(pool.async([&r]() { check_rule(r); }));
        }
        for (auto &f : futures) {
            f.get();
        }
    }

    std::cout << "Done checking rules\n";

    // Remove all fold operations
    for (Rule &r : rules) {
        r.rhs = remove_folds(r.rhs);
    }

    // Normalize LE rules to LT and NE rules to EQ rules where it's possible to invert the RHS for free
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
        if (const NE *lhs = r.lhs.as<NE>()) {
            if (is_const(r.rhs)) {
                r.lhs = (lhs->b == lhs->a);
                r.rhs = simplify(!r.rhs);
            } else if (const LE *rhs = r.rhs.as<LE>()) {
                r.lhs = (lhs->b == lhs->a);
                r.rhs = (rhs->b < rhs->a);
            } else if (const LT *rhs = r.rhs.as<LT>()) {
                r.lhs = (lhs->b == lhs->a);
                r.rhs = (rhs->b <= rhs->a);
            } else if (const EQ *rhs = r.rhs.as<EQ>()) {
                r.lhs = (lhs->b == lhs->a);
                r.rhs = (rhs->a != rhs->b);
            } else if (const NE *rhs = r.rhs.as<NE>()) {
                r.lhs = (lhs->b == lhs->a);
                r.rhs = (rhs->a == rhs->b);
            } else if (const Not *rhs = r.rhs.as<Not>()) {
                r.lhs = (lhs->b == lhs->a);
                r.rhs = rhs->a;
            }
        }
    }

    // Reinject folds
    for (Rule &r : rules) {
        r.rhs = inject_folds(r.rhs);
    }

    // Any constant wildcard not used in a fold and not used in the
    // predicate can just be a regular wildcard. N.B: We must also
    // check the implicit predicate, because it may have been
    // exploited to generate the existing predicate.

    for (Rule &r : rules) {
        class FindConstants : public IRVisitor {
            using IRVisitor::visit;
            void visit(const Call *op) override {
                if (op->name == "fold") {
                    ScopedValue<bool> old_in_fold(in_fold, true);
                    op->args[0].accept(this);
                } else {
                    IRVisitor::visit(op);
                }
            }
            void visit(const Variable *op) override {
                all.insert(op->name);
                if (in_fold) {
                    used_in_fold.insert(op->name);
                }
            }
            bool in_fold = false;

        public:
            set<string> all, used_in_fold;
        } finder;

        ImplicitPredicate imp;
        r.lhs.accept(&imp);

        Expr e = Call::make(Int(32), "dummy", {r.lhs, r.rhs, Call::make(Bool(), "fold", {r.predicate && imp.result}, Call::Intrinsic)}, Call::Intrinsic);
        e.accept(&finder);

        for (const auto &v : finder.all) {
            if (finder.used_in_fold.count(v) || v[0] != 'c') {
                continue;
            }
            // Find a wildcard name that isn't taken. The rule is rewritten as
            // we go, so check it rather than the copy taken above, otherwise
            // every constant wildcard picks the same name and they collapse
            // into one.
            for (const char *n : {"x", "y", "z", "w", "u", "v"}) {
                if (!expr_uses_var(r.lhs, n) &&
                    !expr_uses_var(r.rhs, n) &&
                    !expr_uses_var(r.predicate, n)) {
                    Expr var = Variable::make(Int(32), n);
                    r.lhs = substitute(v, var, r.lhs);
                    r.rhs = substitute(v, var, r.rhs);
                    break;
                }
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

    map<IRNodeType, vector<Rule>> good_ones;

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

    // Canonicalize the variable name ordering
    for (Rule &r : rules) {
        CanonicalizeVariableNames c;
        r.lhs = c.mutate(r.lhs);
        r.rhs = c.mutate(r.rhs);
        r.predicate = c.mutate(r.predicate);
    }

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

    // Filter the rules for exact duplicates, things that don't obey
    // the reduction order, things with false predicates, and things
    // with constant wildcards on the RHS that weren't bound on the
    // LHS.
    int bad_reduction_order = 0;
    Expr last_lhs, last_predicate;
    vector<Rule> filtered_rules;
    for (const Rule &r : rules) {
        if (last_lhs.defined() &&
            equal(r.lhs, last_lhs) &&
            equal(r.predicate, last_predicate)) {
            continue;
        }

        if (r.incorrect) {
            continue;
        }

        // Check for failed predicate synthesis
        if (is_const_zero(r.predicate)) {
            std::cout << "False predicate: " << r.orig << "\n";
            continue;
        }

        if (!valid_reduction_order(r.lhs, r.rhs)) {
            std::cout << "Rule doesn't obey the reduction order, so it could cause the "
                      << "simplifier to loop forever: " << r.lhs << " -> " << r.rhs << "\n";
            bad_reduction_order++;
            continue;
        }
        if (valid_reduction_order(r.rhs, r.lhs)) {
            std::cout << "Rule would be a valid reduction order in either direction. "
                      << "There must be a bug in the reduction order:\n"
                      << r.lhs << " -> " << r.rhs << "\n";
            bad_reduction_order++;
            continue;
        }

        // Check for implicit rules
        bool bad = false;
        auto vars = find_vars(r.rhs);
        for (const auto &p : vars) {
            if (!expr_uses_var(r.lhs, p.first)) {
                std::cout << "Implicit rule: " << r.orig << "\n";
                bad = true;
                break;
            }
        }
        if (bad) {
            continue;
        }

        last_lhs = r.lhs;
        last_predicate = r.predicate;
        filtered_rules.push_back(r);
    }
    filtered_rules.swap(rules);

    for (const Rule &r : rules) {
        // Check if this rule is dominated by another rule
        bool bad = false;
        for (const Rule &r2 : rules) {
            if (&r == &r2) {
                continue;
            }
            map<string, Expr> binding;
            if (equal(r2.lhs, r.lhs) &&
                equal(r2.predicate, r.predicate)) {
                // It's a straight-up duplicate. Don't bother printing anything.
                bad = &r < &r2;  // Arbitrarily pick the one with the lower memory address.
                break;
            }
            Expr p1 = r.predicate;
            Expr p2 = r2.predicate;
            const Call *c1 = p1.as<Call>();
            const Call *c2 = p2.as<Call>();
            if (c1 && c1->name == "prove_me") {
                p1 = c1->args[0];
            }
            if (c2 && c2->name == "prove_me") {
                p2 = c2->args[0];
            }
            if (more_general_than(r2.lhs, r.lhs, binding) &&
                can_prove(p2 || substitute(binding, !p1))) {
                std::cout << "Too specific: " << r.orig
                          << "\n variant " << r.lhs
                          << "\n vs " << r2.orig
                          << "\n variant " << r2.lhs << "\n";

                // Would they also annihilate in the other order?
                binding.clear();
                if (more_general_than(r.lhs, r2.lhs, binding) &&
                    can_prove(p1 || substitute(binding, !p2))) {
                    bad = &r < &r2;  // Arbitrarily pick the one with the lower memory address.
                } else {
                    bad = true;
                    break;
                }
            }
        }
        if (bad) {
            continue;
        }

        // We have a reasonable rule
        std::cout << "Good rule: rewrite(" << r.lhs << ", " << r.rhs << ", " << r.predicate << ")\n";
        good_ones[r.lhs.node_type()].push_back(r);
    }

    std::cout << "Generated rules:\n";
    for (const auto &it : good_ones) {
        std::cout << "Simplify_" << it.first << ".inc:\n";
        std::ostringstream os;
        const char *separator = "";
        for (const auto &r : it.second) {
            vector<Expr> args = {r.lhs, r.rhs};
            if (!is_const_one(r.predicate)) {
                args.push_back(r.predicate);
            }
            os << separator << " " << Call::make(Int(32), "rewrite", args, Call::Extern);
            separator = " ||\n";
        }
        os << "\n";

        // Clean up bool terms that aren't valid C++ in the simplifier
        string rules_src = os.str();
        rules_src = replace_all(rules_src, "(uint1)0", "false");
        rules_src = replace_all(rules_src, "(uint1)1", "true");
        rules_src = replace_all(rules_src, "prove_me(true)", "prove_me(IRMatcher::Const(1))");
        rules_src = replace_all(rules_src, "(uint1)", "");

        std::cout << rules_src;

        if (!output_dir_path.empty()) {
            std::ostringstream filename;
            filename << output_dir_path << "Simplify_" << it.first << ".inc";
            std::ofstream of(filename.str());
            if (of.fail()) {
                std::cerr << "Unable to open " << filename.str() << "\n";
                return -1;
            }
            of << rules_src;
        }
    }

    if (!output_dir_path.empty()) {
        // Make sure we write a complete set of .inc files, to avoid
        // accidentally mixing and matching between experiments.
        for (IRNodeType t : {IRNodeType::Add,
                             IRNodeType::And,
                             IRNodeType::Div,
                             IRNodeType::EQ,
                             IRNodeType::LE,
                             IRNodeType::LT,
                             IRNodeType::Max,
                             IRNodeType::Min,
                             IRNodeType::Mod,
                             IRNodeType::Mul,
                             IRNodeType::Or,
                             IRNodeType::Select,
                             IRNodeType::Sub}) {
            if (good_ones.count(t)) {
                continue;
            }
            std::ostringstream filename;
            filename << output_dir_path << "Simplify_" << t << ".inc";
            std::ofstream of(filename.str());
            if (of.fail()) {
                std::cerr << "Unable to open " << filename.str() << "\n";
                return -1;
            }
            of << "false";
        }
    }

    std::cout << "\n"
              << incorrect_rules << " rule(s) were disproved by z3\n"
              << unverifiable_rules << " rule(s) could not be verified by z3\n"
              << bad_reduction_order << " rule(s) did not obey the reduction order\n";

    if (incorrect_rules || bad_reduction_order) {
        std::cout << "Failure!\n";
        return 1;
    }

    std::cout << "Success!\n";
    return 0;
}
