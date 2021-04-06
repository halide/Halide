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
#include "depthwise_convolution_broadcast_uint8.h"
#include "depthwise_convolution_dm1_uint8.h"
#include "depthwise_convolution_uint8.h"
#include "fill_uint8.h"
#include "fully_connected_uint8.h"
#include "l2_normalization_uint8.h"
#include "max_pool_uint8.h"
#include "softmax_uint8.h"
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

template<typename Ta, typename Tb>
void optimize_elementwise_shapes(HalideBuffer<Ta> &a, HalideBuffer<Tb> &b, int rank) {
    while (can_fuse_cx(a) && can_fuse_cx(b) &&
           a.dim(0).extent() == b.dim(0).extent()) {
        fuse_cx(a);
        fuse_cx(b);
    }
    pad_to_rank(a, rank);
    pad_to_rank(b, rank);
}

template<typename Ta, typename Tb, typename Tc>
void optimize_elementwise_shapes(HalideBuffer<Ta> &a, HalideBuffer<Tb> &b, HalideBuffer<Tc> &c, int rank) {
    while (can_fuse_cx(a) && can_fuse_cx(b) && can_fuse_cx(c) &&
           a.dim(0).extent() == c.dim(0).extent() &&
           b.dim(0).extent() == c.dim(0).extent()) {
        fuse_cx(a);
        fuse_cx(b);
        fuse_cx(c);
    }
    pad_to_rank(a, rank);
    pad_to_rank(b, rank);
    pad_to_rank(c, rank);
}

bool is_alias(const HalideBuffer<const void> &a, const HalideBuffer<const void> &b) {
    return !(a.begin() >= b.end() || a.end() <= b.begin());
}

