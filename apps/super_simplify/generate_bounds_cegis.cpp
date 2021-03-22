#include "super_simplify.h"
#include "expr_util.h"
#include "z3.h"
#include <cstdint>
#include <iostream>
#include <sys/_types/_size_t.h>

using namespace Halide;
using namespace Halide::Internal;
using std::map;
using std::pair;
using std::set;
using std::string;
using std::vector;

// Make an expression which can act as any other small integer
// expression in the given leaf terms, depending on the values of the
// integer opcodes. Not all possible programs are valid (e.g. due to
// type errors), so also returns an Expr on the inputs opcodes that
// encodes whether or not the program is well-formed.
pair<Expr, Expr> interpreter_expr_v2(vector<Expr> terms, vector<Expr> use_counts, vector<Expr> opcodes, Type desired_type, Type int_type, int max_leaves) {
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

        // (rootjalex): Force the use of leaves.
        // Expr arg1_int = max(min(arg1_idx, s - 1), 0);
        // Expr arg2_int = max(min(arg2_idx, s - 1), 0);
        // Expr arg3_int = max(min(arg3_idx, s - 1), 0);
        // int opcodes outside the valid range are constants.
        Expr arg1_int = select(arg1_idx >= s, arg1_idx - s, arg1_idx);
        Expr arg2_int = select(arg2_idx >= s, arg2_idx - s, arg2_idx);
        Expr arg3_int = select(arg3_idx >= s, arg3_idx - s, arg3_idx);

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

        result_bool = select(op == 4, arg1_int < arg2_int, result_bool);
        result_bool = select(op == 5, arg1_int <= arg2_int, result_bool);
        result_bool = select(op == 6, arg1_int == arg2_int, result_bool);
        result_bool = select(op == 7, arg1_int != arg2_int, result_bool);

        // TODO: switch 2 to any constant divisor already found in the input
        result_int = select(op == 8, arg1_int / 2, result_int);
        result_int = select(op == 9, arg1_int % 2, result_int);

        // Meaningful if arg1 is a bool
        result_int = select(op == 10, select(arg1_bool, arg2_int, arg3_int), result_int);
        result_bool = select(op == 11, arg1_bool && arg2_bool, result_bool);
        result_bool = select(op == 12, arg1_bool || arg2_bool, result_bool);
        result_bool = select(op == 13, !arg1_bool, result_bool);
        result_bool = select(op == 14, arg1_bool, result_bool);

        // rootjalex: mins and maxs are more likely in bounds code, try to make them more likely
        result_int = select(op >= 15, min(arg1_int, arg2_int), result_int);
        result_int = select(op >= 20, max(arg1_int, arg2_int), result_int);

        // Type-check it
        program_is_valid = program_is_valid && (op <= 25 && op >= 0);

        terms_int.push_back(result_int);
        terms_bool.push_back(result_bool);
    }

    for (auto u : use_counts) {
        program_is_valid = program_is_valid && (u >= 0);
    }
    // Require that:
    // We don't duplicate any wildcards and we strictly reduce the number of leaf nodes.
    // More precise filtering will be done later.
    // TODO(rootjalex): don't require leaves_used >= 1...
    // TODO(rootjalex): don't have a max_leaves restriction? Or base it on current bounds_of_expr_in_scope.
    program_is_valid = program_is_valid && (leaves_used <= max_leaves);

    Expr result = (desired_type.is_bool()) ? terms_bool.back() : terms_int.back();

    return { result, program_is_valid };
}

class CountLeaves : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Variable *op) override {
        result++;
    }

    void visit(const IntImm *op) override {
        result++;
    }

    void visit(const UIntImm *op) override {
        result++;
    }

    void visit(const FloatImm *op) override {
        result++;
    }

public:
    int result = 0;
};

int count_leaves(const Expr &expr) {
    CountLeaves leaf_counter;
    expr.accept(&leaf_counter);
    return leaf_counter.result;
}

Scope<Interval> make_symbolic_scope(const Expr &expr) {
    Scope<Interval> scope;

    auto vars = find_vars(expr);

    for (auto v : vars) {
        if (const Variable *op = v.second.first.as<Variable>()) {
            assert(op->name == v.first);
            string min_name = op->name + ".min";
            string max_name = op->name + ".max";
            Expr vmin = Variable::make(v.second.first.type(), min_name);
            Expr vmax = Variable::make(v.second.first.type(), max_name);
            scope.push(op->name, Interval(vmin, vmax));
        } else {
            std::cerr << "find_vars returned non-Variable\n";
            return scope;
        }
    }
    return scope;
}

