#include "Associativity.h"
#include "Substitute.h"
#include "Simplify.h"
#include "IROperator.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "Solve.h"
#include "ExprUsesVar.h"
#include "CSE.h"

#include <sstream>

namespace Halide {
namespace Internal {

using std::map;
using std::pair;
using std::string;
using std::vector;

namespace {

// Replace self-reference to Func 'func' with arguments 'args' at index
// 'value_index' in the Expr/Stmt with Var 'substitute'
class ConvertSelfRef : public IRMutator {
    using IRMutator::visit;

    const string func;
    const vector<Expr> args;
    // If that function has multiple values, which value does this
    // call node refer to?
    int value_index;
    string op_x;
    bool is_conditional;

    void visit(const Call *op) {
        if (!is_solvable) {
            return;
        }
        IRMutator::visit(op);
        op = expr.as<Call>();
        internal_assert(op);

        if ((op->call_type == Call::Halide) && (func == op->name)) {
            internal_assert(!op->func.defined())
                << "Func should not have been defined for a self-reference\n";
            internal_assert(args.size() == op->args.size())
                << "Self-reference should have the same number of args as the original\n";
            if (op->value_index != value_index) {
                debug(4) << "Self-reference of " << op->name
                         << " with different index. Cannot prove associativity\n";
            } else if (is_conditional && (op->value_index == value_index)) {
                debug(4) << "Self-reference of " << op->name
                         << " inside a conditional. Operation is not associative\n";
                is_solvable = false;
                return;
            }
            for (size_t i = 0; i < op->args.size(); i++) {
                if (!equal(op->args[i], args[i])) {
                    debug(4) << "Self-reference of " << op->name
                             << " with different args from the LHS. Operation is not associative\n";
                    is_solvable = false;
                    return;
                }
            }
            // Substitute the call
            debug(4) << "   Substituting Call " << op->name << " at value index "
                     << op->value_index << " with " << op_x << "\n";
            expr = Variable::make(op->type, op_x);
            x_part = op;
        }
    }

    void visit(const Select *op) {
        is_conditional = true;
        Expr cond = mutate(op->condition);
        is_conditional = false;

        Expr t = mutate(op->true_value);
        Expr f = mutate(op->false_value);
        if (cond.same_as(op->condition) &&
            t.same_as(op->true_value) &&
            f.same_as(op->false_value)) {
            expr = op;
        } else {
            expr = Select::make(cond, t, f);
        }
    }

public:
    ConvertSelfRef(const string &f, const vector<Expr> &args, int idx, const string &x) :
        func(f), args(args), value_index(idx), op_x(x), is_conditional(false) {}

