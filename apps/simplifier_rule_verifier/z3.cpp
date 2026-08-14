#include "z3.h"
#include "debug.h"
#include "expr_util.h"
#include "parser.h"

#include <cstdlib>

using namespace Halide;
using namespace Halide::Internal;

using std::map;
using std::string;

// Record a binding, if the name is one of the variables we asked about. z3
// invents names of its own for let-bound subexpressions, which we skip.
void record(const map<string, Type> &var_types, const string &name, int64_t value,
            map<string, Expr> *bindings) {
    auto it = var_types.find(name);
    if (it != var_types.end()) {
        (*bindings)[name] = make_const(it->second, value);
    }
}

bool parse_model(const char **cursor, const char *end, const map<string, Type> &var_types,
                 map<string, Expr> *bindings) {
    consume_whitespace(cursor, end);
    // Older versions of z3 tag the model with the token "model"
    if (!consume(cursor, end, "(")) {
        return false;
    }
    consume_whitespace(cursor, end);
    consume(cursor, end, "model");
    consume_whitespace(cursor, end);
    while (consume(cursor, end, "(define-fun")) {
        consume_whitespace(cursor, end);
        string name = consume_token(cursor, end);
        consume_whitespace(cursor, end);
        if (!consume(cursor, end, "()")) {
            return false;
        }
        consume_whitespace(cursor, end);
        if (consume(cursor, end, "Bool")) {
            consume_whitespace(cursor, end);
            if (consume(cursor, end, "true)")) {
                record(var_types, name, 1, bindings);
            } else if (consume(cursor, end, "false)")) {
                record(var_types, name, 0, bindings);
            } else {
                return false;
            }
        } else if (consume(cursor, end, "Int")) {
            consume_whitespace(cursor, end);
            if (consume(cursor, end, "(- ")) {
                record(var_types, name, -std::atoll(consume_token(cursor, end).c_str()), bindings);
                consume(cursor, end, ")");
            } else {
                record(var_types, name, std::atoll(consume_token(cursor, end).c_str()), bindings);
            }
            consume_whitespace(cursor, end);
            consume(cursor, end, ")");
        } else if (consume(cursor, end, "(_ BitVec ")) {
            int64_t bits = consume_int(cursor, end);
            if (!consume(cursor, end, ")")) {
                return false;
            }
            consume_whitespace(cursor, end);
            if (!consume(cursor, end, "#x")) {
                return false;
            }
            // Accumulate the bit pattern unsigned. Shifting into the sign bit
            // of an int64_t, or by 64, would be undefined.
            uint64_t bit_pattern = 0;
            for (int i = 0; i < bits; i += 4) {
                bit_pattern *= 16;
                char next = (**cursor);
                if (next >= '0' && next <= '9') {
                    bit_pattern += next - '0';
                } else if (next >= 'a' && next <= 'f') {
                    bit_pattern += 10 + next - 'a';
                } else {
                    std::cerr << "Bad hex literal char: '" << next << "'\n";
                    abort();
                }
                (*cursor)++;
            }
            // Reinterpret as signed if that's what the variable is
            int64_t value = (int64_t)bit_pattern;
            auto it = var_types.find(name);
            if (it != var_types.end() && it->second.is_int() && bits < 64 &&
                bit_pattern >= (uint64_t)1 << (bits - 1)) {
                value -= (int64_t)1 << bits;
            }
            record(var_types, name, value, bindings);
            consume(cursor, end, ")");
        } else {
            return false;
        }
        consume_whitespace(cursor, end);
    }
    consume_whitespace(cursor, end);
    if (!consume(cursor, end, ")")) {
        return false;
    }
    return true;
}

