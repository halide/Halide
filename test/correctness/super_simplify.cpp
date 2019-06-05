#include "Halide.h"

#include <fstream>

using namespace Halide;
using namespace Halide::Internal;

using std::string;
using std::vector;
using std::map;
using std::set;
using std::pair;
using std::ostringstream;

// Convert from a Halide Expr to SMT2 to pass to z3
string expr_to_smt2(const Expr &e) {
    class ExprToSMT2 : public IRVisitor {
    public:
        std::ostringstream formula;

    protected:

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

        void visit(const Cast *op) override {
            assert(false && "unhandled");
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
    while (*cursor + sz < end && (std::isalnum((*cursor)[sz]) || (*cursor)[sz] == '!')) sz++;
    string result{*cursor, sz};
    *cursor += sz;
    return result;
}

int64_t consume_int(char **cursor, char *end) {
    bool negative = consume(cursor, end, "-");
    int64_t n = 0;
    while (*cursor < end && **cursor >= '0' && **cursor <= '9') {
        n *= 10;
        n += (**cursor - '0');
        (*cursor)++;
    }
    return negative ? -n : n;
}

Expr consume_float(char **cursor, char *end) {
    bool negative = consume(cursor, end, "-");
    int64_t integer_part = consume_int(cursor, end);
    int64_t fractional_part = 0;
    int64_t denom = 1;
    if (consume(cursor, end, ".")) {
        while (*cursor < end && **cursor >= '0' && **cursor <= '9') {
            denom *= 10;
            fractional_part *= 10;
            fractional_part += (**cursor - '0');
            (*cursor)++;
        }
    }
    double d = integer_part + double(fractional_part) / denom;
    if (negative) {
        d = -d;
    }
    if (consume(cursor, end, "h")) {
        return make_const(Float(16), d);
    } else if (consume(cursor, end, "f")) {
        return make_const(Float(32), d);
    } else {
        return make_const(Float(64), d);
    }
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

enum Z3Result {
    Sat, Unsat, Unknown
};
Z3Result satisfy(Expr e, map<string, Expr> *bindings) {

    e = simplify(e);

    if (is_one(e)) {
        return Sat;
    }
    if (is_zero(e)) {
        return Unsat;
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
    /*
    std::cout << "z3 query:\n" << z3_source.str() << "\n";
    */

    string src = z3_source.str();

    TemporaryFile z3_file("query", "z3");
    TemporaryFile z3_output("output", "txt");
    write_entire_file(z3_file.pathname(), &src[0], src.size());

    std::string cmd = "z3 -T:600 " + z3_file.pathname() + " > " + z3_output.pathname();

    int ret = system(cmd.c_str());

    auto result_vec = read_entire_file(z3_output.pathname());
    string result(result_vec.begin(), result_vec.end());

    // std::cout << "z3 produced: " << result << "\n";

    if (starts_with(result, "unknown") || starts_with(result, "timeout")) {
        return Unknown;
    }

    if (ret && !starts_with(result, "unsat")) {
        std::cout << "** z3 query failed with exit code " << ret << "\n"
                  << "** query was:\n" << src << "\n"
                  << "** output was:\n" << result << "\n";
        abort();
    }

    if (starts_with(result, "unsat")) {
        return Unsat;
    } else {
        char *cursor = &(result[0]);
        char *end = &(result[result.size()]);
        expect(&cursor, end, "sat");
        parse_model(&cursor, end, bindings);
        return Sat;
    }
}

Var v0("x"), v1("y"), v2("z"), v3("w"), v4("u"), v5("v5"), v6("v6"), v7("v7"), v8("v8"), v9("v9");
Var v10("v10"), v11("v11"), v12("v12"), v13("v13"), v14("v14"), v15("v15"), v16("v16"), v17("v17"), v18("v18"), v19("v19");
Var v20("v20"), v21("v21"), v22("v22"), v23("v23"), v24("v24"), v25("v25"), v26("v26"), v27("v27"), v28("v28"), v29("v29");

Expr reboolify(const Expr &e) {
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

    std::mt19937 rng(0);
    std::uniform_int_distribution<int> random_int(-3, 3);

    while (1) {
        // First sythesize a counterexample to the current program.
        Expr current_program_works = substitute(current_program, program_works);
        map<string, Expr> counterexample = all_vars_zero;

        // Start with just random fuzzing. If that fails, we'll ask Z3 for a counterexample.
        int counterexamples_found_with_fuzzing = 0;
        for (int i = 0; i < 5; i++) {
            map<string, Expr> rand_binding = all_vars_zero;
            for (auto &it : rand_binding) {
                it.second = random_int(rng);
            }
            auto interpreted = simplify(substitute(rand_binding, current_program_works));
            if (is_one(interpreted)) continue;

            counterexamples.push_back(rand_binding);
            // We probably only want to add a couple
            // counterexamples at a time
            counterexamples_found_with_fuzzing++;
            if (counterexamples_found_with_fuzzing >= 2) {
                break;
            }
        }

        if (counterexamples_found_with_fuzzing == 0) {
            auto result = satisfy(!current_program_works, &counterexample);
            if (result == Unsat) {
                // Woo!
                Expr e = simplify(substitute_in_all_lets(common_subexpression_elimination(substitute(current_program, program))));
                if (was_bool) {
                    e = simplify(substitute_in_all_lets(common_subexpression_elimination(reboolify(e))));
                }
                // std::cout << "*** Success: " << orig << " -> " << result << "\n\n";
                return e;
            } else if (result == Sat) {
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

        // Now synthesize a program that fits all the counterexamples
        Expr works_on_counterexamples = const_true();
        for (auto &c : counterexamples) {
            works_on_counterexamples = works_on_counterexamples && substitute(c, program_works);
        }
        if (satisfy(works_on_counterexamples, &current_program) != Sat) {
            // Failed to synthesize a program
            // std::cout << "Failed to find a program of size " << size << "\n";
            return Expr();
        }
        // We have a new program

        /*
        std::cout << "Current program:";
        for (const auto &o : symbolic_opcodes) {
            std::cout << " " << current_program[o.as<Variable>()->name];
        }
        std::cout << "\n";
        */
    }
}

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

        vector<Expr> result;
        void generate_subgraphs(const set<int> &rejected,
                                const set<int> &current,
                                const set<int> &frontier)  {
            // Pick an arbitrary frontier node to consider
            int v = -1;
            for (auto n : frontier) {
                if (!rejected.count(n) &&
                    !current.count(n) &&
                    !children[n].empty()) {
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

            // Generate all subgraphs with this frontier node not
            // included (replaced with a variable).
            r.insert(v);

            // std::cout << "Excluding " << expr_for_id[v] << "\n";
            generate_subgraphs(r, c, f);

            // Generate all subgraphs with this frontier node included
            if (c.size() < 6) {
                c.insert(v);
                for (auto n : ch) {
                    if (!rejected.count(n) && !current.count(n) && !children[n].empty()) {
                        f.insert(n);
                    }
                }
            }
            // std::cout << "Including " << expr_for_id[v] << "\n";
            generate_subgraphs(rejected, c, f);
        }
    } all_subexprs;

    all_subexprs.mutate(e);

    // Enumerate all sub-dags
    set<int> rejected, current, frontier;
    frontier.insert(0);
    for (int i = 0; i < (int)all_subexprs.children.size(); i++) {
        // Don't consider leaves for roots
        if (all_subexprs.children[i].empty()) continue;
        frontier.insert(i);
        all_subexprs.generate_subgraphs(rejected, current, frontier);
        frontier.clear();
    }

    return all_subexprs.result;
}

Expr super_simplify(Expr e) {
    for (int size = 1; size < 3; size++) {
        Expr r = super_simplify(e, size);
        if (r.defined()) return r;
    }
    return Expr();
}

bool more_general_than(const Expr &a, const Expr &b, map<string, Expr> &bindings);

template<typename Op>
bool more_general_than(const Expr &a, const Op *b, map<string, Expr> &bindings) {
    map<string, Expr> backup = bindings;
    if (more_general_than(a, b->a, bindings)) {
        return true;
    }
    bindings = backup;

    if (more_general_than(a, b->b, bindings)) {
        return true;
    }
    bindings = backup;

    if (const Op *op_a = a.as<Op>()) {
        return (more_general_than(op_a->a, b->a, bindings) &&
                more_general_than(op_a->b, b->b, bindings));
    }
    return false;

}

bool more_general_than(const Expr &a, const Expr &b, map<string, Expr> &bindings) {
    if (const Variable *var = a.as<Variable>()) {
        auto it = bindings.find(var->name);
        if (it == bindings.end()) {
            bindings[var->name] = b;
            return true;
        } else {
            return equal(bindings[var->name], b);
        }
    }

    if (const Min *op = b.as<Min>()) {
        return more_general_than(a, op, bindings);
    }

    if (const Min *op = b.as<Min>()) {
        return more_general_than(a, op, bindings);
    }

    if (const Max *op = b.as<Max>()) {
        return more_general_than(a, op, bindings);
    }

    if (const Add *op = b.as<Add>()) {
        return more_general_than(a, op, bindings);
    }

    if (const Sub *op = b.as<Sub>()) {
        return more_general_than(a, op, bindings);
    }

    if (const Mul *op = b.as<Mul>()) {
        return more_general_than(a, op, bindings);
    }

    if (const Div *op = b.as<Div>()) {
        return more_general_than(a, op, bindings);
    }

    if (const LE *op = b.as<LE>()) {
        return more_general_than(a, op, bindings);
    }

    if (const LT *op = b.as<LT>()) {
        return more_general_than(a, op, bindings);
    }

    if (const EQ *op = b.as<EQ>()) {
        return more_general_than(a, op, bindings);
    }

    if (const NE *op = b.as<NE>()) {
        return more_general_than(a, op, bindings);
    }

    if (const Not *op = b.as<Not>()) {
        map<string, Expr> backup = bindings;
        if (more_general_than(a, op->a, bindings)) {
            return true;
        }
        bindings = backup;

        const Not *op_a = a.as<Not>();
        return (op_a &&
                more_general_than(op_a->a, op->a, bindings));
    }

    if (const Select *op = b.as<Select>()) {
        map<string, Expr> backup = bindings;
        if (more_general_than(a, op->condition, bindings)) {
            return true;
        }
        bindings = backup;

        if (more_general_than(a, op->true_value, bindings)) {
            return true;
        }
        bindings = backup;

        if (more_general_than(a, op->false_value, bindings)) {
            return true;
        }
        bindings = backup;

        const Select *op_a = a.as<Select>();
        return (op_a &&
                more_general_than(op_a->condition, op->condition, bindings) &&
                more_general_than(op_a->true_value, op->true_value, bindings) &&
                more_general_than(op_a->false_value, op->false_value, bindings));
    }

    return false;
}

bool more_general_than(const Expr &a, const Expr &b) {
    map<string, Expr> bindings;
    return more_general_than(a, b, bindings);
}

class CountOps : public IRGraphVisitor {
    using IRGraphVisitor::visit;
    using IRGraphVisitor::include;

    void visit(const Variable *op) override {
        if (op->type != Int(32)) {
            has_unsupported_ir = true;
        } else if (vars_used.count(op->name)) {
            repeated_var = true;
        } else {
            vars_used.insert(op->name);
        }
    }

    void visit(const Div *op) override {
        has_div_mod = true;
    }

    void visit(const Mod *op) override {
        has_div_mod = true;
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

    void include(const Expr &e) override {
        unique_exprs.insert(e);
        IRGraphVisitor::include(e);
    }

public:
    int count() {
        return unique_exprs.size() - (int)vars_used.size();
    }

    bool has_div_mod = false;
    bool has_unsupported_ir = false;
    bool repeated_var = false;
    set<string> vars_used;
};

std::ostream &operator<<(std::ostream &s, IRNodeType t) {
    switch(t) {
    case IRNodeType::IntImm: return (s << "IntImm");
    case IRNodeType::UIntImm: return (s << "UIntImm");
    case IRNodeType::FloatImm: return (s << "FloatImm");
    case IRNodeType::StringImm: return (s << "StringImm");
    case IRNodeType::Broadcast: return (s << "Broadcast");
    case IRNodeType::Cast: return (s << "Cast");
    case IRNodeType::Variable: return (s << "Variable");
    case IRNodeType::Add: return (s << "Add");
    case IRNodeType::Sub: return (s << "Sub");
    case IRNodeType::Mod: return (s << "Mod");
    case IRNodeType::Mul: return (s << "Mul");
    case IRNodeType::Div: return (s << "Div");
    case IRNodeType::Min: return (s << "Min");
    case IRNodeType::Max: return (s << "Max");
    case IRNodeType::EQ: return (s << "EQ");
    case IRNodeType::NE: return (s << "NE");
    case IRNodeType::LT: return (s << "LT");
    case IRNodeType::LE: return (s << "LE");
    case IRNodeType::GT: return (s << "GT");
    case IRNodeType::GE: return (s << "GE");
    case IRNodeType::And: return (s << "And");
    case IRNodeType::Or: return (s << "Or");
    case IRNodeType::Not: return (s << "Not");
    case IRNodeType::Select: return (s << "Select");
    case IRNodeType::Load: return (s << "Load");
    case IRNodeType::Ramp: return (s << "Ramp");
    case IRNodeType::Call: return (s << "Call");
    case IRNodeType::Let: return (s << "Let");
    case IRNodeType::Shuffle: return (s << "Shuffle");
    case IRNodeType::LetStmt: return (s << "LetStmt");
    case IRNodeType::AssertStmt: return (s << "AssertStmt");
    case IRNodeType::ProducerConsumer: return (s << "ProducerConsumer");
    case IRNodeType::For: return (s << "For");
    case IRNodeType::Acquire: return (s << "Acquire");
    case IRNodeType::Store: return (s << "Store");
    case IRNodeType::Provide: return (s << "Provide");
    case IRNodeType::Allocate: return (s << "Allocate");
    case IRNodeType::Free: return (s << "Free");
    case IRNodeType::Realize: return (s << "Realize");
    case IRNodeType::Block: return (s << "Block");
    case IRNodeType::Fork: return (s << "Fork");
    case IRNodeType::IfThenElse: return (s << "IfThenElse");
    case IRNodeType::Evaluate: return (s << "Evaluate");
    case IRNodeType::Prefetch: return (s << "Prefetch");
    default: return s;
    }
};

Expr parse_halide_expr(char **cursor, char *end, Type expected_type) {
    consume_whitespace(cursor, end);

    struct TypePattern {
        const char *cast_prefix = nullptr;
        const char *constant_prefix = nullptr;
        Type type;
        string cast_prefix_storage, constant_prefix_storage;
        TypePattern(Type t) {
            ostringstream cast_prefix_stream, constant_prefix_stream;
            cast_prefix_stream << t << '(';
            cast_prefix_storage = cast_prefix_stream.str();
            cast_prefix = cast_prefix_storage.c_str();

            constant_prefix_stream << '(' << t << ')';
            constant_prefix_storage = constant_prefix_stream.str();
            constant_prefix = constant_prefix_storage.c_str();
            type = t;
        }
    };

    static TypePattern typenames[] = {
        {UInt(1)},
        {Int(8)},
        {UInt(8)},
        {Int(16)},
        {UInt(16)},
        {Int(32)},
        {UInt(32)},
        {Int(64)},
        {UInt(64)},
        {Float(64)},
        {Float(32)}};
    for (auto t : typenames) {
        if (consume(cursor, end, t.cast_prefix)) {
            Expr a = cast(t.type, parse_halide_expr(cursor, end, Type{}));
            expect(cursor, end, ")");
            return a;
        }
        if (consume(cursor, end, t.constant_prefix)) {
            return make_const(t.type, consume_int(cursor, end));
        }
    }
    if (consume(cursor, end, "(let ")) {
        string name = consume_token(cursor, end);
        consume_whitespace(cursor, end);
        expect(cursor, end, "=");
        consume_whitespace(cursor, end);

        Expr value = parse_halide_expr(cursor, end, Type{});

        consume_whitespace(cursor, end);
        expect(cursor, end, "in");
        consume_whitespace(cursor, end);

        Expr body = parse_halide_expr(cursor, end, expected_type);

        Expr a = Let::make(name, value, body);
        expect(cursor, end, ")");
        return a;
    }
    if (consume(cursor, end, "min(")) {
        Expr a = parse_halide_expr(cursor, end, expected_type);
        expect(cursor, end, ",");
        Expr b = parse_halide_expr(cursor, end, expected_type);
        consume_whitespace(cursor, end);
        expect(cursor, end, ")");
        return min(a, b);
    }
    if (consume(cursor, end, "max(")) {
        Expr a = parse_halide_expr(cursor, end, expected_type);
        expect(cursor, end, ",");
        Expr b = parse_halide_expr(cursor, end, expected_type);
        consume_whitespace(cursor, end);
        expect(cursor, end, ")");
        return max(a, b);
    }
    if (consume(cursor, end, "select(")) {
        Expr a = parse_halide_expr(cursor, end, Bool());
        expect(cursor, end, ",");
        Expr b = parse_halide_expr(cursor, end, expected_type);
        expect(cursor, end, ",");
        Expr c = parse_halide_expr(cursor, end, expected_type);
        consume_whitespace(cursor, end);
        expect(cursor, end, ")");
        return select(a, b, c);
    }
    Call::ConstString binary_intrinsics[] =
        {Call::bitwise_and,
         Call::bitwise_or,
         Call::shift_left,
         Call::shift_right};
    for (auto intrin : binary_intrinsics) {
        if (consume(cursor, end, intrin)) {
            expect(cursor, end, "(");
            Expr a = parse_halide_expr(cursor, end, expected_type);
            expect(cursor, end, ",");
            Expr b = parse_halide_expr(cursor, end, expected_type);
            consume_whitespace(cursor, end);
            expect(cursor, end, ")");
            return Call::make(a.type(), intrin, {a, b}, Call::PureIntrinsic);
        }
    }

    if (consume(cursor, end, "round_f32(")) {
        Expr a = parse_halide_expr(cursor, end, Float(32));
        expect(cursor, end, ")");
        return round(a);
    }
    if (consume(cursor, end, "ceil_f32(")) {
        Expr a = parse_halide_expr(cursor, end, Float(32));
        expect(cursor, end, ")");
        return ceil(a);
    }
    if (consume(cursor, end, "floor_f32(")) {
        Expr a = parse_halide_expr(cursor, end, Float(32));
        expect(cursor, end, ")");
        return floor(a);
    }

    if (consume(cursor, end, "(")) {
        Expr a = parse_halide_expr(cursor, end, Type{});
        Expr result;
        consume_whitespace(cursor, end);
        if (consume(cursor, end, "+")) {
            result = a + parse_halide_expr(cursor, end, expected_type);
        }
        if (consume(cursor, end, "*")) {
            result = a * parse_halide_expr(cursor, end, expected_type);
        }
        if (consume(cursor, end, "-")) {
            result = a - parse_halide_expr(cursor, end, expected_type);
        }
        if (consume(cursor, end, "/")) {
            result = a / parse_halide_expr(cursor, end, expected_type);
        }
        if (consume(cursor, end, "%")) {
            result = a % parse_halide_expr(cursor, end, expected_type);
        }
        if (consume(cursor, end, "<=")) {
            result = a <= parse_halide_expr(cursor, end, Type{});
        }
        if (consume(cursor, end, "<")) {
            result = a < parse_halide_expr(cursor, end, Type{});
        }
        if (consume(cursor, end, ">=")) {
            result = a >= parse_halide_expr(cursor, end, Type{});
        }
        if (consume(cursor, end, ">")) {
            result = a > parse_halide_expr(cursor, end, Type{});
        }
        if (consume(cursor, end, "==")) {
            result = a == parse_halide_expr(cursor, end, Type{});
        }
        if (consume(cursor, end, "!=")) {
            result = a != parse_halide_expr(cursor, end, Type{});
        }
        if (consume(cursor, end, "&&")) {
            result = a && parse_halide_expr(cursor, end, Bool());
        }
        if (consume(cursor, end, "||")) {
            result = a || parse_halide_expr(cursor, end, Bool());
        }
        if (result.defined()) {
            consume_whitespace(cursor, end);
            expect(cursor, end, ")");
            return result;
        }
    }
    if (consume(cursor, end, "v")) {
        if (expected_type == Type{}) {
            expected_type = Int(32);
        }
        Expr a = Variable::make(expected_type, "v" + std::to_string(consume_int(cursor, end)));
        std::cout << "Making variable " << a
                  << " with type: " << expected_type << "\n";
        return a;
    }
    if ((**cursor >= '0' && **cursor <= '9') || **cursor == '-') {
        Expr e = make_const(Int(32), consume_int(cursor, end));
        if (**cursor == '.') {
            e += consume_float(cursor, end);
        }
        return e;
    }
    if (consume(cursor, end, "true")) {
        return const_true();
    }
    if (consume(cursor, end, "false")) {
        return const_false();
    }
    if (consume(cursor, end, "!")) {
        return !parse_halide_expr(cursor, end, Bool());
    }

    if (**cursor >= 'a' && **cursor <= 'z') {
        char **tmp = cursor;
        string name = consume_token(tmp, end);
        if (consume(tmp, end, "[")) {
            *cursor = *tmp;
            Expr index = parse_halide_expr(cursor, end, Int(32));
            expect(cursor, end, "]");
            if (expected_type == Type{}) {
                expected_type = Int(32);
            }
            return Load::make(expected_type, name, index, Buffer<>(), Parameter(), const_true(), ModulusRemainder());
        }
    }

    std::cerr << "Failed to parse Halide Expr starting at " << *cursor << "\n";
    abort();
    return Expr();
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cout << "Usage: ./super_simplify halide_exprs.txt\n";
        return 0;
    }

    // Generate LHS patterns from raw exprs
    vector<Expr> exprs;
    std::cout << "Reading expressions from file\n";
    std::ifstream input;
    input.open(argv[1]);
    for (string line; std::getline(input, line);) {
        if (line.empty()) continue;

        // There are some extraneous newlines in some of the files. Balance parentheses...
        size_t open, close;
        while (1) {
            open = std::count(line.begin(), line.end(), '(');
            close = std::count(line.begin(), line.end(), ')');
            if (open == close) break;
            string next;
            assert(std::getline(input, next));
            line += next;
        }

        std::cout << "Parsing expression: '" << line << "'\n";
        char *start = &line[0];
        char *end = &line[line.size()];
        exprs.push_back(parse_halide_expr(&start, end, Type{}));
    }

    set<Expr, IRDeepCompare> patterns;
    size_t handled = 0, total = 0;
    for (auto &e : exprs) {
        std::cout << e << "\n";
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
        total++;
        if (is_one(e)) {
            handled++;
        } else {
            for (auto p : all_possible_lhs_patterns(e)) {
                // We prefer LT rules to LE rules. The LE simplifier just redirects to the LT simplifier.
                /*
                if (const LE *le = p.as<LE>()) {
                    p = le->b < le->a;
                }
                */
                patterns.insert(p);
            }
        }
    }

    std::cout << handled << " / " << total << " rules already simplify to true\n";

    // Generate rules from patterns
    vector<std::future<void>> futures;
    ThreadPool<void> pool;
    std::mutex mutex;
    vector<pair<Expr, Expr>> rules;
    int done = 0;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (int lhs_ops = 1; lhs_ops < 10; lhs_ops++) {
            for (auto p : patterns) {
                CountOps count_ops;
                p.accept(&count_ops);
                // For now we'll focus on patterns  with no divides and
                // with a repeated reuse of a LHS expression.
                if (count_ops.count() != lhs_ops ||
                    count_ops.has_div_mod ||
                    count_ops.has_unsupported_ir ||
                    !count_ops.repeated_var) continue;

                std::cout << "PATTERN " << lhs_ops << " : " << p << "\n";
                futures.emplace_back(pool.async([=, &mutex, &rules, &futures, &done]() {
                            int max_rhs_ops = lhs_ops - 1;
                            Expr e = super_simplify(p, max_rhs_ops);
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
                                        std::cout << "New rule:\n";
                                        std::cout << "{" << p << ", " << e << "},\n";
                                        rules.emplace_back(p, e);
                                    }
                                }
                                done++;
                                if (done % 10 == 0) {
                                    std::cout << done << " / " << futures.size() << "\n";
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

    IRNodeType old;
    for (auto r : filtered) {
        IRNodeType t = r.first.node_type();
        if (t != old) {
            std::cout << t << ":\n";
            old = t;
        }
        std::cout << "    rewrite(" << r.first << ", " << r.second << ") ||\n";
    }

    return 0;
}