    bool is_solvable = true;
    Expr x_part;
};

template<typename T>
bool visit_associative_binary_op(const string &op_x, const string &op_y, Expr x_part,
                                 Expr lhs, Expr rhs, AssociativeOp &op) {
    const Variable *var_a = lhs.as<Variable>();
    if (!var_a || (var_a->name != op_x)) {
        debug(4) << "Can't prove associativity of " << T::make(lhs, rhs) << "\n";
        return false;
    } else if (expr_uses_var(rhs, op_x)) {
        debug(4) << "Can't prove associativity of " << T::make(lhs, rhs) << "\n";
        return false;
    } else {
        // op(x, y)
        op.x = {op_x, x_part};
        op.y = {op_y, rhs};
    }
    return true;
}

bool extract_associative_op(const string &op_x, const string &op_y, Expr x_part,
                            Expr e, AssociativeOp &op) {
    Type t = e.type();
    Expr x = Variable::make(t, op_x);
    Expr y = Variable::make(t, op_y);

    if (!x_part.defined()) { // op(y)
        // Update with no self-recurrence is associative and the identity can be
        // anything since it's going to be replaced anyway
        op.op = y;
        op.identity = make_const(t, 0);
        op.x = {"", Expr()};
        op.y = {op_y, e};
        return true;
    }

    if (const Add *a = e.as<Add>()) {
        op.op = x + y;
        op.identity = make_const(t, 0);
        return visit_associative_binary_op<Add>(op_x, op_y, x_part, a->a, a->b, op);
    } else if (const Sub *s = e.as<Sub>()) {
        op.op = x + y;
        op.identity = make_const(t, 0);
        return visit_associative_binary_op<Sub>(op_x, op_y, x_part, s->a, s->b, op);
    } else if (const Mul *m = e.as<Mul>()) {
        op.op = x * y;
        op.identity = make_const(t, 1);
        return visit_associative_binary_op<Mul>(op_x, op_y, x_part, m->a, m->b, op);
    } else if (const Min *m = e.as<Min>()) {
        op.op = Min::make(x, y);
        op.identity = t.max();
        return visit_associative_binary_op<Min>(op_x, op_y, x_part, m->a, m->b, op);
    } else if (const Max *m = e.as<Max>()) {
        op.op = Max::make(x, y);
        op.identity = t.min();
        return visit_associative_binary_op<Max>(op_x, op_y, x_part, m->a, m->b, op);
    } else if (const And *a = e.as<And>()) {
        op.op = And::make(x, y);
        op.identity = make_const(t, 1);
        return visit_associative_binary_op<And>(op_x, op_y, x_part, a->a, a->b, op);
    } else if (const Or *o = e.as<Or>()) {
        op.op = Or::make(x, y);
        op.identity = make_const(t, 0);
        return visit_associative_binary_op<Or>(op_x, op_y, x_part, o->a, o->b, op);
    } else if (e.as<Let>()) {
        internal_error << "Let should have been substituted before calling this function\n";
    } else {
        debug(4) << "Can't prove associativity of " << e << "\n";
        return false;
    }
    return false;
}

} // anonymous namespace


// TODO(psuriana): This does not handle cross-dependencies among tuple elements.
// It also is not able to handle associative select() (e.g. argmin/argmax)
pair<bool, vector<AssociativeOp>> prove_associativity(const string &f, vector<Expr> args,
                                                      vector<Expr> exprs) {
    vector<AssociativeOp> ops;
    map<Expr, string, ExprCompare> y_subs;

    for (Expr &arg : args) {
        arg = common_subexpression_elimination(arg);
        arg = simplify(arg);
        arg = substitute_in_all_lets(arg);
    }

    // For a Tuple of exprs to be associative, each element of the Tuple
    // has to be associative. This does not handle dependencies across
    // Tuple's elements
    for (size_t idx = 0; idx < exprs.size(); ++idx) {
        string op_x = unique_name("_x_" + std::to_string(idx));
        string op_y = unique_name("_y_" + std::to_string(idx));

        Expr expr = simplify(exprs[idx]);

        // Replace any self-reference to Func 'f' with a Var
        ConvertSelfRef csr(f, args, idx, op_x);
        expr = csr.mutate(expr);
        if (!csr.is_solvable) {
            return std::make_pair(false, vector<AssociativeOp>());
        }

        expr = common_subexpression_elimination(expr);
        expr = simplify(expr);
        expr = solve_expression(expr, op_x).result; // Move 'x' to the left as possible
        if (!expr.defined()) {
            return std::make_pair(false, vector<AssociativeOp>());
        }
        expr = substitute_in_all_lets(expr);

        // Try to infer the 'y' part of the operator. If we couldn't find
        // a single 'y' that satisfy the operator, give up
        AssociativeOp op;
        bool is_associative = extract_associative_op(op_x, op_y, csr.x_part, expr, op);
        if (!is_associative) {
            return std::make_pair(false, vector<AssociativeOp>());
        }
        ops.push_back(op);
    }
    return std::make_pair(true, ops);
}

namespace {

std::string print_args(const string &f, const vector<Expr> &args, const vector<Expr> &exprs) {
    std::ostringstream stream;
    stream << f << "(";
    for (size_t i = 0; i < args.size(); ++i) {
        stream << args[i];
        if (i != args.size() - 1) {
            stream << ", ";
        }
    }
    stream << ") = ";

    if (exprs.size() == 1) {
        stream << exprs[0];
    } else if (exprs.size() > 1) {
        stream << "Tuple(";
        for (size_t i = 0; i < exprs.size(); ++i) {
            stream << exprs[i];
            if (i != exprs.size() - 1) {
                stream << ", ";
            }
        }
        stream << ")";
    }
    return stream.str();
}

void check_associativity(const string &f, vector<Expr> args, vector<Expr> exprs,
                         bool is_associative, vector<AssociativeOp> ops) {
    auto result = prove_associativity(f, args, exprs);
    internal_assert(result.first == is_associative)
        << "Checking associativity: " << print_args(f, args, exprs) << "\n"
        << "  Expect is_associative: " << is_associative << "\n"
        << "  instead of " << result.first << "\n";
    if (is_associative) {
        for (size_t i = 0; i < ops.size(); ++i) {
            const AssociativeOp &op = result.second[i];
            internal_assert(equal(op.identity, ops[i].identity))
                << "Checking associativity: " << print_args(f, args, exprs) << "\n"
                << "  Expect identity: " << ops[i].identity << "\n"
                << "  instead of " << op.identity << "\n";
            internal_assert(equal(op.x.second, ops[i].x.second))
                << "Checking associativity: " << print_args(f, args, exprs) << "\n"
                << "  Expect x: " << ops[i].x.second << "\n"
                << "  instead of " << op.x.second << "\n";
            internal_assert(equal(op.y.second, ops[i].y.second))
                << "Checking associativity: " << print_args(f, args, exprs) << "\n"
                << "  Expect y: " << ops[i].y.second << "\n"
                << "  instead of " << op.y.second << "\n";
            Expr expected_op = ops[i].op;
            if (op.y.second.defined()) {
                expected_op = substitute(
                    ops[i].y.first, Variable::make(op.y.second.type(), op.y.first), expected_op);
            }
            if (op.x.second.defined()) {
                expected_op = substitute(
                    ops[i].x.first, Variable::make(op.x.second.type(), op.x.first), expected_op);
            }
            internal_assert(equal(op.op, expected_op))
                << "Checking associativity: " << print_args(f, args, exprs) << "\n"
                << "  Expect bin op: " << expected_op << "\n"
                << "  instead of " << op.op << "\n";

            debug(4) << "\nExpected op: " << expected_op << "\n";
            debug(4) << "Operator: " << op.op << "\n";
            debug(4) << "   identity: " << op.identity << "\n";
            debug(4) << "   x: " << op.x.first << " -> " << op.x.second << "\n";
            debug(4) << "   y: " << op.y.first << " -> " << op.y.second << "\n";
        }
    }
}

} // anonymous namespace

void associativity_test() {
    Expr x = Variable::make(Int(32), "x");
    Expr y = Variable::make(Int(32), "y");
    Expr z = Variable::make(Int(32), "z");
    Expr rx = Variable::make(Int(32), "rx");

    Expr f_call_0 = Call::make(Int(32), "f", {x}, Call::CallType::Halide, nullptr, 0);
    Expr f_call_1 = Call::make(Int(32), "f", {x}, Call::CallType::Halide, nullptr, 1);
    Expr f_call_2 = Call::make(Int(32), "f", {x}, Call::CallType::Halide, nullptr, 2);
    Expr g_call = Call::make(Int(32), "g", {rx}, Call::CallType::Halide, nullptr, 0);


    // f(x) = min(f(x), int16(z))
    check_associativity("f", {x}, {min(f_call_0, y + Cast::make(Int(16), z))},
                        true, {{min(x, y), Int(32).max(), {"x", f_call_0}, {"y", y + Cast::make(Int(16), z)}}});

    // f(x) = f(x) + g(rx) + y + z
    check_associativity("f", {x}, {y + z + f_call_0},
                        true, {{x + y, make_const(Int(32), 0), {"x", f_call_0}, {"y", y + z}}});

    // f(x) = max(y, f(x))
    check_associativity("f", {x}, {max(y, f_call_0)},
                        true, {{max(x, y), Int(32).min(), {"x", f_call_0}, {"y", y}}});

    // f(x) = Tuple(2, 3, f(x)[2] + z)
    check_associativity("f", {x}, {2, 3, f_call_2 + z},
                        true,
                        {{y, make_const(Int(32), 0), {"", Expr()}, {"y", 2}},
                         {y, make_const(Int(32), 0), {"", Expr()}, {"y", 3}},
                         {x + y, make_const(Int(32), 0), {"x", f_call_2}, {"y", z}},
                        });

    // f(x) = Tuple(min(f(x)[0], g(rx)), f(x)[1]*g(x)*2, f(x)[2] + z)
    check_associativity("f", {x}, {min(f_call_0, g_call), f_call_1*g_call*2, f_call_2 + z},
                        true,
                        {{min(x, y), Int(32).max(), {"x", f_call_0}, {"y", g_call}},
                         {x * y, make_const(Int(32), 1), {"x", f_call_1}, {"y", g_call*2}},
                         {x + y, make_const(Int(32), 0), {"x", f_call_2}, {"y", z}},
                        });

    // f(x) = max(f(x) + g(rx), g(rx)) -> not associative
    check_associativity("f", {x}, {max(f_call_0 + g_call, g_call)},
                        false, {});

    // f(x) = max(f(x) + g(rx), f(x) - 3) -> f(x) + max(g(rx) - 3)
    check_associativity("f", {x}, {max(f_call_0 + g_call, f_call_0 - 3)},
                        true, {{x + y, 0, {"x", f_call_0}, {"y", max(g_call, -3)}}});

    // f(x) = f(x) - g(rx) -> Is associative given that the merging operator is +
    check_associativity("f", {x}, {f_call_0 - g_call},
                        true, {{x + y, 0, {"x", f_call_0}, {"y", g_call}}});

    // f(x) = min(4, g(rx)) -> trivially associative
    check_associativity("f", {x}, {min(4, g_call)},
                        true, {{y, make_const(Int(32), 0), {"", Expr()}, {"y", min(g_call, 4)}}});

    // f(x) = f(x) -> associative but doesn't really make any sense, so we'll treat it as non-associative
    check_associativity("f", {x}, {f_call_0},
                        false, {});

    std::cout << "Associativity test passed" << std::endl;
}


}
}
