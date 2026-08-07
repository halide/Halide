#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

using std::map;
using std::string;
using std::vector;

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

void check_associativity(const string &f, const vector<Expr> &args, const vector<Expr> &exprs,
                         const AssociativeOp &assoc_op) {
    auto result = prove_associativity(f, args, exprs);
    internal_assert(result.associative() == assoc_op.associative())
        << "Checking associativity: " << print_args(f, args, exprs) << "\n"
        << "  Expect is associative: " << assoc_op.associative() << "\n"
        << "  instead of " << result.associative() << "\n";
    if (assoc_op.associative()) {
        map<string, Expr> replacement;
        for (size_t i = 0; i < assoc_op.size(); ++i) {
            internal_assert(equal(result.pattern.identities[i], assoc_op.pattern.identities[i]))
                << "Checking associativity: " << print_args(f, args, exprs) << "\n"
                << "  Index: " << i << "\n"
                << "  Expect identity: " << assoc_op.pattern.identities[i] << "\n"
                << "  instead of " << result.pattern.identities[i] << "\n";
            internal_assert(equal(result.xs[i].expr, assoc_op.xs[i].expr))
                << "Checking associativity: " << print_args(f, args, exprs) << "\n"
                << "  Index: " << i << "\n"
                << "  Expect x: " << assoc_op.xs[i].expr << "\n"
                << "  instead of " << result.xs[i].expr << "\n";
            internal_assert(equal(result.ys[i].expr, assoc_op.ys[i].expr))
                << "Checking associativity: " << print_args(f, args, exprs) << "\n"
                << "  Index: " << i << "\n"
                << "  Expect y: " << assoc_op.ys[i].expr << "\n"
                << "  instead of " << result.ys[i].expr << "\n";

            if (result.xs[i].expr.defined()) {
                replacement.emplace(assoc_op.xs[i].var, Variable::make(result.xs[i].expr.type(), result.xs[i].var));
            }
            if (result.ys[i].expr.defined()) {
                replacement.emplace(assoc_op.ys[i].var, Variable::make(result.ys[i].expr.type(), result.ys[i].var));
            }
        }
        for (size_t i = 0; i < assoc_op.size(); ++i) {
            Expr expected_op = substitute(replacement, assoc_op.pattern.ops[i]);

            internal_assert(equal(result.pattern.ops[i], expected_op))
                << "Checking associativity: " << print_args(f, args, exprs) << "\n"
                << "  Index: " << i << "\n"
                << "  Expect bin op: " << expected_op << "\n"
                << "  instead of " << result.pattern.ops[i] << "\n";

            debug(5) << "\nExpected op: " << expected_op << "\n";
            debug(5) << "Operator: " << result.pattern.ops[i] << "\n";
            debug(5) << "   identity: " << result.pattern.identities[i] << "\n";
            debug(5) << "   x: " << result.xs[i].var << " -> " << result.xs[i].expr << "\n";
            debug(5) << "   y: " << result.ys[i].var << " -> " << result.ys[i].expr << "\n";
        }
    }
}

}  // namespace

