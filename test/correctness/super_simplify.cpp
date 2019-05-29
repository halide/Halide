#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

using std::string;
using std::vector;
using std::map;

// Convert from a Halide Expr to SMT2 to pass to z3
string expr_to_smt2(const Expr &e) {
    class ExprToSMT2 : public IRVisitor {
    public:
        std::ostringstream formula;

    protected:

        using IRVisitor::visit;

        void visit(const IntImm *imm) override {
            formula << imm->value;
        }

        void visit(const UIntImm *imm) override {
            formula << imm->value;
        }

        void visit(const FloatImm *imm) override {
            formula << imm->value;
        }

        void visit(const StringImm *imm) override {
            formula << imm->value;
        }

        void visit(const Variable *var) override {
            formula << var->name;
        }

        void visit(const Add *op) override {
            formula << "(+ ";
            op->a.accept(this);
            formula << " ";
            op->b.accept(this);
            formula << ")";
        }

        void visit(const Sub *op) override {
            formula << "(- ";
            op->a.accept(this);
            formula << " ";
            op->b.accept(this);
            formula << ")";
        }

        void visit(const Mul *op) override {
            formula << "(* ";
            op->a.accept(this);
            formula << " ";
            op->b.accept(this);
            formula << ")";
        }

        void visit(const Div *op) override {
            formula << "(div ";
            op->a.accept(this);
            formula << " ";
            op->b.accept(this);
            formula << ")";
        }

        void visit(const Mod *op) override {
            formula << "(mod ";
            op->a.accept(this);
            formula << " ";
            op->b.accept(this);
            formula << ")";
        }

        void visit(const Min *op) override {
            formula << "(my_min ";
            op->a.accept(this);
            formula << " ";
            op->b.accept(this);
            formula << ")";
        }

        void visit(const Max *op) override {
            formula << "(my_max ";
            op->a.accept(this);
            formula << " ";
            op->b.accept(this);
            formula << ")";
        }

        void visit(const EQ *op) override {
            formula << "(= ";
            op->a.accept(this);
            formula << " ";
            op->b.accept(this);
            formula << ")";
        }

        void visit(const NE *op) override {
            formula << "(not (= ";
            op->a.accept(this);
            formula << " ";
            op->b.accept(this);
            formula << "))";
        }

        void visit(const LT *op) override {
            formula << "(< ";
            op->a.accept(this);
            formula << " ";
            op->b.accept(this);
            formula << ")";
        }

        void visit(const LE *op) override {
            formula << "(<= ";
            op->a.accept(this);
            formula << " ";
            op->b.accept(this);
            formula << ")";
        }

        void visit(const GT *op) override {
            formula << "(> ";
            op->a.accept(this);
            formula << " ";
            op->b.accept(this);
            formula << ")";
        }

        void visit(const GE *op) override {
            formula << "(>= ";
            op->a.accept(this);
            formula << " ";
            op->b.accept(this);
            formula << ")";
        }

        void visit(const And *op) override {
            formula << "(and ";
            op->a.accept(this);
            formula << " ";
            op->b.accept(this);
            formula << ")";
        }

        void visit(const Or *op) override {
            formula << "(or ";
            op->a.accept(this);
            formula << " ";
            op->b.accept(this);
            formula << ")";
        }

        void visit(const Not *op) override {
            formula << "(not ";
            op->a.accept(this);
            formula << ")";
        }

        void visit(const Select *op) override {
            formula << "(ite ";
            op->condition.accept(this);
            formula << " ";
            op->true_value.accept(this);
            formula << " ";
            op->false_value.accept(this);
            formula << ")";
        }

        void visit(const Ramp *op) override {
            /*
            Expr equiv = op->base + lane_var * op->stride;
            equiv.accept(this);
            */
            assert(false && "unhandled");
        }

        void visit(const Let *op) override {
            formula << "(let ((" << op->name << " ";
            op->value.accept(this);
            formula << ")) ";
            op->body.accept(this);
            formula << ")";
        }

        void visit(const Broadcast *op) override {
            op->value.accept(this);
        }

    } to_smt2;

    e.accept(&to_smt2);
    return to_smt2.formula.str();
}

