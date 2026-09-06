#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

void test_scalar_equivalence() {
    ExprInterpreter interp;

    // 1. Integer scalar math equivalence
    auto math_test_int = [](const auto &x, const auto &y) {
        // Keeps values positive to align C++ truncation division with Halide's Euclidean division
        return (x + y) * (x - y) + (x / y) + (x % y);
    };

    int32_t cx = 42, cy = 5;
    int32_t c_res = math_test_int(cx, cy);

    Expr hx = Expr(cx), hy = Expr(cy);
    Expr h_ast = math_test_int(hx, hy);

    auto eval_res = interp.eval(h_ast);
    internal_assert(eval_res.type.is_int() && eval_res.type.bits() == 32 && eval_res.type.lanes() == 1);
    internal_assert(std::get<int64_t>(eval_res.lanes[0]) == c_res)
        << "Integer scalar evaluation mismatch. Expected: " << c_res
        << ", Got: " << std::get<int64_t>(eval_res.lanes[0]);

    // 2. Float scalar math equivalence
    using Halide::sin;
    using std::sin;
    auto math_test_float = [](const auto &x, const auto &y) {
        return (x * y) - sin(x / (y + 1.0f));
    };

    float fx = 3.14f, fy = 2.0f;
    float f_res = math_test_float(fx, fy);

    Expr hfx = Expr(fx), hfy = Expr(fy);
    Expr hf_ast = math_test_float(hfx, hfy);

    auto eval_f_res = interp.eval(hf_ast);
    internal_assert(eval_f_res.type.is_float() && eval_f_res.type.bits() == 32 && eval_f_res.type.lanes() == 1);

    double diff = std::abs(std::get<double>(eval_f_res.lanes[0]) - f_res);
    internal_assert(diff < 1e-5) << "Float scalar evaluation mismatch.";
}

void test_vector_operations() {
    ExprInterpreter interp;

    // 1. Ramp: create a vector <10, 13, 16, 19>
    Expr base = Expr(10);
    Expr stride = Expr(3);
    Expr ramp = Ramp::make(base, stride, 4);

    auto eval_ramp = interp.eval(ramp);
    internal_assert(eval_ramp.type.lanes() == 4);
    internal_assert(std::get<int64_t>(eval_ramp.lanes[0]) == 10);
    internal_assert(std::get<int64_t>(eval_ramp.lanes[1]) == 13);
    internal_assert(std::get<int64_t>(eval_ramp.lanes[2]) == 16);
    internal_assert(std::get<int64_t>(eval_ramp.lanes[3]) == 19);

    // 2. Broadcast: <5, 5, 5>
    Expr bc = Broadcast::make(Expr(5), 3);
    auto eval_bc = interp.eval(bc);
    internal_assert(eval_bc.type.lanes() == 3);
    internal_assert(std::get<int64_t>(eval_bc.lanes[0]) == 5);
    internal_assert(std::get<int64_t>(eval_bc.lanes[1]) == 5);
    internal_assert(std::get<int64_t>(eval_bc.lanes[2]) == 5);

    // 3. Shuffle: reverse the ramp -> <19, 16, 13, 10>
    Expr reversed = Shuffle::make({ramp}, {3, 2, 1, 0});
    auto eval_rev = interp.eval(reversed);
    internal_assert(eval_rev.type.lanes() == 4);
    internal_assert(std::get<int64_t>(eval_rev.lanes[0]) == 19);
    internal_assert(std::get<int64_t>(eval_rev.lanes[1]) == 16);
    internal_assert(std::get<int64_t>(eval_rev.lanes[2]) == 13);
    internal_assert(std::get<int64_t>(eval_rev.lanes[3]) == 10);

    // 4. VectorReduce: Sum the ramp -> 10 + 13 + 16 + 19 = 58
    Expr sum = VectorReduce::make(VectorReduce::Add, ramp, 1);
    auto eval_sum = interp.eval(sum);
    internal_assert(eval_sum.type.lanes() == 1);
    internal_assert(std::get<int64_t>(eval_sum.lanes[0]) == 58);

    // 5. Ramp of Ramp
    Expr ramp_of_ramp = Ramp::make(ramp, Broadcast::make(100, 4), 4);
    auto eval_ror = interp.eval(ramp_of_ramp);
    internal_assert(eval_ror.type.lanes() == 16);
    for (int i = 0; i < 4; ++i) {
        internal_assert(std::get<int64_t>(eval_ror.lanes[4 * i + 0]) == 100 * i + 10);
        internal_assert(std::get<int64_t>(eval_ror.lanes[4 * i + 1]) == 100 * i + 13);
        internal_assert(std::get<int64_t>(eval_ror.lanes[4 * i + 2]) == 100 * i + 16);
        internal_assert(std::get<int64_t>(eval_ror.lanes[4 * i + 3]) == 100 * i + 19);
    }

    // 6. Broadcast of Ramp
    Expr bc_of_ramp = Broadcast::make(ramp, 5);
    auto eval_bor = interp.eval(bc_of_ramp);
    internal_assert(eval_bor.type.lanes() == 20);
    for (int i = 0; i < 5; ++i) {
        internal_assert(std::get<int64_t>(eval_bor.lanes[4 * i + 0]) == 10);
        internal_assert(std::get<int64_t>(eval_bor.lanes[4 * i + 1]) == 13);
        internal_assert(std::get<int64_t>(eval_bor.lanes[4 * i + 2]) == 16);
        internal_assert(std::get<int64_t>(eval_bor.lanes[4 * i + 3]) == 19);
    }
}

void test_let_and_scoping() {
    ExprInterpreter interp;

    // Test: let x = 42 in (let x = x + 8 in x * 2)
    // Inner scoping should shadow outer scoping and evaluate cleanly
    Expr var_x = Variable::make(Int(32), "x");
    Expr inner_let = Let::make("x", var_x + Expr(8), var_x * Expr(2));
    Expr outer_let = Let::make("x", Expr(42), inner_let);

    auto res = interp.eval(outer_let);
    internal_assert(res.type.is_int() && res.type.lanes() == 1);

    // (42 + 8) * 2 = 100
    internal_assert(std::get<int64_t>(res.lanes[0]) == 100)
        << "Variable scoping / Let evaluation failed.";
}
}  // namespace

int main() {
    test_scalar_equivalence();
    test_vector_operations();
    test_let_and_scoping();

    printf("Success!\n");
    return 0;
}