template<typename T, typename U>
void crop_to_union(HalideBuffer<T> &a, HalideBuffer<U> &b) {
    assert(a.dimensions() == b.dimensions());
    for (int d = 0; d < a.dimensions(); d++) {
        int min = std::max(a.dim(d).min(), b.dim(d).min());
        int max = std::min(a.dim(d).max(), b.dim(d).max());
        a.crop(d, min, max - min + 1);
        b.crop(d, min, max - min + 1);
    }
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

Interval get_output_range(ActivationFunction activation, const QuantizationInfo &quantization) {
    const int output_zero = quantization.zero.at(0);
    assert(output_zero >= 0 && output_zero <= 255);

    const float output_scale = quantization.scale.at(0);

    const auto output_range = get_quantized_min_max(activation, output_zero, output_scale);
    assert(output_range.min >= 0 && output_range.min <= 255);
    assert(output_range.max >= 0 && output_range.max <= 255);
    assert(output_range.min <= output_range.max);

    return output_range;
}

struct MultiplyParams {
    int a_zero;
    int b_zero;
    int c_zero;
    QuantizedMulAndShift c;
};

MultiplyParams get_quantized_multiply_params(const QuantizationInfo &a, const QuantizationInfo &b, const QuantizationInfo &c) {
    MultiplyParams result;
    result.a_zero = a.zero.at(0);
    result.b_zero = b.zero.at(0);
    result.c_zero = c.zero.at(0);

    const float a_scale = a.scale.at(0);
    const float b_scale = b.scale.at(0);
    const float c_scale = c.scale.at(0);
    const double ab_scale = a_scale * b_scale;
    result.c = get_quantized_mul_and_shift_smaller_than_one(ab_scale / c_scale);
    result.c.shift = -result.c.shift;

    return result;
}

void add(HalideBuffer<const uint8_t> in1, const QuantizationInfo &in1q,
         HalideBuffer<const uint8_t> in2, const QuantizationInfo &in2q, int in2sign,
         HalideBuffer<uint8_t> out, const QuantizationInfo &outq, ActivationFunction activation) {
    const int in1_zero = in1q.zero.at(0);
    const int in2_zero = in2q.zero.at(0);
    const int out_zero = outq.zero.at(0);

    const float in1_scale = in1q.scale.at(0);
    const float in2_scale = in2q.scale.at(0);
    const float out_scale = outq.scale.at(0);

    const int left_shift = 20;  // 20 for 8-bit, 15 for 16-bit
    const double twice_max_input_scale = 2 * std::max(in1_scale, in2_scale);
    const double real_in1_multiplier = in1_scale / twice_max_input_scale;
    const double real_in2_multiplier = in2_scale / twice_max_input_scale;
    const double real_out_multiplier = twice_max_input_scale / ((1 << left_shift) * out_scale);

    auto in1_mul_and_shift = get_quantized_mul_and_shift_smaller_than_one(real_in1_multiplier);
    auto in2_mul_and_shift = get_quantized_mul_and_shift_smaller_than_one(real_in2_multiplier);
    auto out_mul_and_shift = get_quantized_mul_and_shift_smaller_than_one(real_out_multiplier);
    assert(in1_mul_and_shift.shift <= 0);
    assert(in2_mul_and_shift.shift <= 0);
    assert(out_mul_and_shift.shift <= 0);

    in2_mul_and_shift.multiplier *= in2sign;

    const auto out_range = get_output_range(activation, outq);

    optimize_elementwise_shapes(in1, in2, out, 4);

    CHECK(0 == add_uint8_uint8(left_shift, in1, in2,
                               in1_zero, in1_mul_and_shift.multiplier, -in1_mul_and_shift.shift,
                               in2_zero, in2_mul_and_shift.multiplier, -in2_mul_and_shift.shift,
                               out_zero, out_mul_and_shift.multiplier, -out_mul_and_shift.shift,
                               out_range.min, out_range.max, out));
}

void requantize(const HalideBuffer<const uint8_t> &in, const QuantizationInfo &inq,
                HalideBuffer<uint8_t> out, const QuantizationInfo &outq) {
    if (inq == outq) {
        // Some of these are just copies, or no-ops.
        if (is_alias(in, out)) {
            return;
        } else {
            out.copy_from(in);
        }
    } else {
        // TODO: Maybe a dedicated pipeline for this would be better. It
        // could be a little faster, and avoid some quantization error.
        add(in, inq, in, inq, 0, out, outq, ActivationFunction::None);
    }
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

const char *BinaryOp::to_string(BinaryOp::Operator op) {
    switch (op) {
    case Add:
        return "Add";
    case Sub:
        return "Sub";
    default:
        CHECK(false) << "Unsupported binary op\n";
        return nullptr;
    }
}

void BinaryOp::execute(const Box &crop) {
    const Tensor *in1 = input(0);
    const Tensor *in2 = input(1);
    Tensor *out = output();

    if (in1->is_type<uint8_t>() &&
        in2->is_type<uint8_t>() &&
        out->is_type<uint8_t>()) {
        switch (op_) {
        case Add:
        case Sub:
            add(in1->buffer<const uint8_t>(), in1->quantization(),
                in2->buffer<const uint8_t>(), in2->quantization(), op_ == Add ? 1 : -1,
                out->buffer<uint8_t>(), out->quantization(), activation_);
            break;
        }
    } else {
        CHECK(false) << "Unsupported type " << out->type() << "\n";
    }
}

Op::Bounds ConcatenationOp::infer_bounds(const Box &crop) const {
    // We need everything from the concatenated dimension, everything else
    // is the same as the crop.
    // TODO: It's possible that if the concatenated dimension is cropped
    // from the out, we could reduce the bounds required of some of the ins.
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
    HalideBuffer<void> output_buf = output()->buffer(crop);

    int concatenated_i = 0;
    for (int i = 0; i < input_count(); i++) {
        HalideBuffer<const void> input_buf = input(i)->buffer();
        assert(input_buf.dim(axis_).min() == 0);
        input_buf.translate(axis_, concatenated_i);
        concatenated_i += input_buf.dim(axis_).extent();

        HalideBuffer<void> output_crop = output_buf;
        crop_to_union(output_crop, input_buf);
        requantize(input_buf, input(i)->quantization(), output_crop, output()->quantization());
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
        CHECK(false) << "Unsupported type " << filter()->type() << "\n";
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

        MultiplyParams params =
            get_quantized_multiply_params(in->quantization(), filt->quantization(), out->quantization());

        const auto output_range = get_output_range(activation_, out->quantization());

        assert(filter_buf.dimensions() == 5);
        const int filter_width = filter_buf.dim(3).extent();
        const int filter_height = filter_buf.dim(4).extent();
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
                0 == convolution_r16_uint8(input_buf, filter_buf, bias_buf, (uint8_t)params.a_zero,
                                           (uint8_t)params.b_zero, stride_[0], stride_[1],
                                           dilation_[0], dilation_[1], params.c.multiplier,
                                           params.c.shift, (uint8_t)params.c_zero,
                                           output_range.min, output_range.max, output_buf));
        } else
#endif
        {
            CHECK(
                0 == convolution_uint8(input_buf, filter_buf, bias_buf, (uint8_t)params.a_zero,
                                       (uint8_t)params.b_zero, stride_[0], stride_[1],
                                       dilation_[0], dilation_[1], params.c.multiplier,
                                       params.c.shift, (uint8_t)params.c_zero,
                                       output_range.min, output_range.max, output_buf));
        }
    } else {
        CHECK(false) << "Unsupported type " << out->type() << "\n";
    }
}