// Make an expression which can act as any other small integer expression in
// the given leaf terms, depending on the values of the integer opcodes.
Expr interpreter_expr(vector<Expr> terms, vector<Expr> opcodes) {
    // Each opcode is an enum identifying the op, followed by the indices of the two args.
    assert(opcodes.size() % 3 == 0);

    for (size_t i = 0; i < opcodes.size(); i += 3) {
        Expr op = opcodes[i];
        Expr arg1_idx = opcodes[i+1];
        Expr arg2_idx = opcodes[i+2];

        // Get the args using a select tree
        Expr arg1 = 0, arg2 = 0;
        for (size_t j = 0; j < terms.size(); j++) {
            arg1 = select(arg1_idx == (int)j, terms[j], arg1);
            arg2 = select(arg2_idx == (int)j, terms[j], arg2);
        }

        // Perform the op. TODO: mask off ops stronger than the strongest op in the input
        Expr result = op; // By default it's just the integer constant corresponding to the op code
        result = select(op == 0, arg1 + arg2, result);
        result = select(op == 1, arg1 - arg2, result);
        result = select(op == 2, arg1 * arg2, result);
        //result = select(op == 3, arg1 / arg2, result); // Avoiding div because we synthesize intentional div-by-zero
        result = select(op == 4, select(arg1 < arg2, 1, 0), result);
        result = select(op == 5, select(arg1 <= arg2, 1, 0), result);
        result = select(op == 6, select(arg1 == arg2, 1, 0), result);
        result = select(op == 7, select(arg1 != arg2, 1, 0), result);
        result = select(op == 8, min(arg1, arg2), result);
        result = select(op == 9, max(arg1, arg2), result);
        result = select(op >= 10, op - 10, result); // Positive integer constants

        // TODO: in parallel compute the op histogram, or at least the leading op strength

        terms.push_back(result);
    }

    return simplify(common_subexpression_elimination(terms.back()));
}

bool is_whitespace(char c) {
    return c == ' '  || c == '\n' || c == '\t';
}

void consume_whitespace(char **cursor, char *end) {
    while (*cursor < end && is_whitespace(**cursor)) {
        (*cursor)++;
    }
}

bool consume(char **cursor, char *end, const char *expected) {
    char *tmp = *cursor;
    while (*tmp == *expected && tmp < end && *expected) {
        tmp++;
        expected++;
    }
    if ((*expected) == 0) {
        *cursor = tmp;
        return true;
    } else {
        return false;
    }
}

void expect(char **cursor, char *end, const char *pattern) {
    if (!consume(cursor, end, pattern)) {
        printf("Parsing z3 result failed. Expected %s, got %s\n",
               pattern, *cursor);
        abort();
    }
}

bool check(char **cursor, char *end, const char *pattern) {
    char *tmp_cursor = *cursor;
    return consume(&tmp_cursor, end, pattern);
}

string consume_token(char **cursor, char *end) {
    size_t sz = 0;
    while (*cursor + sz < end && (*cursor)[sz] != ' ' && (*cursor)[sz] != ')') sz++;
    string result{*cursor, sz};
    *cursor += sz;
    return result;
}

void parse_model(char **cursor, char *end, map<string, Expr> *bindings) {
    consume_whitespace(cursor, end);
    expect(cursor, end, "(model");
    consume_whitespace(cursor, end);
    while (consume(cursor, end, "(define-fun")) {
        consume_whitespace(cursor, end);
        string name = consume_token(cursor, end);
        consume_whitespace(cursor, end);
        expect(cursor, end, "()");
        consume_whitespace(cursor, end);
        expect(cursor, end, "Int");
        consume_whitespace(cursor, end);
        if (consume(cursor, end, "(- ")) {
            string val = consume_token(cursor, end);
            if (!starts_with(name, "z3name!")) {
                (*bindings)[name] = -std::atoi(val.c_str());
            }
            consume(cursor, end, ")");
        } else {
            string val = consume_token(cursor, end);
            if (!starts_with(name, "z3name!")) {
                (*bindings)[name] = std::atoi(val.c_str());
            }
        }
        consume_whitespace(cursor, end);
        consume(cursor, end, ")");
        consume_whitespace(cursor, end);
    }
    consume_whitespace(cursor, end);
    expect(cursor, end, ")");
}


class FindVars : public IRVisitor {
    Scope<> lets;

    void visit(const Variable *op) override {
        if (!lets.contains(op->name)) {
            vars.insert(op->name);
        }
    }

    void visit(const Let *op) override {
        op->value.accept(this);
        {
            ScopedBinding<> bind(lets, op->name);
            op->body.accept(this);
        }
    }
public:
    std::set<string> vars;
};


