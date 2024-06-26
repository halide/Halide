#include "Halide.h"

#include "fft.h"

namespace {

using namespace Halide;

enum class FFTNumberType { Real,
                           Complex };
std::map<std::string, FFTNumberType> fft_number_type_enum_map() {
    return {{"real", FFTNumberType::Real},
            {"complex", FFTNumberType::Complex}};
}

// Direction of FFT. Samples can be read as "time" or "spatial" depending
// on the meaning of the input domain.
enum class FFTDirection { SamplesToFrequency,
                          FrequencyToSamples };
std::map<std::string, FFTDirection> fft_direction_enum_map() {
    return {{"samples_to_frequency", FFTDirection::SamplesToFrequency},
            {"frequency_to_samples", FFTDirection::FrequencyToSamples}};
}

class FFTGenerator : public Halide::Generator<FFTGenerator> {
public:
    // Gain to apply to the FFT. This is folded into gains already
    // being applied to the FFT. A gain of 1.0f indicates an
    // unnormalized FFT. 1 / sqrt(N) gives a unitary transform such that
    // forward and inverse operations have the same gain without changing
    // signal magnitude.
    // A common convention is 1/N for the forward direction and 1 for the
    // inverse.
    // "N" above is the size of the input, which is the product of
    // the dimensions.
    GeneratorParam<float> gain{"gain", 1.0f};

    // The following option specifies that a particular vector width should be
    // used when the vector width can change the results of the FFT.
    // Some parts of the FFT algorithm use the vector width to change the way
    // floating point operations are ordered and grouped, which causes the results
    // to vary with respect to the target architecture. Setting this option forces
    // such stages to use the specified vector width (independent of the actual
    // architecture's vector width), which eliminates the architecture specific
    // behavior.
    GeneratorParam<int32_t> vector_width{"vector_width", 0};

    // The following option indicates that the FFT should parallelize within a
    // single FFT. This only makes sense to use on large FFTs, and generally only
    // if there is no outer loop around FFTs that can be parallelized.
    GeneratorParam<bool> parallel{"parallel", false};

    // Indicates forward or inverse Fourier transform --
    // "samples_to_frequency" maps to a forward FFT. (Other packages sometimes call this a sign of -1)
    // "frequency_to_samples" maps to a backward FFT. (Other packages sometimes call this a sign of +1)
    GeneratorParam<FFTDirection> direction{"direction", FFTDirection::SamplesToFrequency,
                                           fft_direction_enum_map()};

    // Whether the input is "real" or "complex".
    GeneratorParam<FFTNumberType> input_number_type{"input_number_type",
                                                    FFTNumberType::Real, fft_number_type_enum_map()};
    // Whether the output is "real" or "complex".
    GeneratorParam<FFTNumberType> output_number_type{"output_number_type",
                                                     FFTNumberType::Real, fft_number_type_enum_map()};

    // Size of first dimension, required to be greater than zero.
    GeneratorParam<int32_t> size0{"size0", 1};
    // Size of second dimension, may be zero for 1D FFT.
    GeneratorParam<int32_t> size1{"size1", 0};
    // TODO(zalman): Add support for 3D and maybe 4D FFTs

    // The input buffer. Must be separate from the output.
    // Only Float(32) is supported.
    //
    // For a real input FFT, this should have the following shape:
    // Dim0: extent = size0, stride = 1
    // Dim1: extent = size1 / 2 - 1, stride = size0
    // Dim2: extent = 1, stride = 1
    //
    // For a complex input FFT, this should have the following shape:
    // Dim0: extent = size0, stride = 2
    // Dim1: extent = size1, stride = size0 * 2
    // Dim2: extent = 2, stride = 1 (real followed by imaginary components)
    Input<Buffer<float, 3>> input{"input"};
    Output<Buffer<float, 3>> output{"output"};

    void generate() {
        _halide_user_assert(size0 > 0) << "FFT must be at least 1D\n";

        Fft2dDesc desc;

        desc.gain = gain;
        desc.vector_width = vector_width;
        desc.parallel = parallel;

        // The logic below calls the specialized r2c or c2r version if
        // applicable to take advantage of better scheduling. It is
        // assumed that projecting a real Func to a ComplexFunc and
        // immediately back has zero cost.

        const int sign = (direction == FFTDirection::SamplesToFrequency) ? -1 : 1;

        if (input_number_type == FFTNumberType::Real) {
            if (direction == FFTDirection::SamplesToFrequency) {
                // TODO: Not sure why this is necessary as ImageParam
                // -> Func conversion should happen, It may not work
                // with implicit dimension (use of _) logic in FFT.
                Func in;
                in(x, y) = input(x, y, 0);

                complex_result = fft2d_r2c(in, size0, size1, target, desc);
            } else {
                ComplexFunc in;
                in(x, y) = ComplexExpr(input(x, y, 0), 0);

                complex_result = fft2d_c2c(in, size0, size1, sign, target, desc);
            }
        } else {
            ComplexFunc in;
            in(x, y) = ComplexExpr(input(x, y, 0), input(x, y, 1));
            if (output_number_type == FFTNumberType::Real &&
                direction == FFTDirection::FrequencyToSamples) {
                real_result = fft2d_c2r(in, size0, size1, target, desc);
            } else {
                complex_result = fft2d_c2c(in, size0, size1, sign, target, desc);
            }
        }

        if (output_number_type == FFTNumberType::Real) {
            if (real_result.defined()) {
                output(x, y, c) = real_result(x, y);
            } else {
                output(x, y, c) = re(complex_result(x, y));
            }
        } else {
            output(x, y, c) = mux(c, {re(complex_result(x, y)), im(complex_result(x, y))});
        }
    }

    void schedule() {
        const int input_comps = (input_number_type == FFTNumberType::Real) ? 1 : 2;
        const int output_comps = (output_number_type == FFTNumberType::Real) ? 1 : 2;

        input.dim(0).set_stride(input_comps);
        input.dim(2).set_min(0).set_extent(input_comps).set_stride(1);

        output.dim(0).set_stride(output_comps);
        output.dim(2).set_min(0).set_extent(output_comps).set_stride(1);

        if (output_comps != 1) {
            output.reorder(c, x, y).unroll(c);
        }

        if (real_result.defined()) {
            real_result.compute_at(output, Var::outermost());
        } else {
            assert(complex_result.defined());
            complex_result.compute_at(output, Var::outermost());
        }
    }

private:
    Var x{"x"}, y{"y"}, c{"c"};
    Func real_result;
    ComplexFunc complex_result;
};

}  // namespace

HALIDE_REGISTER_GENERATOR(FFTGenerator, fft)