int DepthwiseConv2DOp::depth_multiplier() const {
    int input_channels = input()->extent(0);
    int output_channels = output()->extent(0);
    return output_channels / std::min(input_channels, output_channels);
}

Box DepthwiseConv2DOp::input_required(const Box &crop) const {
    Box input_crop = crop;
    Box filter_shape = filter()->box();

    input_crop[0] = crop[0] / depth_multiplier();
    if (input_crop[0].extent() > 1) {
        // TODO: We need padding on the input for a native SIMD vector,
        // don't hardcode this constant.
        input_crop[0].set_extent(std::max(input_crop[0].extent(), 32));
    } else {
        // Don't pad when broadcasting.
    }
    input_crop[1] *= stride_[0];
    input_crop[2] *= stride_[1];
    input_crop[1].max += dilation_[0] * (filter_shape[1].extent() - 1);
    input_crop[2].max += dilation_[1] * (filter_shape[2].extent() - 1);

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

        MultiplyParams params =
            get_quantized_multiply_params(in->quantization(), filt->quantization(), out->quantization());

        const auto output_range = get_output_range(activation_, out->quantization());

        if (depth_multiplier >= output_buf.dim(0).extent()) {
            CHECK(
                0 == depthwise_convolution_broadcast_uint8(
                         input_buf, filter_buf, bias_buf, depth_multiplier,
                         (uint8_t)params.a_zero, (uint8_t)params.b_zero, stride_[0], stride_[1],
                         dilation_[0], dilation_[1], params.c.multiplier, params.c.shift,
                         (uint8_t)params.c_zero, (uint8_t)output_range.min, (uint8_t)output_range.max, output_buf));
        } else if (depth_multiplier == 1) {
            CHECK(
                0 == depthwise_convolution_dm1_uint8(
                         input_buf, filter_buf, bias_buf, depth_multiplier,
                         (uint8_t)params.a_zero, (uint8_t)params.b_zero, stride_[0], stride_[1],
                         dilation_[0], dilation_[1], params.c.multiplier, params.c.shift,
                         (uint8_t)params.c_zero, (uint8_t)output_range.min, (uint8_t)output_range.max, output_buf));
        } else {
            CHECK(
                0 == depthwise_convolution_uint8(
                         input_buf, filter_buf, bias_buf, depth_multiplier,
                         (uint8_t)params.a_zero, (uint8_t)params.b_zero, stride_[0], stride_[1],
                         dilation_[0], dilation_[1], params.c.multiplier, params.c.shift,
                         (uint8_t)params.c_zero, (uint8_t)output_range.min, (uint8_t)output_range.max, output_buf));
        }
    } else {
        CHECK(false) << "Unsupported type " << out->type() << "\n";
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

        // TODO: This should be handled explicitly with a reshape.
        // It's annoying tflite doesn't require this. This means
        // that we can't arbitrarily insert padding of the strides
        // for tensors consumed by this op.
        while (input_buf.dimensions() > 2) {
            CHECK(can_fuse_cx(input_buf)) << "Unfusable fully connected input\n";
            fuse_cx(input_buf);
        }

        MultiplyParams params =
            get_quantized_multiply_params(in->quantization(), filt->quantization(), out->quantization());

        const auto output_range = get_output_range(activation_, out->quantization());

        CHECK(
            0 == fully_connected_uint8(
                     input_buf, filter_buf, bias_buf, (uint8_t)params.a_zero, (uint8_t)params.b_zero,
                     (uint8_t)params.c_zero, params.c.multiplier, params.c.shift, (uint8_t)output_range.min,
                     (uint8_t)output_range.max, output_buf));
    } else {
        CHECK(false) << "Unsupported type " << out->type() << "\n";
    }
}

