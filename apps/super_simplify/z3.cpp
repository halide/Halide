#include "z3.h"
#include "expr_util.h"
#include "parser.h"

using namespace Halide;
using namespace Halide::Internal;

using std::string;
using std::map;

bool parse_model(char **cursor, char *end, map<string, Expr> *bindings) {
    consume_whitespace(cursor, end);
    if (!consume(cursor, end, "(model")) return false;
    consume_whitespace(cursor, end);
    while (consume(cursor, end, "(define-fun")) {
        consume_whitespace(cursor, end);
        string name = consume_token(cursor, end);
        consume_whitespace(cursor, end);
        if (!consume(cursor, end, "()")) return false;
        consume_whitespace(cursor, end);
        if (consume(cursor, end, "Bool")) {
            consume_whitespace(cursor, end);
            bool interesting = !starts_with(name, "z3name!");
            if (consume(cursor, end, "true)")) {
                if (interesting) {
                    (*bindings)[name] = const_true();
                }
            } else if (consume(cursor, end, "false)")) {
                if (interesting) {
                    (*bindings)[name] = const_false();
                }
            } else {
                return false;
            }
            consume_whitespace(cursor, end);
        } else {
            if (!consume(cursor, end, "Int")) return false;
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
    }
    consume_whitespace(cursor, end);
    if (!consume(cursor, end, ")")) return false;
    return true;
}

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
            if (imm->type.is_bool()) {
                if (imm->value) {
                    formula << "true";
                } else {
                    formula << "false";
                }
            } else {
                formula << imm->value;
            }
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
            const Call *call = op->value.as<Call>();
            if (call && op->type == Int(32) && call->name == "abs") {
                Expr equiv = select(op->value < 0, 0 - op->value, op->value);
                equiv.accept(this);
            } else {
                std::cerr << "Unhandled IR node for SMT2: " << Expr(op) << "\n";
                // assert(false && "unhandled");
            }
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


Z3Result satisfy(Expr e, map<string, Expr> *bindings, const string &comment) {

    e = simplify(common_subexpression_elimination(e));

    if (is_one(e)) {
        return Z3Result::Sat;
    }
    if (is_zero(e)) {
        return Z3Result::Unsat;
    }
    if (!e.type().is_bool()) {
        std::cout << "Cannot satisfy non-boolean expression " << e << "\n";
        abort();
    }

    std::ostringstream z3_source;

    z3_source << "; " << comment << "\n";

    for (const auto &v : find_vars(e)) {
        if (v.second.first.type().is_bool()) {
            z3_source << "(declare-const " << v.first << " Bool)\n";
        } else {
            z3_source << "(declare-const " << v.first << " Int)\n";
        }
    }

    z3_source << "(define-fun my_min ((x Int) (y Int)) Int (ite (< x y) x y))\n"
              << "(define-fun my_max ((x Int) (y Int)) Int (ite (< x y) y x))\n";

    Expr orig = e;
    while (const Let *l = e.as<Let>()) {
        if (l->value.type().is_int() && l->value.type().bits() >= 32) {
            z3_source << "(declare-const " << l->name << " Int)\n";
        } else if (l->value.type().is_bool()) {
            z3_source << "(declare-const " << l->name << " Bool)\n";
        } else {
            break;
        }
        z3_source << "(assert (= " << l->name << " " << expr_to_smt2(l->value) << "))\n";
        e = l->body;
    }

    z3_source << "(assert " << expr_to_smt2(e) << ")\n"
              << "(check-sat)\n"
              << "(get-model)\n";

    // std::cout << "z3 query:\n" << z3_source.str() << "\n";


    string src = z3_source.str();

    TemporaryFile z3_file("query", "z3");
    TemporaryFile z3_output("output", "txt");
    write_entire_file(z3_file.pathname(), &src[0], src.size());

    std::string cmd = "z3 -T:10 " + z3_file.pathname() + " > " + z3_output.pathname();

    //int ret = system(cmd.c_str());
    int ret = pclose(popen(cmd.c_str(), "r"));

    auto result_vec = read_entire_file(z3_output.pathname());
    string result(result_vec.begin(), result_vec.end());

    if (starts_with(result, "unknown") || starts_with(result, "timeout")) {
        // std::cout << "z3 produced: " << result << "\n";
        return Z3Result::Unknown;
    }

    if (ret && !starts_with(result, "unsat")) {
        std::cout << "** z3 query failed with exit code " << ret << "\n"
                  << "** query was:\n" << src << "\n"
                  << "** output was:\n" << result << "\n";
        return Z3Result::Unknown;
    }

    if (starts_with(result, "unsat")) {
        return Z3Result::Unsat;
    } else {
        char *cursor = &(result[0]);
        char *end = &(result[result.size()]);
        if (!consume(&cursor, end, "sat")) {
            return Z3Result::Unknown;
        }
        parse_model(&cursor, end, bindings);
        return Z3Result::Sat;
    }
}

Expr z3_simplify(const Expr &may_assume, const Expr &e) {
    std::ostringstream z3_source;

    for (auto v : find_vars(e)) {
        z3_source << "(declare-const " << v.first << " Int)\n";
    }

    z3_source << "(define-fun my_min ((x Int) (y Int)) Int (ite (< x y) x y))\n"
              << "(define-fun my_max ((x Int) (y Int)) Int (ite (< x y) y x))\n"
              << "(assert " << expr_to_smt2(e) << ")\n"
              << "(apply ctx-solver-simplify)\n"
              << "(apply ctx-solver-simplify)\n";

    string src = z3_source.str();

    debug(0) << src << "\n";

    TemporaryFile z3_file("query", "z3");
    TemporaryFile z3_output("output", "txt");
    write_entire_file(z3_file.pathname(), &src[0], src.size());

    std::string cmd = "z3 -T:60 " + z3_file.pathname() + " > " + z3_output.pathname();

    int ret = pclose(popen(cmd.c_str(), "r"));
    (void)ret;

    auto result_vec = read_entire_file(z3_output.pathname());
    string result(result_vec.begin(), result_vec.end());

    debug(0) << result << "\n";
    return e;
}
