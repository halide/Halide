#include "ops.h"
#include "app_util.h"

#include <cmath>
#include <iostream>

#include "AddUint8Uint8.h"
#include "AveragePoolUint8.h"
#include "ConvolutionUint8.h"
#include "DepthwiseConvolutionUint8.h"
#include "MaxPoolUint8.h"

namespace interpret_nn {

namespace {

std::pair<int, int> Intersect(std::pair<int, int> a, std::pair<int, int> b) {
    int max_a = a.first + a.second - 1;
    int max_b = b.first + b.second - 1;
    int min = std::max(a.first, b.first);
    int max = std::min(max_a, max_b);
    return {min, max - min + 1};
}

CropShape Intersect(CropShape a, const CropShape &b) {
    APP_CHECK(a.size() == b.size());
    for (int i = 0; i < (int)a.size(); i++) {
        a[i] = Intersect(a[i], b[i]);
    }
    return a;
}

std::vector<CropShape> SplitCrop(const CropShape &crop, int dim, int factor,
                                 bool shift_inwards = false) {
    std::vector<CropShape> splits;
    int x_min = crop[dim].first;
    int x_extent = crop[dim].second;
    int x_max = x_min + x_extent - 1;
    splits.reserve((x_extent + factor - 1) / factor);
    for (int x = 0; x <= x_max; x += factor) {
        CropShape split_x = crop;
        split_x[dim].first = x;
        if (shift_inwards) {
            split_x[dim].second = factor;
            if (split_x[dim].first + split_x[dim].second > x_extent) {
                split_x[dim].first -= x_extent - split_x[dim].second;
            }
        } else {
            split_x[dim].second = std::min(x + factor - 1, x_max) - x + 1;
        }
        splits.push_back(split_x);
    }
    return splits;
}

struct QuantizedMulAndShift {
    int quantized_multiplier, shift;
};

// Adapted from tflite
QuantizedMulAndShift GetQuantizedMulAndShift(double double_multiplier) {
    if (double_multiplier == 0.) {
        return {0, 0};
    }

    // TODO: consider adding a path here to avoid floating-point operations (eg for Hexagon)
    int shift = 0;
    const double q = std::frexp(double_multiplier, &shift);
    int64_t q_fixed = (int64_t)std::round(q * (1LL << 31));
    APP_CHECK(q_fixed <= (1LL << 31));

    if (q_fixed == (1LL << 31)) {
        q_fixed /= 2;
        ++shift;
    }
    APP_CHECK(q_fixed <= std::numeric_limits<int32_t>::max());

    if (shift < -31) {
        shift = 0;
        q_fixed = 0;
    }
    return {(int)q_fixed, shift};
}

// Adapted from tflite
QuantizedMulAndShift GetQuantizedMulAndShiftSmallerThanOne(double double_multiplier) {
    APP_CHECK(double_multiplier > 0.0 && double_multiplier < 1.0);
    auto result = GetQuantizedMulAndShift(double_multiplier);
    APP_CHECK(result.shift <= 0);
    return result;
}

struct MinMax {
    int min, max;
};

// Adapted from tfmini
MinMax GetQuantizedMinMax(ActivationFunction activation, int zero_point, double scale) {
    double real_activation_min = 0.0;
    double real_activation_max = 0.0;
    bool has_activation_min = false;
    bool has_activation_max = false;
    if (activation == ActivationFunction::None) {
        // nothing
    } else if (activation == ActivationFunction::Relu) {
        real_activation_min = 0.0;
        has_activation_min = true;
    } else if (activation == ActivationFunction::Relu6) {
        real_activation_min = 0.0;
        has_activation_min = true;
        real_activation_max = 6.0;
        has_activation_max = true;
    } else if (activation == ActivationFunction::ReluN1To1) {
        real_activation_min = -1.0;
        has_activation_min = true;
        real_activation_max = 1.0;
        has_activation_max = true;
    } else {
        APP_CHECK(false) << "Unsupported quantized activation function type.";
    }
    int output_activation_min = 0;
    int output_activation_max = 255;
    if (has_activation_min) {
        output_activation_min = std::max(
            output_activation_min,
            zero_point + (int)std::round(real_activation_min / scale));
    }
    if (has_activation_max) {
        output_activation_max = std::min(
            output_activation_max,
            zero_point + (int)std::round(real_activation_max / scale));
    }

    return {output_activation_min, output_activation_max};
}

}  // namespace

Op::Bounds ElementwiseOp::InferBounds(const CropShape &crop) const {
    Bounds result;
    for (int i = 0; i < InputCount(); i++) {
        result.inputs.emplace_back(crop);
    }
    for (int i = 0; i < OutputCount(); i++) {
        result.outputs.emplace_back(crop);
    }
    return result;
}

std::vector<CropShape> ElementwiseOp::Split(const CropShape &crop) const {
    const int kSplit = 2;
    return SplitCrop(crop, 2, kSplit);
}

Op::Bounds PoolOp::InferBounds(const CropShape &crop) const {
    CropShape input_crop = crop;

    input_crop[0] = crop[0];
    for (int dim = 1; dim <= 2; dim++) {
        input_crop[dim].first *= stride_[dim - 1];
        input_crop[dim].second *= stride_[dim - 1];
    }

    input_crop[1].second += filter_size_[1];
    input_crop[2].second += filter_size_[2];
    input_crop = Intersect(input_crop, WithoutStrides(Input()->Shape()));

    Bounds result;
    result.inputs.emplace_back(input_crop);
    result.outputs = {crop};
    return result;
}

std::vector<CropShape> PoolOp::Split(const CropShape &crop) const {
    const int kSplit = 2;
    return SplitCrop(crop, 2, kSplit);
}

void AddOp::Execute(const CropShape &crop) {
    const Tensor *input1 = Input(0);
    const Tensor *input2 = Input(1);
    Tensor *output = Output();

    if (input1->Type() == TensorType::UInt8 &&
        input2->Type() == TensorType::UInt8 &&
        output->Type() == TensorType::UInt8) {
        auto input1_buf = input1->Data<uint8_t>();
        auto input2_buf = input2->Data<uint8_t>();
        auto output_buf = output->Data<uint8_t>(crop);

        int left_shift = 0;
        int input1_offset = 0;
        int input1_multiplier = 0;
        int input1_shift = 0;
        int input2_offset = 0;
        int input2_multiplier = 0;
        int input2_shift = 0;
        int output_offset = 0;
        int output_multiplier = 0;
        int output_shift = 0;
        int output_min = 0;
        int output_max = 0;
        APP_CHECK(0 == AddUint8Uint8(left_shift, input1_buf, input2_buf,
                                     input1_offset, input1_multiplier, input1_shift,
                                     input2_offset, input2_multiplier, input2_shift,
                                     output_offset, output_multiplier, output_shift,
                                     output_min, output_max, output_buf));
    }
}

void AveragePoolOp::Execute(const CropShape &crop) {
    const Tensor *input = Input();
    Tensor *output = Output();

    if (input->Type() == TensorType::UInt8 &&
        output->Type() == TensorType::UInt8) {
        auto input_buf = input->Data<uint8_t>();
        auto output_buf = output->Data<uint8_t>(crop);

        int output_min = 0;
        int output_max = 0;

        APP_CHECK(
            0 == AveragePoolUint8(input_buf, stride_[0], stride_[1],
                                  filter_size_[0], filter_size_[1],
                                  output_min, output_max, output_buf));
    }
}

Op::Bounds Conv2DOp::InferBounds(const CropShape &crop) const {
    CropShape input_crop = crop;
    CropShape filter_shape = WithoutStrides(Filter()->Shape());

    for (int dim = 1; dim <= 2; dim++) {
        input_crop[dim].first *= stride_[dim - 1];
        input_crop[dim].second *= stride_[dim - 1];
    }

    input_crop[0] = filter_shape[3];
    input_crop[1].second += dilation_[0] * filter_shape[1].second;
    input_crop[2].second += dilation_[1] * filter_shape[2].second;
    input_crop = Intersect(input_crop, WithoutStrides(Input()->Shape()));

    Bounds result;
    result.inputs.emplace_back(input_crop);
    result.inputs.emplace_back(std::move(filter_shape));
    result.inputs.emplace_back(WithoutStrides(Bias()->Shape()));
    result.outputs = {crop};
    return result;
}

std::vector<CropShape> Conv2DOp::Split(const CropShape &crop) const {
    const int kSplit = 2;
    return SplitCrop(crop, 2, kSplit);
}

void Conv2DOp::Execute(const CropShape &crop) {
    const Tensor *input = Input();
    const Tensor *filter = Filter();
    const Tensor *bias = Bias();
    Tensor *output = Output();

    if (input->Type() == TensorType::UInt8 &&
        filter->Type() == TensorType::UInt8 &&
        output->Type() == TensorType::UInt8) {
        auto input_buf = input->Data<uint8_t>();
        auto filter_buf = filter->Data<uint8_t>();
        auto bias_buf = bias->Data<int32_t>();
        auto output_buf = output->Data<uint8_t>(crop);

        const int input_offset = input->Quantization().zero.at(0);
        const int filter_offset = filter->Quantization().zero.at(0);
        const int bias_offset = bias->Quantization().zero.at(0);
        const int output_offset = output->Quantization().zero.at(0);

        // TODO: handle unexpected out-of-range data more cleanly.
        APP_CHECK(input_offset >= 0 && input_offset <= 255);
        APP_CHECK(filter_offset >= 0 && filter_offset <= 255);
        APP_CHECK(bias_offset == 0);
        APP_CHECK(output_offset >= 0 && output_offset <= 255);

        const float input_scale = input->Quantization().scale.at(0);
        const float filter_scale = filter->Quantization().scale.at(0);
        const float bias_scale = bias->Quantization().scale.at(0);
        const float output_scale = output->Quantization().scale.at(0);

        const double input_product_scale = input_scale * filter_scale;
        APP_CHECK(std::abs(input_product_scale - bias_scale) <=
                  std::min(input_product_scale, (double)bias_scale) * 1e-6);

        const double real_multiplier = input_product_scale / output_scale;
        const auto mul_and_shift = GetQuantizedMulAndShiftSmallerThanOne(real_multiplier);
        const int output_multiplier = mul_and_shift.quantized_multiplier;
        // GetQuantizedMulAndShiftSmallerThanOne() returns a negative shift;
        // ConvolutionUint8() expects a positive shift.
        const int output_shift = -mul_and_shift.shift;

        const auto min_max = GetQuantizedMinMax(activation_, output_offset, output_scale);
        const int output_min = min_max.min;
        const int output_max = min_max.max;
        APP_CHECK(output_min >= 0 && output_min <= 255);
        APP_CHECK(output_max >= 0 && output_max <= 255);

        // TODO: remove this
        // std::cout << "\n";
        // std::cout << "input_offset " << input_offset << "\n";
        // std::cout << "filter_offset " << filter_offset << "\n";
        // std::cout << "stride_[0] " << stride_[0] << "\n";
        // std::cout << "stride_[1] " << stride_[1] << "\n";
        // std::cout << "dilation_[0] " << dilation_[0] << "\n";
        // std::cout << "dilation_[1] " << dilation_[1] << "\n";
        // std::cout << "real_multiplier " << real_multiplier << "\n";
        // std::cout << "output_multiplier " << output_multiplier << "\n";
        // std::cout << "output_shift " << output_shift << "\n";
        // std::cout << "output_offset " << output_offset << "\n";
        // std::cout << "output_min " << output_min << "\n";
        // std::cout << "output_max " << output_max << "\n";

        APP_CHECK(
            0 == ConvolutionUint8(input_buf, filter_buf, bias_buf, (uint8_t)input_offset,
                                  (uint8_t)filter_offset, stride_[0], stride_[1],
                                  dilation_[0], dilation_[1], output_multiplier,
                                  output_shift, (uint8_t)output_offset,
                                  output_min, output_max, output_buf));
    }
}

Op::Bounds DepthwiseConv2DOp::InferBounds(const CropShape &crop) const {
    CropShape input_crop = crop;
    CropShape filter_shape = WithoutStrides(Filter()->Shape());

    input_crop[0] = crop[0];
    input_crop[0].first /= depth_multiplier_;
    input_crop[0].second /= depth_multiplier_;
    for (int dim = 1; dim <= 2; dim++) {
        input_crop[dim].first *= stride_[dim - 1];
        input_crop[dim].second *= stride_[dim - 1];
    }

    input_crop[1].second += dilation_[0] * filter_shape[1].second;
    input_crop[2].second += dilation_[1] * filter_shape[2].second;
    input_crop = Intersect(input_crop, WithoutStrides(Input()->Shape()));

    Bounds result;
    result.inputs.emplace_back(input_crop);
    result.inputs.emplace_back(std::move(filter_shape));
    result.inputs.emplace_back(WithoutStrides(Bias()->Shape()));
    result.outputs = {crop};
    return result;
}

std::vector<CropShape> DepthwiseConv2DOp::Split(const CropShape &crop) const {
    const int kSplit = 2;
    return SplitCrop(crop, 2, kSplit, true);
}

void DepthwiseConv2DOp::Execute(const CropShape &crop) {
    const Tensor *input = Input();
    const Tensor *filter = Filter();
    const Tensor *bias = Bias();
    Tensor *output = Output();

    if (input->Type() == TensorType::UInt8 &&
        filter->Type() == TensorType::UInt8 &&
        output->Type() == TensorType::UInt8) {
        auto input_buf = input->Data<uint8_t>();
        auto filter_buf = filter->Data<uint8_t>().sliced(3, 0);
        auto bias_buf = bias->Data<int32_t>();
        auto output_buf = output->Data<uint8_t>(crop);

        int depth_multiplier =
            output_buf.dim(0).extent() / input_buf.dim(0).extent();

        int16_t input_offset = 0;
        int16_t filter_offset = 0;
        int output_offset = 0;
        int output_multiplier = 0;
        int output_shift = 0;
        int output_min = 0;
        int output_max = 0;

        APP_CHECK(
            0 == DepthwiseConvolutionUint8(
                     input_buf, filter_buf, bias_buf, depth_multiplier,
                     input_offset, filter_offset, stride_[0], stride_[1],
                     dilation_[0], dilation_[1], output_multiplier, output_shift,
                     output_offset, output_min, output_max, output_buf));
    }
}

void MaxPoolOp::Execute(const CropShape &crop) {
    const Tensor *input = Input();
    Tensor *output = Output();

    if (input->Type() == TensorType::UInt8 &&
        output->Type() == TensorType::UInt8) {
        auto input_buf = input->Data<uint8_t>();
        auto output_buf = output->Data<uint8_t>(crop);

        int output_min = 0;
        int output_max = 0;

        APP_CHECK(
            0 == MaxPoolUint8(input_buf, stride_[0], stride_[1],
                              filter_size_[0], filter_size_[1],
                              output_min, output_max, output_buf));
    }
}

Op::Bounds PadOp::InferBounds(const CropShape &crop) const {
    auto padding = Input(1)->Data<const int32_t>();

    Bounds result;

    CropShape padded_crop = crop;
    for (int d = 0; d < 4; d++) {
        padded_crop[d].first += padding(d);
    }

    result.inputs.emplace_back(
        Intersect(padded_crop, WithoutStrides(Input(0)->Shape())));
    result.inputs.emplace_back(WithoutStrides(Input(1)->Shape()));
    result.outputs.emplace_back(crop);
    return result;
}

std::vector<CropShape> PadOp::Split(const CropShape &crop) const {
    const int kSplit = 2;
    return SplitCrop(crop, 2, kSplit);
}

void PadOp::Execute(const CropShape &crop) {
    const Tensor *input = Input(0);
    auto padding = Input(1)->Data<const int32_t>();
    Tensor *output = Output();

    if (SizeOfTensorType(output->Type()) == 1) {
        auto input_buf = input->Data<uint8_t>();
        auto output_buf = output->Data<uint8_t>(crop);

        uint8_t pad_value = 0;

        for (int i = 0; i < 4; i++) {
            input_buf.translate(i, padding(i));
        }

        // TODO: This is pretty hard to beat, but surely it's possible.
        for (int y = output_buf.dim(2).min(); y <= output_buf.dim(2).max(); y++) {
            auto output_y = output_buf.sliced(2, y);
            output_y.fill(pad_value);
            if (input_buf.dim(2).min() <= y && y <= input_buf.dim(2).max()) {
                auto input_y = input_buf.sliced(2, y);
                output_y.copy_from(input_y);
            }
        }
    }
}

// TODO: Maybe this is only a reshape in some dimensions, in which case we might be able to split it.
Op::Bounds ReshapeOp::InferBounds(const CropShape &crop) const {
    Bounds result;
    result.inputs = {WithoutStrides(Input()->Shape())};
    result.outputs = {crop};
    return result;
}

std::vector<CropShape> ReshapeOp::Split(const CropShape &crop) const {
    return {crop};
}

void ReshapeOp::Execute(const CropShape &crop) {
    const Tensor *input = Input();
    Tensor *output = Output();

    if (input->Type() == TensorType::UInt8 &&
        output->Type() == TensorType::UInt8) {
        auto input_buf = input->Data<uint8_t>();
        auto output_buf = output->Data<uint8_t>(crop);

        // TODO: This should probably just be implemented by aliasing two of the tensors.
        APP_CHECK(input_buf.number_of_elements() == output_buf.number_of_elements());
        // TODO: This should also check the strides are dense.
        memcpy(output_buf.data(), input_buf.data(), input_buf.number_of_elements());
    }
}

}  // namespace interpret_nn
