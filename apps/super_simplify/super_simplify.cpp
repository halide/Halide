#include "super_simplify.h"
#include "expr_util.h"
#include "z3.h"

using namespace Halide;
using namespace Halide::Internal;
using std::map;
using std::pair;
using std::vector;
using std::string;
using std::set;

// Make an expression which can act as any other small integer
// expression in the given leaf terms, depending on the values of the
// integer opcodes. Not all possible programs are valid (e.g. due to
// type errors), so also returns an Expr on the inputs opcodes that
// encodes whether or not the program is well-formed.
pair<Expr, Expr> interpreter_expr(vector<Expr> terms, vector<Expr> use_counts, vector<Expr> opcodes) {
    // Each opcode is an enum identifying the op, followed by the indices of the two args.
    assert(opcodes.size() % 3 == 0);
    assert(terms.size() == use_counts.size());

    Expr program_is_valid = const_true();

    // Type type of each term. Encode int as 0, bool as 1.
    vector<Expr> types;
    for (auto t : terms) {
        if (t.type() == Int(32)) {
            types.push_back(0);
        } else if (t.type() == Bool()) {
            types.push_back(1);
        } else {
            std::cout << t;
            assert(false && "Unhandled wildcard type");
        }
    }

    for (size_t i = 0; i < opcodes.size(); i += 3) {
        Expr op = opcodes[i];
        Expr arg1_idx = opcodes[i+1];
        Expr arg2_idx = opcodes[i+2];

        // Get the args using a select tree. args are either the index of an existing value, or some constant.
        Expr arg1 = arg1_idx, arg2 = arg2_idx;

        // The constants are ints, so make out-of-range values zero.
        Expr arg1_type = 0, arg2_type = 0;
        for (size_t j = 0; j < terms.size(); j++) {
            arg1 = select(arg1_idx == (int)j, terms[j], arg1);
            arg2 = select(arg2_idx == (int)j, terms[j], arg2);
            arg1_type = select(arg1_idx == (int)j, types[j], arg1_type);
            arg2_type = select(arg2_idx == (int)j, types[j], arg2_type);
        }
        int s = (int)terms.size();
        arg1 = select(arg1_idx >= s, arg1_idx - s, arg1);
        arg2 = select(arg2_idx >= s, arg2_idx - s, arg2);

        // Perform the op.
        Expr result = arg1; // By default it's just equal to the first operand. This covers constants too.
        Expr result_type = arg1_type; // Most operators take on the type of the first arg and require the types to match.
        Expr types_ok = arg1_type == arg2_type;

        for (int j = 0; j < (int)use_counts.size(); j++) {
            // We've potentially soaked up one allowed use of each original term
            use_counts[j] -= select((arg1_idx == j) || (op != 0 && arg2_idx == j), 1, 0);
        }

        result = select(op == 1, arg1 + arg2, result);
        result = select(op == 2, arg1 - arg2, result);
        // Only use +/- on integers
        types_ok = (op < 1 || op > 3 || (arg1_type == 0 && arg2_type == 0));

        result = select(op == 3, arg1 * arg2, result);
        // We use bool * int for select(b, x, 0). bool * bool and int
        // * int also make sense.
        types_ok = types_ok || op == 3;

        result = select(op == 4, select(arg1 < arg2, 1, 0), result);
        result = select(op == 5, select(arg1 <= arg2, 1, 0), result);
        result = select(op == 6, select(arg1 == arg2, 1, 0), result);
        result = select(op == 7, select(arg1 != arg2, 1, 0), result);
        // These operators all return bools. They can usefully accept
        // bools too. E.g. A <= B is A implies B. A < B is !A && B.
        result_type = select(op >= 4 && op <= 7, 1, result_type);

        // min/max encodes for or/and, if the types are bool
        result = select(op == 8, min(arg1, arg2), result);
        result = select(op == 9, max(arg1, arg2), result);

        // Only generate div/mod with a few specific constant
        // denominators. Rely on predicates to generalize it.
        // Including these slows synthesis down dramatically.
        /*
        result = select(op == 10, arg1 / 2, result);
        result = select(op == 11, arg1 % 2, result);
        result = select(op == 12, arg1 / 3, result);
        result = select(op == 13, arg1 % 3, result);
        result = select(op == 14, arg1 / 4, result);
        result = select(op == 15, arg1 % 4, result);
        */
        types_ok = select(op > 9, arg1_type == 0 && arg2_idx == 0, types_ok);

        // Type-check it
        program_is_valid = program_is_valid && types_ok && (op <= 9 && op >= 0);

        // TODO: in parallel compute the op histogram, or at least the leading op strength
        terms.push_back(result);
        types.push_back(result_type);
    }

    for (auto u : use_counts) {
        program_is_valid = program_is_valid && (u >= 0);
    }

    return {terms.back(), program_is_valid};
}