std::vector<SplitInfo> L2NormalizationOp::get_split_info() const {
    // Allow any split on any dimension other than the first dimension, where we
    // compute a reduction.
    std::vector<SplitInfo> splits(output()->rank(), SplitInfo::any_split());
    splits.front() = SplitInfo::no_split();
    return splits;
}

void L2NormalizationOp::execute(const Box &crop) {
    const Tensor *in = input();
    Tensor *out = output();

    if (in->is_type<uint8_t>() && out->is_type<uint8_t>()) {
        auto in_buf = in->buffer<const uint8_t>();
        auto output_buf = out->buffer<uint8_t>(crop);

        const int input_zero = in->quantization().zero.at(0);
        assert(input_zero >= 0 && input_zero <= 255);

        assert(out->quantization().scale.at(0) == 1.0f / 128.0f);
        assert(out->quantization().zero.at(0) == 128);

        CHECK(0 == l2_normalization_uint8(in_buf, input_zero, output_buf));
    } else {
        CHECK(false) << "Unsupported type " << out->type() << "\n";
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
            for (int d = 0; d < std::min(padding.extent(1), output_buf.dimensions()); d++) {
                input_buf.translate(d, padding(0, d));
            }
        }

        uint8_t pad_value = in->quantization().zero.at(0);

        for (int d = output_buf.dimensions() - 1; d >= 0; d--) {
            int input_min = input_buf.dim(d).min();
            int output_min = output_buf.dim(d).min();
            if (output_min < input_min) {
                auto before = output_buf.cropped(d, output_min, input_min - output_min);
                CHECK(0 == fill_uint8(pad_value, before));
            } else {
                input_min = output_min;
            }
            int input_max = input_buf.dim(d).max();
            int output_max = output_buf.dim(d).max();
            if (output_max > input_max) {
                auto after = output_buf.cropped(d, input_max + 1, output_max - input_max);
                CHECK(0 == fill_uint8(pad_value, after));
            } else {
                input_max = output_max;
            }
            output_buf.crop(d, input_min, input_max - input_min + 1);
        }
        if (!is_alias(input_buf, output_buf)) {
            CHECK(0 == copy_uint8_uint8(input_buf, output_buf));
        }
    } else {
        CHECK(false) << "Unsupported type " << out->type() << "\n";
    }
}

namespace {

int compute_padding(int stride, int in_size, int filter_size, int out_size) {
    const int effective_filter_size = (filter_size - 1) + 1;
    const int total_padding = std::max(0, ((out_size - 1) * stride + effective_filter_size - in_size));
    return total_padding / 2;
}

int compute_out_size(Padding padding, int image_size, int filter_size, int stride) {
    switch (padding) {
    case Padding::Same: {
        return (image_size + stride - 1) / stride;
    }
    case Padding::Valid: {
        const int effective_filter_size = (filter_size - 1) + 1;
        return (image_size + stride - effective_filter_size) / stride;
    }
    default:
        return 0;
    }
}

}  // namespace

void PoolOp::compute_padding_values() {
    const Tensor *in = input();
    Tensor *out = output();

    auto input_buf = in->buffer<void>();
    auto output_buf = out->buffer<void>();

    const int in_width = input_buf.dim(1).extent();
    const int in_height = input_buf.dim(2).extent();
    const int out_width = output_buf.dim(1).extent();
    const int out_height = output_buf.dim(2).extent();

    const int out_width_padded = compute_out_size(padding_, in_width, filter_size_[0], stride_[0]);
    const int out_height_padded = compute_out_size(padding_, in_height, filter_size_[1], stride_[1]);

    // TODO: logic for compute_out_size() is adapted from general code that applied to Conv2D/DConv2D as well;
    // it's not clear whether or not we could expect a different output size, since we have no dilation factor
    // here. Leaving in this CHECK() for now.
    CHECK(out_width == out_width_padded);
    CHECK(out_height == out_height_padded);

    padding_values_.width = compute_padding(stride_[0], in_width, filter_size_[0], out_width_padded);
    padding_values_.height = compute_padding(stride_[1], in_height, filter_size_[1], out_height_padded);
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
    std::vector<SplitInfo> splits(output()->rank(), SplitInfo::any_split());
    return splits;
}

