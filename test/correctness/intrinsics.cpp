#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

#define internal_assert _halide_user_assert

Expr signed_widen(Expr x) {
    return cast(x.type().with_bits(x.type().bits() * 2).with_code(halide_type_int), x);
}

Expr saturating_narrow(Expr x) {
    return saturating_cast(x.type().with_bits(x.type().bits() / 2), x);
}

void run_tests(int base_bits, int lanes) {
    Expr xi = Variable::make(Int(base_bits, lanes), "xi");
    Expr yi = Variable::make(Int(base_bits, lanes), "yi");
    Expr xu = Variable::make(UInt(base_bits, lanes), "xu");
    Expr yu = Variable::make(UInt(base_bits, lanes), "yu");
    Expr xf = Variable::make(Float(base_bits, lanes), "xf");
    Expr yf = Variable::make(Float(base_bits, lanes), "yf");

    Expr xiw = Variable::make(Int(base_bits * 2, lanes), "xiw");
    Expr yiw = Variable::make(Int(base_bits * 2, lanes), "yiw");
    Expr xuw = Variable::make(UInt(base_bits * 2, lanes), "xuw");
    Expr yuw = Variable::make(UInt(base_bits * 2, lanes), "yuw");
    Expr xfw = Variable::make(Float(base_bits * 2, lanes), "xfw");
    Expr yfw = Variable::make(Float(base_bits * 2, lanes), "yfw");

    struct Test {
        Call::IntrinsicOp intrinsic;
        std::vector<Expr> tests;
        std::vector<Expr> negative_tests;
    };

    Test tests[] =
    {
        {
            Call::shift_left,
            {
                { xi * 2 },
                { xu * 4 },
            },
            { { xi * 3 } }
        },
        {
            Call::shift_right,
            {
                { xi / 2 },
                { xu / 4 },
            },
            { { xi / 3 } }
        },
        {
            Call::widening_shift_left,
            {
                { widen(xi) * 2 },
                { widen(xu) * 4 },
            },
            { { widen(xi) * 3 } }
        },
        {
            Call::rounding_shift_right,
            {
                { narrow((widen(xi) + 8) / 16) },
                { narrow(widening_add(xi, 4) / 8) },
                { saturating_add(xi, 1) / 2 },
            },
        },
        {
            Call::widening_add,
            {
                { widen(xi) + yi },
                { widen(xu) + yu },
                { signed_widen(xu) + yu },
                { widen(xf) + yf },
            },
            {
                { signed_widen(xu) + yi },
                { widen(xf) + yi },
            }
        },
        {
            Call::widening_sub,
            {
                { widen(xi) - yi },
                { signed_widen(xu) - yu },
                { widen(xf) - yf },
            },
            {
                { signed_widen(xu) - yi },
            }
        },
        {
            Call::widening_mul,
            {
                { widen(xi) * yi },
                { widen(xu) * yu },
                { widen(xf) * yf },
            },
            {
                { widen(xi) * yu },
            },
        },
        {
            Call::saturating_add,
            {
                { saturating_narrow(widen(xi) + yi) },
                { saturating_narrow(widen(xu) + yu) },
            },
        },
        {
            Call::saturating_sub,
            {
                { saturating_narrow(widen(xi) - yi) },
            },
        },
        {
            Call::halving_add,
            {
                { narrow((widen(xi) + yi) / 2) },
                { narrow((widen(xu) + yu) / 2) },
                { narrow(widening_add(xi, yi) / 2) },
                { narrow(widening_add(xu, yu) / 2) },
            },
        },
        {
            Call::halving_sub,
            {
                { narrow((widen(xi) - yi) / 2) },
                //{ narrow((widen(xu) - yu) / 2) },
                { narrow(widening_sub(xi, yi) / 2) },
                //{ narrow(widening_sub(xu, yu) / 2) },
            },
        },
        {
            Call::rounding_halving_add,
            {
                { narrow((widen(xi) + yi + 1) / 2) },
                { narrow((widen(xu) + yu + 1) / 2) },
            },
        },
        {
            Call::rounding_halving_sub,
            {
                { narrow((widen(xi) - yi + 1) / 2) },
                //{ narrow((widen(xu) - yu + 1) / 2) },
            },
        },
    };

    for (const Test &test : tests) {
        for (const Expr &i : test.tests) {
            Expr lowered = pattern_match_intrinsics(i);
            if (const Cast *cast = lowered.as<Cast>()) {
                if (cast->type.bits() == cast->value.type().bits()) {
                    // Ignore casts that don't change type.
                    lowered = cast->value;
                }
            }
            if (!Call::as_intrinsic(lowered, {test.intrinsic})) {
                printf("Failed to match intrinsic %s\n", Call::get_intrinsic_name(test.intrinsic));
                std::cout << "input: " << i << "\n";
                std::cout << "got: " << lowered << "\n";
                exit(-1);
            }
        }
    }
}

int main(int argc, char **argv) {

    for (int bits : {8, 16, 32}) {
        for (int lanes : {1, 2, 3, 4}) {
            run_tests(bits, lanes);
        }
    }

    printf("Success!\n");
    return 0;
}