// Use CEGIS to construct an equivalent expression to the input of the given size.
Expr generate_bound(Expr e, bool upper, int size, int max_leaves) {
    // debug(0) << "\n-------------------------------------------\n";
    std::cerr << "generate_bound_" << (upper ? "upper" : "lower") << "(" << e << ")" << "\n";

    int z3_timeout = 10; // seconds

    std::string z3_comment;
    {
        std::ostringstream sstr;
        sstr << e << " at size " << size;
        z3_comment = sstr.str();
    }

    // We may assume there's no undefined behavior in the existing
    // left-hand-side.
    class CheckForUB : public IRVisitor {
        using IRVisitor::visit;
        void visit(const Mod *op) override {
            safe = safe && (op->b != 0);
        }
        void visit(const Div *op) override {
            safe = safe && (op->b != 0);
        }
        void visit(const Let *op) override {
            assert(false && "CheckForUB not written to handle Lets");
        }

    public:
        Expr safe = const_true();
    } ub_checker;
    e.accept(&ub_checker);

    auto vars = find_vars(e);
    vector<Expr> leaves;
    vector<Expr> use_counts;

    // This expr holds all of the bounds of the variables in the original expression.
    Expr variable_bounds = const_true();

    map<string, Expr> var_intervals;

    map<string, Expr> var_extremes;

    Expr prime_bounds = const_true();

    for (auto v : vars) {
        if (const Variable *op = v.second.first.as<Variable>()) {
            assert(op->name == v.first);
            string min_name = op->name + ".min";
            string max_name = op->name + ".max";
            string extreme_name = op->name + ".prime";
            Expr vmin = Variable::make(v.second.first.type(), min_name);
            Expr vmax = Variable::make(v.second.first.type(), max_name);
            Expr v_extreme = Variable::make(v.second.first.type(), extreme_name);

            // Save these vars
            var_intervals[min_name] = vmin;
            var_intervals[max_name] = vmax;
            var_extremes[op->name] = v_extreme;

            // The bounds are our leaves.
            leaves.emplace_back(vmin);
            leaves.emplace_back(vmax);

            // TODO(rootjalex): This is arbitrary, be smarter.
            use_counts.push_back(v.second.second * 10); // vmin
            use_counts.push_back(v.second.second * 10); // vmax

            // Construct the bounds.
            variable_bounds = variable_bounds && (v.second.first >= vmin) && (v.second.first <= vmax);
            prime_bounds = (v_extreme >= vmin) && (v_extreme <= vmax);
        } else {
            std::cerr << "Found var that isn't a var:" << v.first << ": " << v.second.first << std::endl;
            return Expr();
        }
    }

    vector<map<string, Expr>> counterexamples;

    map<string, Expr> current_program;

    vector<Expr> symbolic_opcodes;

    for (int i = 0; i < size * 4; i++) {
        string name = "op" + std::to_string(i);
        symbolic_opcodes.push_back(Variable::make(Int(32), name));

        // The initial program is some garbage
        current_program[name] = 0;
    }

    map<string, Expr> all_vars_zero;
    for (auto v : vars) {
        all_vars_zero[v.first] = make_zero(v.second.first.type());
    }

    for (auto const &bound : var_intervals) {
      all_vars_zero[bound.first] = make_zero(bound.second.type());
    }

    Expr program, program_works;
    {
        auto p = interpreter_expr(leaves, use_counts, symbolic_opcodes, e.type(), Int(32), max_leaves);
        program = p.first;
        if (upper) {
            program_works = (e <= program) && p.second;
        } else {
            program_works = (e >= program) && p.second;
        }

        // std::cout << program << " and " << p.second << std::endl;
        program = simplify(common_subexpression_elimination(program));
        // std::cerr << "program works (before): " << program_works << std::endl;
        program_works = simplify(common_subexpression_elimination(program_works));
        // std::cerr << "program works (after): " << program_works << std::endl;
    }

    std::mt19937 rng(0);
    std::uniform_int_distribution<int> random_int(-3, 3);

    // TODO(rootjalex): remove this at some point.

    size_t iters = 0;

    size_t max_iters = 32;


    while (true) {
        if (counterexamples.size() > 100) {
          debug(0) << "TOO MANY COUNTEREXAMPLES, bailing for size=" << size << "\ne="<<e<<"\n";
          return Expr();
        }
        if (iters++ > max_iters) {
            debug(0) << "Gave up on iteration: " << iters << "\n";
            return Expr();
        }

        // First sythesize a counterexample to the current program.
        // std::cerr << "program_works: " << program_works << std::endl;
        Expr current_program_works = substitute(current_program, program_works);
        map<string, Expr> counterexample = all_vars_zero;

        Expr candidate_RHS = simplify(simplify(substitute_in_all_lets(substitute(current_program, program))));

        // debug(0) << "RHS expression:" << program << "\n";

        // debug(0) << "works? " << current_program_works << "\n";
        debug(0) << "works? (simpl)" << simplify(current_program_works) << "\n";

        debug(0) << "Candidate RHS:\n\t" << candidate_RHS << "\n";

        if (!counterexamples.empty()) {
            Expr opt_counterexample_RHS = (upper) ? e.type().min() : e.type().max();

            for (auto &c : counterexamples) {
                if (upper) {
                    opt_counterexample_RHS = max(opt_counterexample_RHS, substitute(c, candidate_RHS));
                } else {
                    opt_counterexample_RHS = min(opt_counterexample_RHS, substitute(c, candidate_RHS));
                }
            }

            opt_counterexample_RHS = simplify(opt_counterexample_RHS);

            Expr program_tighter = (upper) ? (program < opt_counterexample_RHS) : (program > opt_counterexample_RHS);
            Expr no_program_regression = (upper) ? (program <= opt_counterexample_RHS) : (program >= opt_counterexample_RHS);

            // std::cerr << "program_tighter:\n\t" << program_tighter << "\n";

            Expr is_tighter_somewhere = const_false();
            Expr no_tightness_regressions = const_false();
            Expr works_on_counterexamples = const_true();

            for (auto &c : counterexamples) {
                works_on_counterexamples = works_on_counterexamples && substitute(c, program_works);
                is_tighter_somewhere = is_tighter_somewhere || substitute(c, program_tighter);
                no_tightness_regressions = no_tightness_regressions && substitute(c, no_program_regression);
            }

            // TODO: should this just use current_program?
            map<string, Expr> tighter_program;
            bool found_tighter = false;

            // Iteratively find a tighter RHS
            while (true) {
                auto z3_result = satisfy(works_on_counterexamples && is_tighter_somewhere && no_tightness_regressions, &tighter_program, "finding tighter program for " + z3_comment);

                if (z3_result == Z3Result::Sat) {
                    found_tighter = true;
                    std::cerr << "Found tighter RHS\n";
                    Expr temp = simplify(simplify(substitute_in_all_lets(substitute(tighter_program, program))));
                    std::cerr << "\t" << temp << "\n";


                    current_program_works = simplify(substitute(tighter_program, program_works));
                    std::cerr << "works? (updated)" << current_program_works << "\n";
                    
                    // for (auto &c : counterexamples) {
                    //     Expr t1 = substitute(c, simplify(substitute_in_all_lets(substitute(tighter_program, program_tighter))));
                    //     std::cerr << "Counterexample tightness:" << t1 << std::endl;
                    //     std::cerr << "previous: " << simplify(substitute(c, opt_counterexample_RHS)) << std::endl;
                    //     std::cerr << "updated: " << simplify(substitute(c, temp)) << std::endl;
                    // }

                    std::cerr << "RHS update: " << opt_counterexample_RHS << "\t->\t" << temp << "\n";

                    opt_counterexample_RHS = std::move(temp);

                    program_tighter = (upper) ? (program < opt_counterexample_RHS) : (program > opt_counterexample_RHS);
                    no_program_regression = (upper) ? (program <= opt_counterexample_RHS) : (program >= opt_counterexample_RHS);

                    // Need to re-do the tightness criterion.
                    // TODO: is there a smarter / faster way?
                    is_tighter_somewhere = const_false();
                    works_on_counterexamples = const_true();
                    no_tightness_regressions = const_false();

                    for (auto &c : counterexamples) {
                        works_on_counterexamples = works_on_counterexamples && substitute(c, program_works);
                        is_tighter_somewhere = is_tighter_somewhere || substitute(c, program_tighter);
                        no_tightness_regressions = no_tightness_regressions && substitute(c, no_program_regression);
                    }

                    current_program = std::move(tighter_program);

                    continue;
                } else if (z3_result == Z3Result::Unsat) {
                    std::cerr << "No tighter RHS on counterexamples\n";
                    break;
                } else {
                    std::cerr << "z3 tightness query returned Unknown\n";
                    break;
                }
            }
        }

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
            auto attempt = substitute(rand_binding, ub_checker.safe && variable_bounds && !current_program_works);
            auto interpreted = simplify(attempt);
            if (is_const_one(interpreted)) {
                std::cerr << "found fuzzing counterexample!\n";
                const char *prefix = "";
                for (auto it : rand_binding) {
                    std::cout << prefix << it.first << " = " << it.second;
                    prefix = ", ";
                }
                std::cout << "\n";

                counterexamples.push_back(rand_binding);
                // We probably only want to add a couple
                // counterexamples at a time
                counterexamples_found_with_fuzzing++;
                if (counterexamples_found_with_fuzzing >= 2) {
                    break;
                }
            } else {
              // std::cerr << "Fuzzing attempt: " << interpreted << std::endl;
              // std::cerr << "Before:" << attempt << std::endl;
            }
        }

        if (counterexamples_found_with_fuzzing == 0) {
            std::cerr << "Checking satisfiability of: " << simplify(substitute_in_all_lets(current_program_works)) << std::endl;
            auto result = satisfy(ub_checker.safe && variable_bounds && !current_program_works, &counterexample,
                                  "finding counterexamples for " + z3_comment, z3_timeout);
            if (result == Z3Result::Unsat) {
                // Woo!
                Expr e = simplify(substitute_in_all_lets(common_subexpression_elimination(substitute(current_program, program))));
                // TODO: Figure out why I need to simplify twice
                // here. There are still exprs for which the
                // simplifier requires repeated applications, and
                // it's not supposed to.
                e = simplify(e);

                std::cout << "*** Success: " << e << " -> " << result << "\n\n";
                return e;
            } else if (result == Z3Result::Sat) {
                
                std::cout << "Counterexample: ";
                const char *prefix = "";
                for (auto it : counterexample) {
                    std::cout << prefix << it.first << " = " << it.second;
                    prefix = ", ";
                }
                std::cout << "\n";
                std::cout << "Current program works: " << simplify(substitute_in_all_lets(current_program_works)) << "\n";
                Expr check = simplify(substitute(counterexample, current_program_works));
                std::cout << "Check: " << check << "\n";

                if (can_prove(check)) {
                    debug(0) << "Not a counterexample!\n";
                    abort();
                }
                
                counterexamples.push_back(counterexample);
            } else {
                return Expr();
            }
        }

        // Now synthesize a program that fits all the counterexamples
        Expr works_on_counterexamples = const_true();

        for (auto &c : counterexamples) {
            works_on_counterexamples = works_on_counterexamples && substitute(c, program_works);
        }

        std::cerr << "Querying\n";
        // std::cerr << "works_on_counterexamples" << simplify(works_on_counterexamples) << std::endl;
        if (satisfy(works_on_counterexamples, &current_program,
                    "finding program for " + z3_comment) != Z3Result::Sat) {
            // Failed to synthesize a program
            debug(0) << "Failed to find a program in the integers\n";
            return Expr();
        }

        std::cerr << "Successful query\n";
        // Now we have a new program.
        // Expr tightness_check = simplify(substitute(current_program, tightness_on_counterexamples));
        // std::cerr << "tightness check: " << tightness_check << std::endl;


        // If we start to have many many counterexamples, we should
        // double-check things are working as intended.
        if (counterexamples.size() > 30) {
            Expr sanity_check = simplify(substitute(current_program, works_on_counterexamples));
            // Might fail to be the constant true due to overflow, so just make sure it's not the constant false
            if (is_const_zero(sanity_check)) {
                Expr p = simplify(common_subexpression_elimination(substitute(current_program, program)));
                std::cout << "Synthesized program doesn't actually work on counterexamples!\n"
                          << "Original expr: " << e << "\n"
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

        std::cout << "Current program:";
        for (const auto &o : symbolic_opcodes) {
            std::cout << " " << o.as<Variable>()->name << ": " << current_program[o.as<Variable>()->name];
        }
        std::cout << "\n";
    }
}
