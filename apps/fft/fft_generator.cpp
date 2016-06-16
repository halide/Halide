#include "Halide.h"

#include "fft.h"

namespace {

using namespace Halide;

enum class FFTNumberType { Real, Complex };
std::map<std::string, FFTNumberType> fft_number_type_enum_map() {
     return { { "real", FFTNumberType::Real },
              { "complex", FFTNumberType::Complex } };
}

// Direction of FFT. Samples can be read as "time" or "spatial" depending
// on the meaning of the input domain.
enum class FFTDirection { SamplesToFrequency, FrequencyToSamples };
std::map<std::string, FFTDirection> fft_direction_enum_map() {
  return { { "samples_to_frequency", FFTDirection::SamplesToFrequency },
           { "frequency_to_samples", FFTDirection::FrequencyToSamples } };
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
    // "frequency_to_samples" maps to a forward FFT. (Other packages sometimes call this a sign of +1)
    GeneratorParam<FFTDirection> direction{"direction", FFTDirection::SamplesToFrequency,
        fft_direction_enum_map() };

    // Whether the input is "real" or "complex".
    GeneratorParam<FFTNumberType> input_number_type{"input_number_type",
        FFTNumberType::Real, fft_number_type_enum_map() };
    // Whether the output is "real" or "complex".
    GeneratorParam<FFTNumberType> output_number_type{"output_number_type",
        FFTNumberType::Real, fft_number_type_enum_map() };

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
    //
    // For a complex input FFT, this should have the following shape:
    // Dim0: extent = 2, stride = 1 -- real followed by imaginary components
    // Dim1: extent = size0, stride = 2
    // Dim2: extent = size1, stride = size0 * 2
    ImageParam input;

    Func build() {
        Var c{"c"}, x{"x"}, y{"y"};

        _halide_user_assert(size0 > 0) << "FFT must be at least 1D\n";

        Fft2dDesc desc;

        desc.gain = gain;
        desc.vector_width = vector_width;

        // The logic below calls the specialized r2c or c2r version if
        // applicable to take advantae of better scheduling. It is
        // assumed that projecting a real Func to a ComplexFunc and
        // immediately back has zero cost.

        Func result;
        ComplexFunc complex_result;
        if (input_number_type == FFTNumberType::Real) {
            input = ImageParam(Float(32), 2, "input");

            if (direction == FFTDirection::SamplesToFrequency) {
                // TODO: Not sure why this is necessary as ImageParam
                // -> Func conversion should happen, It may not work
                // with implicit dimension (use of _) logic in FFT.
                Func input_real;
                input_real(x, y) = input(x, y);

                complex_result = fft2d_r2c(input_real, size0, size1, target, desc);
            } else {
                ComplexFunc in;
                in(x, y) = ComplexExpr(input(x, y), 0);

                complex_result = fft2d_c2c(in, size0, size1, 1, target, desc);
            }
        } else {
            input = ImageParam(Float(32), 3, "input");
            input.set_bounds(0, 0, 2);
            input.set_stride(1, 2);

            ComplexFunc in;
            in(x, y) = ComplexExpr(input(0, x, y), input(1, x, y));
            if (output_number_type == FFTNumberType::Real &&
                direction == FFTDirection::FrequencyToSamples) {
                result = fft2d_c2r(in, size0, size1, target, desc);
            } else {
                complex_result = fft2d_c2c(in, size0, size1, 
                                           (direction == FFTDirection::SamplesToFrequency) ? -1 : 1,
                                           target, desc);
            }
        }

        if (output_number_type == FFTNumberType::Real) {
            if (!result.defined()) {
                 result(x, y) = re(complex_result(x, y));
            }
        } else {
            result(c, x, y) = select(c == 0, re(complex_result(x, y)), im(complex_result(x, y)));
            result.output_buffer().set_bounds(0, 0, 2);
            result.output_buffer().set_stride(1, 2);
        }

        return result;
    }
};

Halide::RegisterGenerator<FFTGenerator> register_fft{"fft"};

}
