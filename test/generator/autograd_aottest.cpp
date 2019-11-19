#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include <cmath>
#include <cstdio>

#include "autograd.h"
#include "autograd_grad.h"

using namespace Halide::Runtime;

const int kSize = 32;

int main(int argc, char **argv) {
    int result;

    auto f = [](float a, float b, float c) -> float {
        return 33.f * std::pow(a, 3) +
               22.f * std::pow(b, 2) +
               11.f * c +
               1.f;
    };

    Buffer<float> a(kSize);
    Buffer<float> b(kSize);
    Buffer<float> c(kSize);
    Buffer<float> out(kSize);

    a.for_each_element([&](int x) { a(x) = x; });
    b.for_each_element([&](int x) { b(x) = x; });
    c.for_each_element([&](int x) { c(x) = x; });

    result = autograd(a, b, c, out);
    if (result != 0) {
        exit(-1);
    }
    out.for_each_element([&](int x) {
        float expected = f(a(x), b(x), c(x));
        float actual = out(x);
        assert(expected == actual);
    });

    Buffer<float> L(kSize);
    L.for_each_element([&](int x) { L(x) = x - kSize / 2; });

    Buffer<float> grad_loss_out_wrt_a(kSize);
    Buffer<float> grad_loss_out_wrt_b(kSize);
    Buffer<float> grad_loss_out_wrt_c(kSize);
    grad_loss_out_wrt_c.fill(12345);

    result = autograd_grad(/*inputs*/ a, b, c, L,
                           /*outputs*/ grad_loss_out_wrt_a,
                           grad_loss_out_wrt_b,
                           grad_loss_out_wrt_c);
    if (result != 0) {
        exit(-1);
    }

    // Although the values are float, all should be exact results,
    // so we don't need to worry about comparing vs. an epsilon
    grad_loss_out_wrt_a.for_each_element([&](int x) {
        // ∂𝐿/∂a = 3a^2 * 33 * L
        float expected = L(x) * std::pow(a(x), 2) * 3.f * 33.f;
        float actual = grad_loss_out_wrt_a(x);
        assert(expected == actual);
    });
    grad_loss_out_wrt_b.for_each_element([&](int x) {
        // ∂𝐿/∂b = b * 44 * L
        float expected = L(x) * b(x) * 44.f;
        float actual = grad_loss_out_wrt_b(x);
        assert(expected == actual);
    });
    grad_loss_out_wrt_c.for_each_element([&](int x) {
        // ∂𝐿/∂c = 11 * L
        float expected = L(x) * 11.f;
        float actual = grad_loss_out_wrt_c(x);
        assert(expected == actual);
    });

    printf("Success!\n");
    return 0;
}
