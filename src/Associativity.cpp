#include "Associativity.h"
#include "Substitute.h"
#include "Simplify.h"
#include "IROperator.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "Solve.h"
#include "ExprUsesVar.h"
#include "CSE.h"

#include <set>
#include <sstream>

namespace Halide {
namespace Internal {

using std::map;
using std::pair;
using std::set;
using std::string;
using std::vector;

namespace {

/** Substitute an expr for a var in a graph. */
class GraphSubstitute : public IRGraphMutator {
    string var;
    Expr value;

    using IRGraphMutator::visit;

    void visit(const Variable *op) {
        if (op->name == var) {
            expr = value;
        } else {
            expr = op;
        }
    }

public:
    GraphSubstitute(const string &var, Expr value) : var(var), value(value) {}
};

/** Substitute in all let Exprs in a piece of IR. Doesn't substitute
 * in let stmts, as this may change the meaning of the IR (e.g. by
 * moving a load after a store). Produces graphs of IR, so don't use
 * non-graph-aware visitors or mutators on it until you've CSE'd the
 * result. */
class SubstituteInAllLets : public IRGraphMutator {

    using IRGraphMutator::visit;

    void visit(const Let *op) {
        Expr value = mutate(op->value);
        Expr body = mutate(op->body);
        expr = GraphSubstitute(op->name, value).mutate(body);
    }

    void visit(const LetStmt *op) {
        Expr value = mutate(op->value);
        Stmt body = mutate(op->body);
        stmt = GraphSubstitute(op->name, value).mutate(body);
    }
};

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
    map<int, Expr> *self_ref_subs;
    bool is_conditional;

