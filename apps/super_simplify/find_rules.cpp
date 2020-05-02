#include "Halide.h"

#include <fstream>

#include "expr_util.h"
#include "parser.h"
#include "reduction_order.h"
#include "super_simplify.h"
#include "synthesize_predicate.h"

using namespace Halide;
using namespace Halide::Internal;

using std::map;
using std::ostringstream;
using std::pair;
using std::set;
using std::string;
using std::tuple;
using std::vector;

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
                                const set<int> &frontier) {
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
                    if (building.size() <= 0 || renumbering.size() > 6) {
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

            bool must_include = false;  //is_const(expr_for_id[v]);
            bool may_include = true;    //!is_const(expr_for_id[v]);
            if (!must_include) {
                // Generate all subgraphs with this frontier node not
                // included (replaced with a variable).
                r.insert(v);

                // std::cout << "Excluding " << expr_for_id[v] << "\n";
                generate_subgraphs(r, c, f);
            }

            // Generate all subgraphs with this frontier node included
            if (may_include && (must_include || c.size() < 12)) {  // Max out at some number of unique nodes
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
class CountOps : public IRMutator {
    using IRMutator::visit;

    Expr visit(const Variable *op) override {
        num_var_leaves++;
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
        return op;
    }

    Expr visit(const Div *op) override {
        has_div_mod = true;
        if (!is_const(op->b)) {
            // z3 isn't going to be able to do anything with this
            has_unsupported_ir = true;
        }
        return IRMutator::visit(op);
    }

    Expr visit(const Mod *op) override {
        has_div_mod = true;
        if (!is_const(op->b)) {
            // z3 isn't going to be able to do anything with this
            has_unsupported_ir = true;
        }
        return IRMutator::visit(op);
    }

    Expr visit(const Call *op) override {
        has_unsupported_ir = true;
        return op;
    }

    Expr visit(const Cast *op) override {
        has_unsupported_ir = true;
        return op;
    }

    Expr visit(const Load *op) override {
        has_unsupported_ir = true;
        return op;
    }

    set<Expr, IRDeepCompare> unique_exprs;

public:
    using IRMutator::mutate;

    Expr mutate(const Expr &e) override {
        if (is_const(e)) {
            num_constants++;
        } else {
            unique_exprs.insert(e);
            IRMutator::mutate(e);
        }
        return e;
    }

    int count_unique_exprs() const {
        return unique_exprs.size() - (int)vars_used.size();
    }

    int count_leaves() const {
        return num_var_leaves + num_constants;
    }

    int num_constants = 0;
    int num_var_leaves = 0;

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
        auto it = bound_values.find(op);
        // Assume repeated instance of the same var is the same
        // wildcard var. If we have rules where that isn't true we'll
        // need to see examples where the values differ.
        if (it == bound_values.end()) {
            string name = "c" + std::to_string(counter++);
            binding[name] = op;
            Expr v = Variable::make(op->type, name);
            bound_values[op] = v;
            return v;
        } else {
            return it->second;
        }
    }
    Expr visit(const Variable *op) override {
        free_vars.insert(op->name);
        return op;
    }

    map<Expr, Expr, IRDeepCompare> bound_values;
    // TODO: float constants

public:
    int counter = 0;
    map<string, Expr> binding;
    set<string> free_vars;
};

int main(int argc, char **argv) {
    if (argc < 4) {
        std::cout << "Usage: ./find_rules input_exprs.txt output_rules.txt blacklist.txt\n";
        return 0;
    }

    const string input_exprs_path = argv[1];
    const string output_rules_path = argv[2];
    const string blacklist_path = argv[3];

    // Generate LHS patterns from raw exprs
    vector<Expr> exprs = parse_halide_exprs_from_file(input_exprs_path);

    // Try to load a blacklist of patterns to skip over that are known
    // to fail. Delete the blacklist whenever you make a change that
    // might make things work for more expressions.
    set<Expr, IRDeepCompare> blacklist;
    if (file_exists(blacklist_path)) {
        auto b = parse_halide_exprs_from_file(blacklist_path);
        blacklist.insert(b.begin(), b.end());
    }
    {
        // Whether or not it already exists, ensure that blacklist file
        // can be opened for appending (so we don't unexpectedly fail after
        // hours of work)
        std::ofstream b;
        b.open(blacklist_path, std::ofstream::out | std::ofstream::app);
        if (b.fail()) {
            debug(0) << "Unable to open blacklist: " << blacklist_path;
            assert(false);
        }
    }

    std::cout << blacklist.size() << " blacklisted patterns\n";

    map<Expr, int, IRDeepCompare> patterns_without_constants;

    set<Expr, IRDeepCompare> patterns;
    size_t handled = 0, total = 0;
    for (auto &e : exprs) {
        debug(0) << e << "\n";
        e = substitute_in_all_lets(e);
        Expr orig = e;
        e = simplify(e);
        Expr second = simplify(e);
        /*
        while (!equal(e, second)) {
            std::cerr << "Warning: Expression required multiple applications of the simplifier:\n"
                      << e << " -> " << second << "\n";
            e = second;
            second = simplify(e);
        }
        */
        std::cout << "Simplified: " << e << "\n";
        total++;
        if (is_one(e)) {
            handled++;
        } else {
            {
                ReplaceConstants replacer;
                int count = patterns_without_constants[replacer.mutate(e)]++;
                // We don't want tons of exprs that are the same except for different constants
                if (count > 1) {
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
                ReplaceConstants replacer;
                if (!blacklist.count(p) && !patterns.count(p)) {
                    int count = patterns_without_constants[replacer.mutate(p)]++;
                    if (count < 1) {
                        // We don't need more than one examples of the
                        // same rule with different constants, because
                        // we're synthesize predicates as a post-pass.
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
        for (int lhs_ops = 1; lhs_ops < 7; lhs_ops++) {
            for (auto p : patterns) {
                CountOps count_ops;
                count_ops.mutate(p);

                if (count_ops.count_leaves() != (lhs_ops + 1) ||
                    count_ops.has_unsupported_ir ||
                    !(count_ops.has_repeated_var ||
                      (lhs_ops < 7 && count_ops.num_constants > 0))) {
                    continue;
                }

                // HACK while testing bool vars
                // if (!count_ops.has_bool_var) continue;

                std::cout << "PATTERN " << lhs_ops << " : " << p << "\n";
                futures.emplace_back(pool.async([=, &mutex, &rules, &futures, &done]() {
                    Expr e;
                    if (true) {
                        // Try something dumb first before using the CEGIS hammer
                        for (Expr r : generate_reassociated_variants(p)) {
                            // Is there already a simplifier rule that handles some
                            // reassociation of this expression?
                            Expr simpler_r = simplify(r);
                            CountOps counter;
                            counter.mutate(simpler_r);
                            if (counter.count_leaves() < lhs_ops + 1) {
                                std::lock_guard<std::mutex> lock(mutex);
                                e = simpler_r;
                                break;
                            }
                        }
                    }

                    // TODO: check if any existing LHS is more general than this pattern

                    for (int budget = 0; !e.defined() && budget <= lhs_ops; budget++) {
                        e = super_simplify(p, budget);
                    }

                    bool success = false;
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        if (e.defined()) {
                            // Quick check of reduction order
                            if (e.defined() && !valid_reduction_order(p, e)) {
                                debug(1) << "Does not obey reduction order: " << p << " -> " << e << "\n";
                            } else {
                                std::cout << "RULE: " << p << " -> " << e << "\n";
                                rules.emplace_back(p, e);
                                success = true;
                            }
                        }
                        done++;
                        if (done % 100 == 0) {
                            std::cout << done << " / " << futures.size() << "\n";
                        }
                        if (!success) {
                            debug(0) << "BLACKLISTING: " << p << "\n";

                            // Add it to the blacklist so we
                            // don't waste time on this
                            // pattern again. Delete the
                            // blacklist whenever you make a
                            // change that might make things
                            // work for new patterns.
                            std::ofstream b;
                            b.open(blacklist_path, std::ofstream::out | std::ofstream::app);
                            if (b.fail()) {
                                debug(0) << "Unable to open blacklist: " << blacklist_path;
                                assert(false);
                            }
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

    debug(0) << "Final rules length: " << rules.size() << " (sorting now)...\n";

    // Sort generated rules
    std::sort(rules.begin(), rules.end(), [](const pair<Expr, Expr> &r1, const pair<Expr, Expr> &r2) {
        return IRDeepCompare{}(r1.first, r2.first);
    });

    {
        std::ofstream of;
        of.open(output_rules_path);
        if (of.fail()) {
            debug(0) << "Unable to open output: " << output_rules_path;
            assert(false);
        }
        for (auto r : rules) {
            ReplaceConstants replacer;
            r.first = replacer.mutate(r.first);
            r.second = replacer.mutate(r.second);
            of << "rewrite(" << r.first << ", " << r.second << ")\n";
        }
        of.close();
    }

    futures.clear();
    return 0;
}
