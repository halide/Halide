#include "bounds_incremental.h"
#include "super_simplify.h"
#include "expr_util.h"
// #include "bounds_simplify.h"
#include "generate_bounds_cegis.h"
#include "z3.h"
#include <iostream>

using namespace Halide;
using namespace Halide::Internal;
using std::map;
using std::pair;
using std::set;
using std::string;
using std::vector;

#define internal_assert _halide_user_assert

typedef map<string, pair<Expr, int>> VarMap;

typedef map<Expr, int, IRDeepCompare> ConstExprMap;

Expr make_bound(const Variable *v, bool upper) {
    string name = v->name + (upper ? ".max" : ".min");
    return Variable::make(v->type, name);
}

pair<vector<Expr>, pair<vector<Expr>, Expr>> make_requirements(const VarMap &vars, const ConstExprMap &consts, const vector<string> &var_list, size_t current_var) {
    vector<Expr> leaves, use_counts;
    Expr variable_bounds = const_true();
    for (size_t i = 0; i < var_list.size(); i++) {
        const auto var_iter = vars.find(var_list[i]);
        internal_assert(var_iter != vars.end()) << "var_list contains var not in var map\n";
        const auto var = *var_iter;
        const Variable *op = var.second.first.as<Variable>();
        internal_assert(op) << "Found var that isn't a var:" << var.first << ": " << var.second.first << "\n";

        if (i <= current_var) {
            Expr vmin = make_bound(op, /* upper */ false);
            Expr vmax = make_bound(op, /* upper */ true);

            leaves.emplace_back(vmin);
            leaves.emplace_back(vmax);

            use_counts.push_back(var.second.second); // vmin
            use_counts.push_back(var.second.second); // vmax

            variable_bounds = variable_bounds && (var.second.first <= vmax) && (vmin <= var.second.first);
        } else {
            leaves.push_back(var.second.first);
            use_counts.push_back(var.second.second);
        }
    }

    // Let constants in the original expression be leaves in the new expression.
    for (const auto &c : consts) {
        leaves.push_back(c.first);
        use_counts.push_back(c.second);
    }

    return {leaves, {use_counts, variable_bounds}};
}

vector<Expr> make_symbolic_opcodes(map<string, Expr> &current_program, int size) {
    vector<Expr> symbolic_opcodes;

    for (int i = 0; i < size * 4; i++) {
        string name = "op" + std::to_string(i);
        symbolic_opcodes.push_back(Variable::make(Int(32), name));

        // The initial program is some garbage
        current_program[name] = 0;
    }

    return symbolic_opcodes;
}

map<string, Expr> make_all_vars_zero(const vector<Expr> &leaves) {
    map<string, Expr> all_vars_zero;

    for (const auto &leaf : leaves) {
        const Variable *op = leaf.as<Variable>();
        // remove this check, leaves can be constants now.
        // internal_assert(op) << "Found leaf that isn't a var:" << leaf << "\n";
        if (op) {
            all_vars_zero[op->name] = make_zero(leaf.type());
        }
    }

    return all_vars_zero;
}

int get_max_leaves(const Expr &expr, bool upper) {
    auto expr_scope = make_symbolic_scope(expr);

    const Interval expr_interval = Halide::Internal::bounds_of_expr_in_scope(expr, expr_scope);
    internal_assert( (upper && expr_interval.has_upper_bound()) || (!upper && expr_interval.has_lower_bound()) ) << "expr_interval was unbounded: [" << expr_interval.min << ", " << expr_interval.max << "]\n";
    const Expr expr_bound = (upper) ? expr_interval.max : expr_interval.min;
    return count_leaves(expr_bound);
}

void update_counterexamples(vector<map<string, Expr>> &counterexamples, const vector<string> &var_list, size_t current_var) {
    const string var_name = var_list[current_var];
    const string min_name = var_name + ".min";
    const string max_name = var_name + ".max";

    for (auto &counterex : counterexamples) {
        const auto var_iter = counterex.find(var_name);
        internal_assert(var_iter != counterex.end()) << "counterexample doesn't countain var: " << var_name << "\n";

        // Allows for trivially true bounds for counterexamples
        if (counterex.find(min_name) == counterex.end()) {
            counterex[min_name] = var_iter->second;
        }

        if (counterex.find(max_name) == counterex.end()) {
            counterex[max_name] = var_iter->second;
        }
    }
}