int main() {
    typedef AssociativeOp::Replacement Replacement;

    {
        // Tests for saturating addition
        Type t = UInt(8);
        Expr x = Variable::make(t, "x");
        Expr y = Variable::make(t, "y");
        Expr x_idx = Variable::make(Int(32), "x_idx");
        Expr f_call_0 = Call::make(t, "f", {x_idx}, Call::CallType::Halide, FunctionPtr(), 0);

        for (const Expr &e : {cast<uint8_t>(min(cast<uint16_t>(x) + y, 255)),
                              select(x > 255 - y, make_const(UInt(8), 255), x + y),
                              select(x < ~y, x + y, make_const(UInt(8), 255)),
                              saturating_add(x, y),
                              saturating_add(y, x),
                              saturating_cast<uint8_t>(widening_add(x, y))}) {
            check_associativity("f", {x_idx}, {substitute("x", f_call_0, e)},
                                AssociativeOp(
                                    AssociativePattern(solve_expression(e, "x").result,
                                                       make_const(t, 0), true),
                                    {Replacement("x", f_call_0)},
                                    {Replacement("y", y)},
                                    true));
        }
    }

    {
        // Tests for logical And/Or
        Type t = UInt(1);
        Expr x = Variable::make(t, "x");
        Expr y = Variable::make(t, "y");
        Expr x_idx = Variable::make(Int(32), "x_idx");
        Expr f_call_0 = Call::make(t, "f", {x_idx}, Call::CallType::Halide, FunctionPtr(), 0);

        // f(x) = y && f(x)
        check_associativity("f", {x_idx}, {And::make(y, f_call_0)},
                            AssociativeOp(
                                AssociativePattern(And::make(x, y), const_true(), true),
                                {Replacement("x", f_call_0)},
                                {Replacement("y", y)},
                                true));

        // f(x) = y || f(x)
        check_associativity("f", {x_idx}, {Or::make(y, f_call_0)},
                            AssociativeOp(
                                AssociativePattern(Or::make(x, y), const_false(), true),
                                {Replacement("x", f_call_0)},
                                {Replacement("y", y)},
                                true));
    }

    {
        // Tests for 1D reduction
        Type t = Int(32);
        Expr x = Variable::make(t, "x");
        Expr y = Variable::make(t, "y");
        Expr z = Variable::make(t, "z");
        Expr rx = Variable::make(t, "rx");
        Expr f_call_0 = Call::make(t, "f", {x}, Call::CallType::Halide, FunctionPtr(), 0);
        Expr g_call_0 = Call::make(t, "g", {rx}, Call::CallType::Halide, FunctionPtr(), 0);

        // f(x) = f(x)
        check_associativity("f", {x}, {f_call_0},
                            AssociativeOp(
                                AssociativePattern(x, make_const(t, 0), true),
                                {Replacement("x", f_call_0)},
                                {Replacement("", Expr())},
                                true));

        // f(x) = min(f(x), y + int16(z))
        check_associativity("f", {x}, {min(f_call_0, y + Cast::make(Int(16), z))},
                            AssociativeOp(
                                AssociativePattern(min(x, y), t.max(), true),
                                {Replacement("x", f_call_0)},
                                {Replacement("y", y + Cast::make(Int(16), z))},
                                true));

        // f(x) = f(x) + g(rx) + y + z
        check_associativity("f", {x}, {y + z + f_call_0},
                            AssociativeOp(
                                AssociativePattern(x + y, make_const(t, 0), true),
                                {Replacement("x", f_call_0)},
                                {Replacement("y", y + z)},
                                true));

        // f(x) = max(y, f(x))
        check_associativity("f", {x}, {max(y, f_call_0)},
                            AssociativeOp(
                                AssociativePattern(max(x, y), t.min(), true),
                                {Replacement("x", f_call_0)},
                                {Replacement("y", y)},
                                true));

        // f(x) = max(f(x) + g(rx), g(rx)) -> not associative
        check_associativity("f", {x}, {max(f_call_0 + g_call_0, g_call_0)}, AssociativeOp());

        // f(x) = max(f(x) + g(rx), f(x) - 3) -> f(x) + max(g(rx) - 3)
        check_associativity("f", {x}, {max(f_call_0 + g_call_0, f_call_0 - 3)},
                            AssociativeOp(
                                AssociativePattern(x + y, 0, true),
                                {Replacement("x", f_call_0)},
                                {Replacement("y", max(g_call_0, -3))},
                                true));

        // f(x) = max(max(min(f(x), g(rx) + 2), f(x)), g(rx) + 2) -> can be simplified into max(f(x), g(rx) + 2)
        check_associativity("f", {x}, {max(max(min(f_call_0, g_call_0 + 2), f_call_0), g_call_0 + 2)},
                            AssociativeOp(
                                AssociativePattern(max(x, y), t.min(), true),
                                {Replacement("x", f_call_0)},
                                {Replacement("y", g_call_0 + 2)},
                                true));

        // f(x) = max(x0, f(x)) -> x0 may conflict with the wildcard associative op pattern
        Expr x0 = Variable::make(t, "x0");
        check_associativity("f", {x}, {max(x0, f_call_0)},
                            AssociativeOp(
                                AssociativePattern(max(x, y), t.min(), true),
                                {Replacement("x", f_call_0)},
                                {Replacement("y", x0)},
                                true));
    }

    {
        // Tests for multi-dimensional reduction (with mixed types)
        Type t = Int(32);
        Expr x = Variable::make(t, "x");
        Expr y = Variable::make(t, "y");
        Expr z = Variable::make(t, "z");
        Expr rx = Variable::make(t, "rx");

        vector<Type> ts = {Int(32), Int(32), Float(32)};
        vector<Expr> xs(3), ys(3), zs(3);
        for (size_t i = 0; i < xs.size(); ++i) {
            xs[i] = Variable::make(ts[i], "x" + std::to_string(i));
            ys[i] = Variable::make(ts[i], "y" + std::to_string(i));
            zs[i] = Variable::make(ts[i], "z" + std::to_string(i));
        }

        Expr f_call_0 = Call::make(ts[0], "f", {x}, Call::CallType::Halide, FunctionPtr(), 0);
        Expr f_call_1 = Call::make(ts[1], "f", {x}, Call::CallType::Halide, FunctionPtr(), 1);
        Expr f_call_2 = Call::make(ts[2], "f", {x}, Call::CallType::Halide, FunctionPtr(), 2);
        Expr g_call_0 = Call::make(ts[0], "g", {rx}, Call::CallType::Halide, FunctionPtr(), 0);
        Expr g_call_1 = Call::make(ts[1], "g", {rx}, Call::CallType::Halide, FunctionPtr(), 1);

        // f(x) = Tuple(f(x)[0], f(x)[2] + z)
        check_associativity("f", {x}, {f_call_0, f_call_1 + cast(ts[1], z)},
                            AssociativeOp(
                                AssociativePattern({xs[0], xs[1] + ys[1]},
                                                   {make_const(ts[0], 0), make_const(ts[1], 0)},
                                                   true),
                                {Replacement("x0", f_call_0), Replacement("x1", f_call_1)},
                                {Replacement("", Expr()), Replacement("y1", cast(ts[1], z))},
                                true));

        // f(x) = Tuple(min(f(x)[0], g(rx)), f(x)[1]*g(x)*2, f(x)[2] + z)
        check_associativity("f", {x}, {min(f_call_0, g_call_0), f_call_1 * g_call_0 * 2, f_call_2 + cast(ts[2], z)},
                            AssociativeOp(
                                AssociativePattern(
                                    {min(xs[0], ys[0]), xs[1] * ys[1], xs[2] + ys[2]},
                                    {ts[0].max(), make_const(ts[1], 1), make_const(ts[2], 0)},
                                    true),
                                {Replacement("x0", f_call_0), Replacement("x1", f_call_1), Replacement("x2", f_call_2)},
                                {Replacement("y0", g_call_0), Replacement("y1", g_call_0 * 2), Replacement("y2", cast(ts[2], z))},
                                true));

        // Complex multiplication: f(x) = Tuple(f(x)[0]*g(r.x)[0] - f(x)[1]*g(r.x)[1], f(x)[0]*g(r.x)[1] + f(x)[1]*g(r.x)[0])
        check_associativity("f", {x}, {f_call_0 * g_call_0 - f_call_1 * g_call_1, f_call_0 * g_call_1 + f_call_1 * g_call_0},
                            AssociativeOp(
                                AssociativePattern(
                                    {xs[0] * ys[0] - ys[1] * xs[1], xs[1] * ys[0] + ys[1] * xs[0]},
                                    {make_const(ts[0], 1), make_const(ts[1], 0)},
                                    true),
                                {Replacement("x0", f_call_0), Replacement("x1", f_call_1)},
                                {Replacement("y0", g_call_0), Replacement("y1", g_call_1)},
                                true));

        // 1D argmin: f(x) = Tuple(min(f(x)[0], g(r.x)[0]), select(f(x)[0] < g(r.x)[0], f(x)[1], g(r.x)[1])
        check_associativity("f", {x}, {min(f_call_0, g_call_0), select(f_call_0 < g_call_0, f_call_1, g_call_1)},
                            AssociativeOp(
                                AssociativePattern(
                                    {min(xs[0], ys[0]), select(xs[0] < ys[0], xs[1], ys[1])},
                                    {ts[0].max(), make_const(ts[1], 0)},
                                    true),
                                {Replacement("x0", f_call_0), Replacement("x1", f_call_1)},
                                {Replacement("y0", g_call_0), Replacement("y1", g_call_1)},
                                true));
    }

    {
        Type t = Int(32);
        Expr x = Variable::make(t, "x");
        Expr y = Variable::make(t, "y");
        Expr rx = Variable::make(t, "rx");
        Expr ry = Variable::make(t, "ry");

        vector<Type> ts = {UInt(8), Int(32), Int(16), Float(32)};
        vector<Expr> xs(4), ys(4), zs(4);
        for (size_t i = 0; i < xs.size(); ++i) {
            xs[i] = Variable::make(ts[i], "x" + std::to_string(i));
            ys[i] = Variable::make(ts[i], "y" + std::to_string(i));
            zs[i] = Variable::make(ts[i], "z" + std::to_string(i));
        }

        Expr f_xy_call_0 = Call::make(ts[0], "f", {x, y}, Call::CallType::Halide, FunctionPtr(), 0);
        Expr f_xy_call_1 = Call::make(ts[1], "f", {x, y}, Call::CallType::Halide, FunctionPtr(), 1);
        Expr f_xy_call_2 = Call::make(ts[2], "f", {x, y}, Call::CallType::Halide, FunctionPtr(), 2);
        Expr f_xy_call_3 = Call::make(ts[3], "f", {x, y}, Call::CallType::Halide, FunctionPtr(), 3);
        Expr g_xy_call_0 = Call::make(ts[0], "g", {rx, ry}, Call::CallType::Halide, FunctionPtr(), 0);

        // 2D argmin + sum
        // f(x, y) = Tuple(min(f(x, y)[0], g(r.x, r.y)[0]),
        //                 f(x, y)[1] + r.x,
        //                 select(f(x, y)[0] < g(r.x, r.y)[0], f(x)[2], r.x),
        //                 select(f(x, y)[0] < g(r.x, r.y)[0], f(x)[3], r.y))
        check_associativity("f", {x, y},
                            {min(f_xy_call_0, g_xy_call_0),
                             f_xy_call_1 + rx,
                             select(f_xy_call_0 < g_xy_call_0, f_xy_call_2, cast(Int(16), rx)),
                             select(f_xy_call_0 < g_xy_call_0, f_xy_call_3, cast(Float(32), ry))},
                            AssociativeOp(
                                AssociativePattern(
                                    {min(xs[0], ys[0]), xs[1] + ys[1], select(xs[0] < ys[0], xs[2], ys[2]), select(xs[0] < ys[0], xs[3], ys[3])},
                                    {ts[0].max(), make_const(ts[1], 0), make_const(ts[2], 0), make_const(ts[3], 0)},
                                    true),
                                {Replacement("x0", f_xy_call_0), Replacement("x1", f_xy_call_1),
                                 Replacement("x2", f_xy_call_2), Replacement("x3", f_xy_call_3)},
                                {Replacement("y0", g_xy_call_0), Replacement("y1", rx),
                                 Replacement("y2", cast(Int(16), rx)), Replacement("y3", cast(Float(32), ry))},
                                true));
    }

    printf("Success!\n");
    return 0;
}
