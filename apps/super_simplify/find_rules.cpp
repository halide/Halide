#include "Halide.h"

#include <fstream>

#include "expr_util.h"
#include "parser.h"
#include "synthesize_predicate.h"
#include "super_simplify.h"

using namespace Halide;
using namespace Halide::Internal;

using std::string;
using std::vector;
using std::map;
using std::set;
using std::pair;
using std::ostringstream;
using std::tuple;

Var v0("x"), v1("y"), v2("z"), v3("w"), v4("u"), v5("v5"), v6("v6"), v7("v7"), v8("v8"), v9("v9");
Var v10("v10"), v11("v11"), v12("v12"), v13("v13"), v14("v14"), v15("v15"), v16("v16"), v17("v17"), v18("v18"), v19("v19");
Var v20("v20"), v21("v21"), v22("v22"), v23("v23"), v24("v24"), v25("v25"), v26("v26"), v27("v27"), v28("v28"), v29("v29");

// Enumerate all possible patterns that would match any portion of the
// given expression.
vector<Expr> all_possible_lhs_patterns(const Expr &e) {
    // Convert the expression to a DAG
    class DAGConverter : public IRMutator {
    public:

        using IRMutator::mutate;

        int current_parent = -1;

        Expr mutate(const Expr &e) override {
            if (building.empty()) {
                int current_id = (int)id_for_expr.size();
                auto it = id_for_expr.emplace(e, current_id);
                bool unseen = it.second;
                current_id = it.first->second;

                if (unseen) {
                    if (expr_for_id.size() < id_for_expr.size()) {
                        expr_for_id.resize(id_for_expr.size());
                        children.resize(id_for_expr.size());
                    }
                    expr_for_id[current_id] = e;
                    int old_parent = current_parent;
                    current_parent = current_id;
                    IRMutator::mutate(e);
                    current_parent = old_parent;
                }

                if (current_parent != -1) {
                    children[current_parent].insert(current_id);
                }

                return e;
            } else {
                // Building a subexpr
                auto it = id_for_expr.find(e);
                assert(it != id_for_expr.end());
                if (building.count(it->second)) {
                    return IRMutator::mutate(e);
                } else {
                    int new_id = (int)renumbering.size();
                    new_id = renumbering.emplace(it->second, new_id).first->second;
                    // We're after end
                    const char *names[] = {"x", "y", "z", "w", "u", "v"};
                    string name = "v" + std::to_string(new_id);
                    if (new_id >= 0 && new_id < 6) {
                        name = names[new_id];
                    }
                    return Variable::make(e.type(), name);
                }
            }
        }

        // Map between exprs and node ids
        map<Expr, int, IRDeepCompare> id_for_expr;
        vector<Expr> expr_for_id;
        // The DAG structure. Every node has outgoing edges (child
        // nodes) and incoming edges (parent nodes).
        vector<set<int>> children;

        // The current expression being built
        set<int> building;
        map<int, int> renumbering;

        bool may_add_to_frontier(const set<int> &rejected, const set<int> &current, int n) {
            if (rejected.count(n)) return false;
            if (current.count(n)) return false;
            if (expr_for_id[n].as<Variable>()) return false;
            return true;
        }

        vector<Expr> result;

        // Generate all subgraphs of a directed graph
        void generate_subgraphs(const set<int> &rejected,
                                const set<int> &current,
                                const set<int> &frontier)  {
            // Pick an arbitrary frontier node to consider
            int v = -1;
            for (auto n : frontier) {
                if (may_add_to_frontier(rejected, current, n)) {
                    v = n;
                    break;
                }
            }

            if (v == -1) {
                if (!current.empty()) {
                    building = current;
                    renumbering.clear();
                    Expr pat = mutate(expr_for_id[*(building.begin())]);
                    // Apply some rejection rules
                    if (building.size() <= 1 || renumbering.size() > 6) {
                        // Too few inner nodes or too many wildcards
                    } else {
                        result.push_back(pat);
                    }
                }
                return;
            }

            const set<int> &ch = children[v];

            set<int> r = rejected, c = current, f = frontier;

            f.erase(v);

            bool must_include = false; //is_const(expr_for_id[v]);
            bool may_include = true; //!is_const(expr_for_id[v]);
            if (!must_include) {
                // Generate all subgraphs with this frontier node not
                // included (replaced with a variable).
                r.insert(v);

                // std::cout << "Excluding " << expr_for_id[v] << "\n";
                generate_subgraphs(r, c, f);
            }

            // Generate all subgraphs with this frontier node included
            if (may_include && (must_include || c.size() < 12)) { // Max out at some number of unique nodes
                c.insert(v);
                for (auto n : ch) {
                    if (may_add_to_frontier(rejected, current, n)) {
                        f.insert(n);
                    }
                }
                // std::cout << "Including " << expr_for_id[v] << "\n";
                generate_subgraphs(rejected, c, f);
            }
        }
    } all_subexprs;

    all_subexprs.mutate(e);

    // Enumerate all sub-dags
    set<int> rejected, current, frontier;
    frontier.insert(0);
    for (int i = 0; i < (int)all_subexprs.children.size(); i++) {
        // Don't consider leaves for roots. We can't simplify "x" or
        // "3".
        if (all_subexprs.children[i].empty()) continue;
        frontier.insert(i);
        all_subexprs.generate_subgraphs(rejected, current, frontier);
        frontier.clear();
    }

    return all_subexprs.result;
}