// Make an expression which can act as any other small integer
// expression in the given leaf terms, depending on the values of the
// integer opcodes. Not all possible programs are valid (e.g. due to
// type errors), so also returns an Expr on the inputs opcodes that
// encodes whether or not the program is well-formed.
pair<Expr, Expr> interpreter_expr_v3(vector<Expr> terms, vector<Expr> use_counts, vector<Expr> opcodes, Type desired_type, Type int_type, int max_leaves) {
    // Each opcode is an enum identifying the op, followed by the indices of the three args.
    assert(opcodes.size() % 4 == 0);
    assert(terms.size() == use_counts.size());

    Expr program_is_valid = const_true();

    // Type type of each term. Encode int as 0, bool as 1.
    vector<Expr> terms_int, terms_bool;
    for (auto &t : terms) {
        if (t.type() == int_type) {
            terms_int.push_back(t);
            terms_bool.push_back(const_false());
        } else if (t.type() == Bool()) {
            terms_int.push_back(0);
            terms_bool.push_back(t);
        } else {
            std::cerr << t << " " << int_type << "\n";
            assert(false && "Unhandled wildcard type");
        }
    }

    // TODO: bound constants to be within the ranges of the constants in the input

    Expr leaves_used = cast(int_type, 0);

    int initial_terms = (int) terms.size();

    for (size_t i = 0; i < opcodes.size(); i += 4) {
        Expr op = opcodes[i];
        Expr arg1_idx = opcodes[i + 1];
        Expr arg2_idx = opcodes[i + 2];
        Expr arg3_idx = opcodes[i + 3];

        // Get the args using a select tree. args are either the index of an existing value, or some constant.

        int s = (int) std::max(terms_int.size(), terms_bool.size());

        // TODO: maybe don't force out constants?
        Expr arg1_int = max(min(arg1_idx, s - 1), 0);
        Expr arg2_int = max(min(arg2_idx, s - 1), 0);
        Expr arg3_int = max(min(arg3_idx, s - 1), 0);

        // int opcodes outside the valid range are constants.
        // Expr arg1_int = select(arg1_idx >= s, arg1_idx - s, arg1_idx);
        // Expr arg2_int = select(arg2_idx >= s, arg2_idx - s, arg2_idx);
        // Expr arg3_int = select(arg3_idx >= s, arg3_idx - s, arg3_idx);

        for (size_t j = 0; j < terms_int.size(); j++) {
            arg1_int = select(arg1_idx == (int) j, terms_int[j], arg1_int);
            arg2_int = select(arg2_idx == (int) j, terms_int[j], arg2_int);
            arg3_int = select(arg3_idx == (int) j, terms_int[j], arg3_int);
        }

        // Bool opcodes beyond the end of the valid range are true. Negative ones are false
        Expr arg1_bool = (arg1_idx >= s);
        Expr arg2_bool = (arg2_idx >= s);
        Expr arg3_bool = (arg3_idx >= s);

        for (size_t j = 0; j < terms_bool.size(); j++) {
            arg1_bool = select(arg1_idx == (int) j, terms_bool[j], arg1_bool);
            arg2_bool = select(arg2_idx == (int) j, terms_bool[j], arg2_bool);
            arg3_bool = select(arg3_idx == (int) j, terms_bool[j], arg3_bool);
        }

        // Perform the op.
        Expr result_int = cast(int_type, 0);
        Expr result_bool = const_false();

        Expr arg1_used = const_true();
        Expr arg2_used = op != 0 && op != 10 && op != 11 && op != 15 && op != 16;
        Expr arg3_used = op == 12;

        Expr arg1_leaf = arg1_idx < initial_terms || arg1_idx >= s;
        Expr arg2_leaf = arg2_idx < initial_terms || arg2_idx >= s;
        Expr arg3_leaf = arg3_idx < initial_terms || arg3_idx >= s;

        for (int j = 0; j < (int) use_counts.size(); j++) {
            // We've potentially soaked up one allowed use of each original term
            use_counts[j] -= select(arg1_idx == j && arg1_used, cast(int_type, 1), cast(int_type, 0));
            use_counts[j] -= select(arg2_idx == j && arg2_used, cast(int_type, 1), cast(int_type, 0));
            use_counts[j] -= select(arg3_idx == j && arg3_used, cast(int_type, 1), cast(int_type, 0));
        }

        leaves_used += select(arg1_leaf && arg1_used, cast(int_type, 1), cast(int_type, 0));
        leaves_used += select(arg2_leaf && arg2_used, cast(int_type, 1), cast(int_type, 0));
        leaves_used += select(arg3_leaf && arg3_used, cast(int_type, 1), cast(int_type, 0));

        result_int = select(op == 0, arg1_int, result_int);
        result_bool = select(op == 0, arg1_bool, result_bool);
        result_int = select(op == 1, arg1_int + arg2_int, result_int);
        result_int = select(op == 2, arg1_int - arg2_int, result_int);
        result_int = select(op == 3, arg1_int * arg2_int, result_int);
        result_int = select(op == 4, min(arg1_int, arg2_int), result_int);
        result_int = select(op == 5, max(arg1_int, arg2_int), result_int);
        result_bool = select(op == 6, arg1_int < arg2_int, result_bool);
        result_bool = select(op == 7, arg1_int <= arg2_int, result_bool);
        result_bool = select(op == 8, arg1_int == arg2_int, result_bool);
        result_bool = select(op == 9, arg1_int != arg2_int, result_bool);

        // TODO: maybe add these back?
        // result_int = select(op == 10, arg1_int / 2, result_int);
        // result_int = select(op == 11, arg1_int % 2, result_int);

        // Meaningful if arg1 is a bool
        result_int = select(op == 10, select(arg1_bool, arg2_int, arg3_int), result_int);
        result_bool = select(op == 11, arg1_bool && arg2_bool, result_bool);
        result_bool = select(op == 12, arg1_bool || arg2_bool, result_bool);
        result_bool = select(op == 13, !arg1_bool, result_bool);
        result_bool = select(op == 14, arg1_bool, result_bool);

        // Type-check it
        program_is_valid = program_is_valid && (op <= 14 && op >= 0);

        terms_int.push_back(result_int);
        terms_bool.push_back(result_bool);
    }

    for (auto u : use_counts) {
        program_is_valid = program_is_valid && (u >= 0);
    }
    // Require that:
    // We don't duplicate any wildcards and we strictly reduce the number of leaf nodes.
    // More precise filtering will be done later.
    program_is_valid = program_is_valid && (leaves_used <= max_leaves);

    Expr result = (desired_type.is_bool()) ? terms_bool.back() : terms_int.back();

    return { result, program_is_valid };
}


