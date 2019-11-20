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
pair<Expr, Expr> interpreter_expr(vector<Expr> terms, vector<Expr> use_counts, vector<Expr> opcodes, Type desired_type) {
    // Each opcode is an enum identifying the op, followed by the indices of the three args.
    assert(opcodes.size() % 4 == 0);
    assert(terms.size() == use_counts.size());

    Expr program_is_valid = const_true();

    // Type type of each term. Encode int as 0, bool as 1.
    vector<Expr> terms_int, terms_bool;
    for (auto &t : terms) {
        if (t.type() == Int(32)) {
            terms_int.push_back(t);
            terms_bool.push_back(const_false());
        } else if (t.type() == Bool()) {
            terms_int.push_back(0);
            terms_bool.push_back(t);
        } else {
            std::cout << t;
            assert(false && "Unhandled wildcard type");
        }
    }

    for (size_t i = 0; i < opcodes.size(); i += 4) {
        Expr op = opcodes[i];
        Expr arg1_idx = opcodes[i+1];
        Expr arg2_idx = opcodes[i+2];
        Expr arg3_idx = opcodes[i+3];

        // Get the args using a select tree. args are either the index of an existing value, or some constant.

        int s = (int)terms.size();

        // int opcodes outside the valid range are constants.
        Expr arg1_int = select(arg1_idx >= s, arg1_idx - s, arg1_idx);
        Expr arg2_int = select(arg2_idx >= s, arg2_idx - s, arg2_idx);
        Expr arg3_int = select(arg3_idx >= s, arg3_idx - s, arg3_idx);

        for (size_t j = 0; j < terms_int.size(); j++) {
            arg1_int = select(arg1_idx == (int)j, terms_int[j], arg1_int);
            arg2_int = select(arg2_idx == (int)j, terms_int[j], arg2_int);
            arg3_int = select(arg3_idx == (int)j, terms_int[j], arg3_int);
        }

        // Bool opcodes beyond the end of the valid range are true. Negative ones are false
        Expr arg1_bool = (arg1_idx >= s);
        Expr arg2_bool = (arg2_idx >= s);
        Expr arg3_bool = (arg3_idx >= s);

        for (size_t j = 0; j < terms_bool.size(); j++) {
            arg1_bool = (arg1_idx == (int)j && terms_bool[j]) || arg1_bool;
            arg2_bool = (arg2_idx == (int)j && terms_bool[j]) || arg2_bool;
            arg3_bool = (arg3_idx == (int)j && terms_bool[j]) || arg3_bool;
        }

        // Perform the op.
        Expr result_int = 0;
        Expr result_bool = const_false();

        for (int j = 0; j < (int)use_counts.size(); j++) {
            // We've potentially soaked up one allowed use of each original term
            use_counts[j] -= select((arg1_idx == j) || (op != 0 && arg2_idx == j), 1, 0);
        }

        result_int = select(op == 0, arg1_int, result_int);
        result_bool = select(op == 0, arg1_bool, result_bool);
        result_int = select(op == 1, arg1_int + arg2_int, result_int);
        result_int = select(op == 2, arg1_int - arg2_int, result_int);
        result_int = select(op == 3, arg1_int * arg2_int, result_int);
        result_int = select(op == 4, min(arg1_int, arg2_int), result_int);
        result_int = select(op == 5, max(arg1_int, arg2_int), result_int);
        result_bool = (op == 6 && arg1_int < arg2_int) || result_bool;
        result_bool = (op == 7 && arg1_int <= arg2_int) || result_bool;
        result_bool = (op == 8 && arg1_int == arg2_int) || result_bool;
        result_bool = (op == 9 && arg1_int != arg2_int) || result_bool;

        result_int = select(op == 10, arg1_int / 2, result_int);
        result_int = select(op == 11, arg1_int % 2, result_int);

        // Meaningful if arg1 is a bool
        result_int = select(op == 12, select(arg1_bool, arg2_int, arg3_int), result_int);
        result_bool = (op == 13 && arg1_bool && arg2_bool) || result_bool;
        result_bool = (op == 14 && (arg1_bool || arg2_bool)) || result_bool;
        result_bool = (op == 15 && !arg1_bool) || result_bool;
        result_bool = (op == 16 && arg1_bool) || result_bool;

        // Type-check it
        program_is_valid = program_is_valid && (op <= 16 && op >= 0);

        terms_int.push_back(result_int);
        terms_bool.push_back(result_bool);
    }

    for (auto u : use_counts) {
        program_is_valid = program_is_valid && (u >= 0);
    }
    program_is_valid = const_true();

    Expr result = (desired_type.is_bool()) ? terms_bool.back() : terms_int.back();

    return {result, program_is_valid};
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
    vector<Expr> leaves, use_counts;
    for (auto v : vars) {
        leaves.push_back(v.second.first);
        use_counts.push_back(v.second.second);
    }

    vector<map<string, Expr>> counterexamples;

    map<string, Expr> current_program;

    vector<Expr> symbolic_opcodes;
    for (int i = 0; i < size * 4; i++) {
        Var op("op" + std::to_string(i));
        symbolic_opcodes.push_back(op);

        // The initial program is some garbage
        current_program[op.name()] = 0;
    }

    map<string, Expr> all_vars_zero;
    for (auto v : vars) {
        all_vars_zero[v.first] = make_zero(v.second.first.type());
    }

    #if 0
    {
        // (x - select(y, z, ((w + x) + -62)))
        // select(y, x - z, 62 - w)

        vector<Expr> leaves = {Expr(Var("x")),
                               Variable::make(Bool(), "y"),
                               Expr(Var("z")),
                               Expr(Var("w"))};
        vector<Expr> use_counts = {1, 1, 1, 1};

        vector<Expr> opcodes = {2, 0, 2, 100, // x - z
                                2, 66, 3, 100, // 62 - w
                                0, 1, 4, 5}; // select

        debug(0) << "ELEPHANT: " << simplify(common_subexpression_elimination(interpreter_expr(leaves, use_counts, opcodes, Int(32)).first)) << "\n";
        abort();
    }
    #endif

    auto p = interpreter_expr(leaves, use_counts, symbolic_opcodes, e.type());
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
        if (satisfy(works_on_counterexamples, &current_program,
                    "finding program for " + z3_comment) != Z3Result::Sat) {
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
