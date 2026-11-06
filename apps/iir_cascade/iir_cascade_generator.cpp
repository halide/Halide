// This file defines a generator for a cascade of second-order IIR filters
// (biquads) applied to many channels of audio. It computes the same thing as
// scipy.signal.sosfilt.
//
// Each section is an inductive Func: a Func defined in terms of its own
// previous outputs. This lets the entire cascade be computed in a single pass
// over the samples, keeping only the last few outputs of each section in
// registers.

#include "Halide.h"

using namespace Halide;

class IIRCascade : public Generator<IIRCascade> {
public:
    GeneratorParam<int> sections{"sections", 8};

    // The signal, indexed by channel then sample.
    Input<Buffer<float, 2>> input{"input"};
    // The filter coefficients, indexed by coefficient then section. The
    // coefficients of each section are b0, b1, b2, a0, a1, a2, normalized so
    // that a0 is one.
    Input<Buffer<float, 2>> sos{"sos"};
    Output<Buffer<float, 2>> output{"output"};

    void generate() {
        Var c("c"), n("n");

        std::vector<Func> cascade;
        Func prev = BoundaryConditions::constant_exterior(input,
                                                          0.f,
                                                          {{Expr{}, Expr{}},
                                                           {0, input.dim(1).extent()}});
        for (int k = 0; k < sections; k++) {
            Expr b0 = sos(0, k), b1 = sos(1, k), b2 = sos(2, k);
            Expr a1 = sos(4, k), a2 = sos(5, k);

            // Inductive Funcs must have their type declared up front.
            Func f(Float(32), "section_" + std::to_string(k));
            /*
            // The clamps keep the reads of prev in bounds near the start of
            // the signal, where the selects discard them.
            f(c, n) = (b0 * prev(c, max(n, 0)) +
                       select(n < 1, 0.f,
                              likely(b1 * prev(c, max(n - 1, 0)) - a1 * f(c, n - 1))) +
                       select(n < 2, 0.f,
                              likely(b2 * prev(c, max(n - 2, 0)) - a2 * f(c, n - 2))));
            */

            f(c, n) = (b0 * prev(c, n) +
                       b1 * prev(c, n - 1) +
                       b2 * prev(c, n - 2) -
                       select(n < 1, 0.f, a1 * f(c, n - 1)) -
                       select(n < 2, 0.f, likely(a2 * f(c, n - 2))));
            cascade.push_back(f);
            prev = f;
        }
        output(c, n) = prev(c, n);

        // Schedule
        // Each thread walks along the samples once for a vector of channels,
        // computing every section at each sample. Each section slides over
        // the samples, and the three values of it in use at once stay in
        // registers.
        const int vec = natural_vector_size<float>();
        output.vectorize(c, vec, TailStrategy::RoundUp)
            .reorder(n, c)
            .parallel(c);

        for (Func f : cascade) {
            f.store_at(output, c)
                .compute_at(output, n)
                .vectorize(c)
                .store_in(MemoryType::Register);
        }

        if (0) {
            Buffer<float> input(256, 1024), output(256, 1024), sos(6, sections);
            input.fill([](int c, int n) { return sin(2 * M_PI * 440 * n / 48000) * (1.0f - 2.0f * c / 256); });
            sos.fill([&](int i, int k) -> float {
                const double fs = 48000, q = 0.8;
                double f0 = 120 * std::pow(8000.0 / 120, (double)k / ((int)sections - 1));
                double gain = std::pow(10.0, ((k % 2) ? 3.0 : -3.0) / 40);
                double w0 = 2 * M_PI * f0 / fs;
                double alpha = std::sin(w0) / (2 * q);
                double a0 = 1 + alpha / gain;
                switch (i) {
                case 0:
                    return (1 + alpha * gain) / a0;
                case 1:
                    return -2 * std::cos(w0) / a0;
                case 2:
                    return (1 - alpha * gain) / a0;
                case 3:
                    return 1;
                case 4:
                    return -2 * std::cos(w0) / a0;
                case 5:
                    return (1 - alpha / gain) / a0;
                default:
                    return 0.0f;
                }
            });
            halidoscope(input, sos, output);
        }
    }
};

HALIDE_REGISTER_GENERATOR(IIRCascade, iir_cascade)