// Compute some basic information about an Expr: op counts, variables
// used, etc.
class CountOps : public IRGraphVisitor {
    using IRGraphVisitor::visit;
    using IRGraphVisitor::include;

    void visit(const Variable *op) override {
        if (op->type != Int(32) && op->type != Bool()) {
            has_unsupported_ir = true;
        } else if (vars_used.count(op->name)) {
            has_repeated_var = true;
        } else {
            if (op->type == Bool()) {
                has_bool_var = true;
            }
            vars_used.insert(op->name);
        }
    }

    void visit(const Div *op) override {
        has_div_mod = true;
        if (!is_const(op->b)) {
            // z3 isn't going to be able to do anything with this
            has_unsupported_ir = true;
        }
        IRGraphVisitor::visit(op);
    }

    void visit(const Mod *op) override {
        has_div_mod = true;
        if (!is_const(op->b)) {
            // z3 isn't going to be able to do anything with this
            has_unsupported_ir = true;
        }
        IRGraphVisitor::visit(op);
    }

    void visit(const Call *op) override {
        has_unsupported_ir = true;
    }

    void visit(const Cast *op) override {
        has_unsupported_ir = true;
    }

    void visit(const Load *op) override {
        has_unsupported_ir = true;
    }

    set<Expr, IRDeepCompare> unique_exprs;

public:

    void include(const Expr &e) override {
        if (is_const(e)) {
            num_constants++;
        } else {
            unique_exprs.insert(e);
            IRGraphVisitor::include(e);
        }
    }

    int count() {
        return unique_exprs.size() - (int)vars_used.size();
    }

    int num_constants = 0;

    bool has_div_mod = false;
    bool has_unsupported_ir = false;
    bool has_repeated_var = false;
    bool has_bool_var = false;
    set<string> vars_used;
};

// Replace all integer constants with wildcards
class ReplaceConstants : public IRMutator {
    using IRMutator::visit;
    Expr visit(const IntImm *op) override {
        auto it = bound_values.find(op->value);
        // Assume repeated instance of the same var is the same
        // wildcard var. If we have rules where that isn't true we'll
        // need to see examples where the values differ.
        if (it == bound_values.end()) {
            string name = "c" + std::to_string(counter++);
            binding[name] = op;
            Expr v = Variable::make(op->type, name);
            bound_values[op->value] = v;
            return v;
        } else {
            return it->second;
        }
    }
    Expr visit(const Variable *op) override {
        free_vars.insert(op->name);
        return op;
    }