// Convert from a Halide Expr to SMT2 to pass to z3. Returns false if the Expr
// uses something the conversion doesn't model, in which case the caller must
// treat the query as undecidable.
bool expr_to_smt2(const Expr &e, string *result) {
    class ExprToSMT2 : public IRVisitor {
    public:
        std::ostringstream formula;
        Expr unhandled;

        void give_up(const Expr &e) {
            if (!unhandled.defined()) {
                unhandled = e;
            }
            formula << "<UNHANDLED>";
        }

    protected:
        bool use_bitvector(Type t) {
            return (t.is_int() && t.bits() < 32) || t.is_uint();
        }

        void visit(const IntImm *imm) override {
            if (imm->type.bits() >= 32) {
                formula << imm->value;
            } else {
                formula << "#b";
                for (int i = imm->type.bits() - 1; i >= 0; i--) {
                    formula << (int((imm->value >> i) & 1));
                }
            }
        }

        void visit(const UIntImm *imm) override {
            if (imm->type.is_bool()) {
                if (imm->value) {
                    formula << "true";
                } else {
                    formula << "false";
                }
            } else {
                formula << "#b";
                for (int i = imm->type.bits() - 1; i >= 0; i--) {
                    formula << (int(imm->value >> i) & 1);
                }
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
            if (use_bitvector(op->type)) {
                formula << "(bvadd ";
            } else {
                formula << "(+ ";
            }
            op->a.accept(this);
            formula << " ";
            op->b.accept(this);
            formula << ")";
        }

        void visit(const Sub *op) override {
            if (use_bitvector(op->type)) {
                formula << "(bvsub ";
            } else {
                formula << "(- ";
            }
            op->a.accept(this);
            formula << " ";
            op->b.accept(this);
            formula << ")";
        }

        void visit(const Mul *op) override {
            if (use_bitvector(op->type)) {
                formula << "(bvmul ";
            } else {
                formula << "(* ";
            }
            op->a.accept(this);
            formula << " ";
            op->b.accept(this);
            formula << ")";
        }

        void visit(const Div *op) override {
            if (op->type.is_int() && op->type.bits() < 32) {
                formula << "(my_bvsdiv ";
            } else if (op->type.is_uint()) {
                formula << "(my_bvudiv ";
            } else {
                formula << "(my_div ";
            }
            op->a.accept(this);
            formula << " ";
            op->b.accept(this);
            formula << ")";
        }

        void visit(const Mod *op) override {
            if (op->type.is_int() && op->type.bits() < 32) {
                formula << "(my_bvsmod ";
            } else if (op->type.is_uint()) {
                formula << "(my_bvumod ";
            } else {
                formula << "(my_mod ";
            }
            op->a.accept(this);
            formula << " ";
            op->b.accept(this);
            formula << ")";
        }

        void visit(const Min *op) override {
            if (op->type.is_int() && op->type.bits() < 32) {
                formula << "(my_bvsmin ";
            } else if (op->type.is_uint()) {
                formula << "(my_bvumin ";
            } else {
                formula << "(my_min ";
            }
            op->a.accept(this);
            formula << " ";
            op->b.accept(this);
            formula << ")";
        }

        void visit(const Max *op) override {
            if (op->type.is_int() && op->type.bits() < 32) {
                formula << "(my_bvsmax ";
            } else if (op->type.is_uint()) {
                formula << "(my_bvumax ";
            } else {
                formula << "(my_max ";
            }
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
            if (op->a.type().is_int() && op->a.type().bits() < 32) {
                formula << "(bvslt ";
            } else if (op->a.type().is_uint()) {
                formula << "(bvult ";
            } else {
                formula << "(< ";
            }
            op->a.accept(this);
            formula << " ";
            op->b.accept(this);
            formula << ")";
        }

        void visit(const LE *op) override {
            if (op->a.type().is_int() && op->a.type().bits() < 32) {
                formula << "(bvsle ";
            } else if (op->a.type().is_uint()) {
                formula << "(bvule ";
            } else {
                formula << "(<= ";
            }
            op->a.accept(this);
            formula << " ";
            op->b.accept(this);
            formula << ")";
        }

        void visit(const GT *op) override {
            if (op->a.type().is_int() && op->a.type().bits() < 32) {
                formula << "(bvsgt ";
            } else if (op->a.type().is_uint()) {
                formula << "(bvugt ";
            } else {
                formula << "(> ";
            }
            op->a.accept(this);
            formula << " ";
            op->b.accept(this);
            formula << ")";
        }

        void visit(const GE *op) override {
            if (op->a.type().is_int() && op->a.type().bits() < 32) {
                formula << "(bvsge ";
            } else if (op->a.type().is_uint()) {
                formula << "(bvuge ";
            } else {
                formula << "(>= ";
            }
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
            } else if (op->value.type().is_bool()) {
                Expr equiv = select(op->value, cast(op->type, 1), cast(op->type, 0));
                equiv.accept(this);
            } else {
                give_up(op);
            }
        }

        void visit(const Call *op) override {
            if (op->is_intrinsic(Call::signed_integer_overflow)) {
                // Hrm. Just generate invalid SMT2 so we can fail in peace.
                formula << "<SIGNED_INTEGER_OVERFLOW>";
            } else if (op->name == "fold" || op->name == "prove_me") {
                // Markers used by rewrite rules. They don't change the value.
                op->args[0].accept(this);
            } else {
                give_up(op);
            }
        }

        void visit(const Ramp *op) override {
            give_up(op);
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
    if (to_smt2.unhandled.defined()) {
        debug(1) << "Unhandled IR node for SMT2: " << to_smt2.unhandled << "\n";
        return false;
    }
    *result = to_smt2.formula.str();
    return true;
}

// The z3 binary to run. Override it with the HL_Z3 environment variable if it
// isn't on the PATH.
string z3_executable() {
    const char *s = getenv("HL_Z3");
    return s ? s : "z3";
}

// Rules with several symbolic constants under a div or mod can take z3 a long
// while. HL_Z3_TIMEOUT raises the per-query limit, in seconds.
int z3_timeout(int suggested) {
    const char *s = getenv("HL_Z3_TIMEOUT");
    return s ? atoi(s) : suggested;
}

Z3Result
satisfy(Expr e, map<string, Expr> *bindings, const string &comment, int timeout) {

    // Branch hints don't affect the value, and z3 has no notion of them
    e = simplify(common_subexpression_elimination(remove_likelies(e)));

    if (is_const_one(e)) {
        return Z3Result::Sat;
    }
    if (is_const_zero(e)) {
        return Z3Result::Unsat;
    }
    if (!e.type().is_bool()) {
        std::cout << "Cannot satisfy non-boolean expression " << e << "\n";
        abort();
    }

    std::ostringstream z3_source;

    z3_source << "; " << comment << "\n";

    map<string, Type> var_types;
    for (const auto &v : find_vars(e)) {
        Type t = v.second.first.type();
        var_types[v.first] = t;
        if (t.is_bool()) {
            z3_source << "(declare-const " << v.first << " Bool)\n";
        } else if (t.is_int() && t.bits() >= 32) {
            // Matches expr_to_smt2, which uses unbounded Int arithmetic for
            // signed types of 32 bits and wider
            z3_source << "(declare-const " << v.first << " Int)\n";
        } else {
            z3_source << "(declare-const " << v.first << " (_ BitVec " << v.second.first.type().bits() << "))\n";
        }
    }

    z3_source << "(define-fun my_min ((x Int) (y Int)) Int (ite (< x y) x y))\n"
              << "(define-fun my_max ((x Int) (y Int)) Int (ite (< x y) y x))\n"
              << "(define-fun my_div ((x Int) (y Int)) Int (ite (= y 0) 0 (div x y)))\n"
              << "(define-fun my_mod ((x Int) (y Int)) Int (ite (= y 0) 0 (mod x y)))\n";

    // Halide's integer division and modulo are Euclidean: 0 <= a%b < |b|, and
    // (a/b)*b + a%b == a. Both return zero when b is zero. SMT-LIB's Int div
    // and mod are Euclidean too, but its bit-vector ops are not: bvsdiv
    // truncates towards zero and bvsrem takes the sign of the dividend, so the
    // fixed-width versions correct the quotient and remainder when bvsrem
    // comes out negative.
    for (int i = 8; i <= 32; i *= 2) {
        std::ostringstream ty, zero, args;
        ty << "(_ BitVec " << i << ")";
        zero << "(_ bv0 " << i << ")";
        args << "((x " << ty.str() << ") (y " << ty.str() << ")) " << ty.str();
        const string z = zero.str();
        z3_source << "(define-fun my_bvsmin " << args.str() << " (ite (bvslt x y) x y))\n"
                  << "(define-fun my_bvsmax " << args.str() << " (ite (bvslt x y) y x))\n"
                  << "(define-fun my_bvumin " << args.str() << " (ite (bvult x y) x y))\n"
                  << "(define-fun my_bvumax " << args.str() << " (ite (bvult x y) y x))\n"
                  << "(define-fun my_bvsmod " << args.str()
                  << " (ite (= y " << z << ") " << z
                  << " (let ((r (bvsrem x y)))"
                  << " (ite (bvslt r " << z << ")"
                  << " (bvadd r (ite (bvslt y " << z << ") (bvneg y) y)) r))))\n"
                  << "(define-fun my_bvsdiv " << args.str()
                  << " (ite (= y " << z << ") " << z
                  << " (let ((q (bvsdiv x y)))"
                  << " (ite (bvslt (bvsrem x y) " << z << ")"
                  << " (ite (bvsgt y " << z << ") (bvsub q (_ bv1 " << i << ")) (bvadd q (_ bv1 " << i << ")))"
                  << " q))))\n"
                  << "(define-fun my_bvumod " << args.str()
                  << " (ite (= y " << z << ") " << z << " (bvurem x y)))\n"
                  << "(define-fun my_bvudiv " << args.str()
                  << " (ite (= y " << z << ") " << z << " (bvudiv x y)))\n";
    }

    Expr orig = e;
    while (const Let *l = e.as<Let>()) {
        if (l->value.type().is_int() && l->value.type().bits() >= 32) {
            z3_source << "(declare-const " << l->name << " Int)\n";
        } else if (l->value.type().is_bool()) {
            z3_source << "(declare-const " << l->name << " Bool)\n";
        } else {
            break;
        }
        string value;
        if (!expr_to_smt2(l->value, &value)) {
            return Z3Result::Unknown;
        }
        z3_source << "(assert (= " << l->name << " " << value << "))\n";
        e = l->body;
    }

    string formula;
    if (!expr_to_smt2(e, &formula)) {
        return Z3Result::Unknown;
    }

    z3_source << "(assert " << formula << ")\n"
              << "(check-sat)\n"
              << "(get-model)\n";

    string src = z3_source.str();

    debug(2) << "z3 query:\n"
             << src << "\n";

    TemporaryFile z3_file("query", "z3");
    TemporaryFile z3_output("output", "txt");
    write_entire_file(z3_file.pathname(), &src[0], src.size());

    // No shell involved, so nothing here needs quoting or escaping
    int ret = run_process({z3_executable(),
                           "-T:" + std::to_string(z3_timeout(timeout)),
                           z3_file.pathname()},
                          z3_output.pathname(), "");

    auto result_vec = read_entire_file(z3_output.pathname());
    string result(result_vec.begin(), result_vec.end());

    debug(2) << "z3 produced: " << result << "\n";

    if (starts_with(result, "unknown") || starts_with(result, "timeout")) {
        return Z3Result::Unknown;
    }

    if (ret && !starts_with(result, "unsat")) {
        std::cout << "** z3 query failed with exit code " << ret << "\n"
                  << "** query was:\n"
                  << src << "\n"
                  << "** output was:\n"
                  << result << "\n"
                  << "** Expr was:\n"
                  << orig << "\n";
        return Z3Result::Unknown;
    }

    if (starts_with(result, "unsat")) {
        return Z3Result::Unsat;
    } else {
        const char *cursor = &(result[0]);
        const char *end = &(result[result.size()]);
        if (!consume(&cursor, end, "sat")) {
            return Z3Result::Unknown;
        }
        parse_model(&cursor, end, var_types, bindings);
        return Z3Result::Sat;
    }
}