bool satisfy(Expr e, map<string, Expr> *bindings) {

    e = simplify(e);

    if (is_one(e)) {
        return true;
    }
    if (is_zero(e)) {
        return false;
    }
    if (!e.type().is_bool()) {
        std::cout << "Cannot satisfy non-boolean expression " << e << "\n";
        abort();
    }

    FindVars find_vars;

    e.accept(&find_vars);

    std::ostringstream z3_source;

    for (auto v : find_vars.vars) {
        z3_source << "(declare-const " << v << " Int)\n";
    }

    z3_source << "(define-fun my_min ((x Int) (y Int)) Int (ite (< x y) x y))\n"
              << "(define-fun my_max ((x Int) (y Int)) Int (ite (< x y) y x))\n"
              << "(assert " << expr_to_smt2(e) << ")\n"
              << "(check-sat)\n"
              << "(get-model)\n";
    // std::cout << "z3 query:\n" << z3_source.str() << "\n";

    string src = z3_source.str();

    TemporaryFile z3_file("query", "z3");
    TemporaryFile z3_output("output", "txt");
    write_entire_file(z3_file.pathname(), &src[0], src.size());

    std::string cmd = "z3 " + z3_file.pathname() + " > " + z3_output.pathname();

    int ret = system(cmd.c_str());

    auto result_vec = read_entire_file(z3_output.pathname());
    string result(result_vec.begin(), result_vec.end());

    // std::cout << "z3 produced: " << result << "\n";

    if (ret && !starts_with(result, "unsat")) {
        std::cout << "** z3 query failed with exit code " << ret << "\n"
                  << "** query was:\n" << src << "\n"
                  << "** output was:\n" << result << "\n";
        abort();
    }

    if (starts_with(result, "unsat")) {
        return false;
    } else {
        char *cursor = &(result[0]);
        char *end = &(result[result.size()]);
        expect(&cursor, end, "sat");
        parse_model(&cursor, end, bindings);
        return true;
    }
}

Var v0("v0"), v1("v1"), v2("v2"), v3("v3"), v4("v4"), v5("v5");

// Use CEGIS to optimally simplify an expression.
Expr super_simplify(Expr e, int size) {
    bool was_bool = e.type().is_bool();
    Expr orig = e;
    if (was_bool) {
        e = select(e, 1, 0);
    }

    FindVars find_vars;
    e.accept(&find_vars);
    vector<Expr> leaves;
    for (auto v : find_vars.vars) {
        leaves.push_back(Variable::make(Int(32), v));
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
    for (auto v : find_vars.vars) {
        all_vars_zero[v] = 0;
    }

    Expr program = interpreter_expr(leaves, symbolic_opcodes);
    Expr program_works = (e == program);

    while (1) {
        // First sythesize a counterexample to the current program.
        Expr current_program_works = substitute(current_program, program_works);
        map<string, Expr> counterexample = all_vars_zero;
        if (!satisfy(!current_program_works, &counterexample)) {
            // Woo!
            Expr result = simplify(substitute(current_program, program));
            if (was_bool) {
                result = simplify(result == 1);
            }
            std::cout << "*** Success: " << orig << " -> " << result << "\n\n";
            return result;
        } else {
            std::cout << "Counterexample: ";
            const char *prefix = "";
            for (auto it : counterexample) {
                std::cout << prefix << it.first << " = " << it.second;
                prefix = ", ";
            }
            std::cout << "\n";
            counterexamples.push_back(counterexample);
        }

        // Now synthesize a program that fits all the counterexamples
        Expr works_on_counterexamples = const_true();
        for (auto &c : counterexamples) {
            works_on_counterexamples = works_on_counterexamples && substitute(c, program_works);
        }
        if (!satisfy(works_on_counterexamples, &current_program)) {
            // There is no such program
            std::cout << "Failed to find a program of size " << size << "\n";
            return Expr();
        }
        // We have a new program

        std::cout << "Current program:";
        for (const auto &o : symbolic_opcodes) {
            std::cout << " " << current_program[o.as<Variable>()->name];
        }
        std::cout << "\n";
    }
}

Expr super_simplify(Expr e) {
    for (int size = 1; size < 10; size++) {
        Expr r = super_simplify(e, size);
        if (r.defined()) return r;
    }
    return Expr();
}

int main(int argc, char **argv) {
    // Give the variables aliases, to facilitate copy-pastes from elsewhere
    Expr x = v0, y = v1, z = v2, w = v3, u = v4, v = v5;

    super_simplify((v0 + v1) + v2 <= v1);
    super_simplify(x + x*y);
    super_simplify(z + min(x, y - z));
    super_simplify((min(v0, -v1) + v2) + v1 <= v2);
    super_simplify((v0 + (v1 + v2)) - (v3 + (v4 + v2)));
    return 0;
}