    map<int64_t, Expr> bound_values;
    // TODO: float constants

public:
    int counter = 0;
    map<string, Expr> binding;
    set<string> free_vars;
};


int main(int argc, char **argv) {
    if (argc < 2) {
        std::cout << "Usage: ./find_rules halide_exprs.txt\n";
        return 0;
    }

    // Generate LHS patterns from raw exprs
    vector<Expr> exprs = parse_halide_exprs_from_file(argv[1]);

    // Try to load a blacklist of patterns to skip over that are known
    // to fail. Delete the blacklist whenever you make a change that
    // might make things work for more expressions.
    set<Expr, IRDeepCompare> blacklist;
    if (file_exists("blacklist.txt")) {
        auto b = parse_halide_exprs_from_file("blacklist.txt");
        blacklist.insert(b.begin(), b.end());
    }
    std::cout << blacklist.size() << " blacklisted patterns\n";

    for (auto b : blacklist) {
        std::cout << b << "\n";
    }

    map<Expr, int, IRDeepCompare> patterns_without_constants;

    set<Expr, IRDeepCompare> patterns;
    size_t handled = 0, total = 0;
    for (auto &e : exprs) {
        e = substitute_in_all_lets(e);
        Expr orig = e;
        e = simplify(e);
        Expr second = simplify(e);
        while (!equal(e, second)) {
            std::cerr << "Warning: Expression required multiple applications of the simplifier:\n"
                      << e << " -> " << second << "\n";
            e = second;
            second = simplify(e);
        }
        std::cout << "Simplified: " << e << "\n";
        total++;
        if (is_one(e)) {
            handled++;
        } else {
            {
                ReplaceConstants replacer;
                int count = patterns_without_constants[replacer.mutate(e)]++;
                // We don't want tons of exprs that are the same except for different constants
                if (count > 10) {
                    std::cout << "Skipping. Already seen it too many times\n";
                    continue;
                }
            }

            for (auto p : all_possible_lhs_patterns(e)) {
                // We prefer LT rules to LE rules. The LE simplifier just redirects to the LT simplifier.
                /*
                  if (const LE *le = p.as<LE>()) {
                  p = le->b < le->a;
                  }
                */

                if (!blacklist.count(p) && !patterns.count(p)) {
                    ReplaceConstants replacer;
                    int count = patterns_without_constants[replacer.mutate(p)]++;
                    if (count < 10) {
                        // We don't need tons of examples of the same
                        // rule with different constants.
                        patterns.insert(p);
                    }
                }
            }
        }
    }

    std::cout << patterns.size() << " candidate lhs patterns generated \n";

    std::cout << handled << " / " << total << " rules already simplify to true\n";

    // Generate rules from patterns
    vector<std::future<void>> futures;
    ThreadPool<void> pool;
    std::mutex mutex;
    vector<pair<Expr, Expr>> rules;
    int done = 0;

    {
        std::lock_guard<std::mutex> lock(mutex);
        for (int lhs_ops = 1; lhs_ops < 5; lhs_ops++) {
            for (auto p : patterns) {
                CountOps count_ops;
                count_ops.include(p);

                if (count_ops.count() != lhs_ops ||
                    count_ops.has_unsupported_ir ||
                    !(count_ops.has_repeated_var ||
                      count_ops.num_constants > 2)) {
                    continue;
                }

                // HACK while testing bool vars
                // if (!count_ops.has_bool_var) continue;

                std::cout << "PATTERN " << lhs_ops << " : " << p << "\n";
                futures.emplace_back(pool.async([=, &mutex, &rules, &futures, &done]() {
                            Expr e;
                            // Try something dumb first before using the CEGIS hammer
                            for (Expr r : generate_reassociated_variants(p)) {
                                // Is there already a simplifier rule that handles some
                                // reassociation of this expression?
                                Expr simpler_r = simplify(r);
                                CountOps counter;
                                counter.include(simpler_r);
                                if (counter.count() < lhs_ops) {
                                    std::lock_guard<std::mutex> lock(mutex);
                                    std::cout << "Skipping CEGIS - we managed to simplify a reassociation: "
                                              << p << " -> "
                                              << r << " -> "
                                              << simpler_r << "\n";
                                    e = simpler_r;
                                    break;
                                }
                            }

                            for (int budget = 0; !e.defined() && budget < lhs_ops && budget <= 8 - lhs_ops; budget++) {
                                e = super_simplify(p, budget);
                            }
                            bool success = false;
                            {
                                std::lock_guard<std::mutex> lock(mutex);
                                if (e.defined()) {
                                    bool suppressed = false;
                                    for (auto &r : rules) {
                                        if (more_general_than(r.first, p)) {
                                            std::cout << "Ignoring specialization of earlier rule\n";
                                            suppressed = true;
                                            break;
                                        }
                                        if (more_general_than(p, r.first)) {
                                            std::cout << "Replacing earlier rule with this more general form:\n"
                                                      << "{" << p << ", " << e << "},\n";
                                            r.first = p;
                                            r.second = e;
                                            suppressed = true;
                                            break;
                                        }
                                    }
                                    if (!suppressed) {
                                        std::cout << "RULE: " << p << " = " << e << "\n";
                                        rules.emplace_back(p, e);
                                        success = true;
                                    }
                                }
                                done++;
                                if (done % 100 == 0) {
                                    std::cout << done << " / " << futures.size() << "\n";
                                }
                                if (!success) {
                                    // Add it to the blacklist so we
                                    // don't waste time on this
                                    // pattern again. Delete the
                                    // blacklist whenever you make a
                                    // change that might make things
                                    // work for new patterns.
                                    std::ofstream b;
                                    b.open("blacklist.txt", std::ofstream::out | std::ofstream::app);
                                    b << p << "\n";
                                }
                            }
                        }));
            }
        }
    }

    for (auto &f : futures) {
        f.get();
    }

    // Filter rules, though specialization should not have snuck through the filtering above
    vector<pair<Expr, Expr>> filtered;

    for (auto r1 : rules) {
        bool duplicate = false;
        pair<Expr, Expr> suppressed_by;
        for (auto r2 : rules) {
            bool g = more_general_than(r2.first, r1.first) && !equal(r1.first, r2.first);
            if (g) {
                suppressed_by = r2;
            }
            duplicate |= g;
        }
        if (!duplicate) {
            filtered.push_back(r1);
        } else {
            // std::cout << "This LHS: " << r1.first << " was suppressed by this LHS: " << suppressed_by.first << "\n";
        }
    }

    std::sort(filtered.begin(), filtered.end(), [](const pair<Expr, Expr> &r1, const pair<Expr, Expr> &r2) {
            return IRDeepCompare{}(r1.first, r2.first);
        });

    // Now try to generalize rules involving constants by replacing constants with wildcards and synthesizing a predicate.

    vector<tuple<Expr, Expr, Expr>> predicated_rules;
    vector<pair<Expr, Expr>> failed_predicated_rules;

    // Abstract away the constants and cluster the rules by LHS structure
    map<Expr, vector<map<string, Expr>>, IRDeepCompare> generalized;

    for (auto r : filtered) {
        std::cout << "Trying to generalize " << r.first << " -> " << r.second << "\n";
        Expr orig = r.first == r.second;
        ReplaceConstants replacer;
        r.first = replacer.mutate(r.first);
        r.second = replacer.mutate(r.second);
        std::cout << "Generalized LHS: " << r.first << "\n";
        if (replacer.counter == 0) {
            // No need to generalize this one
            predicated_rules.emplace_back(r.first, r.second, const_true());
        } else {
            generalized[r.first == r.second].emplace_back(std::move(replacer.binding));
        }
    }

    futures.clear();

    for (auto it : generalized) {
        ///futures.emplace_back(pool.async([=, &mutex, &predicated_rules, &failed_predicated_rules]() {
        auto work = [=, &mutex, &predicated_rules, &failed_predicated_rules]() {
                    const EQ *eq = it.first.as<EQ>();
                    assert(eq);
                    Expr lhs = eq->a, rhs = eq->b;
                    map<string, Expr> binding;
                    Expr predicate = synthesize_predicate(lhs, rhs, it.second, &binding);

                    if (!predicate.defined()) {
                        std::lock_guard<std::mutex> lock(mutex);
                        failed_predicated_rules.emplace_back(lhs, rhs);
                        return;
                    }

                    lhs = substitute(binding, lhs);

                    // In the RHS, we want to wrap fold() around computed combinations of the constants
                    for (auto &it : binding) {
                        if (!is_const(it.second) && !it.second.as<Variable>()) {
                            it.second = Call::make(it.second.type(), "fold", {it.second}, Call::PureExtern);
                        }
                    }

                    rhs = substitute(binding, rhs);

                    // After doing the substitution we might be able
                    // to statically fold (e.g. we may get c0 + 0).
                    class SimplifyFolds : public IRMutator {
                        using IRMutator::visit;

                        Expr visit(const Call *op) override {
                            if (op->name == "fold") {
                                Expr e = simplify(op->args[0]);
                                if (is_const(e) || e.as<Variable>()) {
                                    return e;
                                } else {
                                    return Call::make(op->type, "fold", {e}, Call::PureExtern);
                                }
                            } else {
                                return IRMutator::visit(op);
                            }
                        }
                    } simplify_folds;
                    rhs = simplify_folds.mutate(rhs);

                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        predicated_rules.emplace_back(lhs, rhs, predicate);
                        std::cout << "PREDICATED RULE: " << predicate << " => " << lhs << " = " << rhs << "\n";
                    }
                    };
        work();
                    //                }));
    }
    /*
    for (auto &f : futures) {
        f.get();
    }
    */

    for (auto r : failed_predicated_rules) {
        std::cout << "Failed to synthesize a predicate for rule: "
                  << r.first << " -> " << r.second
                  << " from these instances:\n";
        Expr eq = r.first == r.second;
        const vector<map<string, Expr>> &examples = generalized[eq];
        for (const auto &e : examples) {
            std::cout << "FAILED: " << substitute(e, eq) << "\n";
        }
    }

    // Filter again, now that constants are gone.
    vector<tuple<Expr, Expr, Expr>> predicated_filtered;

    for (auto r1 : predicated_rules) {
        bool duplicate = false;
        tuple<Expr, Expr, Expr> suppressed_by;
        Expr lhs1 = std::get<0>(r1);
        for (auto r2 : predicated_rules) {
            Expr lhs2 = std::get<0>(r2);
            bool g = more_general_than(lhs2, lhs1) && !equal(lhs1, lhs2);
            if (g) {
                suppressed_by = r2;
            }
            duplicate |= g;
        }
        if (!duplicate) {
            predicated_filtered.push_back(r1);
        } else {
            // std::cout << "This LHS: " << r1.first << " was suppressed by this LHS: " << suppressed_by.first << "\n";
        }
    }

    std::sort(predicated_filtered.begin(), predicated_filtered.end(),
              [](const tuple<Expr, Expr, Expr> &r1, const tuple<Expr, Expr, Expr> &r2) {
                  return IRDeepCompare{}(std::get<0>(r1), std::get<0>(r2));
              });

    IRNodeType old = IRNodeType::IntImm;
    for (auto r : predicated_filtered) {
        Expr lhs = std::get<0>(r);
        Expr rhs = std::get<1>(r);
        Expr predicate = std::get<2>(r);
        IRNodeType t = lhs.node_type();
        if (t != old) {
            std::cout << t << ":\n";
            old = t;
        }
        if (is_one(predicate)) {
            std::cout << "    rewrite(" << lhs << ", " << rhs << ") ||\n";
        } else {
            std::cout << "    rewrite(" << lhs << ", " << rhs << ", " << predicate << ") ||\n";
        }
    }


    return 0;
}
