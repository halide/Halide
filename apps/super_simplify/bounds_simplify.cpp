#include "bounds_simplify.h"
#include "super_simplify.h"
#include "generate_bounds_cegis.h"
#include "expr_util.h"
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

Expr bounds_simplify(Expr expr, bool upper, int size) {
    std::cerr << "bounds_simplify_" << (upper ? "upper" : "lower") << "(" << expr << ")" << "\n";
    std::cerr << "\t(" << upper << ", " << size << ")\n";

    int z3_timeout = 10; // seconds

    std::string z3_comment;
    {
        std::ostringstream sstr;
        sstr << expr << " at size " << size;
        z3_comment = sstr.str();
    }

    auto expr_scope = make_symbolic_scope(expr);

    const Interval expr_interval = Halide::Internal::bounds_of_expr_in_scope(expr, expr_scope);
    internal_assert( (upper && expr_interval.has_upper_bound()) || (!upper && expr_interval.has_lower_bound()) ) << "expr_interval was unbounded: [" << expr_interval.min << ", " << expr_interval.max << "]\n";
    const Expr expr_bound = (upper) ? expr_interval.max : expr_interval.min;

    std::cerr << "existing bound of " << expr << "is: " << expr_bound << "\n"; 

    auto vars = find_vars(expr);
    auto bound_vars = find_vars(expr_bound);
    vector<Expr> leaves;
    vector<Expr> use_counts;

    const int max_leaves = count_leaves(expr_bound);

    // This expr holds all of the bounds of the variables in the original expression.
    Expr variable_bounds = const_true();
    Expr bounds_relations = const_true();

    for (auto v : vars) {
        const Variable *op = v.second.first.as<Variable>();
        internal_assert(op) << "Found var that isn't a var:" << v.first << ": " << v.second.first << "\n";
        string min_name = op->name + ".min";
        string max_name = op->name + ".max";

        Expr vmin, vmax;
        int vmin_count, vmax_count;

        if (bound_vars.count(min_name) > 0) {
            vmin = bound_vars[min_name].first;
            vmin_count = bound_vars[min_name].second;
        } else {
            internal_assert(bound_vars.count(max_name) > 0) << "bound_vars does not contain an Expr for either bound on: " << op->name << "\n";
            vmin = Variable::make(v.second.first.type(), min_name);
            // TODO(rootjalex): can't tell if this is smart or stupid..
            vmin_count = bound_vars[max_name].second;
        }

        if (bound_vars.count(max_name) > 0) {
            vmax = bound_vars[max_name].first;
            vmax_count = bound_vars[max_name].second;
        } else {
            internal_assert(bound_vars.count(min_name) > 0) << "bound_vars does not contain an Expr for either bound on: " << op->name << "\n";
            vmax = Variable::make(v.second.first.type(), max_name);
            // TODO(rootjalex): can't tell if this is smart or stupid..
            vmax_count = bound_vars[min_name].second;
        }

        // The bounds are our leaves.
        leaves.emplace_back(vmin);
        leaves.emplace_back(vmax);

        use_counts.push_back(vmin_count); // vmin
        use_counts.push_back(vmax_count); // vmax

        // Construct the bounds.
        variable_bounds = variable_bounds && (v.second.first >= vmin) && (v.second.first <= vmax);
        bounds_relations = bounds_relations && (vmin <= vmax);
    }


    // Same as regular synthesis
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
    for (auto v : bound_vars) {
      all_vars_zero[v.first] = make_zero(v.second.first.type());
    }

    Expr program, program_works, program_tighter;
    {
        // + 1 in case the original formula is the best option...
        auto p = interpreter_expr(leaves, use_counts, symbolic_opcodes, expr.type(), Int(32), max_leaves + 1);
        program = p.first;
        // TODO(rootjalex): we need investigation to make sure that this is reasonable..
        if (upper) {
            program_works = (expr <= program) && p.second;
            program_tighter = (program <= expr_bound) && p.second;
        } else {
             program_works = (program <= expr) && (expr_bound <= program) && p.second;
             program_tighter = (expr_bound <= program) && p.second;
        }

        program = simplify(common_subexpression_elimination(program));
        program_works = simplify(common_subexpression_elimination(program_works));
        program_tighter = simplify(common_subexpression_elimination(program_tighter));
    }

    std::cerr << "program:\n\t" << program << "\n";

    std::mt19937 rng(0);
    std::uniform_int_distribution<int> random_int(-3, 3);

    // auto z_result = satisfy(program == expr_bound, &current_program,
    //                               "finding counterexamples for " + z3_comment, z3_timeout);
    // if (z_result != Z3Result::Sat) {
    //     std::cerr << "damn\n";
    //     return Expr();
    // } else {
    //     std::cerr << "nice\n";
    //     return Expr();
    // }

    while (true) {
        if (counterexamples.size() > 100) {
          debug(0) << "TOO MANY COUNTEREXAMPLES, bailing for size=" << size << "\ne=" << expr << "\n";
          return Expr();
        }

        Expr current_program_works = substitute(current_program, program_works);
        Expr current_program_tighter = substitute(current_program, program_tighter);
        map<string, Expr> counterexample = all_vars_zero;

        Expr candidate_RHS = simplify(simplify(substitute_in_all_lets(substitute(current_program, program))));

        std::cerr << "Candidate RHS:\n\t" << candidate_RHS << "\n";

        // Start with just random fuzzing. If that fails, we'll ask Z3 for a counterexample.
        int counterexamples_found_with_fuzzing = 0;

        // TODO: can we fuzz tightness? it's an `or`, so I don't think so...
        for (int i = 0; i < 5; i++) {
            map<string, Expr> rand_binding = all_vars_zero;
            for (auto &it : rand_binding) {
                if (it.second.type() == Bool()) {
                    it.second = (random_int(rng) & 1) ? const_true() : const_false();
                } else {
                    it.second = random_int(rng);
                }
            }
            auto attempt = substitute(rand_binding, bounds_relations && variable_bounds && !current_program_works);
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
            std::cerr << "Checking satisfiability of: " << simplify(substitute_in_all_lets(current_program_works)) << std::endl;
            auto result = satisfy(bounds_relations && variable_bounds && !current_program_works, &counterexample,
                                  "finding counterexamples for " + z3_comment, z3_timeout);
            if (result == Z3Result::Unsat) {
                // Woo!
                Expr RHS = simplify(substitute_in_all_lets(common_subexpression_elimination(substitute(current_program, program))));
                // TODO: Figure out why I need to simplify twice
                // here. There are still exprs for which the
                // simplifier requires repeated applications, and
                // it's not supposed to.
                RHS = simplify(RHS);

                std::cout << "*** Success: " << expr << " -> " << RHS << "\n\n";
                return RHS;
              } else if (result == Z3Result::Sat) {
                std::cout << "Counterexample: ";
                print_counterexample(counterexample);
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
                std::cerr << "Synthesis failed with Unknown\n";
                return Expr();
            }
        }

        // Now synthesize a program that fits all the counterexamples
        Expr works_on_counterexamples = const_true();
        Expr tighter_on_a_counterexample = const_false();

        for (auto &c : counterexamples) {
            works_on_counterexamples = works_on_counterexamples && substitute(c, program_works);
            tighter_on_a_counterexample = tighter_on_a_counterexample || substitute(c, program_tighter);
        }

        works_on_counterexamples = simplify(works_on_counterexamples);
        tighter_on_a_counterexample = simplify(tighter_on_a_counterexample);

        std::cerr << "Querying\n";

        std::cerr << "works_on_counterexamples" << works_on_counterexamples << std::endl;
        std::cerr << "tighter_on_a_counterexample" << tighter_on_a_counterexample << std::endl;

        if (satisfy(works_on_counterexamples && tighter_on_a_counterexample, &current_program,
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

        std::cout << "Current program:";
        for (const auto &o : symbolic_opcodes) {
            std::cout << " " << o.as<Variable>()->name << ": " << current_program[o.as<Variable>()->name];
        }
        std::cout << "\n";
    }
}
