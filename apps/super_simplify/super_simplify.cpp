#include "super_simplify.h"
#include "expr_util.h"
#include "z3.h"

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
pair<Expr, Expr> interpreter_expr(vector<Expr> terms, vector<Expr> use_counts, vector<Expr> opcodes, Type desired_type, Type int_type) {
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

    for (size_t i = 0; i < opcodes.size(); i += 4) {
        Expr op = opcodes[i];
        Expr arg1_idx = opcodes[i + 1];
        Expr arg2_idx = opcodes[i + 2];
        Expr arg3_idx = opcodes[i + 3];

        // Get the args using a select tree. args are either the index of an existing value, or some constant.

        int s = (int) terms.size();

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
        Expr result_int = 0;
        Expr result_bool = const_false();

        for (int j = 0; j < (int) use_counts.size(); j++) {
            // We've potentially soaked up one allowed use of each original term
            use_counts[j] -= select(((arg1_idx == j) ||
                                     (arg2_idx == j && op != 0 && op != 10 && op != 11) ||
                                     (arg3_idx == j && op == 12)),
                                    cast(int_type, 1), cast(int_type, 0));
        }

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

        // TODO: switch 2 to any constant divisor already found in the input
        result_int = select(op == 10, arg1_int / 2, result_int);
        result_int = select(op == 11, arg1_int % 2, result_int);

        // Meaningful if arg1 is a bool
        result_int = select(op == 12, select(arg1_bool, arg2_int, arg3_int), result_int);
        result_bool = select(op == 13, arg1_bool && arg2_bool, result_bool);
        result_bool = select(op == 14, arg1_bool || arg2_bool, result_bool);
        result_bool = select(op == 15, !arg1_bool, result_bool);
        result_bool = select(op == 16, arg1_bool, result_bool);

        // Type-check it
        program_is_valid = program_is_valid && (op <= 16 && op >= 0);

        terms_int.push_back(result_int);
        terms_bool.push_back(result_bool);
    }

    Expr total_use_count = 0;
    for (auto u : use_counts) {
        program_is_valid = program_is_valid && (u >= 0);
        total_use_count += u;
    }
    // Require that the use count strictly decreases, and that it
    // decreases or stays constant for every variable.
    program_is_valid = program_is_valid && (total_use_count > 0);

    Expr result = (desired_type.is_bool()) ? terms_bool.back() : terms_int.back();

    return { result, program_is_valid };
}