Expr generate_bounds_incremental(Expr expr, bool upper, int size) {
    std::cerr << "generate_bound_incremental_" << (upper ? "upper" : "lower") << "(" << expr << ")" << "\n";

    int z3_timeout = 10; // seconds

    std::string z3_comment;
    {
        std::ostringstream sstr;
        sstr << expr << " at size " << size;
        z3_comment = sstr.str();
    }

    const auto vars = find_vars(expr);
    const auto consts = find_consts(expr);

    vector<string> var_list;
    for (auto v : vars) {
        var_list.push_back(v.first);
    }
    size_t current_var = 0;

    map<string, Expr> current_program;
    vector<Expr> symbolic_opcodes = make_symbolic_opcodes(current_program, size);
    const int max_leaves = get_max_leaves(expr, upper);

    std::cerr << "max_leaves" << max_leaves << std::endl;

    auto reqs = make_requirements(vars, consts, var_list, current_var);

    // These will be updated in `update_current_var`
    vector<Expr> current_leaves = std::move(reqs.first);
    vector<Expr> current_use_counts = std::move(reqs.second.first);
    Expr variable_bounds = std::move(reqs.second.second);
    map<string, Expr> all_vars_zero = make_all_vars_zero(current_leaves);
    Expr program, program_works;
    auto update_program = [&](const Expr &old_program) {
        // the last iteration of the program
        // Expr old_program = (current_var == 0) ? expr : simplify(simplify(substitute_in_all_lets(substitute(current_program, program))));

        std::cerr << "old program: " << old_program << std::endl;

        auto p = interpreter_expr_v3(current_leaves, current_use_counts, symbolic_opcodes, expr.type(), Int(32), max_leaves);
        program = p.first;
        if (upper) {
            program_works = (old_program <= program) && p.second;
        } else {
            program_works = (old_program >= program) && p.second;
        }

        program = simplify(common_subexpression_elimination(program));
        program_works = simplify(common_subexpression_elimination(program_works));
        std::cerr << "generated new program\n";
    };
    // This will be updated too (see `update_counterexamples`).
    vector<map<string, Expr>> counterexamples;

    // for the first round
    update_program(expr);

    auto update_current_var = [&](const Expr &RHS) -> bool {
        current_var++;
        if (current_var == var_list.size()) {
            // we're done!
            return false;
        } else {
            auto reqs = make_requirements(vars, consts, var_list, current_var);
            current_leaves = std::move(reqs.first);
            current_use_counts = std::move(reqs.second.first);
            variable_bounds = std::move(reqs.second.second);

            all_vars_zero = make_all_vars_zero(current_leaves);

            update_program(RHS);
            update_counterexamples(counterexamples, var_list, current_var);
            return true;
        }
    };

    
    std::mt19937 rng(0);
    std::uniform_int_distribution<int> random_int(-3, 3);

    // This is the CEGIS loop.

    while (true) {
        if (counterexamples.size() > 100) {
          debug(0) << "TOO MANY COUNTEREXAMPLES, bailing for size=" << size << "\ne=" << expr << "\n";
          return Expr();
        }
    
        Expr current_program_works = substitute(current_program, program_works);

        map<string, Expr> counterexample = all_vars_zero;

        Expr candidate_RHS = simplify(simplify(substitute_in_all_lets(substitute(current_program, program))));

        std::cerr << "Candidate RHS:\n\t" << candidate_RHS << "\n";

        // Start with just random fuzzing. If that fails, we'll ask Z3 for a counterexample.
        int counterexamples_found_with_fuzzing = 0;

        for (int i = 0; i < 5; i++) {
            map<string, Expr> rand_binding = all_vars_zero;
            for (auto &it : rand_binding) {
                if (it.second.type() == Bool()) {
                    it.second = (random_int(rng) & 1) ? const_true() : const_false();
                } else {
                    it.second = random_int(rng);
                }
            }
            auto attempt = substitute(rand_binding, variable_bounds && !current_program_works);
            auto interpreted = simplify(attempt);
            if (is_const_one(interpreted)) {
                std::cerr << "found fuzzing counterexample!\n";

                print_counterexample(rand_binding);

                counterexamples.push_back(rand_binding);
                // We probably only want to add a couple
                // counterexamples at a time
                counterexamples_found_with_fuzzing++;
                if (counterexamples_found_with_fuzzing >= 2) {
                    break;
                }
            }
        }

        if (counterexamples_found_with_fuzzing == 0) {
            // std::cerr << "Checking satisfiability of: " << simplify(substitute_in_all_lets(current_program_works)) << std::endl;
            auto result = satisfy(variable_bounds && !current_program_works, &counterexample,
                                  "finding counterexamples for " + z3_comment, z3_timeout);
            
            if (result == Z3Result::Unsat) {
                // Woo, we have a bound on the current Expr!

                Expr RHS = simplify(substitute_in_all_lets(common_subexpression_elimination(substitute(current_program, program))));
                // TODO: Figure out why I need to simplify twice
                // here. There are still exprs for which the
                // simplifier requires repeated applications, and
                // it's not supposed to.
                RHS = simplify(RHS);

                if (!update_current_var(RHS)) {
                    // We're done!
                    std::cout << "\n\n*** Success: " << expr << " -> " << RHS << "\n\n";
                    return RHS;
                } else {
                    std::cout << "\n\n*** Intermediate: " << expr << " -> " << RHS << "\n\n";
                }
            } else if (result == Z3Result::Sat) {
                // std::cout << "Counterexample: ";
                // print_counterexample(counterexample);
                // std::cout << "\n";
                // std::cout << "Current program works: " << simplify(substitute_in_all_lets(current_program_works)) << "\n";
                Expr check = simplify(substitute(counterexample, current_program_works));
                // std::cout << "Check: " << check << "\n";

                if (can_prove(check)) {
                    debug(0) << "Not a counterexample!\n";
                    abort();
                }

                counterexamples.push_back(counterexample);
            } else {
                std::cerr << "Synthesis failed with Unknown\n";
                return Expr();
            }
        }

        // Now synthesize a program that fits all the counterexamples
        Expr works_on_counterexamples = const_true();
        Expr tighter_on_a_counterexample = const_false();

        for (auto &c : counterexamples) {
            works_on_counterexamples = works_on_counterexamples && substitute(c, program_works);
        }

        works_on_counterexamples = simplify(works_on_counterexamples);
    
        // TODO: either remove these or uncomment them
        // std::cerr << "Querying\n";
        // std::cerr << "works_on_counterexamples" << works_on_counterexamples << std::endl;

        if (satisfy(works_on_counterexamples, &current_program,
                    "finding program for " + z3_comment) != Z3Result::Sat) {
            // Failed to synthesize a program
            debug(0) << "Failed to find a program in the integers\n";
            return Expr();
        }

        std::cerr << "Successful query\n";
    
        // If we start to have many many counterexamples, we should
        // double-check things are working as intended.
        if (counterexamples.size() > 30) {
            Expr sanity_check = simplify(substitute(current_program, works_on_counterexamples));
            // Might fail to be the constant true due to overflow, so just make sure it's not the constant false
            if (is_const_zero(sanity_check)) {
                Expr p = simplify(common_subexpression_elimination(substitute(current_program, program)));
                std::cout << "Synthesized program doesn't actually work on counterexamples!\n"
                          << "Original expr: " << expr << "\n"
                          << "Program: " << p << "\n"
                          << "Check: " << sanity_check << "\n";
                std::cout << "Opcodes: \n";
                for (auto p : current_program) {
                    std::cout << p.first << " = " << p.second << "\n";
                }
                std::cout << "Counterexamples: \n";
                for (auto c : counterexamples) {
                    const char *prefix = "";
                    for (auto it : c) {
                        std::cout << prefix << it.first << " = " << it.second;
                        prefix = ", ";
                    }
                    std::cout << "\n";
                }
                return Expr();
            }
        }
    }
}