    void visit(const Call *op) {
        if (is_not_associative) {
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
            if (is_conditional && (op->value_index == value_index)) {
                debug(4) << "Self-reference of " << op->name
                         << " inside a conditional. Operation is not associative\n";
                is_not_associative = true;
                return;
            }
            for (size_t i = 0; i < op->args.size(); i++) {
                if (!equal(op->args[i], args[i])) {
                    debug(4) << "Self-reference of " << op->name
                             << " with different args from the LHS. Operation is not associative\n";
                    is_not_associative = true;
                    return;
                }
            }
            // Substitute the call
            const auto &iter = self_ref_subs->find(op->value_index);
            if (iter != self_ref_subs->end()) {
                const Variable *v = iter->second.as<Variable>();
                internal_assert(v);
                internal_assert(v->type == op->type);
                debug(4) << "   Substituting Call " << op->name << " at value index "
                         << op->value_index << " with " << v->name << "\n";
                expr = iter->second;
            } else {
                debug(4) << "   Substituting Call " << op->name << " at value index "
                         << op->value_index << " with " << op_x << "\n";
                expr = Variable::make(op->type, op_x);
                self_ref_subs->emplace(op->value_index, expr);
            }
            if (op->value_index == value_index) {
                current_x = op;
            }
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
    ConvertSelfRef(const string &f, const vector<Expr> &args, int idx,
                   const string &x, map<int, Expr> *subs) :
        func(f), args(args), value_index(idx), op_x(x), self_ref_subs(subs),
        is_conditional(false), is_not_associative(false) {}

    bool is_not_associative;
    Expr current_x;
};

// Given an update definition of a Func of the form: update(_x, ...), where '_x'
// is the self-reference to the Func, try to infer a single Var '_y' that is the
// remainder of the op not including '_x'. For example, min(_x, 2*g(r.x) + 4)
// is converted into min(_x, _y), where '_y' is 2*g(r.x) + 4. If there is no
// single Var that satisfies requirement, set 'is_solvable' to false.
class ExtractBinaryOp : public IRMutator {
    using IRMutator::visit;

    const string func;
    const vector<Expr> args;
    map<int, Expr> self_ref_subs;
    string op_y;
    map<Expr, string, ExprCompare> y_subs;

    enum OpType {
        OP_X,       // x only or mixed of x/constant
        OP_Y,       // y only
        OP_MIXED,   // mixed of x/y
    };

    OpType type;

    bool is_x(const string &name) {
        for (const auto &iter : self_ref_subs) {
            const Variable *v = iter.second.as<Variable>();
            internal_assert(v);
            if (v->name == name) {
                return true;
            }
        }
        return false;
    }

    template<typename T>
    void visit_unary_op(const T *op) {
        type = OP_Y;
        current_y = Expr(op);
        expr = Variable::make(op->type, op_y);
    }

    void visit(const IntImm *op)    { visit_unary_op<IntImm>(op); }
    void visit(const UIntImm *op)   { visit_unary_op<UIntImm>(op); }
    void visit(const FloatImm *op)  { visit_unary_op<FloatImm>(op); }
    void visit(const StringImm *op) { visit_unary_op<StringImm>(op); }

    void visit(const Variable *op) {
        if (!is_solvable) {
            return;
        }
        if (is_x(op->name)) {
            type = OP_X;
            expr = op;
            return;
        }
        type = OP_Y;
        current_y = Expr(op);
        expr = Variable::make(op->type, op_y);
    }

    void visit(const Cast *op) {
        if (!is_solvable) {
            return;
        }
        Expr val = mutate(op->value);
        if (type == OP_Y) {
            current_y = Expr(op);
            expr = Variable::make(op->type, op_y);
        } else {
            // Either x or pair of x/y
            expr = Cast::make(op->type, val);
        }
    }

    template<typename T, typename Opp>
    void visit_binary_op(const T *op) {
        if (!is_solvable) {
            return;
        }
        Expr a = mutate(op->a);
        OpType a_type = type;
        Expr b = mutate(op->b);
        OpType b_type = type;

        internal_assert(a.type() == b.type());
        if ((a_type == OP_MIXED) || (b_type == OP_MIXED)) {
            is_solvable = false;
            return;
        }
        if ((a_type == OP_X) && (b_type == OP_X)) {
            is_solvable = false;
            return;
        }

        if ((a_type == OP_X) || (b_type == OP_X)) {
            // Pair of x and y
            type = OP_MIXED;
            expr = Opp::make(a, b);
        } else {
            internal_assert((a_type == OP_Y) && (b_type == OP_Y));
            type = OP_Y;
            current_y = Expr(op);
            expr = Variable::make(op->type, op_y);
        }
    }

    void visit(const Add *op) { visit_binary_op<Add, Add>(op); }
    void visit(const Sub *op) { visit_binary_op<Sub, Sub>(op); }
    void visit(const Mul *op) { visit_binary_op<Mul, Mul>(op); }
    void visit(const Div *op) { visit_binary_op<Div, Div>(op); }
    void visit(const Mod *op) { visit_binary_op<Mod, Mod>(op); }
    void visit(const Min *op) { visit_binary_op<Min, Min>(op); }
    void visit(const Max *op) { visit_binary_op<Max, Max>(op); }
    void visit(const And *op) { visit_binary_op<And, And>(op); }
    void visit(const Or *op) { visit_binary_op<Or, Or>(op); }
    void visit(const LE *op) { visit_binary_op<LE, LE>(op); }
    void visit(const LT *op) { visit_binary_op<LT, LT>(op); }
    void visit(const GE *op) { visit_binary_op<GE, GE>(op); }
    void visit(const GT *op) { visit_binary_op<GT, GT>(op); }
    void visit(const EQ *op) { visit_binary_op<EQ, EQ>(op); }
    void visit(const NE *op) { visit_binary_op<NE, NE>(op); }

    void visit(const Load *op) {
        internal_error << "Can't handle Load\n";
    }

    void visit(const Ramp *op) {
        internal_error << "Can't handle Ramp\n";
    }

    void visit(const Broadcast *op) {
        internal_error << "Can't handle Broadcast\n";
    }

    void visit(const Let *op) {
        internal_error << "Let should have been substituted before calling this mutator\n";
    }

    void visit(const Select *op) {
        if (!is_solvable) {
            return;
        }

        Expr old_y;

        Expr cond = mutate(op->condition);
        if ((type != OP_X)) {
            if (y_subs.count(current_y) == 0) {
                old_y = current_y;
            } else {
                // We already have substitute for 'current_y' (e.g. Var 'y' from
                // other Tuple element), use that instead of creating a new
                // Var
                cond = substitute(op_y, Variable::make(current_y.type(), y_subs[current_y]), cond);
            }
        }
        if (!is_solvable) {
            return;
        }

        Expr true_value = mutate(op->true_value);
        if (!is_solvable) {
            return;
        }
        if (type == OP_MIXED) {
            // select(x + g(y1), x + g(y2), ...) is not solvable (it's not associative)
            is_solvable = false;
            return;
        } else if (type == OP_Y) {
            if (old_y.defined()) {
                if (!equal(old_y, current_y)) {
                    if (is_const(current_y)) {
                        current_y = old_y;
                    } else if (!is_const(old_y)) {
                        is_solvable = false;
                        return;
                    }
                }
            }
            old_y = current_y;
        }

        Expr false_value = mutate(op->false_value);
        if (!is_solvable) {
            return;
        }
        if (type == OP_MIXED) {
            is_solvable = false;
            return;
        } else if (type == OP_Y) {
            if (old_y.defined()) {
                if (!equal(old_y, current_y)) {
                    if (is_const(current_y)) {
                        current_y = old_y;
                    } else if (!is_const(old_y)) {
                        is_solvable = false;
                        return;
                    }
                }
            }
            old_y = current_y;
        }
        expr = Select::make(cond, true_value, false_value);
    }

    void visit(const Not *op) {
        if (!is_solvable) {
            return;
        }
        Expr a = mutate(op->a);
        if (type == OP_Y) {
            current_y = Expr(op);
            expr = Variable::make(op->type, op_y);
        } else {
            expr = Not::make(a);
        }
    }

    void visit(const Call *op) {
        if (!is_solvable) {
            return;
        }
        if (op->call_type != Call::Halide) {
            is_solvable = false;
            return;
        }

        // Mutate the args
        for (size_t i = 0; i < op->args.size(); i++) {
            Expr new_args = mutate(op->args[i]);
            if (type != OP_Y) {
                is_solvable = false;
                return;
            }
        }
        internal_assert(type == OP_Y);
        current_y = Expr(op);
        expr = Variable::make(op->type, op_y);
    }

public:
    ExtractBinaryOp(const string &f, const vector<Expr> &args, const map<int, Expr> &x_subs,
                      const string &y, const map<Expr, string, ExprCompare> &y_subs) :
        func(f), args(args), self_ref_subs(x_subs), op_y(y), y_subs(y_subs), is_solvable(true) {}

    bool is_solvable;
    Expr current_y;
};

// Given a binary expression operator 'bin_op' in the form of op(x, y), find
// the identity of the operator. If the identity is not in [0, 1, +inf, -inf],
// this returns undefined Expr
Expr find_identity(Expr bin_op, const string &op_x, const string &op_y, Type t) {
    vector<Expr> possible_identities = {make_const(t, 0), make_const(t, 1), t.min(), t.max()};
    // For unary op (the one where 'x' does not appear), any value would be fine
    for (const Expr &val : possible_identities) {
        Expr subs = substitute(op_y, val, bin_op);
        subs = common_subexpression_elimination(subs);
        Expr compare = simplify(subs == Variable::make(t, op_x));
        if (is_one(compare)) {
            return val;
        }
    }
    debug(4) << "Failed to find identity of " << bin_op << "\n";
    return Expr(); // Fail to find the identity
}

// Given a binary expression operator 'bin_op' in the form of op(x, y), prove that
// 'bin_op' is associative, i.e. prove that (x op y) op z == x op (y op z).
// TODO(psuriana): The current implementation can't prove associativity of select()
// as in argmax/argmin
bool is_bin_op_associative(Expr bin_op, const string &op_x, const string &op_y, Type t) {
    Expr x = Variable::make(t, op_x);
    Expr y = Variable::make(t, op_y);
    string op_z = unique_name("_z");
    Expr z = Variable::make(t, op_z);

    Expr lhs = substitute(op_y, z, bin_op);
    lhs = substitute(op_x, bin_op, lhs);

    Expr rhs = substitute({{op_x, y}, {op_y, z}}, bin_op);
    rhs = substitute(op_y, rhs, bin_op);

    // Canonicalize the lhs and rhs before comparing them so that we get
    // a better chance of simplifying the equality.
    vector<string> vars = {op_x, op_y, op_z};
    for (const string &v : vars) {
        lhs = solve_expression(lhs, v);
        rhs = solve_expression(rhs, v);
    }

    Expr compare = simplify(simplify(common_subexpression_elimination(lhs == rhs)));
    return is_one(compare);
}

} // anonymous namespace


pair<bool, vector<AssociativeOp>> prove_associativity(const string &f, vector<Expr> args,
                                                      vector<Expr> exprs) {
    vector<AssociativeOp> ops;
    map<int, Expr> self_ref_subs;
    map<Expr, string, ExprCompare> y_subs;

    // For a Tuple of exprs to be associative, each element of the Tuple
    // has to be associative
    for (size_t idx = 0; idx < exprs.size(); ++idx) {
        Expr expr = simplify(exprs[idx]);
        for (Expr &arg : args) {
            arg = common_subexpression_elimination(arg);
            arg = simplify(arg);
            arg = SubstituteInAllLets().mutate(arg);
        }
        string op_x = unique_name("_x_" + std::to_string(idx));
        string op_y = unique_name("_y_" + std::to_string(idx));

        // Replace any self-reference to Func 'f' with a Var
        ConvertSelfRef csr(f, args, idx, op_x, &self_ref_subs);
        expr = csr.mutate(expr);
        if (csr.is_not_associative) {
            return std::make_pair(false, vector<AssociativeOp>());
        }

        expr = common_subexpression_elimination(expr);
        expr = simplify(expr);
        expr = SubstituteInAllLets().mutate(expr);
        for (const auto &iter : self_ref_subs) {
            const Variable *v = iter.second.as<Variable>();
            internal_assert(v);
            expr = solve_expression(expr, v->name); // Move 'x' to the left as possible
        }
        if (!expr.defined()) {
            return std::make_pair(false, vector<AssociativeOp>());
        }
        // Need to substitute in all lets again since 'solve_expression' might
        // introduce some new let stmts
        expr = SubstituteInAllLets().mutate(expr);

        // Try to infer the 'y' part of the operator. If we couldn't find
        // a single 'y' that satisfy the operator, give up
        ExtractBinaryOp conv(f, args, self_ref_subs, op_y, y_subs);
        expr = conv.mutate(expr);
        if (!conv.is_solvable) {
            return std::make_pair(false, vector<AssociativeOp>());
        }

        Expr y_part = conv.current_y;
        internal_assert(y_part.defined());
        y_subs.emplace(y_part, op_y);

        if (self_ref_subs.count(idx) == 0) {
            internal_assert(!csr.current_x.defined());
            if (is_const(y_part)) {
                // Update with a constant is associative and the identity can be
                // anything since it's going to be replaced anyway
                ops.push_back({expr, 0, {"", Expr()}, {op_y, y_part}});
                continue;
            } else {
                debug(4) << "Update by non-constant is not associative\n";
                return std::make_pair(false, vector<AssociativeOp>());
            }
        }

        internal_assert(self_ref_subs.count(idx));
        internal_assert(csr.current_x.defined());

        Type type_y = y_part.type();
        Type type_x = self_ref_subs[idx].type();
        if (type_y != type_x) {
            return std::make_pair(false, vector<AssociativeOp>());
        }

        // After we managed to extract the operator, try to prove its
        // associativity
        if (!is_bin_op_associative(expr, op_x, op_y, type_y)){
            return std::make_pair(false, vector<AssociativeOp>());
        }

        // We managed to prove associativity of the operator. Now, try to
        // find its identity
        Expr identity = find_identity(expr, op_x, op_y, type_y);
        if (!identity.defined()) {
            // Failed to find an identity
            return std::make_pair(false, vector<AssociativeOp>());
        }
        ops.push_back({expr, identity, {op_x, csr.current_x}, {op_y, y_part}});
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
        << "  instead of " << is_associative << "\n";
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
        }
    }

