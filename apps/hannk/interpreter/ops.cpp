#include "interpreter/ops.h"
#include "util/error_util.h"

#include <atomic>
#include <cmath>
#include <iostream>

#include "add_uint8_uint8.h"
#include "average_pool_uint8.h"
#include "convolution_uint8.h"
#ifdef CONV_R16
#include "convolution_r16_uint8.h"
#endif
#include "copy_uint8_uint8.h"
#include "depthwise_convolution_uint8.h"
#include "depthwise_convolution_uint8_broadcast.h"
#include "fully_connected_uint8.h"
#include "max_pool_uint8.h"
#include "tile_convolution_filter_uint8.h"

namespace hannk {

namespace {

// Check if dimension 0 and dimension 1 of buf can be fused.
template<typename T>
bool can_fuse(const HalideBuffer<T> &buf, int d0, int d1) {
    assert(d0 != d1);
    return d0 < buf.dimensions() &&
           d1 < buf.dimensions() &&
           buf.dim(d0).min() == 0 &&
           buf.dim(d1).stride() > 0 &&
           buf.dim(d1).stride() == buf.dim(d0).extent() * buf.dim(d0).stride();
}
template<typename T>
bool can_fuse_cx(const HalideBuffer<T> &buf) {
    return can_fuse(buf, 0, 1);
}
template<typename T>
bool can_fuse_xy(const HalideBuffer<T> &buf) {
    return can_fuse(buf, 1, 2);
}

// Fuse the first two dimensions of buf. d1 is deleted from the buffer.
template<typename T>
void fuse(HalideBuffer<T> &buf, int d0, int d1) {
    halide_dimension_t &dim0 = buf.raw_buffer()->dim[d0];
    halide_dimension_t &dim1 = buf.raw_buffer()->dim[d1];
    dim0.extent *= dim1.extent;
    for (int d = d1; d + 1 < buf.dimensions(); d++) {
        buf.raw_buffer()->dim[d] = buf.raw_buffer()->dim[d + 1];
    }
    buf.slice(buf.dimensions() - 1);
}
template<typename T>
void fuse_cx(HalideBuffer<T> &buf) {
    fuse(buf, 0, 1);
}
template<typename T>
void fuse_xy(HalideBuffer<T> &buf) {
    fuse(buf, 1, 2);
}

// Embed extent 1 dimensions until buf has the given rank.
template<typename T>
void pad_to_rank(HalideBuffer<T> &buf, int rank) {
    while (buf.dimensions() < rank) {
        buf.embed(buf.dimensions(), 0);
    }
}

bool is_alias(const HalideBuffer<const void> &a, const HalideBuffer<const void> &b) {
    return !(a.begin() >= b.end() || a.end() <= b.begin());
}

struct QuantizedMulAndShift {
    int multiplier, shift;
};

QuantizedMulAndShift get_quantized_mul_and_shift(double double_multiplier) {
    if (double_multiplier == 0.) {
        return {0, 0};
    }

    int shift = 0;
    const double q = std::frexp(double_multiplier, &shift);
    int64_t q_fixed = (int64_t)std::round(q * (1LL << 31));
    assert(q_fixed <= (1LL << 31));

    if (q_fixed == (1LL << 31)) {
        q_fixed /= 2;
        ++shift;
    }
    assert(q_fixed <= std::numeric_limits<int32_t>::max());

    if (shift < -31) {
        shift = 0;
        q_fixed = 0;
    }
    return {(int)q_fixed, shift};
}

QuantizedMulAndShift get_quantized_mul_and_shift_smaller_than_one(double double_multiplier) {
    assert(double_multiplier >= 0.0 && double_multiplier < 1.0);
    auto result = get_quantized_mul_and_shift(double_multiplier);
    assert(result.shift <= 0);
    return result;
}

Interval get_quantized_min_max(ActivationFunction activation, int zero_point, double scale) {
    int min = 0;
    int max = 255;
    if (activation == ActivationFunction::None) {
        // nothing
    } else if (activation == ActivationFunction::Relu) {
        min = zero_point;
    } else if (activation == ActivationFunction::Relu6) {
        min = zero_point;
        max = zero_point + (int)std::round(6.0 / scale);
    } else if (activation == ActivationFunction::ReluN1To1) {
        min = zero_point + (int)std::round(-1.0 / scale);
        max = zero_point + (int)std::round(1.0 / scale);
    } else {
        CHECK(false) << "Unsupported quantized activation function type.";
    }
    return {std::max(min, 0), std::min(max, 255)};
}

Interval get_output_range(ActivationFunction activation, Tensor *out) {
    const int output_offset = out->quantization().zero.at(0);
    assert(output_offset >= 0 && output_offset <= 255);

    const float output_scale = out->quantization().scale.at(0);

    const auto output_range = get_quantized_min_max(activation, output_offset, output_scale);
    assert(output_range.min >= 0 && output_range.min <= 255);
    assert(output_range.max >= 0 && output_range.max <= 255);
    assert(output_range.min <= output_range.max);

    return output_range;
}

}  // namespace

Op::Bounds ElementwiseOp::infer_bounds(const Box &crop) const {
    Bounds result;
    for (int i = 0; i < input_count(); i++) {
        result.inputs.emplace_back(crop);
    }
    for (int i = 0; i < output_count(); i++) {
        result.outputs.emplace_back(crop);
    }
    return result;
}

Op::Bounds PoolOp::infer_bounds(const Box &crop) const {
    Box input_crop = crop;

    input_crop[0] = crop[0];
    input_crop[1] *= stride_[0];
    input_crop[2] *= stride_[1];
    input_crop[1].max += filter_size_[0] - 1;
    input_crop[2].max += filter_size_[1] - 1;

    Bounds result;
    result.inputs = {input_crop};
    result.outputs = {crop};
    return result;
}

std::vector<SplitInfo> PoolOp::get_split_info() const {
    return {
        SplitInfo::any_split(),
        SplitInfo::any_split(),
        SplitInfo::any_split(),
        SplitInfo::any_split(),
    };
}

void AddOp::execute(const Box &crop) {
    const Tensor *in1 = input(0);
    const Tensor *in2 = input(1);
    Tensor *out = output();

    if (in1->is_type<uint8_t>() &&
        (!in2 || in2->is_type<uint8_t>()) &&
        out->is_type<uint8_t>()) {
        auto in1_buf = in1->buffer<const uint8_t>();
        auto in2_buf = in2 ? in2->buffer<const uint8_t>() : in1_buf;
        auto output_buf = out->buffer<uint8_t>(crop);

        const int in1_offset = in1->quantization().zero.at(0);
        const int in2_offset = in2 ? in2->quantization().zero.at(0) : 0;
        const int output_offset = out->quantization().zero.at(0);
        assert(in1_offset >= 0 && in1_offset <= 255);
        assert(in2_offset >= 0 && in2_offset <= 255);
        assert(output_offset >= 0 && output_offset <= 255);

        const float in1_scale = in1->quantization().scale.at(0);
        const float in2_scale = in2 ? in2->quantization().scale.at(0) : 0.0f;
        const float output_scale = out->quantization().scale.at(0);

        const int left_shift = 20;  // 20 for 8-bit, 15 for 16-bit
        const double twice_max_input_scale = 2 * std::max(in1_scale, in2_scale);
        const double real_in1_multiplier = in1_scale / twice_max_input_scale;
        const double real_in2_multiplier = in2_scale / twice_max_input_scale;
        const double real_output_multiplier = twice_max_input_scale / ((1 << left_shift) * output_scale);

        auto in1_mul_and_shift = get_quantized_mul_and_shift_smaller_than_one(real_in1_multiplier);
        auto in2_mul_and_shift = get_quantized_mul_and_shift_smaller_than_one(real_in2_multiplier);
        auto output_mul_and_shift = get_quantized_mul_and_shift_smaller_than_one(real_output_multiplier);
        assert(in1_mul_and_shift.shift <= 0);
        assert(in2_mul_and_shift.shift <= 0);
        assert(output_mul_and_shift.shift <= 0);

        in2_mul_and_shift.multiplier *= input2_sign_;

        const auto output_range = get_output_range(activation_, out);

        while (can_fuse_cx(in1_buf) && can_fuse_cx(in2_buf) && can_fuse_cx(output_buf)) {
            fuse_cx(in1_buf);
            fuse_cx(in2_buf);
            fuse_cx(output_buf);
        }
        pad_to_rank(in1_buf, 4);
        pad_to_rank(in2_buf, 4);
        pad_to_rank(output_buf, 4);

        CHECK(0 == add_uint8_uint8(left_shift, in1_buf, in2_buf,
                                   in1_offset, in1_mul_and_shift.multiplier, -in1_mul_and_shift.shift,
                                   in2_offset, in2_mul_and_shift.multiplier, -in2_mul_and_shift.shift,
                                   output_offset, output_mul_and_shift.multiplier, -output_mul_and_shift.shift,
                                   output_range.min, output_range.max, output_buf));
    } else {
        CHECK(false);
    }
}

void AveragePoolOp::execute(const Box &crop) {
    const Tensor *in = input();
    Tensor *out = output();

    if (in->is_type<uint8_t>() && out->is_type<uint8_t>()) {
        auto input_buf = in->buffer<const uint8_t>();
        auto output_buf = out->buffer<uint8_t>(crop);

        const auto output_range = get_output_range(activation_, out);

        // TODO: does this need to handle Padding::Same?
        CHECK(padding_ == Padding::Valid) << "AveragePoolOp doesn't handle all paddings yet";

        CHECK(
            0 == average_pool_uint8(input_buf, stride_[0], stride_[1],
                                    filter_size_[0], filter_size_[1],
                                    output_range.min, output_range.max, output_buf));
    }
}

Op::Bounds ConcatenationOp::infer_bounds(const Box &crop) const {
    // We need everything from the concatenated dimension, everything else
    // is the same as the crop.
    // TODO: It's possible that if the concatenated dimension is cropped
    // from the out, we could reduce the bounds required of some of the
    // ins.
    Bounds result;
    for (int i = 0; i < input_count(); i++) {
        result.inputs.emplace_back(crop);
        result.inputs.back()[axis_] = input(i)->interval(axis_);
    }
    result.outputs.emplace_back(crop);
    result.outputs.back()[axis_] = output()->interval(axis_);
    return result;
}

std::vector<SplitInfo> ConcatenationOp::get_split_info() const {
    // Allow any split on any dimension other than the concatenated dimension.
    std::vector<SplitInfo> splits(output()->rank(), SplitInfo::any_split());
    splits[axis_] = SplitInfo::no_split();
    return splits;
}

void ConcatenationOp::execute(const Box &crop) {
    Tensor *out = output();

    auto output_buf = out->buffer(crop);

    int output_i = output_buf.dim(axis_).min();
    for (int i = 0; i < input_count(); i++) {
        auto input_buf = input(i)->buffer(crop);
        for (int j = input_buf.dim(axis_).min(); j <= input_buf.dim(axis_).max(); j++) {
            // TODO: Maybe we could just copy whole buffers?
            auto input_j = input_buf.sliced(axis_, j);
            auto output_j = output_buf.sliced(axis_, output_i++);
            output_j.copy_from(input_j);
        }
    }
}

halide_type_t Conv2DOp::filter_type() const {
    const halide_filter_metadata_t *metadata = convolution_uint8_metadata();
    return metadata->arguments[1].type;
}

Box Conv2DOp::filter_required() const {
    if (filter()->is_type<uint8_t>()) {
        // Pass minimal sized buffers to learn about the alignment requirements.
        HalideBuffer<uint8_t> input_buf(nullptr, 1, 1, 1, 1);
        HalideBuffer<int32_t> bias_buf(nullptr, 1);
        Halide::Runtime::Buffer<void, 5> filter_buf(filter_type(), 1, 1, 1, 1, 1);
        // TODO: How to initialize the above buffer without allocating?
        filter_buf.deallocate();
        HalideBuffer<uint8_t> output_buf;

        CHECK(0 == convolution_uint8(input_buf, filter_buf, bias_buf, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, output_buf));

        const int vector_reduction = filter_buf.dim(0).extent();
        const int unroll_reduction = filter()->extent(0) >= 16 ? 16 : 4;
        const int vector_alignment = filter_buf.dim(1).extent();
        const int channel_alignment = unroll_reduction / vector_reduction;
        return {
            {0, vector_reduction - 1},
            {0, align_up(filter()->extent(3), vector_alignment) - 1},
            {0, align_up(ceil_div(filter()->extent(0), vector_reduction), channel_alignment) - 1},
            {filter()->interval(1)},
            {filter()->interval(2)},
        };
    } else {
        CHECK(false);
    }
}

Box Conv2DOp::input_required(const Box &crop) const {
    Box input_crop = crop;
    Box filter_shape = filter()->box();

    input_crop[0] = filter_shape[0];
    input_crop[1] *= stride_[0];
    input_crop[2] *= stride_[1];
    input_crop[1].max += dilation_[0] * (filter_shape[1].extent() - 1);
    input_crop[2].max += dilation_[1] * (filter_shape[2].extent() - 1);

    if (padding_ == Padding::Same) {
        const int input_width = input()->extent(1);
        const int input_height = input()->extent(2);
        const int filter_width = filter()->extent(1);
        const int filter_height = filter()->extent(2);
        const int output_width = output()->extent(1);
        const int output_height = output()->extent(2);

        const int dilated_filter_width = dilation_[0] * (filter_width - 1) + 1;
        const int dilated_filter_height = dilation_[1] * (filter_height - 1) + 1;

        const int pad_width =
            std::max(0, ((output_width - 1) * stride_[0] + dilated_filter_width - input_width) / 2);
        const int pad_height =
            std::max(0, ((output_height - 1) * stride_[1] + dilated_filter_height - input_height) / 2);

        input_crop[1].max += pad_width;
        input_crop[2].max += pad_height;
    }
    return input_crop;
}

Op::Bounds Conv2DOp::infer_bounds(const Box &crop) const {
    Bounds result;
    result.inputs = {
        input_required(crop),
        filter()->box(),
        bias()->box(),
    };
    result.outputs = {crop};

    return result;
}

std::vector<SplitInfo> Conv2DOp::get_split_info() const {
    return {
        SplitInfo::no_split(),
        SplitInfo::any_split(),
        SplitInfo::any_split(),
        SplitInfo::any_split()};
}

void Conv2DOp::execute(const Box &crop) {
    const Tensor *in = input();
    const Tensor *filt = filter();
    Tensor *out = output();

    if (in->is_type<uint8_t>() &&
        out->is_type<uint8_t>()) {
        // TODO: reduce code duplication between here and DepthwiseConv2D
        auto input_buf = in->buffer<const uint8_t>();
        auto filter_buf = filt->buffer<const void>();
        auto bias_buf = bias()->buffer<const int32_t>();
        auto output_buf = out->buffer<uint8_t>(crop);

        const int input_offset = in->quantization().zero.at(0);
        const int filter_offset = filt->quantization().zero.at(0);
#ifndef NDEBUG
        const int bias_offset = bias()->quantization().zero.at(0);
#endif
        const int output_offset = out->quantization().zero.at(0);
        assert(input_offset >= 0 && input_offset <= 255);
        assert(filter_offset >= 0 && filter_offset <= 255);
        assert(bias_offset == 0);
        assert(output_offset >= 0 && output_offset <= 255);

        const float input_scale = in->quantization().scale.at(0);
        const float filter_scale = filt->quantization().scale.at(0);
#ifndef NDEBUG
        const float bias_scale = bias()->quantization().scale.at(0);
#endif
        const float output_scale = out->quantization().scale.at(0);

        const double input_product_scale = input_scale * filter_scale;
        assert(std::abs(input_product_scale - bias_scale) <=
               std::min(input_product_scale, (double)bias_scale) * 1e-6);

        const double real_multiplier = input_product_scale / output_scale;
        const auto mul_and_shift = get_quantized_mul_and_shift_smaller_than_one(real_multiplier);
        const int output_multiplier = mul_and_shift.multiplier;
        // get_quantized_mul_and_shift_smaller_than_one() returns a negative shift;
        // convolution_uint8() expects a positive shift.
        const int output_shift = -mul_and_shift.shift;

        const auto output_range = get_output_range(activation_, out);

        const int filter_width = filter_buf.dim(1).extent();
        const int filter_height = filter_buf.dim(2).extent();
        if (filter_width == 1 && filter_height == 1) {
            // For 1x1 filters, we can fuse x and y, which can help avoid overhead for
            // small output sizes.
            while (can_fuse_xy(input_buf) && can_fuse_xy(output_buf)) {
                fuse_xy(input_buf);
                fuse_xy(output_buf);
            }
            pad_to_rank(input_buf, 4);
            pad_to_rank(output_buf, 4);
        }

#ifdef CONV_R16
        if (input_buf.dim(0).extent() >= 16) {
            // For large reductions, use the big reduction version.
            // TODO: We really ought to be able to do this with GuardWithIf
            // and/or specialize.
            CHECK(
                0 == convolution_r16_uint8(input_buf, filter_buf, bias_buf, (uint8_t)input_offset,
                                           (uint8_t)filter_offset, stride_[0], stride_[1],
                                           dilation_[0], dilation_[1], output_multiplier,
                                           output_shift, (uint8_t)output_offset,
                                           output_range.min, output_range.max, output_buf));
        } else
#endif
        {
            CHECK(
                0 == convolution_uint8(input_buf, filter_buf, bias_buf, (uint8_t)input_offset,
                                       (uint8_t)filter_offset, stride_[0], stride_[1],
                                       dilation_[0], dilation_[1], output_multiplier,
                                       output_shift, (uint8_t)output_offset,
                                       output_range.min, output_range.max, output_buf));
        }
    } else {
        CHECK(false);
    }
}

int DepthwiseConv2DOp::depth_multiplier() const {
    return output()->extent(0) / input()->extent(0);
}

Box DepthwiseConv2DOp::input_required(const Box &crop) const {
    Box input_crop = crop;
    Box filter_shape = filter()->box();

    input_crop[0] = crop[0] / depth_multiplier();
    input_crop[1] *= stride_[0];
    input_crop[2] *= stride_[1];
    input_crop[1].max += dilation_[0] * (filter_shape[1].extent() - 1);
    input_crop[2].max += dilation_[1] * (filter_shape[2].extent() - 1);

    if (padding_ == Padding::Same) {
        const int input_width = input()->extent(1);
        const int input_height = input()->extent(2);
        const int filter_width = filter()->extent(1);
        const int filter_height = filter()->extent(2);
        const int output_width = output()->extent(1);
        const int output_height = output()->extent(2);

        const int dilated_filter_width = dilation_[0] * (filter_width - 1) + 1;
        const int dilated_filter_height = dilation_[1] * (filter_height - 1) + 1;

        const int pad_width =
            std::max(0, ((output_width - 1) * stride_[0] + dilated_filter_width - input_width) / 2);
        const int pad_height =
            std::max(0, ((output_height - 1) * stride_[1] + dilated_filter_height - input_height) / 2);

        input_crop[1].max += pad_width;
        input_crop[2].max += pad_height;
    }
    return input_crop;
}

Op::Bounds DepthwiseConv2DOp::infer_bounds(const Box &crop) const {
    Bounds result;
    result.inputs = {
        input_required(crop),
        filter()->box(),
        bias()->box(),
    };
    result.outputs = {crop};
    return result;
}

std::vector<SplitInfo> DepthwiseConv2DOp::get_split_info() const {
    return {
        SplitInfo::no_split(),
        SplitInfo::guard_with_if(2),
        SplitInfo::guard_with_if(2),
        SplitInfo::any_split()};
}

void DepthwiseConv2DOp::execute(const Box &crop) {
    const Tensor *in = input();
    const Tensor *filt = filter();
    Tensor *out = output();

    if (in->is_type<uint8_t>() &&
        filt->is_type<uint8_t>() &&
        out->is_type<uint8_t>()) {
        // TODO: reduce code duplication between here and Conv2D
        auto input_buf = in->buffer<const uint8_t>();
        auto filter_buf = filt->buffer<const uint8_t>().sliced(3, 0);
        auto bias_buf = bias()->buffer<const int32_t>();
        auto output_buf = out->buffer<uint8_t>(crop);

        int depth_multiplier = this->depth_multiplier();
        assert(depth_multiplier * input_buf.dim(0).extent() == output_buf.dim(0).extent());

        const int input_offset = in->quantization().zero.at(0);
        const int filter_offset = filt->quantization().zero.at(0);
#ifndef NDEBUG
        const int bias_offset = bias()->quantization().zero.at(0);
#endif
        const int output_offset = out->quantization().zero.at(0);
        assert(input_offset >= 0 && input_offset <= 255);
        assert(filter_offset >= 0 && filter_offset <= 255);
        assert(bias_offset == 0);
        assert(output_offset >= 0 && output_offset <= 255);

        const float input_scale = in->quantization().scale.at(0);
        const float filter_scale = filt->quantization().scale.at(0);
#ifndef NDEBUG
        const float bias_scale = bias()->quantization().scale.at(0);
#endif
        const float output_scale = out->quantization().scale.at(0);

        const double input_product_scale = input_scale * filter_scale;
        assert(std::abs(input_product_scale - bias_scale) <=
               std::min(input_product_scale, (double)bias_scale) * 1e-6);

        const double real_multiplier = input_product_scale / output_scale;
        const auto mul_and_shift = get_quantized_mul_and_shift_smaller_than_one(real_multiplier);
        const int output_multiplier = mul_and_shift.multiplier;
        // get_quantized_mul_and_shift_smaller_than_one() returns a negative shift;
        // depthwise_convolution_uint8() expects a positive shift.
        const int output_shift = -mul_and_shift.shift;

        const auto output_range = get_output_range(activation_, out);

        if (depth_multiplier >= output_buf.dim(0).extent()) {
            CHECK(
                0 == depthwise_convolution_uint8_broadcast(
                         input_buf, filter_buf, bias_buf, depth_multiplier,
                         (uint8_t)input_offset, (uint8_t)filter_offset, stride_[0], stride_[1],
                         dilation_[0], dilation_[1], output_multiplier, output_shift,
                         (uint8_t)output_offset, (uint8_t)output_range.min, (uint8_t)output_range.max, output_buf));
        } else {
            CHECK(
                0 == depthwise_convolution_uint8(
                         input_buf, filter_buf, bias_buf, depth_multiplier,
                         (uint8_t)input_offset, (uint8_t)filter_offset, stride_[0], stride_[1],
                         dilation_[0], dilation_[1], output_multiplier, output_shift,
                         (uint8_t)output_offset, (uint8_t)output_range.min, (uint8_t)output_range.max, output_buf));
        }
    } else {
        CHECK(false);
    }
}

Op::Bounds FullyConnectedOp::infer_bounds(const Box &crop) const {
    Bounds result;
    result.inputs.emplace_back(input()->box());
    result.inputs.emplace_back(filter()->box());
    result.inputs.emplace_back(bias()->box());
    result.outputs.emplace_back(output()->box());
    return result;
}

std::vector<SplitInfo> FullyConnectedOp::get_split_info() const {
    return {
        SplitInfo::no_split(),
        SplitInfo::any_split(),
    };
}

void FullyConnectedOp::execute(const Box &crop) {
    const Tensor *in = input();
    const Tensor *filt = filter();
    Tensor *out = output();

    if (in->is_type<uint8_t>() &&
        filt->is_type<uint8_t>() &&
        out->is_type<uint8_t>()) {
        auto input_buf = in->buffer<const uint8_t>();
        auto filter_buf = filt->buffer<const uint8_t>();
        auto bias_buf = bias()->buffer<const int32_t>();
        auto output_buf = out->buffer<uint8_t>(crop);

        const int input_offset = in->quantization().zero.at(0);
        const int filter_offset = filt->quantization().zero.at(0);
#ifndef NDEBUG
        const int bias_offset = bias()->quantization().zero.at(0);
#endif
        const int output_offset = out->quantization().zero.at(0);
        assert(input_offset >= 0 && input_offset <= 255);
        assert(filter_offset >= 0 && filter_offset <= 255);
        assert(bias_offset == 0);
        assert(output_offset >= 0 && output_offset <= 255);

        const float input_scale = in->quantization().scale.at(0);
        const float filter_scale = filt->quantization().scale.at(0);
#ifndef NDEBUG
        const float bias_scale = bias()->quantization().scale.at(0);
#endif
        const float output_scale = out->quantization().scale.at(0);

        const double input_product_scale = input_scale * filter_scale;
        assert(std::abs(input_product_scale - bias_scale) <=
               std::min(input_product_scale, (double)bias_scale) * 1e-6);

        const double real_multiplier = input_product_scale / output_scale;
        const auto mul_and_shift = get_quantized_mul_and_shift_smaller_than_one(real_multiplier);
        const int output_multiplier = mul_and_shift.multiplier;
        const int output_shift = -mul_and_shift.shift;

        const auto output_range = get_output_range(activation_, out);

        CHECK(false) << "FullyConnectedOp isn't complete yet and probably isn't correct.";

        CHECK(
            0 == fully_connected_uint8(
                     input_buf, filter_buf, bias_buf,
                     (uint8_t)input_offset, (uint8_t)filter_offset, output_multiplier, output_shift,
                     (uint8_t)output_offset, (uint8_t)output_range.min, (uint8_t)output_range.max, output_buf));
    } else {
        CHECK(false);
    }
}

void MaxPoolOp::execute(const Box &crop) {
    const Tensor *in = input();
    Tensor *out = output();

    if (in->is_type<uint8_t>() && out->is_type<uint8_t>()) {
        auto input_buf = in->buffer<const uint8_t>();
        auto output_buf = out->buffer<uint8_t>(crop);

        // TODO: does this need to handle Padding::Same?
        CHECK(padding_ == Padding::Valid) << "AveragePoolOp doesn't handle all paddings yet";

        const auto output_range = get_output_range(activation_, out);

        CHECK(
            0 == max_pool_uint8(input_buf, stride_[0], stride_[1],
                                filter_size_[0], filter_size_[1],
                                output_range.min, output_range.max, output_buf));
    } else {
        CHECK(false);
    }
}

Op::Bounds PadOp::infer_bounds(const Box &crop) const {
    Bounds result;
    if (input(1)) {
        auto padding = input(1)->buffer<const int32_t>();

        Box padded_crop = crop;
        for (int d = 0; d < output()->rank(); d++) {
            padded_crop[d] += padding(0, d);
        }

        result.inputs.emplace_back(
            intersect(padded_crop, input(0)->box()));
        result.inputs.emplace_back(input(1)->box());
    } else {
        result.inputs.emplace_back(crop);
        result.inputs.emplace_back(Box());
    }
    result.outputs.emplace_back(crop);
    return result;
}

std::vector<SplitInfo> PadOp::get_split_info() const {
    return {(size_t)output()->rank(), SplitInfo::any_split()};
}

void PadOp::execute(const Box &crop) {
    const Tensor *in = input(0);
    Tensor *out = output();

    if (out->type().bytes() == 1) {
        auto input_buf = in->buffer<const uint8_t>();
        auto output_buf = out->buffer<uint8_t>(crop);

        if (input(1)) {
            auto padding = input(1)->buffer<const int32_t>();
            for (int d = 0; d < output_buf.dimensions(); d++) {
                input_buf.translate(d, padding(0, d));
            }
        }

        uint8_t pad_value = in->quantization().zero.at(0);

        if (is_alias(input_buf, output_buf)) {
            // This is an in-place padding. Just fill in the
            // padded areas.
            // Fill dimensions outermost to innermost, to increase the
            // area which should vectorize cleanly.
            for (int d = output_buf.dimensions() - 1; d >= 0; d--) {
                int input_min = input_buf.dim(d).min();
                int output_min = output_buf.dim(d).min();
                if (output_min < input_min) {
                    auto before = output_buf.cropped(d, output_min, input_min - output_min);
                    before.fill(pad_value);
                }
                int input_max = input_buf.dim(d).max();
                int output_max = output_buf.dim(d).max();
                if (output_max > input_max) {
                    auto after = output_buf.cropped(d, input_max + 1, output_max - input_max);
                    after.fill(pad_value);
                }
                input_min = std::max(input_min, output_min);
                input_max = std::min(input_max, output_max);
                output_buf.crop(d, input_min, input_max - input_min + 1);
            }
        } else {
            CHECK(0 == copy_uint8_uint8(input_buf, pad_value, output_buf));
        }
    } else {
        CHECK(false);
    }
}

// TODO: Maybe this is only a reshape in some dimensions, in which case we might be able to split it.
Op::Bounds ReshapeOp::infer_bounds(const Box &crop) const {
    Bounds result;
    result.inputs = {input()->box()};
    result.outputs = {crop};
    return result;
}

std::vector<SplitInfo> ReshapeOp::get_split_info() const {
    return {};
}

void ReshapeOp::execute(const Box &crop) {
    const Tensor *in = input();
    Tensor *out = output();

    auto input_buf = in->buffer<const void>();
    auto output_buf = out->buffer(crop);

    // TODO: should reality-check that the output buf matches the shape we expect
    // assert((int) new_shape_.size() == output_buf.dimensions());
    // for (int d = 0; d < output_buf.dimensions(); d++) {
    //     assert(new_shape_.at(d) == output_buf.dim(d).extent());
    // }

    assert(input_buf.number_of_elements() == output_buf.number_of_elements());
    size_t output_size = output_buf.number_of_elements() * out->type().bytes();
    if (is_alias(input_buf, output_buf)) {
        assert(input_buf.begin() == output_buf.begin());
        assert(input_buf.end() == output_buf.end());
    } else {
        // TODO: This should also check the strides are dense.
        memcpy(output_buf.data(), input_buf.data(), output_size);
    }
}

Op::Bounds TileConvFilterOp::infer_bounds(const Box &crop) const {
    assert(crop[0].min == 0);
    Box input = {
        crop[2] * crop[0].extent(),
        crop[3],
        crop[4],
        crop[1],
    };
    Bounds result;
    result.inputs = {input};
    result.outputs = {crop};
    return result;
}

std::vector<SplitInfo> TileConvFilterOp::get_split_info() const {
    return {};
}

void TileConvFilterOp::execute(const Box &crop) {
    const Tensor *in = input();
    Tensor *out = output();

    if (in->is_type<uint8_t>()) {
        auto input_buf = in->buffer<const uint8_t>();
        auto output_buf = out->buffer<void>(crop);

        int input_offset = in->quantization().zero.at(0);
        int output_offset = out->quantization().zero.at(0);

        CHECK(0 == tile_convolution_filter_uint8(input_buf, input_offset, output_offset, output_buf));
    } else {
        CHECK(false);
    }
}

void AddOp::accept(OpVisitor *v) {
    v->visit(this);
}

void AveragePoolOp::accept(OpVisitor *v) {
    v->visit(this);
}

void ConcatenationOp::accept(OpVisitor *v) {
    v->visit(this);
}

void Conv2DOp::accept(OpVisitor *v) {
    v->visit(this);
}

void DepthwiseConv2DOp::accept(OpVisitor *v) {
    v->visit(this);
}

void FullyConnectedOp::accept(OpVisitor *v) {
    v->visit(this);
}

void MaxPoolOp::accept(OpVisitor *v) {
    v->visit(this);
}

void PadOp::accept(OpVisitor *v) {
    v->visit(this);
}

void ReshapeOp::accept(OpVisitor *v) {
    v->visit(this);
}

void TileConvFilterOp::accept(OpVisitor *v) {
    v->visit(this);
}

}  // namespace hannk
