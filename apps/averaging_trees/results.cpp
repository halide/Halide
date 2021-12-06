// Print stats for the averaging trees described directly in the paper
#include <Halide.h>

using namespace Halide;
using namespace Halide::Internal;

Var x;

Expr widen(Expr a) {
    return cast(a.type().with_bits(a.type().bits() * 2), a);
}

Expr narrow(Expr a) {
    return cast(a.type().with_bits(a.type().bits() / 2), a);
}

Expr avg_u(Expr a, Expr b) {
    // return Internal::rounding_halving_add(a, b);
    return narrow((widen(a) + b + 1) / 2);
}

Expr avg_d(Expr a, Expr b) {
    // return Internal::halving_add(a, b);
    return narrow((widen(a) + b) / 2);
}

Expr k11(Expr v0, Expr v1) {
    Expr v2 = avg_u(v0, v1);  //  Kernel: 1 1  : 0.25 0 0.5
    Expr v3 = avg_u(v0, v2);  //  Kernel: 3 1  : 0.375 0 0.75
    Expr v4 = avg_d(v1, v2);  //  Kernel: 1 3  : -0.125 -0.5 0.25
    Expr v5 = avg_d(v3, v4);  //  Kernel: 4 4  : 0 -0.5 0.5
    return v5;
}

Expr k112(Expr v2, Expr v0, Expr v1) {
    Expr v3 = avg_u(v0, v1);  //  Kernel: 1 1 0  : 0.25 0 0.5
    Expr v4 = avg_u(v0, v2);  //  Kernel: 1 0 1  : 0.25 0 0.5
    Expr v5 = avg_d(v3, v4);  //  Kernel: 2 1 1  : 0 -0.5 0.5
    return v5;
}

Expr k1111(Expr v0, Expr v1, Expr v2, Expr v3) {
    Expr v4 = avg_u(v0, v1);  //  Kernel: 1 1 0 0  : 0.25 0 0.5
    Expr v5 = avg_u(v2, v3);  //  Kernel: 0 0 1 1  : 0.25 0 0.5
    Expr v6 = avg_d(v4, v5);  //  Kernel: 1 1 1 1  : 0 -0.5 0.5
    return v6;
}

Expr k1133(Expr v2, Expr v3, Expr v0, Expr v1) {
    Expr v4 = avg_d(v0, v1);  //  Kernel: 1 1 0 0  : -0.25 -0.5 0
    Expr v5 = avg_u(v0, v1);  //  Kernel: 1 1 0 0  : 0.25 0 0.5
    Expr v6 = avg_u(v2, v3);  //  Kernel: 0 0 1 1  : 0.25 0 0.5
    Expr v7 = avg_u(v4, v6);  //  Kernel: 1 1 1 1  : 0.25 -0.25 0.75
    Expr v8 = avg_d(v5, v7);  //  Kernel: 3 3 1 1  : 0 -0.5 0.5
    return v8;
}

Expr k1339(Expr v1, Expr v2, Expr v3, Expr v0) {
    Expr v4 = avg_d(v0, v1);  //  Kernel: 1 1 0 0  : -0.25 -0.5 0
    Expr v5 = avg_u(v0, v1);  //  Kernel: 1 1 0 0  : 0.25 0 0.5
    Expr v6 = avg_u(v2, v3);  //  Kernel: 0 0 1 1  : 0.25 0 0.5
    Expr v7 = avg_u(v4, v6);  //  Kernel: 1 1 1 1  : 0.25 -0.25 0.75
    Expr v8 = avg_u(v5, v7);  //  Kernel: 3 3 1 1  : 0.5 0 1
    Expr v9 = avg_d(v2, v8);  //  Kernel: 3 3 9 1  : 0 -0.5 0.5
    return v9;
}

Expr k13(Expr v1, Expr v0) {
    Expr v2 = avg_d(v0, v1);  //  Kernel: 1 1  : -0.25 -0.5 0
    Expr v3 = avg_u(v0, v1);  //  Kernel: 1 1  : 0.25 0 0.5
    Expr v4 = avg_u(v0, v2);  //  Kernel: 3 1  : 0.125 -0.25 0.5
    Expr v5 = avg_u(v0, v4);  //  Kernel: 7 1  : 0.3125 -0.125 0.75
    Expr v6 = avg_d(v3, v4);  //  Kernel: 5 3  : -0.0625 -0.5 0.375
    Expr v7 = avg_d(v5, v6);  //  Kernel: 12 4  : 0 -0.5 0.5
    return v7;
}

