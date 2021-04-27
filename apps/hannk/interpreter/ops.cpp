#include "interpreter/ops.h"
#include "interpreter/elementwise_program.h"
#include "util/error_util.h"

#include <atomic>
#include <cmath>
#include <iostream>

#include "add_uint8_uint8.h"
#include "average_pool_uint8.h"
#include "conv_uint8.h"
#ifdef CONV_R16
#include "conv_r16_uint8.h"
#endif
#include "copy_uint8_uint8.h"
#include "depthwise_conv_broadcast_uint8.h"
#include "depthwise_conv_dm1_uint8.h"
#include "depthwise_conv_uint8.h"
#include "elementwise_5xuint8_1xuint8.h"
#include "elementwise_5xint16_1xuint8int16.h"
#include "fill_uint8.h"
#include "fully_connected_uint8_int16.h"
#include "fully_connected_uint8_uint8.h"
#include "l2_normalization_uint8.h"
#include "max_pool_uint8.h"
#include "mean_uint8.h"
#include "mul_uint8_uint8_uint8.h"
#include "softmax_uint8.h"
#include "tile_conv_filter_uint8.h"

namespace hannk {

namespace {

// Check if dimension 0 and dimension 1 of buf can be fused.
// We avoid the use of Halide::Runtime::Buffer where possible in these helpers
// to reduce template instantiation and runtime overhead.
bool can_fuse(const halide_buffer_t *buf, int d0, int d1) {
    assert(d0 != d1);
    return d0 < buf->dimensions &&
           d1 < buf->dimensions &&
           buf->dim[d0].min == 0 &&
           buf->dim[d1].stride > 0 &&
           buf->dim[d1].stride == buf->dim[d0].extent * buf->dim[d0].stride;
}
bool can_fuse_cx(const halide_buffer_t *buf) {
    return can_fuse(buf, 0, 1);
}
bool can_fuse_xy(const halide_buffer_t *buf) {
    return can_fuse(buf, 1, 2);
}

// Fuse the first two dimensions of buf. d1 is deleted from the buffer.
void fuse(halide_buffer_t *buf, int d0, int d1) {
    halide_dimension_t &dim0 = buf->dim[d0];
    halide_dimension_t &dim1 = buf->dim[d1];
    dim0.extent *= dim1.extent;
    for (int d = d1; d + 1 < buf->dimensions; d++) {
        buf->dim[d] = buf->dim[d + 1];
    }
    buf->dimensions--;
}
void fuse_cx(halide_buffer_t *buf) {
    fuse(buf, 0, 1);
}
void fuse_xy(halide_buffer_t *buf) {
    fuse(buf, 1, 2);
}

// Embed extent 1 dimensions until buf has the given rank.
template<typename T>
void pad_to_rank(int rank, HalideBuffer<T> &buf) {
    while (buf.dimensions() < rank) {
        buf.embed(buf.dimensions(), 0);
    }
}

template <typename... Ts>
void fuse_cx(halide_buffer_t *a, HalideBuffer<Ts> &... rest) {
    fuse_cx(a);
    fuse_cx(rest...);
}

template <typename Ta, typename... Ts>
void pad_to_rank(int rank, HalideBuffer<Ta> &a, HalideBuffer<Ts> &... rest) {
    pad_to_rank(rank, a);
    pad_to_rank(rank, rest...);
}

bool all(bool first) {
    return first;
}

template <typename... T>
bool all(bool first, T... rest) {
    return first && all(rest...);
}

// Fuse the innermost (stride 1) dimension with other dimensions as much as possible.
// This may enable the buffers to be processed with fewer instances of the "tail" of
// a vectorization loop.
template<typename Ta, typename... Ts>
void optimize_elementwise_shapes(int rank, HalideBuffer<Ta> &a, HalideBuffer<Ts> &... rest) {
    while (can_fuse_cx(a) && all(can_fuse_cx(rest)...) &&
           all(a.dim(0).extent() == rest.dim(0).extent()...)) {
        fuse_cx(a, rest...);
    }
    pad_to_rank(rank, a, rest...);
}

template<int FnRank, typename Fn, typename T, typename... Ts>
void loop_nest_impl(Fn &&fn, HalideBuffer<T> op0, HalideBuffer<Ts>... ops) {
    if (op0.dimensions() == FnRank) {
        fn(op0, ops...);
    } else {
        const int last_dim = op0.dimensions() - 1;
        for (int i = op0.dim(last_dim).min(); i <= op0.dim(last_dim).max(); i++) {
            loop_nest_impl<FnRank>(fn, op0.sliced(last_dim, i), ops.sliced(last_dim, i)...);
        }
    }
}

// Call an elementwise operation that accepts operands of a particular rank,
// and calls it on operands of any rank by slicing off or padding (in a loop)
// the outer dimensions.
template<int FnRank, typename Fn, typename T, typename... Ts>
void elementwise_loop_nest(Fn &&fn, HalideBuffer<T> op0, HalideBuffer<Ts>... ops) {
    optimize_elementwise_shapes(FnRank, op0, ops...);
    loop_nest_impl<FnRank>(fn, op0, ops...);
}

// Similar to the above, but do not fuse dimensions when possible.
template<int FnRank, typename Fn, typename T, typename... Ts>
void loop_nest(Fn &&fn, HalideBuffer<T> op0, HalideBuffer<Ts>... ops) {
    pad_to_rank(FnRank, op0, ops...);
    loop_nest_impl<FnRank>(fn, op0, ops...);
}

// Broadcast the extent 1 dimensions of one shape to match the extent of the
// other shape.
template<typename Ta, typename Tb>
void broadcast_shapes(HalideBuffer<Ta> &a, HalideBuffer<Tb> &b) {
    int rank = std::max(a.dimensions(), b.dimensions());
    pad_to_rank(rank, a);
    pad_to_rank(rank, b);

    halide_buffer_t *raw_a = a.raw_buffer();
    halide_buffer_t *raw_b = b.raw_buffer();
    for (int d = 0; d < rank; d++) {
        if (raw_a->dim[d].extent == raw_b->dim[d].extent) {
            continue;
        }
        if (raw_a->dim[d].extent == 1) {
            raw_a->dim[d].extent = raw_b->dim[d].extent;
            raw_a->dim[d].stride = 0;
        } else if (raw_b->dim[d].extent == 1) {
            raw_b->dim[d].extent = raw_a->dim[d].extent;
            raw_b->dim[d].stride = 0;
        } else {
            LOG(FATAL) << "Can't broadcast shapes";
        }
    }
}

// Check if and b are aliases of the same buffer.
bool is_alias(const HalideBuffer<const void> &a, const HalideBuffer<const void> &b) {
    return !(a.begin() >= b.end() || a.end() <= b.begin());
}

// Crop both a and b to the union of both buffers.
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

QuantizedMulAndShift get_quantized_mul_and_shift(double double_multiplier, int bits = 32) {
    if (double_multiplier == 0.) {
        return {0, 0};
    }

    int shift = 0;
    const double q = std::frexp(double_multiplier, &shift);
    int64_t q_fixed = (int64_t)std::round(q * (1LL << (bits - 1)));
    assert(std::abs(q_fixed) <= (1LL << (bits - 1)));

    if (std::abs(q_fixed) == (1LL << (bits - 1))) {
        q_fixed /= 2;
        ++shift;
    }
    assert(std::abs(q_fixed) <= std::numeric_limits<int32_t>::max());

    if (shift < -(bits - 1)) {
        shift = 0;
        q_fixed = 0;
    }

    return {(int)q_fixed, shift};
}

QuantizedMulAndShift get_quantized_mul_and_shift_smaller_than_one(double double_multiplier, int bits = 32) {
    assert(-1.0 < double_multiplier && double_multiplier < 1.0);
    auto result = get_quantized_mul_and_shift(double_multiplier, bits);
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
        LOG(FATAL) << "Unsupported quantized activation function type.";
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

void add(HalideBuffer<const uint8_t> in1, const QuantizationInfo &in1q, int in1sign,
         HalideBuffer<const uint8_t> in2, const QuantizationInfo &in2q, int in2sign,
         HalideBuffer<uint8_t> out, const QuantizationInfo &outq,
         ActivationFunction activation = ActivationFunction::None) {
    // TODO: We should require the buffers are already broadcasted appropriately before
    // getting here.
    broadcast_shapes(in1, in2);

    const int in1_zero = in1q.zero.at(0);
    const int in2_zero = in2q.zero.at(0);
    const int out_zero = outq.zero.at(0);

    const float in1_scale = in1q.scale.at(0);
    const float in2_scale = in2q.scale.at(0);
    const float out_scale = outq.scale.at(0);

    const int left_shift = 20;
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

    in1_mul_and_shift.multiplier *= in1sign;
    in2_mul_and_shift.multiplier *= in2sign;

    const auto out_range = get_output_range(activation, outq);

    auto add_rank2 = [&](halide_buffer_t *in1_buf, halide_buffer_t *in2_buf, halide_buffer_t *out_buf) {
        CHECK(0 == add_uint8_uint8(in1_buf, in1_zero, in1_mul_and_shift.multiplier, -in1_mul_and_shift.shift,
                                   in2_buf, in2_zero, in2_mul_and_shift.multiplier, -in2_mul_and_shift.shift,
                                   out_zero, out_mul_and_shift.multiplier, -out_mul_and_shift.shift,
                                   out_range.min, out_range.max, out_buf));
    };
    elementwise_loop_nest<2>(add_rank2, in1, in2, out);
}

void mul(HalideBuffer<const uint8_t> in1, const QuantizationInfo &in1q,
         HalideBuffer<const uint8_t> in2, const QuantizationInfo &in2q,
         HalideBuffer<uint8_t> out, const QuantizationInfo &outq,
         ActivationFunction activation = ActivationFunction::None) {
    // TODO: We should require the buffers are already broadcasted appropriately before
    // getting here.
    broadcast_shapes(in1, in2);

    const int in1_zero = in1q.zero.at(0);
    const int in2_zero = in2q.zero.at(0);
    const int out_zero = outq.zero.at(0);

    const float in1_scale = in1q.scale.at(0);
    const float in2_scale = in2q.scale.at(0);
    const float out_scale = outq.scale.at(0);

    const int left_shift = 6;
    const double multiplier = in1_scale * in2_scale / (out_scale * (1 << (2 * left_shift)));

    auto mul_and_shift = get_quantized_mul_and_shift_smaller_than_one(multiplier);
    assert(mul_and_shift.shift <= 0);

    const auto out_range = get_output_range(activation, outq);

    auto mul_rank2 = [&](halide_buffer_t *in1_buf, halide_buffer_t *in2_buf, halide_buffer_t *out_buf) {
        CHECK(0 == mul_uint8_uint8_uint8(in1_buf, in1_zero, in2_buf, in2_zero,
                                         out_zero, mul_and_shift.multiplier, -mul_and_shift.shift,
                                         out_range.min, out_range.max, out_buf));
    };
    elementwise_loop_nest<2>(mul_rank2, in1, in2, out);
}

void requantize(const HalideBuffer<const void> &in, const QuantizationInfo &inq,
                HalideBuffer<void> out, const QuantizationInfo &outq,
                ActivationFunction activation = ActivationFunction::None) {
    if (inq == outq) {
        // Some of these are just copies, or no-ops.
        if (is_alias(in, out)) {
            return;
        } else {
            out.copy_from(in);
        }
    } else if (in.type() == halide_type_of<uint8_t>() && out.type() == halide_type_of<uint8_t>()) {
        // TODO: Maybe a dedicated pipeline for this would be better. It
        // could be a little faster, and avoid some quantization error.
        add(in, inq, 1, in, inq, 0, out, outq, activation);
    } else {
        LOG(FATAL) << "Unable to requantize " << in.type() << " -> " << out.type() << "\n";
    }
}

ActivationFunction to_activation(UnaryOp::Operator op) {
    switch (op) {
    case UnaryOp::Relu:
        return ActivationFunction::Relu;
    case UnaryOp::Relu6:
        return ActivationFunction::Relu6;
    case UnaryOp::ReluN1To1:
        return ActivationFunction::ReluN1To1;
    case UnaryOp::Tanh:
        return ActivationFunction::Tanh;
    default:
        LOG(FATAL) << UnaryOp::to_string(op) << " is not an activation function";
        return ActivationFunction::None;
    }
}

}  // namespace

BoundsMap ElementwiseOp::map_bounds(int input_idx, int output_idx) const {
    int rank = output(output_idx)->rank();
    assert(rank == input(input_idx)->rank());
    return BoundsMap::elementwise(rank);
}

const char *BinaryOp::to_string(BinaryOp::Operator op) {
    switch (op) {
    case Add:
        return "Add";
    case Sub:
        return "Sub";
    case Mul:
        return "Mul";
    case Less:
        return "Less";
    case LessEqual:
        return "LessEqual";
    case Equal:
        return "Equal";
    case NotEqual:
        return "NotEqual";
    default:
        LOG(FATAL) << "Unsupported binary op\n";
        return nullptr;
    }
}

double dequantize_scalar(const Tensor *t) {
    assert(t->rank() == 0);

    const QuantizationInfo &q = t->quantization();
    float scale = q.scale.empty() ? 1.0f : q.scale.front();
    int zero = q.zero.empty() ? 0 : q.zero.front();

    HalideBuffer<const void> buf = t->buffer<const void>();
    if (buf.type() == halide_type_of<uint8_t>()) {
        return (buf.as<const uint8_t>()() - zero) * scale;
    } else if (buf.type() == halide_type_of<int8_t>()) {
        return (buf.as<const int8_t>()() - zero) * scale;
    } else if (buf.type() == halide_type_of<uint16_t>()) {
        return (buf.as<const uint16_t>()() - zero) * scale;
    } else if (buf.type() == halide_type_of<int16_t>()) {
        return (buf.as<const int16_t>()() - zero) * scale;
    } else if (buf.type() == halide_type_of<uint32_t>()) {
        return (buf.as<const uint32_t>()() - zero) * scale;
    } else if (buf.type() == halide_type_of<int32_t>()) {
        return (buf.as<const int32_t>()() - zero) * scale;
    } else if (buf.type() == halide_type_of<float>()) {
        return (buf.as<const float>()() - zero) * scale;
    } else if (buf.type() == halide_type_of<double>()) {
        return (buf.as<const double>()() - zero) * scale;
    } else {
        LOG(FATAL) << "Unsupported type " << buf.type();
        return std::numeric_limits<double>::quiet_NaN();
    }
}

void BinaryOp::execute() {
    const TensorPtr in1 = input(0);
    const TensorPtr in2 = input(1);
    TensorPtr out = output();

    if (in1->type() == halide_type_of<uint8_t>() &&
        in2->type() == halide_type_of<uint8_t>() &&
        out->type() == halide_type_of<uint8_t>()) {
        auto in1_buf = in1->buffer<const uint8_t>();
        auto in2_buf = in2->buffer<const uint8_t>();
        auto out_buf = out->buffer<uint8_t>();

        switch (op_) {
        case Add:
        case Sub:
            add(in1_buf, in1->quantization(), 1, in2_buf, in2->quantization(), op_ == Add ? 1 : -1, out_buf, out->quantization(), activation_);
            return;
        case Mul:
            mul(in1_buf, in1->quantization(), in2_buf, in2->quantization(), out_buf, out->quantization(), activation_);
            return;
        default:
            break;
        }
    } else if (out->type() == halide_type_of<bool>() && out->rank() == 0) {
        double in1_scalar = dequantize_scalar(in1.get());
        double in2_scalar = dequantize_scalar(in2.get());
        auto out_buf = out->buffer<bool>();

        switch (op_) {
        case Less:
            out_buf() = in1_scalar < in2_scalar;
            return;
        case LessEqual:
            out_buf() = in1_scalar <= in2_scalar;
            return;
        case Equal:
            out_buf() = in1_scalar == in2_scalar;
            return;
        case NotEqual:
            out_buf() = in1_scalar != in2_scalar;
            return;
        default:
            break;
        }
    }
    LOG(FATAL)
        << "Unsupported binary op " << to_string(op_)
        << " for types " << in1->type() << ", " << in2->type() << ", " << out->type();
}

BoundsMap ConcatenationOp::map_bounds(int input_idx, int output_idx) const {
    int rank = output()->rank();
    assert(rank == input(input_idx)->rank());

    int offset = 0;
    for (int i = 0; i < input_idx; i++) {
        offset += input(i)->extent(axis_);
    }
    BoundsMap result = BoundsMap::elementwise(rank);
    result.at(axis_, axis_).bounds += offset;
    return result;
}

void ConcatenationOp::execute() {
    HalideBuffer<void> output_buf = output()->buffer();

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
    if (input()->type() == halide_type_of<uint8_t>() &&
        output()->type() == halide_type_of<uint8_t>()) {
        const halide_filter_metadata_t *metadata = conv_uint8_metadata();
        return metadata->arguments[2].type;
    } else {
        LOG(FATAL) << "Unsupported type " << output()->type() << "\n";
        return halide_type_t(halide_type_int, 0, 0);
    }
}

BoundsMap Conv2DOp::map_bounds(int input_idx, int output_idx) const {
#ifdef CONV_R16
    const int unroll_reduction = filter()->extent(0) >= 16 ? 16 : 4;
#else
    const int unroll_reduction = 4;
#endif
    if (input_idx == 0) {
        return BoundsMap(4, output()->rank())
            .constant(0, align_up(input()->extent(0), unroll_reduction))
            .downsample(1, 1, stride_[0], Interval(0, dilation_[0] * (filter()->extent(1) - 1)))
            .downsample(2, 2, stride_[1], Interval(0, dilation_[1] * (filter()->extent(2) - 1)))
            .elementwise(3, 3);
    } else if (input_idx == 1) {
        // Pass minimal sized buffers to learn about the alignment requirements.
        HalideBuffer<uint8_t> input_buf(nullptr, 1, 1, 1, 1);
        HalideBuffer<int32_t> bias_buf(nullptr, 1);
        HalideBuffer<void> filter_buf(filter_type(), 1, 1, 1, 1, 1, 1);
        // TODO: How to initialize the above buffer without allocating?
        filter_buf.deallocate();
        HalideBuffer<uint8_t> output_buf;
        CHECK(0 == conv_uint8(input_buf, 0, filter_buf, 0, bias_buf, 1, 1, 1, 1, 0, 0, 0, 0, 0, output_buf));

        const int vector_reduction = filter_buf.dim(0).extent();
        const int vector_tile = filter_buf.dim(1).extent();
        const int channel_alignment = unroll_reduction / vector_reduction;
        return BoundsMap(6, 4)
            .constant(0, vector_reduction)
            .constant(1, vector_tile)
            .constant(2, align_up(ceil_div(filter()->extent(0), vector_reduction), channel_alignment))
            .upsample(3, 0, vector_tile)
            .constant(4, filter()->bounds(1))
            .constant(5, filter()->bounds(2));
    } else {
        assert(input_idx == 2);
        return BoundsMap(1, 4).elementwise(0, 0);
    }
}

namespace {

void conv_uint8(halide_buffer_t *input, halide_buffer_t *filter, halide_buffer_t *bias,
                const MultiplyParams &params, const std::vector<int> &stride,
                const std::vector<int> &dilation, const Interval &output_range,
                halide_buffer_t *output) {
#ifdef CONV_R16
    if (input->dim[0].extent >= 16) {
        // For large reductions, use the big reduction version.
        // TODO: We really ought to be able to do this with GuardWithIf
        // and/or specialize.
        CHECK(
            0 == conv_r16_uint8(
                     input, (uint8_t)params.a_zero, filter, (uint8_t)params.b_zero, bias,
                     stride[0], stride[1], dilation[0], dilation[1], params.c.multiplier,
                     params.c.shift, (uint8_t)params.c_zero, output_range.min, output_range.max,
                     output));
    } else
#endif
    {
        CHECK(
            0 == ::hannk::conv_uint8(
                     input, (uint8_t)params.a_zero, filter, (uint8_t)params.b_zero, bias,
                     stride[0], stride[1], dilation[0], dilation[1], params.c.multiplier,
                     params.c.shift, (uint8_t)params.c_zero, output_range.min, output_range.max,
                     output));
    }
}

}  // namespace

void Conv2DOp::execute() {
    const TensorPtr in = input();
    const TensorPtr filt = filter();
    TensorPtr out = output();

    if (in->type() == halide_type_of<uint8_t>() &&
        out->type() == halide_type_of<uint8_t>()) {
        auto input_buf = in->buffer<const uint8_t>();
        auto filter_buf = filt->buffer<const void>();
        auto bias_buf = bias()->buffer<const int32_t>();
        auto output_buf = out->buffer<uint8_t>();

        MultiplyParams params =
            get_quantized_multiply_params(in->quantization(), filt->quantization(), out->quantization());

        const auto output_range = get_output_range(activation_, out->quantization());

        assert(filter_buf.dimensions() == 6);
        const int filter_width = filter_buf.dim(4).extent();
        const int filter_height = filter_buf.dim(5).extent();
        if (filter_width == 1 && filter_height == 1) {
            // For 1x1 filters, we can fuse x and y, which can help avoid overhead for
            // small output sizes.
            while (can_fuse_xy(input_buf) && can_fuse_xy(output_buf) &&
                   input_buf.dim(1).extent() == output_buf.dim(1).extent()) {
                fuse_xy(input_buf);
                fuse_xy(output_buf);
            }
            pad_to_rank(4, input_buf);
            pad_to_rank(4, output_buf);
        }

        conv_uint8(input_buf, filter_buf, bias_buf, params, stride_, dilation_, output_range, output_buf);
    } else {
        LOG(FATAL) << "Unsupported type " << out->type() << "\n";
    }
}

namespace {

// Wrapper to dispatch to the appropriate variant of depthwise_conv.
void depthwise_conv_uint8(
    halide_buffer_t *input, halide_buffer_t *filter, halide_buffer_t *bias,
    int depth_multiplier, const MultiplyParams &params, const std::vector<int> &stride, const std::vector<int> &dilation,
    const Interval &output_range, halide_buffer_t *output) {
    if (depth_multiplier >= output->dim[0].extent) {
        CHECK(
            0 == depthwise_conv_broadcast_uint8(
                     input, (uint8_t)params.a_zero, filter, (uint8_t)params.b_zero, bias, depth_multiplier,
                     stride[0], stride[1], dilation[0], dilation[1], params.c.multiplier, params.c.shift,
                     (uint8_t)params.c_zero, (uint8_t)output_range.min, (uint8_t)output_range.max, output));
    } else if (depth_multiplier == 1) {
        CHECK(
            0 == depthwise_conv_dm1_uint8(
                     input, (uint8_t)params.a_zero, filter, (uint8_t)params.b_zero, bias, depth_multiplier,
                     stride[0], stride[1], dilation[0], dilation[1], params.c.multiplier, params.c.shift,
                     (uint8_t)params.c_zero, (uint8_t)output_range.min, (uint8_t)output_range.max, output));
    } else {
        CHECK(
            0 == ::hannk::depthwise_conv_uint8(
                     input, (uint8_t)params.a_zero, filter, (uint8_t)params.b_zero, bias, depth_multiplier,
                     stride[0], stride[1], dilation[0], dilation[1], params.c.multiplier, params.c.shift,
                     (uint8_t)params.c_zero, (uint8_t)output_range.min, (uint8_t)output_range.max, output));
    }
}

}  // namespace

BoundsMap DepthwiseConv2DOp::map_bounds(int input_idx, int output_idx) const {
    assert(output_idx == 0);
    if (input_idx == 0) {
        BoundsMap result(4, 4);
        result
            .upsample(0, 0, depth_multiplier_)
            .downsample(1, 1, stride_[0], Interval(0, dilation_[0] * (filter()->extent(1) - 1)))
            .downsample(2, 2, stride_[1], Interval(0, dilation_[1] * (filter()->extent(2) - 1)))
            .elementwise(3, 3);
        if (depth_multiplier_ == 1) {
            // TODO: Handle this padding for SIMD width elsewhere. Either fix depthwise
            // so it doesn't need this, or pass alignment information somewhere else.
#if defined(__arm__) || defined(__aarch64__)
            result.align(0, 16);
#else
            result.align(0, 32);
#endif
        }
        return result;
    } else if (input_idx == 1) {
        return BoundsMap(3, 4)
            .elementwise(0, 0)
            .constant(1, filter()->bounds(1))
            .constant(2, filter()->bounds(2));
    } else if (input_idx == 2) {
        return BoundsMap(1, 4).elementwise(0, 0);
    } else {
        return BoundsMap(0, 4);
    }
}

void DepthwiseConv2DOp::execute() {
    const TensorPtr in = input();
    const TensorPtr filt = filter();
    TensorPtr out = output();

    if (in->type() == halide_type_of<uint8_t>() &&
        filt->type() == halide_type_of<uint8_t>() &&
        out->type() == halide_type_of<uint8_t>()) {
        auto input_buf = in->buffer<const uint8_t>();
        auto filter_buf = filt->buffer<const uint8_t>().sliced(3, 0);
        auto bias_buf = bias()->buffer<const int32_t>();
        auto output_buf = out->buffer<uint8_t>();

        MultiplyParams params =
            get_quantized_multiply_params(in->quantization(), filt->quantization(), out->quantization());

        const auto output_range = get_output_range(activation_, out->quantization());

        depthwise_conv_uint8(input_buf, filter_buf, bias_buf, depth_multiplier_, params,
                             stride_, dilation_, output_range, output_buf);
    } else {
        LOG(FATAL) << "Unsupported type " << out->type() << "\n";
    }
}

BoundsMap FullyConnectedOp::map_bounds(int input_idx, int output_idx) const {
    assert(output_idx == 0);
    if (input_idx == 0) {
        return BoundsMap(2, 2).constant(0, input()->extent(0)).elementwise(1, 1);
    } else if (input_idx == 1) {
        return BoundsMap(2, 2).constant(0, filter()->extent(0)).elementwise(1, 0);
    } else if (input_idx == 2) {
        return BoundsMap(1, 2).elementwise(0, 0);
    } else {
        return BoundsMap(0, 2);
    }
}

bool can_use_elementwise_program(const Op *op,
                                 const std::vector<halide_type_t> &input_types,
                                 const std::vector<halide_type_t> &output_types) {
    if (op->input_count() > (int)input_types.size()) {
        return false;
    }
    if (op->output_count() > (int)output_types.size()) {
        return false;
    }
    for (int i = 0; i < op->input_count(); i++) {
        if (op->input(i)->type() != input_types[i]) {
            return false;
        }
    }
    for (int i = 0; i < op->output_count(); i++) {
        if (op->output(i)->type() != output_types[i]) {
            return false;
        }
    }
    return true;
}

void ElementwiseProgramOp::execute() {
    HalideBuffer<const void> in0 = input(0)->buffer();
    HalideBuffer<const void> in1 = input(std::min(input_count() - 1, 1))->buffer();
    HalideBuffer<const void> in2 = input(std::min(input_count() - 1, 2))->buffer();
    HalideBuffer<const void> in3 = input(std::min(input_count() - 1, 3))->buffer();
    HalideBuffer<const void> in4 = input(std::min(input_count() - 1, 4))->buffer();
    HalideBuffer<void> out0 = output(0)->buffer();
    HalideBuffer<void> out1 = output(std::min(output_count() - 1, 1))->buffer();
    using arg_ptr = halide_buffer_t *;
    if (can_use_elementwise_program(this, {5, halide_type_of<uint8_t>()}, {halide_type_of<uint8_t>()})) {
        auto elementwise_rank1 = [&](arg_ptr in0, arg_ptr in1, arg_ptr in2, arg_ptr in3, arg_ptr in4, arg_ptr out0) {
            CHECK(0 == elementwise_5xuint8_1xuint8(in0, in1, in2, in3, in4, program_, out0));
        };
        loop_nest<1>(elementwise_rank1, in0, in1, in2, in3, in4, out0);
        return;
    } else if (can_use_elementwise_program(this, {5, halide_type_of<int16_t>()}, {halide_type_of<uint8_t>(), halide_type_of<int16_t>()})) {
        auto elementwise_rank1 = [&](arg_ptr in0, arg_ptr in1, arg_ptr in2, arg_ptr in3, arg_ptr in4, arg_ptr out0, arg_ptr out1) {
            CHECK(0 == elementwise_5xint16_1xuint8int16(in0, in1, in2, in3, in4, program_, out0, out1));
        };
        loop_nest<1>(elementwise_rank1, in0, in1, in2, in3, in4, out0, out1);
        return;
    }
    LOG(FATAL) << "Unsupported elementwise program\n";
}

void FullyConnectedOp::execute() {
    const TensorPtr in = input();
    const TensorPtr filt = filter();
    TensorPtr out = output();

    if (in->type() == halide_type_of<uint8_t>() &&
        filt->type() == halide_type_of<uint8_t>()) {
        auto input_buf = in->buffer<const uint8_t>();
        auto filter_buf = filt->buffer<const uint8_t>();
        auto bias_buf = bias()->buffer<const int32_t>();
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

        if (out->type() == halide_type_of<uint8_t>()) {
            auto output_buf = out->buffer<uint8_t>();

            const auto output_range = get_output_range(activation_, out->quantization());

            CHECK(
                0 == fully_connected_uint8_uint8(
                         input_buf, (uint8_t)params.a_zero, filter_buf, (uint8_t)params.b_zero, bias_buf,
                         (uint8_t)params.c_zero, params.c.multiplier, params.c.shift, (uint8_t)output_range.min,
                         (uint8_t)output_range.max, output_buf));
            return;
        } else if (out->type() == halide_type_of<int16_t>()) {
            auto output_buf = out->buffer<int16_t>();

            CHECK(
                0 == fully_connected_uint8_int16(
                         input_buf, (uint8_t)params.a_zero, filter_buf, (uint8_t)params.b_zero, bias_buf,
                         0, params.c.multiplier, params.c.shift, 0, 0, output_buf));
            return;
        }
    }
    LOG(FATAL) << "Unsupported type " << out->type() << "\n";
}

BoundsMap L2NormalizationOp::map_bounds(int input_idx, int output_idx) const {
    assert(input_idx == 0);
    assert(output_idx == 0);
    return BoundsMap(2, 2)
        .constant(0, input()->bounds(0))
        .elementwise(1, 1);
}

void L2NormalizationOp::execute() {
    const TensorPtr in = input();
    TensorPtr out = output();

    if (in->type() == halide_type_of<uint8_t>() &&
        out->type() == halide_type_of<uint8_t>()) {
        auto in_buf = in->buffer<const uint8_t>();
        auto out_buf = out->buffer<uint8_t>();

        const int input_zero = in->quantization().zero.at(0);
        assert(input_zero >= 0 && input_zero <= 255);

        assert(out->quantization().scale.at(0) == 1.0f / 128.0f);
        assert(out->quantization().zero.at(0) == 128);

        auto l2_normalization_rank2 = [&](halide_buffer_t *in_buf, halide_buffer_t *out_buf) {
            CHECK(0 == l2_normalization_uint8(in_buf, input_zero, out_buf));
        };
        loop_nest<2>(l2_normalization_rank2, in_buf, out_buf);
    } else {
        LOG(FATAL) << "Unsupported type " << out->type() << "\n";
    }
}

BoundsMap PadOp::map_bounds(int input_idx, int output_idx) const {
    assert(output_idx == 0);
    const int rank = output()->rank();
    if (input_idx == 0) {
        if (input(1)) {
            BoundsMap result(rank, rank);
            auto padding = input(1)->buffer<const int32_t>();
            for (int d = 0; d < output()->rank(); d++) {
                result.elementwise(d, d, padding(0, d));
            }
            return result;
        } else {
            return BoundsMap::elementwise(rank);
        }
    } else {
        assert(input_idx == 1);
        return BoundsMap(1, rank).constant(0, rank);
    }
}

void PadOp::execute() {
    const TensorPtr in = input(0);
    TensorPtr out = output();

    if (out->type().bytes() == 1) {
        auto input_buf = in->buffer<const uint8_t>();
        auto output_buf = out->buffer<uint8_t>();

        if (input(1)) {
            auto padding = input(1)->buffer<const int32_t>();
            for (int d = 0; d < output_buf.dimensions(); d++) {
                input_buf.translate(d, padding(0, d));
            }
        }

        uint8_t pad_value = in->quantization().zero.at(0);

        int fill_min_dim = 0;
        if (input_buf.dim(0).extent() == 3 && output_buf.dim(0).extent() == 4) {
            // copy can handle padding dimension 0, which is much faster than
            // filling the extra channel for interleaved 3/4 channel paddings.
            fill_min_dim = 1;
        }
        for (int d = output_buf.dimensions() - 1; d >= fill_min_dim; d--) {
            int input_min = input_buf.dim(d).min();
            int output_min = output_buf.dim(d).min();
            int input_max = input_buf.dim(d).max();
            int output_max = output_buf.dim(d).max();
            if (output_min < input_min) {
                auto before = output_buf.cropped(d, output_min, input_min - output_min);
                CHECK(0 == fill_uint8(pad_value, before));
            } else {
                input_min = output_min;
            }
            if (output_max > input_max) {
                auto after = output_buf.cropped(d, input_max + 1, output_max - input_max);
                CHECK(0 == fill_uint8(pad_value, after));
            } else {
                input_max = output_max;
            }
            output_buf.crop(d, input_min, input_max - input_min + 1);
        }
        if (!is_alias(input_buf, output_buf) ||
            input_buf.dim(0).min() > output_buf.dim(0).min() ||
            input_buf.dim(0).max() < output_buf.dim(0).max()) {
            CHECK(0 == copy_uint8_uint8(input_buf, pad_value, output_buf));
        }
    } else {
        LOG(FATAL) << "Unsupported type " << out->type() << "\n";
    }
}

namespace {

int compute_padding(int stride, int in_size, int filter_size, int out_size) {
    const int effective_filter_size = (filter_size - 1) + 1;
    const int total_padding = std::max(0, ((out_size - 1) * stride + effective_filter_size - in_size));
    return total_padding / 2;
}

}  // namespace

const char *PoolOp::to_string(PoolOp::Operator op) {
    switch (op) {
    case Average:
        return "Average";
    case Max:
        return "Max";
    default:
        LOG(FATAL) << "Unsupported pool op\n";
        return nullptr;
    }
}

BoundsMap PoolOp::map_bounds(int input_idx, int output_idx) const {
    assert(output_idx == 0);
    return BoundsMap(4, 4)
        .elementwise(0, 0)
        .downsample(1, 1, stride_[0], Interval(0, filter_size_[0] - 1))
        .downsample(2, 2, stride_[1], Interval(0, filter_size_[1] - 1))
        .elementwise(3, 3);
}

void PoolOp::execute() {
    const TensorPtr in = input();
    TensorPtr out = output();

    if (in->type() == halide_type_of<uint8_t>() &&
        out->type() == halide_type_of<uint8_t>()) {
        auto input_buf = in->buffer<const uint8_t>();
        auto output_buf = out->buffer<uint8_t>();

        const auto output_range = get_output_range(activation_, out->quantization());

        const int in_width = input_buf.dim(1).extent();
        const int in_height = input_buf.dim(2).extent();
        const int out_width = output_buf.dim(1).extent();
        const int out_height = output_buf.dim(2).extent();
        input_buf.translate(1, compute_padding(stride_[0], in_width, filter_size_[0], out_width));
        input_buf.translate(2, compute_padding(stride_[1], in_height, filter_size_[1], out_height));

        switch (op_) {
        case Average:
            CHECK(
                0 == average_pool_uint8(input_buf, stride_[0], stride_[1],
                                        filter_size_[0], filter_size_[1],
                                        output_range.min, output_range.max, output_buf));
            break;
        case Max:
            CHECK(
                0 == max_pool_uint8(input_buf, stride_[0], stride_[1],
                                    filter_size_[0], filter_size_[1],
                                    output_range.min, output_range.max, output_buf));
            break;
        }
    } else {
        LOG(FATAL) << "Unsupported type " << out->type() << "\n";
    }
}

const char *ReductionOp::to_string(Operator op) {
    switch (op) {
    case Mean:
        return "Mean";
    default:
        LOG(FATAL) << "Unsupported reduction operator.\n";
        return nullptr;
    }
}

bool ReductionOp::reducing(int d) const {
    auto indices = input(1)->buffer<const int32_t>();
    for (int i = 0; i < indices.dim(0).extent(); i++) {
        if (indices(i) == d) {
            return true;
        }
    }
    return false;
}

BoundsMap ReductionOp::map_bounds(int input_idx, int output_idx) const {
    assert(output_idx == 0);

    if (input_idx == 0) {
        int output_d = 0;
        BoundsMap result(input()->rank(), output()->rank());
        for (int d = 0; d < input()->rank(); d++) {
            if (reducing(d)) {
                result.constant(d, input()->bounds(d));
            } else {
                result.elementwise(d, output_d++);
            }
        }
        assert(output_d == output()->rank());
        return result;
    } else {
        return BoundsMap(1, output()->rank()).all(input(1)->bounds(), output()->rank());
    }
}

void ReductionOp::execute() {
    auto indices = input(1)->buffer<const int32_t>();

    const TensorPtr in = input();
    TensorPtr out = output();

    if (in->type() == halide_type_of<uint8_t>() &&
        out->type() == halide_type_of<uint8_t>()) {
        auto input_buf = in->buffer<const uint8_t>();
        auto output_buf = out->buffer<uint8_t>();

        if (op_ == Mean) {
            int mins[4] = {0, 0, 0, 0};
            int extents[4] = {1, 1, 1, 1};
            for (int d = 0; d < 4; d++) {
                if (reducing(d)) {
                    mins[d] = input_buf.dim(d).min();
                    extents[d] = input_buf.dim(d).extent();
                }
            }
            CHECK(0 == mean_uint8(input_buf, mins[0], extents[0], mins[1], extents[1],
                                  mins[2], extents[2], mins[3], extents[3], output_buf));
        }
    }
}

// TODO: Maybe this is only a reshape in some dimensions, in which case we might be able to split it.
BoundsMap ReshapeOp::map_bounds(int input_idx, int output_idx) const {
    assert(input_idx == 0);
    assert(output_idx == 0);
    return BoundsMap::all(input()->bounds(), output()->rank());
}

void ReshapeOp::execute() {
    const TensorPtr in = input();
    const TensorPtr shape = input(1);
    TensorPtr out = output();

    auto input_buf = in->buffer<const void>();
    auto output_buf = out->buffer();

    std::vector<int32_t> new_shape;
    // The shape can be specified by a Tensor or a constant array (but not both).
    // It's legal for the Tensor to be dynamic, so we have to keep a reference to it
    // and extract the data at execution time.
    if (shape && shape->rank() == 1 && shape->type() == halide_type_of<int32_t>()) {
        auto shape_buf = shape->buffer<const int32_t>();
        new_shape.assign(shape_buf.begin(), shape_buf.end());
    } else {
        new_shape = shape_array_;
        if (new_shape.size() == 1 && new_shape[0] == 0) {
            // Legacy tflite models use a shape parameter of [0] to indicate scalars,
            // so adjust accordingly.
            new_shape.clear();
        }
    }
    std::reverse(new_shape.begin(), new_shape.end());

    // One of the shape values can be -1, meaning "calculate it for me".
    int output_elements = 1;
    int stretch_dim = -1;
    for (size_t i = 0; i < new_shape.size(); ++i) {
        int value = new_shape[i];
        if (value == -1) {
            CHECK(stretch_dim == -1);
            stretch_dim = i;
        } else {
            output_elements *= value;
        }
    }
    if (stretch_dim != -1) {
        new_shape[stretch_dim] = input_buf.number_of_elements() / output_elements;
        output_elements *= new_shape[stretch_dim];
        CHECK(output_elements == (int)output_buf.number_of_elements());
    }

    CHECK((int)new_shape.size() == output_buf.dimensions());
    for (int d = 0; d < output_buf.dimensions(); d++) {
        CHECK(new_shape.at(d) == output_buf.dim(d).extent());
    }

    CHECK(input_buf.number_of_elements() == output_buf.number_of_elements());
    size_t output_size = output_buf.number_of_elements() * out->type().bytes();
    if (is_alias(input_buf, output_buf)) {
        assert(input_buf.begin() == output_buf.begin());
        assert(input_buf.end() == output_buf.end());
    } else {
        // TODO: This should also check the strides are dense.
        memcpy(output_buf.data(), input_buf.data(), output_size);
    }
}

BoundsMap ShapeOp::map_bounds(int input_idx, int output_idx) const {
    assert(input_idx == 0);
    assert(output_idx == 0);
    // This doesn't actually read anything from the input.
    return BoundsMap(input()->rank(), 1);
}

void ShapeOp::execute() {
    const TensorPtr in = input();
    TensorPtr out = output();

    if (out->type() == halide_type_of<int32_t>()) {
        HalideBuffer<int32_t> out_buf = out->buffer<int32_t>();
        assert(out_buf.dimensions() == 1);
        for (int i = out_buf.dim(0).min(); i <= out_buf.dim(0).max(); i++) {
            out_buf(i) = in->extent(i);
        }
    } else {
        LOG(FATAL) << "Unsupported type " << out->type() << "\n";
    }
}

BoundsMap SoftmaxOp::map_bounds(int input_idx, int output_idx) const {
    assert(input_idx == 0);
    assert(output_idx == 0);
    return BoundsMap(2, 2)
        .constant(0, input()->bounds(0))
        .elementwise(1, 1);
}

void SoftmaxOp::execute() {
    const TensorPtr in = input();
    TensorPtr out = output();

    if (in->type() == halide_type_of<uint8_t>() &&
        out->type() == halide_type_of<uint8_t>()) {
        auto in_buf = in->buffer<const uint8_t>();
        auto out_buf = out->buffer<uint8_t>();

        // It's a easier to compute 2^(x*(B*log2(e))) than e^(x*B).
        const float beta2 = beta_ * std::log2(std::exp(1.0f));

        // We don't need the input zero point because this op exploits the
        // identity exp(x_i)/sum(exp(x_i)) == exp(x_i + C)/sum(exp(x_i + C))
        const int output_zero = out->quantization().zero.at(0);
        assert(output_zero >= 0 && output_zero <= 255);

        const float in_scale = in->quantization().scale.at(0);
        // TODO: Debug why this extra factor of 2 is needed. There's something
        // wrong with the fixed point tricks in the implementation.
        const float output_scale = out->quantization().scale.at(0) * 2.0f;

        const int left_shift = 6;
        const double real_in_multiplier = in_scale * beta2 / (1 << left_shift);

        auto in_mul_and_shift = get_quantized_mul_and_shift_smaller_than_one(real_in_multiplier, 16);
        auto output_mul_and_shift = get_quantized_mul_and_shift_smaller_than_one(output_scale, 16);
        assert(in_mul_and_shift.shift <= 0);
        assert(output_mul_and_shift.shift <= 0);

        auto softmax_rank2 = [&](halide_buffer_t *in_buf, halide_buffer_t *out_buf) {
            CHECK(0 == softmax_uint8(in_buf, in_mul_and_shift.multiplier, -in_mul_and_shift.shift,
                                     output_zero, output_mul_and_shift.multiplier, -output_mul_and_shift.shift,
                                     out_buf));
        };
        loop_nest<2>(softmax_rank2, in_buf, out_buf);
    } else {
        LOG(FATAL) << "Unsupported type " << out->type() << "\n";
    }
}

namespace {

template<typename T>
inline void DepthToSpace(const HalideBuffer<const T> &input, int block_size, HalideBuffer<T> output) {
    // This is really slow, if profiling has brought you here, optimize it.
    output.for_each_element([&](int c, int x, int y, int b) {
        int xi = floor_div(x, block_size);
        int yi = floor_div(y, block_size);
        int ci = (y - yi * block_size) * block_size + (x - xi * block_size);
        output(c, x, y, b) = input(c * block_size * block_size + ci, xi, yi, b);
    });
}

template<typename T>
inline void SpaceToDepth(const HalideBuffer<const T> &input, int block_size, HalideBuffer<T> output) {
    // This is really slow, if profiling has brought you here, optimize it.
    output.for_each_element([&](int c, int x, int y, int b) {
        int ci = floor_div(c, block_size * block_size);
        int xyi = c - ci * block_size * block_size;
        int yi = xyi / block_size;
        int xi = xyi % block_size;
        output(c, x, y, b) = input(ci, x * block_size + xi, y * block_size + yi, b);
    });
}

}  // namespace

BoundsMap SpaceDepthOp::map_bounds(int input_idx, int output_idx) const {
    assert(input_idx == 0);
    assert(output_idx == 0);

    const int rank = output()->rank();
    assert(input()->rank() == rank);
    BoundsMap result(rank, rank);
    if (block_size_ > 0) {
        result.upsample(0, 0, block_size_ * block_size_);
        result.downsample(1, 1, block_size_);
        result.downsample(2, 2, block_size_);
    } else {
        result.downsample(0, 0, block_size_ * block_size_);
        result.upsample(1, 1, -block_size_);
        result.upsample(2, 2, -block_size_);
    }
    for (int d = 3; d < rank; d++) {
        result.elementwise(d, d);
    }
    return result;
}

void SpaceDepthOp::execute() {
    const TensorPtr in = input();
    TensorPtr out = output();

    if (in->type() == halide_type_of<uint8_t>() &&
        out->type() == halide_type_of<uint8_t>()) {
        auto in_buf = in->buffer<const uint8_t>();
        auto out_buf = out->buffer<uint8_t>();

        if (block_size_ > 0) {
            SpaceToDepth(in_buf, block_size_, out_buf);
        } else {
            DepthToSpace(in_buf, -block_size_, out_buf);
        }
    }
}

BoundsMap SplitOp::map_bounds(int input_idx, int output_idx) const {
    assert(input_idx == 0);
    const int rank = input()->rank();
    assert(output(output_idx)->rank() == rank);

    int offset = 0;
    for (int i = 0; i < output_idx; i++) {
        offset += output(i)->extent(axis_);
    }

    BoundsMap result = BoundsMap::elementwise(rank);
    result.at(axis_, axis_).bounds -= offset;
    return result;
}

void SplitOp::execute() {
    HalideBuffer<const void> input_buf = input()->buffer();

    int concatenated_i = 0;
    for (int i = 0; i < output_count(); i++) {
        HalideBuffer<void> output_buf = output(i)->buffer();
        assert(output_buf.dim(axis_).min() == 0);

        HalideBuffer<const void> input_crop = input_buf;
        input_crop.translate(axis_, -concatenated_i);
        crop_to_union(input_crop, output_buf);
        requantize(input_crop, input()->quantization(), output_buf, output(i)->quantization());

        concatenated_i += output_buf.dim(axis_).extent();
    }
}

BoundsMap TileConvFilterOp::map_bounds(int input_idx, int output_idx) const {
    assert(input_idx == 0);
    assert(output_idx == 0);
    // TODO: Maybe we could say more here, but it usually doesn't
    // matter because this op usually gets constant folded.
    return BoundsMap::all(input()->bounds(), output()->rank());
}

void TileConvFilterOp::execute() {
    const TensorPtr in = input();
    TensorPtr out = output();

    if (in->type() == halide_type_of<uint8_t>()) {
        auto input_buf = in->buffer<const uint8_t>();
        auto output_buf = out->buffer<void>();

        int input_zero = in->quantization().zero.at(0);
        int output_zero = out->quantization().zero.at(0);

        CHECK(0 == tile_conv_filter_uint8(input_buf, input_zero, output_zero, output_buf));
    } else {
        LOG(FATAL) << "Unsupported type " << in->type() << "\n";
    }
}

const char *UnaryOp::to_string(UnaryOp::Operator op) {
    switch (op) {
    case Logistic:
        return "Logistic";
    case Negate:
        return "Negate";
    case Relu:
        return "Relu";
    case Relu6:
        return "Relu6";
    case ReluN1To1:
        return "ReluN1To1";
    case Square:
        return "Square";
    case Tanh:
        return "Tanh";
    default:
        LOG(FATAL) << "Unsupported unary op\n";
        return nullptr;
    }
}

void UnaryOp::execute() {
    const TensorPtr in = input();
    TensorPtr out = output();

    if (in->type() == halide_type_of<uint8_t>() && out->type() == halide_type_of<uint8_t>()) {
        auto in_buf = in->buffer<const uint8_t>();
        auto out_buf = out->buffer<uint8_t>();

        const int input_zero = in->quantization().zero.at(0);
        assert(input_zero >= 0 && input_zero <= 255);
        const float in_scale = in->quantization().scale.at(0);

        const int left_shift = 6;

        std::array<int16_t, 64> program_buffer;
        if (op_ == Logistic) {
            const double real_in_multiplier = in_scale / (1 << left_shift);

            auto in_mul_and_shift = get_quantized_mul_and_shift_smaller_than_one(real_in_multiplier, 16);
            assert(in_mul_and_shift.shift <= 0);

            assert(out->quantization().scale.at(0) == 1.0f / 256.0f);
            assert(out->quantization().zero.at(0) == 0);

            // Build a program to implement the logistic op.
            ElementwiseAssembler p(program_buffer);
            auto input_zeroed = p.sub(p.input(0), input_zero);
            auto input_scaled = p.mul_shift(input_zeroed, in_mul_and_shift.multiplier, 15 - left_shift);
            auto result = p.logistic(8, input_scaled, -in_mul_and_shift.shift);
            auto program_buf = p.assemble({result});

            auto logistic_rank1 = [&](halide_buffer_t *in_buf, halide_buffer_t *out_buf) {
                CHECK(0 == elementwise_5xuint8_1xuint8(in_buf, in_buf, in_buf, in_buf, in_buf, program_buf, out_buf));
            };
            elementwise_loop_nest<1>(logistic_rank1, in_buf, out_buf);
            return;
        } else if (op_ == Tanh) {
            const double real_in_multiplier = in_scale / (1 << left_shift);

            auto in_mul_and_shift = get_quantized_mul_and_shift_smaller_than_one(real_in_multiplier, 16);
            assert(in_mul_and_shift.shift <= 0);

            assert(out->quantization().scale.at(0) == 1.0f / 128.0f);
            assert(out->quantization().zero.at(0) == 128);

            // Build a program to implement the tanh op.
            ElementwiseAssembler p(program_buffer);
            auto input_zeroed = p.sub(p.input(0), input_zero);
            auto input_scaled = p.mul_shift(input_zeroed, in_mul_and_shift.multiplier, 15 - left_shift);
            auto result = p.add(p.tanh(7, input_scaled, -in_mul_and_shift.shift), 128);
            auto program_buf = p.assemble({result});

            auto tanh_rank1 = [&](halide_buffer_t *in_buf, halide_buffer_t *out_buf) {
                CHECK(0 == elementwise_5xuint8_1xuint8(in_buf, in_buf, in_buf, in_buf, in_buf, program_buf, out_buf));
            };
            elementwise_loop_nest<1>(tanh_rank1, in_buf, out_buf);
            return;
        } else if (op_ == Negate) {
            add(in_buf, in->quantization(), -1, in_buf, in->quantization(), 0, out_buf, out->quantization());
            return;
        } else if (op_ == Square) {
            mul(in_buf, in->quantization(), in_buf, in->quantization(), out_buf, out->quantization());
            return;
        } else if (op_ == Relu || op_ == Relu6 || op_ == ReluN1To1) {
            requantize(in_buf, in->quantization(), out_buf, out->quantization(), to_activation(op_));
            return;
        }
    }
    LOG(FATAL)
        << "Unsupported unary op " << to_string(op_)
        << " for types " << in->type() << ", " << out->type();
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

void ElementwiseProgramOp::accept(OpVisitor *v) {
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

void ShapeOp::accept(OpVisitor *v) {
    v->visit(this);
}

void SoftmaxOp::accept(OpVisitor *v) {
    v->visit(this);
}

void SpaceDepthOp::accept(OpVisitor *v) {
    v->visit(this);
}

void SplitOp::accept(OpVisitor *v) {
    v->visit(this);
}

void ReductionOp::accept(OpVisitor *v) {
    v->visit(this);
}

void ReshapeOp::accept(OpVisitor *v) {
    v->visit(this);
}

void TileConvFilterOp::accept(OpVisitor *v) {
    v->visit(this);
}

void UnaryOp::accept(OpVisitor *v) {
    v->visit(this);
}

}  // namespace hannk