Expr reboolify(const Expr &e) {
    if (e.type().is_bool()) return e;
    // e is an integer expression encoding a bool. We want to convert it back to the bool
    if (const Min *op = e.as<Min>()) {
        return reboolify(op->a) && reboolify(op->b);
    } else if (const Max *op = e.as<Max>()) {
        return reboolify(op->a) || reboolify(op->b);
    } else if (const LE *op = e.as<LE>()) {
        return !reboolify(op->a) || reboolify(op->b);
    } else if (const LT *op = e.as<LT>()) {
        return !reboolify(op->a) && reboolify(op->b);
    } else {
        return e == 1;
    }
}

// Use CEGIS to construct an equivalent expression to the input of the given size.
Expr super_simplify(Expr e, int size) {
    bool was_bool = e.type().is_bool();
    Expr orig = e;
    if (was_bool) {
        e = select(e, 1, 0);
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
    vector<Expr> leaves, use_counts;
    for (auto v : vars) {
        leaves.push_back(Variable::make(Int(32), v.first));
        use_counts.push_back(v.second);
    }

    vector<map<string, Expr>> counterexamples;

    map<string, Expr> current_program;

    vector<Expr> symbolic_opcodes;
    for (int i = 0; i < size*3; i++) {
        Var op("op" + std::to_string(i));
        symbolic_opcodes.push_back(op);

        // The initial program is some garbage
        current_program[op.name()] = 0;
    }

    map<string, Expr> all_vars_zero;
    for (auto v : vars) {
        all_vars_zero[v.first] = 0;
    }

    auto p = interpreter_expr(leaves, use_counts, symbolic_opcodes);
    Expr program = p.first;
    Expr program_works = (e == program) && p.second;
    program = simplify(common_subexpression_elimination(program));
    program_works = simplify(common_subexpression_elimination(program_works));

    std::mt19937 rng(0);
    std::uniform_int_distribution<int> random_int(-3, 3);

    while (1) {
        // First sythesize a counterexample to the current program.
        Expr current_program_works = substitute(current_program, program_works);
        map<string, Expr> counterexample = all_vars_zero;

        /*
        std::cout << "Candidate RHS:\n"
                  << simplify(simplify(substitute_in_all_lets(substitute(current_program, program)))) << "\n";
        */

        // Start with just random fuzzing. If that fails, we'll ask Z3 for a counterexample.
        int counterexamples_found_with_fuzzing = 0;
        for (int i = 0; i < 5; i++) {
            map<string, Expr> rand_binding = all_vars_zero;
            for (auto &it : rand_binding) {
                it.second = random_int(rng);
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
            auto result = satisfy(ub_checker.safe && !current_program_works, &counterexample);
            if (result == Z3Result::Unsat) {
                // Woo!
                Expr e = simplify(substitute_in_all_lets(common_subexpression_elimination(substitute(current_program, program))));
                if (was_bool) {
                    e = simplify(reboolify(e));
                }
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
                */
                counterexamples.push_back(counterexample);
            } else {
                return Expr();
            }
        }

        // std::cout << "Counterexample found...\n";

        // Now synthesize a program that fits all the counterexamples
        Expr works_on_counterexamples = const_true();
        for (auto &c : counterexamples) {
            works_on_counterexamples = works_on_counterexamples && substitute(c, program_works);
        }
        if (satisfy(works_on_counterexamples, &current_program) != Z3Result::Sat) {
            // Failed to synthesize a program
            // std::cout << "Failed to find a program of size " << size << "\n";
            return Expr();
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
                          << "Check: " << sanity_check << "\n"
                          << "Counterexamples: \n";
                for (auto c : counterexamples) {
                    const char *prefix = "";
                    for (auto it : c) {
                        std::cout << prefix << it.first << " = " << it.second;
                        prefix = ", ";
                    }
                    std::cout << "\n";
                }
                abort();
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