Expr k11446(Expr v0, Expr v4, Expr v1, Expr v3, Expr v2) {
    Expr v5 = avg_d(v0, v4);     //  Kernel: 1 0 0 0 1  : -0.25 -0.5 0
    Expr v6 = avg_d(v2, v5);     //  Kernel: 1 0 2 0 1  : -0.375 -0.75 0
    Expr v7 = avg_u(v2, v6);     //  Kernel: 1 0 6 0 1  : 0.0625 -0.375 0.5
    Expr v8 = avg_d(v1, v3);     //  Kernel: 0 1 0 1 0  : -0.25 -0.5 0
    Expr v9 = avg_u(v7, v8);     //  Kernel: 1 4 6 4 1  : 0.15625 -0.4375 0.75
    Expr v10 = avg_u(v0, v4);    //  Kernel: 1 0 0 0 1  : 0.25 0 0.5
    Expr v11 = avg_u(v2, v10);   //  Kernel: 1 0 2 0 1  : 0.375 0 0.75
    Expr v12 = avg_u(v2, v11);   //  Kernel: 1 0 6 0 1  : 0.4375 0 0.875
    Expr v13 = avg_u(v1, v3);    //  Kernel: 0 1 0 1 0  : 0.25 0 0.5
    Expr v14 = avg_d(v12, v13);  //  Kernel: 1 4 6 4 1  : 0.09375 -0.5 0.6875
    Expr v15 = avg_d(v9, v14);   //  Kernel: 2 8 12 8 2  : 0 -0.5 0.5
    return v15;
}

void show(const Expr &e, const std::string &name) {
    Func f(name);
    f(x) = e;
    f.vectorize(x, 16, TailStrategy::RoundUp);
    f.align_bounds(x, 16);
    std::cout << "Writing " << name << ".s\n";
    f.compile_to_assembly(name + ".s", f.infer_arguments(), Target{"x86-64-avx2-linux-no_runtime-no_asserts-no_bounds_query-disable_llvm_loop_opt"});
}

void show(const std::vector<int> &kernel) {
    int kernel_sum = 0;
    ImageParam input(UInt(16), 1);
    ImageParam white_noise(UInt(16), 1);
    std::string k_str;
    std::vector<Expr> vals;
    Expr e;
    size_t i = 0;
    if (false && kernel.size() >= 2 && kernel[0] == 1 && kernel[1] == 1) {
        // Doesn't actually seem to lower op count, so disabled.
        e = Internal::widening_add(input(x), input(x + 1));
        kernel_sum = 2;
        k_str = "11";
        i = 2;
    } else {
        e = cast<uint32_t>(0);
    }
    for (; i < kernel.size(); i++) {
        Expr v = cast<uint32_t>(input(x + (int)i)) * kernel[i];
        e += v;
        kernel_sum += kernel[i];
        k_str += std::to_string(kernel[i]);
    }

    while (kernel_sum & (kernel_sum - 1)) {
        kernel_sum--;
    }
    assert(kernel_sum > 0);

    int kernel_sum_bits = 0;
    while ((1 << kernel_sum_bits) < kernel_sum) {
        kernel_sum_bits++;
    }

    // Round up
    {
        Expr r = e;
        r = rounding_shift_right(r, kernel_sum_bits);
        /*
        r += kernel_sum / 2;
        r /= kernel_sum;
        */
        r = cast<uint16_t>(r);
        show(r, "up" + k_str);
    }

    // Round to even
    {
        Expr r = e;
        r += kernel_sum / 2 - 1;
        // If the result rounding down would be odd, add one before rounding
        r += (r & kernel_sum) / kernel_sum;
        r /= kernel_sum;
        r = cast<uint16_t>(r);
        show(r, "even" + k_str);
    }

    // Dither
    {
        Expr r = e;
        Expr dither_idx = (((x >> 4) * 37) & 0xff) + (x & 15);
        r += white_noise(dither_idx) & (kernel_sum - 1);
        r /= kernel_sum;
        r = cast<uint16_t>(r);
        show(r, "dither" + k_str);
    }
}

void show_averaging_trees() {
    ImageParam input(UInt(16), 1);
    Expr inputs[5];
    for (int i = 0; i < 5; i++) {
        inputs[i] = input(x + i);
    }
    show(k11(inputs[0], inputs[1]), "k11");
    show(k112(inputs[0], inputs[1], inputs[2]), "k112");
    show(k1111(inputs[0], inputs[1], inputs[2], inputs[3]), "k1111");
    show(k1133(inputs[0], inputs[1], inputs[2], inputs[3]), "k1133");
    show(k13(inputs[0], inputs[1]), "k13");
    show(k1339(inputs[0], inputs[1], inputs[2], inputs[3]), "k1339");
    show(k11446(inputs[0], inputs[1], inputs[2], inputs[3], inputs[4]), "k11446");
}

int main(int argc, char **argv) {
    show({1, 1});
    show({1, 1, 1, 1});
    show({1, 1, 2});
    show({1, 1, 3, 3});
    show({1, 3});
    show({1, 3, 3, 9});
    show({1, 1, 4, 4, 6});
    show_averaging_trees();
    return 0;
}
