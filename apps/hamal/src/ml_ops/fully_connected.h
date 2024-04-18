#ifndef HALIDE_APPS_HAMAL_FULLY_CONNECTED_H_
#define HALIDE_APPS_HAMAL_FULLY_CONNECTED_H_

#include <string>

#include "Halide.h"

namespace hamal {

// TODO: Move to its common header for GeneratorParams that are used across manyops.
enum QuantizationKind {
    None,
    QC8NoBias,  // Is no bias inplied by qc8?
};

inline const std::map<std::string, QuantizationKind> quantization_names = {
    {"none", QuantizationKind::None},
    {"qc8_no_bias", QuantizationKind::QC8NoBias},
};

struct FullyConnected : public Halide::NamesInterface {
    FullyConnected(const std::string &base_name)
        : base_name(base_name),
          result(base_name + "_fc"),
          weights(base_name + "_fc_weights"),
          scale(base_name + "_fc_scale") {
    }
    std::string base_name;
    Func result;

    QuantizationKind quantization_kind;
    Halide::Type processing_type;
    int input_features_size;
    int output_features_size;
    Var i{"i"};

    Halide::GeneratorInput<Buffer<>> *weights_input;
    Halide::GeneratorInput<Buffer<>> *scale_input;
    Func weights;
    Func scale;

    RDom r, r_tail;

    void add_inputs(QuantizationKind kind,
                    const Halide::Type &processing_type_arg,
                    int input_features_size_arg, int output_features_size_arg,
                    Halide::Internal::GeneratorBase *generator) {
        quantization_kind = kind;
        processing_type = processing_type_arg;
        input_features_size = input_features_size_arg;
        output_features_size = output_features_size_arg;
        weights_input = generator->add_input<Buffer<>>(base_name + "_weights",
                                                       processing_type, 2);
        scale_input =
            generator->add_input<Buffer<>>(base_name + "_scale", Float(32), 1);
    }

    // TODO: Should this return result?
    void apply(Func input, const Target &target) {
        if (!weights.defined()) {
            weights_input->dim(0).set_min(0).dim(1).set_min(0);
            weights = *weights_input;
            assert(weights.args().size() == 2);
        }

        // Arguments to inner func
        std::vector<Var> args = input.args();
        assert(args.size() == 3);
        Var t = args[1];
        Var b = args[2];

        Expr scale_expr = 1.0f;
        if (quantization_kind == QuantizationKind::QC8NoBias) {
            scale = *scale_input;
            assert(scale.args().size() == 1);
            scale_expr = scale(i);
        }

        r = RDom(0, input_features_size, base_name + "_r");

        result(i, t, b) += input(r, t, b) * weights(r, i) * scale_expr;
    }

    // TODO: better API needed
    static FullyConnected float32_layer(Func inputs, Func weights, int input_size,
                                        int output_size, const Target &target) {
        FullyConnected result("float32_layer");
        result.quantization_kind = QuantizationKind::None;
        result.processing_type = Float(32);
        result.input_features_size = input_size;
        result.output_features_size = output_size;
        result.weights = weights;
        result.apply(inputs, target);
        return result;
    }

    void default_schedule(LoopLevel result_loop_level, const Target &target) {
        const int vec_size = target.natural_vector_size<float>();
        result.compute_at(result_loop_level).vectorize(i, vec_size);

        RVar ro("ro"), ri("ri");
        Var fo("fo"), fi("fi");
        result.update()
            .split(r, ro, ri, vec_size * 32)
            .split(i, fo, fi, 256)
            .atomic()
            .vectorize(ri)
            .parallel(fo);
    }
};

}  // namespace hamal
#endif  // HALIDE_APPS_HAMAL_FULLY_CONNECTED_H_