// Use CEGIS to construct an equivalent expression to the input of the given size.
Expr super_simplify(Expr e, int size) {
    // debug(0) << "\n-------------------------------------------\n";

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
    vector<Expr> leaves, leaves8, use_counts, use_counts8;
    for (auto v : vars) {
        leaves.push_back(v.second.first);
        leaves8.push_back(Variable::make(Int(8), v.first + "_8"));
        use_counts.push_back(v.second.second);
        use_counts8.push_back(cast<int8_t>(v.second.second));
    }

    vector<map<string, Expr>> counterexamples;

    map<string, Expr> current_program;

    vector<Expr> symbolic_opcodes, symbolic_opcodes8;
    for (int i = 0; i < size * 4; i++) {
        string name = "op" + std::to_string(i);
        symbolic_opcodes.push_back(Variable::make(Int(32), name));
        symbolic_opcodes8.push_back(Variable::make(Int(8), name));

        // The initial program is some garbage
        current_program[name] = 0;
    }

    map<string, Expr> all_vars_zero;
    for (auto v : vars) {
        all_vars_zero[v.first] = make_zero(v.second.first.type());
    }

    Expr program, program_works;
    {
        auto p = interpreter_expr(leaves, use_counts, symbolic_opcodes, e.type(), Int(32));
        program = p.first;
        program_works = (e == program) && p.second;
        program = simplify(common_subexpression_elimination(program));
        program_works = simplify(common_subexpression_elimination(program_works));
    }

    // Make an 8-bit version of the interpreter too so that we can use SAT solvers.
    Expr program8, program8_works;
    {
        auto p = interpreter_expr(leaves8, use_counts8, symbolic_opcodes8, e.type(), Int(8));
        program8 = p.first;
        if (e.type().is_bool()) {
            program8_works = (e == program8) && p.second;
        } else {
            program8_works = (cast<int8_t>(e) == program8) && p.second;
        }
        program8 = simplify(common_subexpression_elimination(program8));
        program8_works = simplify(common_subexpression_elimination(program8_works));
    }

    std::mt19937 rng(0);
    std::uniform_int_distribution<int> random_int(-3, 3);

    while (1) {
        // First sythesize a counterexample to the current program.
        Expr current_program_works = substitute(current_program, program_works);
        map<string, Expr> counterexample = all_vars_zero;

        /*
        debug(0) << "Candidate RHS:\n"
                 << simplify(simplify(substitute_in_all_lets(substitute(current_program, program)))) << "\n";
        */

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
            auto interpreted = simplify(substitute(rand_binding, ub_checker.safe && !current_program_works));
            if (is_one(interpreted)) {
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
            auto result = satisfy(ub_checker.safe && !current_program_works, &counterexample,
                                  "finding counterexamples for " + z3_comment);
            if (result == Z3Result::Unsat) {
                // Woo!
                Expr e = simplify(substitute_in_all_lets(common_subexpression_elimination(substitute(current_program, program))));
                // TODO: Figure out why I need to simplify twice
                // here. There are still exprs for which the
                // simplifier requires repeated applications, and
                // it's not supposed to.
                e = simplify(e);

                // std::cout << "*** Success: " << orig << " -> " << result << "\n\n";
                return e;
            } else if (result == Z3Result::Sat) {
                /*
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
                */
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

        // First try for an 8-bit program
        bool have_8_bit_program = false;
        if (false) {  // Just seems to slow things down, sadly
            Expr works_on_counterexamples8 = const_true();
            for (const auto &c : counterexamples) {
                map<string, Expr> c8;
                for (auto p : c) {
                    c8[p.first + "_8"] = simplify(cast<int8_t>(p.second));
                }
                works_on_counterexamples8 = (works_on_counterexamples8 &&
                                             substitute(c8, substitute(c, program8_works)));
            }
            have_8_bit_program = satisfy(works_on_counterexamples8, &current_program,
                                         "finding 8-bit program for " + z3_comment) == Z3Result::Sat;

            if (have_8_bit_program) {
                // Map program opcodes back to the integers and check it
                for (auto &p : current_program) {
                    p.second = cast<int>(p.second);
                }
                // debug(0) << "Candidate 8-bit program: " << simplify(substitute(current_program, program)) << "\n";
                Expr check = substitute(current_program, works_on_counterexamples);
                if (!can_prove(check)) {
                    // debug(0) << "8-bit program doesn't work on integers in full space\n";
                    have_8_bit_program = false;
                } else {
                    // debug(0) << "8-bit program also works in the integers for current counterexamples (" << counterexamples.size() << ")\n";
                }
            }
        }

        if (!have_8_bit_program) {
            // debug(0) << "Failed to solve for 8-bit program. Trying to find a program in the integers\n";
            if (satisfy(works_on_counterexamples, &current_program,
                        "finding program for " + z3_comment) != Z3Result::Sat) {
                // Failed to synthesize a program
                // debug(0) << "Failed to find a program in the integers\n";
                return Expr();
            }
            // debug(0) << "Found a program in the integers: " << simplify(substitute(current_program, program)) << "\n";
        } else {
            // debug(0) << "Found a working 8-bit program.\n";
        }
        // We have a new program

        // If we start to have many many counterexamples, we should
        // double-check things are working as intended.
        if (counterexamples.size() > 30) {
            Expr sanity_check = simplify(substitute(current_program, works_on_counterexamples));
            // Might fail to be the constant true due to overflow, so just make sure it's not the constant false
            if (is_zero(sanity_check)) {
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

        /*
        std::cout << "Current program:";
        for (const auto &o : symbolic_opcodes) {
            std::cout << " " << current_program[o.as<Variable>()->name];
        }
        std::cout << "\n";
        */
    }
}
