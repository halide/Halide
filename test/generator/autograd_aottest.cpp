#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include <cmath>
#include <cstdio>

#include "autograd.h"
#include "autograd_grad.h"

using namespace Halide::Runtime;

constexpr int kSize = 64;

int main(int argc, char **argv) {
    int result;

    auto f = [](float a, float b, float c) -> float {
        return 33.f * std::pow(a, 3) +
               22.f * std::pow(b, 2) +
               11.f * c +
               1.f;
    };

    Buffer<float, 1> a(kSize);
    Buffer<float, 1> b(kSize);
    Buffer<float, 1> c(kSize);
    Buffer<float, 1> out(kSize);

    a.for_each_element([&](int x) { a(x) = (float)x; });
    b.for_each_element([&](int x) { b(x) = (float)x; });
    c.for_each_element([&](int x) { c(x) = (float)x; });

    Buffer<uint8_t, 1> lut(256);
    Buffer<uint8_t, 1> lut_indices(kSize);
    Buffer<uint8_t, 1> out_lut(kSize);
    lut.for_each_element([&](int x) { lut(x) = (uint8_t)(x ^ 0xAA); });
    lut_indices.for_each_element([&](int x) { lut_indices(x) = x * 2; });

    result = autograd(a, b, c, lut, lut_indices, out, out_lut);
    if (result != 0) {
        exit(-1);
    }
    out.for_each_element([&](int x) {
        float expected = f(a(x), b(x), c(x));
        float actual = out(x);
        assert(expected == actual);
    });
    out_lut.for_each_element([&](int x) {
        uint8_t expected = (uint8_t)(x * 2) ^ 0xAA;
        uint8_t actual = out_lut(x);
        assert(expected == actual);
    });

    Buffer<float, 1> L(kSize);
    L.for_each_element([&](int x) { L(x) = (float)(x - kSize / 2); });

    /*
        The gradient version should have the following args (in this order):
        Inputs:
            input_a
            input_b
            input_c
            lut
            lut_indices
            _grad_loss_for_output     (synthesized)
            _grad_loss_for_output_lut (synthesized)
        Outputs:
            _grad_loss_output_wrt_input_a
            _grad_loss_output_wrt_input_b
            _grad_loss_output_wrt_input_c
            _dummy_grad_loss_output_wrt_lut
            _dummy_grad_loss_output_wrt_lut_indices
            _dummy_grad_loss_output_lut_wrt_input_a
            _dummy_grad_loss_output_lut_wrt_input_b
            _dummy_grad_loss_output_lut_wrt_input_c
            _grad_loss_output_lut_wrt_lut
            _grad_loss_output_lut_wrt_lut_indices

        Note that the outputs with "_dummy" prefixes are placeholder
        outputs that are always filled with zeroes; in those cases,
        there is no derivative for the output/input pairing, but we
        emit an output nevertheless so that the function signature
        is always mechanically predictable from the list of inputs and outputs.
    */

    Buffer<float, 1> grad_loss_out_wrt_a(kSize);
    Buffer<float, 1> grad_loss_out_wrt_b(kSize);
    Buffer<float, 1> grad_loss_out_wrt_c(kSize);
    Buffer<float, 1> dummy_grad_loss_output_wrt_lut(kSize);
    Buffer<float, 1> dummy_grad_loss_output_wrt_lut_indices(kSize);
    Buffer<float, 1> dummy_grad_loss_output_lut_wrt_input_a(kSize);
    Buffer<float, 1> dummy_grad_loss_output_lut_wrt_input_b(kSize);
    Buffer<float, 1> dummy_grad_loss_output_lut_wrt_input_c(kSize);
    Buffer<uint8_t, 1> grad_loss_output_lut_wrt_lut(kSize);
    Buffer<uint8_t, 1> grad_loss_output_lut_wrt_lut_indices(kSize);

    result = autograd_grad(/*inputs*/ a, b, c, lut, lut_indices, L, L,
                           /*outputs*/
                           grad_loss_out_wrt_a,
                           grad_loss_out_wrt_b,
                           grad_loss_out_wrt_c,
                           dummy_grad_loss_output_wrt_lut,
                           dummy_grad_loss_output_wrt_lut_indices,
                           dummy_grad_loss_output_lut_wrt_input_a,
                           dummy_grad_loss_output_lut_wrt_input_b,
                           dummy_grad_loss_output_lut_wrt_input_c,
                           grad_loss_output_lut_wrt_lut,
                           grad_loss_output_lut_wrt_lut_indices);
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
    dummy_grad_loss_output_wrt_lut.for_each_value([](float f) { assert(f == 0.f); });
    dummy_grad_loss_output_wrt_lut_indices.for_each_value([](float f) { assert(f == 0.f); });
    dummy_grad_loss_output_lut_wrt_input_a.for_each_value([](float f) { assert(f == 0.f); });
    dummy_grad_loss_output_lut_wrt_input_b.for_each_value([](float f) { assert(f == 0.f); });
    dummy_grad_loss_output_lut_wrt_input_c.for_each_value([](float f) { assert(f == 0.f); });
    grad_loss_output_lut_wrt_lut.for_each_element([&](int x) {
        // TODO: is zero really expected?
        uint8_t expected = 0;
        uint8_t actual = grad_loss_output_lut_wrt_lut(x);
        assert(expected == actual);
    });
    grad_loss_output_lut_wrt_lut_indices.for_each_element([&](int x) {
        // TODO: is zero really expected?
        uint8_t expected = 0;
        uint8_t actual = grad_loss_output_lut_wrt_lut_indices(x);
        assert(expected == actual);
    });

    printf("Success!\n");
    return 0;
}