const char *PoolOp::to_string(PoolOp::Operator op) {
    switch (op) {
    case Average:
        return "Average";
    case Max:
        return "Max";
    default:
        CHECK(false) << "Unsupported pool op\n";
        return nullptr;
    }
}

void PoolOp::execute(const Box &crop) {
    const Tensor *in = input();
    Tensor *out = output();

    if (in->is_type<uint8_t>() && out->is_type<uint8_t>()) {
        auto input_buf = in->buffer<const uint8_t>();
        auto output_buf = out->buffer<uint8_t>(crop);

        const auto output_range = get_output_range(activation_, out->quantization());

        switch (op_) {
        case Average:
            CHECK(
                0 == average_pool_uint8(input_buf, stride_[0], stride_[1],
                                        filter_size_[0], filter_size_[1],
                                        padding_values_.width, padding_values_.height,
                                        output_range.min, output_range.max, output_buf));
            break;
        case Max:
            CHECK(
                0 == max_pool_uint8(input_buf, stride_[0], stride_[1],
                                    filter_size_[0], filter_size_[1],
                                    padding_values_.width, padding_values_.height,
                                    output_range.min, output_range.max, output_buf));
            break;
        }
    } else {
        CHECK(false) << "Unsupported type " << out->type() << "\n";
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

std::vector<SplitInfo> SoftmaxOp::get_split_info() const {
    // Allow any split on any dimension other than the first dimension, where we
    // compute a reduction.
    std::vector<SplitInfo> splits(output()->rank(), SplitInfo::any_split());
    splits.front() = SplitInfo::no_split();
    return splits;
}

void SoftmaxOp::execute(const Box &crop) {
    const Tensor *in = input();
    Tensor *out = output();

    if (in->is_type<uint8_t>() && out->is_type<uint8_t>()) {
        auto in_buf = in->buffer<const uint8_t>();
        auto output_buf = out->buffer<uint8_t>(crop);

        // It's a easier to compute 2^(x*(B*log2(e))) than e^(x*B).
        const float beta2 = beta_ * std::log2(std::exp(1.0f));

        // We don't need the input zero point because this op exploits the
        // identity exp(x_i)/sum(exp(x_i)) == exp(x_i + C)/sum(exp(x_i + C))
        const int output_zero = out->quantization().zero.at(0);
        assert(output_zero >= 0 && output_zero <= 255);

        const float in_scale = in->quantization().scale.at(0) * beta2;
        const float output_scale = out->quantization().scale.at(0);

        const int left_shift = 22;
        const double real_in_multiplier = in_scale / (1 << left_shift);

        auto in_mul_and_shift = get_quantized_mul_and_shift_smaller_than_one(real_in_multiplier);
        auto output_mul_and_shift = get_quantized_mul_and_shift_smaller_than_one(output_scale);
        assert(in_mul_and_shift.shift <= 0);
        assert(output_mul_and_shift.shift <= 0);

        CHECK(0 == softmax_uint8(left_shift, in_buf,
                                 in_mul_and_shift.multiplier, -in_mul_and_shift.shift,
                                 output_zero, output_mul_and_shift.multiplier, -output_mul_and_shift.shift,
                                 output_buf));
    } else {
        CHECK(false) << "Unsupported type " << out->type() << "\n";
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

        int input_zero = in->quantization().zero.at(0);
        int output_zero = out->quantization().zero.at(0);

        CHECK(0 == tile_convolution_filter_uint8(input_buf, input_zero, output_zero, output_buf));
    } else {
        CHECK(false) << "Unsupported type " << in->type() << "\n";
    }
}

void BinaryOp::accept(OpVisitor *v) {
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

void L2NormalizationOp::accept(OpVisitor *v) {
    v->visit(this);
}

void PadOp::accept(OpVisitor *v) {
    v->visit(this);
}

void PoolOp::accept(OpVisitor *v) {
    v->visit(this);
}

void SoftmaxOp::accept(OpVisitor *v) {
    v->visit(this);
}

void ReshapeOp::accept(OpVisitor *v) {
    v->visit(this);
}

void TileConvFilterOp::accept(OpVisitor *v) {
    v->visit(this);
}

}  // namespace hannk
