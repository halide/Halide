// Creates a cascade of 1D forward-only IIR filters with tanh nonlinearity.
// Contains a version that uses inductive functions, and a version that does not.

#include "Halide.h"

using namespace Halide;
using namespace Halide::BoundaryConditions;

class IIRCascade : public Generator<IIRCascade> {
public:
    Input<Buffer<float, 2>> input{"input"};
    GeneratorParam<int> N{"N", 4};                 // number of filter iterations
    GeneratorParam<float> weight{"weight", 0.3f};  // IIR coefficient
    GeneratorParam<float> gain{"gain", 3.f};       // gain applied to filter output before nonlinearity
    GeneratorParam<bool> inductive{"inductive", true};
    Output<Buffer<float, 2>> output{"output"};

    void generate() {
        Var t("t"), s("s");
        Func in_f("in");
        in_f(t, s) = BoundaryConditions::repeat_edge(input)(t, s);
        std::vector<Func> filt(N);
        RDom rt(1, input.width() - 1, "rt");

        for (int k = 0; k < N; k++) {
            filt[k] = Func(Float(32), "filt" + std::to_string(k));
            Func src;
            if (k == 0) {
                src = in_f;
            } else {
                src = filt[k - 1];
            }
            if (inductive) {
                filt[k](t, s) = select(t <= 0,
                                       weight * src(t, s),
                                       likely((1.f - weight) * filt[k](t - 1, s) + weight * tanh(gain * src(t, s))));
            } else {
                filt[k](t, s) = undef<float>();
                filt[k](0, s) = weight * src(0, s);
                filt[k](rt, s) = (1.f - weight) * filt[k](rt - 1, s) + weight * tanh(gain * src(rt, s));
            }
        }

        output(t, s) = undef<float>();
        RDom ro(0, input.width(), "ro");
        output(ro, s) = tanh(gain * filt[N - 1](ro, s));

        int VEC;
        if (inductive) {
            VEC = 32;
        } else {
            VEC = 4;
        }

        Var so("so"), si("si");
        if (get_target().has_feature(Target::CUDA)) {
            // Similarly to iir_blur, we can't get parallelism from the recursive dimension.
            // The inductive version uses less i/o because it does not have to write the
            // entire intermediate filter outputs to global memory.
            const int WARP = 32;
            output.update().split(s, so, si, WARP).gpu_blocks(so).gpu_lanes(si).reorder(ro, si, so);
            for (int k = 0; k < N; k++) {
                if (inductive) {
                    filt[k].fold_storage(t, 2);
                    filt[k].store_at(output, si).compute_at(output, ro);
                } else {
                    filt[k].compute_at(output, si).reorder_storage(s, t).store_in(MemoryType::Heap);
                    filt[k].update(1).unroll(rt, 8);
                }
            }
        } else {
            // The inductive version is generally not faster on CPU unless the non-linearity is changed to a
            // less expensive function and the input is large enough to saturate the last-level cache.
            output.split(s, so, si, VEC).vectorize(si);
            output.update()
                .split(s, so, si, VEC)
                .reorder(si, ro, so)
                .vectorize(si);

            for (int k = 0; k < N; k++) {
                if (inductive) {
                    filt[k].reorder_storage(s, t).fold_storage(t, 2);
                    filt[k].store_at(output, so).compute_at(output, ro).vectorize(s, VEC);
                } else {
                    filt[k].compute_at(output, so).reorder_storage(s, t).vectorize(s, VEC).update().vectorize(s, VEC);
                    filt[k].update(1).vectorize(s, VEC);
                }
            }

            output.dim(0).set_bounds(0, input.width());
        }
    }
};

HALIDE_REGISTER_GENERATOR(IIRCascade, iir_cascade)