    for (const auto &op : result.second) {
        debug(4) << "Operator: " << op.op << "\n";
        debug(4) << "   identity: " << op.identity << "\n";
        debug(4) << "   x: " << op.x.first << " -> " << op.x.second << "\n";
        debug(4) << "   y: " << op.y.first << " -> " << op.y.second << "\n";
    }
    debug(4) << "\n";
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
                        true, {{Expr(), Int(32).max(), {"", f_call_0}, {"", y + Cast::make(Int(16), z)}}});

    // f(x) = f(x) + g(rx) + y + z
    check_associativity("f", {x}, {y + z + f_call_0},
                        true, {{Expr(), make_const(Int(32), 0), {"", f_call_0}, {"", y + z}}});

    // f(x) = max(y, f(x))
    check_associativity("f", {x}, {max(y, f_call_0)},
                        true, {{Expr(), Int(32).min(), {"", f_call_0}, {"", y}}});

    // f(x) = Tuple(2, 3, f(x)[2] + z)
    check_associativity("f", {x}, {2, 3, f_call_2 + z},
                        true,
                        {{Expr(), make_const(Int(32), 0), {"", Expr()}, {"", 2}},
                         {Expr(), make_const(Int(32), 0), {"", Expr()}, {"", 3}},
                         {Expr(), make_const(Int(32), 0), {"", f_call_2}, {"", z}},
                        });

    // f(x) = Tuple(min(f(x)[0], g(rx)), f(x)[1]*g(x)*2, f(x)[2] + z)
    check_associativity("f", {x}, {min(f_call_0, g_call), f_call_1*g_call*2, f_call_2 + z},
                        true,
                        {{Expr(), Int(32).max(), {"", f_call_0}, {"", g_call}},
                         {Expr(), make_const(Int(32), 1), {"", f_call_1}, {"", g_call*2}},
                         {Expr(), make_const(Int(32), 0), {"", f_call_2}, {"", z}},
                        });

    // f(x) = max(f(x) + g(rx), g(rx)) -> not associative
    check_associativity("f", {x}, {max(f_call_0 + g_call, g_call)},
                        false, {});

    // f(x) = max(f(x) + g(rx), f(x) - 3) -> f(x) + max(g(rx) - 3)
    check_associativity("f", {x}, {max(f_call_0 + g_call, f_call_0 - 3)},
                        true, {{Expr(), 0, {"", f_call_0}, {"", max(g_call, -3)}}});

    // f(x) = f(x) - g(rx) -> not associative
    check_associativity("f", {x}, {f_call_0 - g_call},
                        false, {});

    std::cout << "Associativity test passed" << std::endl;
}


}
}
