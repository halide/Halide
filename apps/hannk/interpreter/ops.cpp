#include <atomic>
#include <cmath>
#include <iostream>

#include "halide/add_uint8_uint8.h"
#include "halide/average_pool_uint8.h"
#include "halide/constants.h"
#include "halide/conv_u8_u8_i16.h"
#include "halide/conv_u8_u8_u8.h"
#ifdef CONV_R16
#include "halide/conv_r16_u8_u8_i16.h"
#include "halide/conv_r16_u8_u8_u8.h"
#endif
#include "halide/copy_uint8_uint8.h"
#include "halide/depthwise_conv_broadcast_uint8.h"
#include "halide/depthwise_conv_shallow_uint8.h"
#include "halide/depthwise_conv_uint8.h"
#include "halide/elementwise_5xint16_1xuint8int16.h"
#include "halide/elementwise_5xuint8_1xuint8.h"
#include "halide/fill_uint8.h"
#include "halide/l2_normalization_uint8.h"
#include "halide/max_pool_uint8.h"
#include "halide/mean_uint8.h"
#include "halide/mul_uint8_uint8_uint8.h"
#include "halide/softmax_uint8.h"
#include "halide/tile_conv_filter_uint8.h"
#include "halide/upsample_channels_uint8.h"
#include "interpreter/elementwise_program.h"
#include "interpreter/ops.h"
#include "util/error_util.h"

namespace hannk {

namespace {

#if 0
// Useful for debugging
std::string dims_to_string(const halide_buffer_t *buf) {
    std::ostringstream oss;
    oss << "{";
    for (int i = 0; i < buf->dimensions; i++) {
        oss << "{" << buf->dim[i] << "},";
    }
    oss << "}";
    return oss.str();
}
#endif

// Split a dimension d into two new dimensions. Dim d will have min 0
// and extent factor, while the new dim d + 1 will have the outer split dimension.
template<typename T>
void split(int d, int factor, HalideBuffer<T> &buf) {
    buf.embed(d, 0);
    halide_dimension_t &dim0 = buf.raw_buffer()->dim[d];
    halide_dimension_t &dim1 = buf.raw_buffer()->dim[d + 1];
    dim0.min = 0;
    dim0.extent = factor;
    dim0.stride = dim1.stride;
    assert(dim1.min % factor == 0);
    assert(dim1.extent % factor == 0);
    dim1.min /= factor;
    dim1.extent /= factor;
    dim1.stride *= factor;
}

// TODO: FuseType::Delete is unused and could likely be removed.
enum class FuseType {
    // Delete the second of the fused dimension, reducing the rank by 1.
    Delete,
    // Replace the second fused dimension with a dimension of extent 1
    // and min 0, preserving the rank.
    InPlace,
    // Put a new dimension of extent 1 and min 0 at the end of the buffer.
    Pad,
};

// Check if dimension 0 and dimension 1 of buf can be fused, and that the
// operation is not a no-op.
// We avoid the use of Halide::Runtime::Buffer where possible in these helpers
// to reduce template instantiation and runtime overhead.
bool can_fuse(int d0, int d1, FuseType type, const halide_buffer_t *buf) {
    assert(d0 != d1);
    assert(d0 < buf->dimensions);
    assert(d1 < buf->dimensions);
    if (buf->dim[d0].min != 0 ||
        buf->dim[d1].stride != buf->dim[d0].extent * buf->dim[d0].stride) {
        return false;
    }
    if (type == FuseType::Delete) {
        // This is never a no-op.
        return true;
    }
    if (buf->dim[d1].extent != 1) {
        // This is not a no-op if the second dimension is not extent 1.
        return true;
    }
    if (type == FuseType::Pad) {
        // This is a not no-op if any dimension after d1 is not extent 1.
        for (int d = d1 + 1; d < buf->dimensions; d++) {
            if (buf->dim[d].extent != 1) {
                return true;
            }
        }
    }
    return false;
}
bool can_fuse_cx(FuseType type, const halide_buffer_t *buf) {
    return can_fuse(0, 1, type, buf);
}
bool can_fuse_xy(FuseType type, const halide_buffer_t *buf) {
    return can_fuse(1, 2, type, buf);
}

// Fuse dimensions d0 and d1 of buf.
// If type==Pad, add a new dimension of extent 1 and min 0 at the end.
// If type==Delete, d1 is deleted from the buffer.
void fuse(int d0, int d1, FuseType type, halide_buffer_t *buf) {
    assert(can_fuse(d0, d1, type, buf));
    halide_dimension_t &dim0 = buf->dim[d0];
    halide_dimension_t &dim1 = buf->dim[d1];
    dim0.extent *= dim1.extent;
    if (type == FuseType::Delete || type == FuseType::Pad) {
        for (int d = d1; d + 1 < buf->dimensions; d++) {
            buf->dim[d] = buf->dim[d + 1];
        }
        if (type == FuseType::Pad) {
            halide_dimension_t &padded = buf->dim[buf->dimensions - 1];
            halide_dimension_t &prev = buf->dim[buf->dimensions - 2];
            padded.min = 0;
            padded.extent = 1;
            padded.stride = prev.extent * prev.stride;
        } else {
            buf->dimensions--;
        }
    } else {
        dim1.min = 0;
        dim1.extent = 1;
        dim1.stride = dim0.stride * dim0.extent;
    }
}
void fuse_cx(FuseType type, halide_buffer_t *buf) {
    fuse(0, 1, type, buf);
}
void fuse_xy(FuseType type, halide_buffer_t *buf) {
    fuse(1, 2, type, buf);
}

template<typename... Bufs>
void fuse(int d0, int d1, FuseType type, halide_buffer_t *a, Bufs *...rest) {
    fuse(d0, d1, type, a);
    fuse(d0, d1, type, rest...);
}

// Embed extent 1 dimensions until buf has the given rank.
template<typename T>
void pad_to_rank(int rank, HalideBuffer<T> &buf) {
    while (buf.dimensions() < rank) {
        buf.embed(buf.dimensions(), 0);
    }
}

template<typename Ta, typename... Ts>
void pad_to_rank(int rank, HalideBuffer<Ta> &a, HalideBuffer<Ts> &...rest) {
    pad_to_rank(rank, a);
    pad_to_rank(rank, rest...);
}

bool all(bool first) {
    return first;
}

template<typename... T>
bool all(bool first, T... rest) {
    return first && all(rest...);
}

// Fuse the innermost (stride 1) dimension with other dimensions as much as possible.
// This may enable the buffers to be processed with fewer instances of the "tail" of
// a vectorization loop, and fewer levels of recursion in the loop nest helpers below.
template<typename... Bufs>
void optimize_elementwise_shapes(halide_buffer_t *a, Bufs *...rest) {
    assert(all(a->dimensions == rest->dimensions...));
    for (int d = 0; d + 1 < a->dimensions; d++) {
        while (can_fuse(d, d + 1, FuseType::Pad, a) &&
               all(can_fuse(d, d + 1, FuseType::Pad, rest)...) &&
               all(a->dim[d].extent == rest->dim[d].extent...)) {
            fuse(d, d + 1, FuseType::Pad, a, rest...);
        }
    }
}

// A hack to allow us to pass a type for each halide_buffer_t object.
template<typename T>
class TypedBufferT : public halide_buffer_t {};

// We can safely slice the last dim of a halide_buffer_t, because we don't need
// to modify any of the dim objects.
halide_buffer_t slice_last_dim(halide_buffer_t buf, int at) {
    buf.dimensions--;
    buf.host += buf.type.bytes() * buf.dim[buf.dimensions].stride * at;
    return buf;
}

template<typename T>
TypedBufferT<T> slice_last_dim(TypedBufferT<T> buf, int at) {
    buf.dimensions--;
    buf.host += buf.type.bytes() * buf.dim[buf.dimensions].stride * at;
    return buf;
}

template<int FnRank, typename Fn, typename... Bufs>
void loop_nest_impl(Fn &&fn, halide_buffer_t op0, Bufs... ops) {
    assert(all(op0.dimensions == ops.dimensions...));
    if (op0.dimensions == FnRank) {
        fn(&op0, &ops...);
    } else {
        const int last_dim = op0.dimensions - 1;
        const int min = op0.dim[last_dim].min;
        const int extent = op0.dim[last_dim].extent;
        const int max = min + extent - 1;
        for (int i = min; i <= max; i++) {
            loop_nest_impl<FnRank>(fn, slice_last_dim(op0, i), slice_last_dim(ops, i)...);
        }
    }
}

template<typename Fn, typename T, typename... Ts>
void scalar_loop_nest_impl(Fn &&fn, TypedBufferT<T> op0, TypedBufferT<Ts>... ops) {
    if (op0.dimensions == 0) {
        fn(*(T *)op0.host, *(Ts *)ops.host...);
    } else {
        const int last_dim = op0.dimensions - 1;
        const int min = op0.dim[last_dim].min;
        const int extent = op0.dim[last_dim].extent;
        const int max = min + extent - 1;
        for (int i = min; i <= max; i++) {
            scalar_loop_nest_impl(fn, slice_last_dim(op0, i), slice_last_dim(ops, i)...);
        }
    }
}

void broadcast_dims(int min, int extent) {
}

template<typename... Dims>
void broadcast_dims(int min, int extent, halide_dimension_t *dim, Dims *...rest) {
    if (dim->extent == 1) {
        dim->min = min;
        dim->extent = extent;
        if (extent > 1) {
            dim->stride = 0;
        }
    }
    broadcast_dims(min, extent, rest...);
}

int max(const halide_dimension_t &d) {
    return d.min + d.extent - 1;
}

// Broadcast the extent 1 dimensions of one shape to match the extent of the
// other shape.
template<typename... Bufs>
void broadcast_shapes(int rank, halide_buffer_t *a, Bufs *...rest) {
    for (int d = 0; d < rank; d++) {
        int min = std::min({a->dim[d].min, rest->dim[d].min...});
        int extent = std::max({max(a->dim[d]), max(rest->dim[d])...}) - min + 1;
        broadcast_dims(min, extent, &a->dim[d], &rest->dim[d]...);
    }
}

// This helper implements all of the logic necessary for elementwise operations:
// 1. Broadcasting any extents of 1 to match the rest of the dimensions.
// 2. Optimizing the shapes by fusing dimensions where possible.
// 3. Padding shapes to the required rank of `fn`.
// 4. Iterating and slicing the extra dimensions of the shapes before calling `fn`.
template<int FnRank, typename Fn, typename T, typename... Ts>
void elementwise_loop_nest(Fn &&fn, HalideBuffer<T> op0, HalideBuffer<Ts>... ops) {
    const int rank = std::max({FnRank, op0.dimensions(), ops.dimensions()...});
    pad_to_rank(rank, op0, ops...);
    broadcast_shapes(rank, op0.raw_buffer(), ops.raw_buffer()...);
    optimize_elementwise_shapes(op0.raw_buffer(), ops.raw_buffer()...);
    loop_nest_impl<FnRank>(fn, *op0.raw_buffer(), *ops.raw_buffer()...);
}

// This is the same as the above, except it calls fn with scalar values at each
// element of the buffer.
template<typename Fn, typename T, typename... Ts>
void scalar_elementwise_loop_nest(Fn &&fn, HalideBuffer<T> op0, HalideBuffer<Ts>... ops) {
    const int rank = std::max({op0.dimensions(), ops.dimensions()...});
    pad_to_rank(rank, op0, ops...);
    broadcast_shapes(rank, op0.raw_buffer(), ops.raw_buffer()...);
    optimize_elementwise_shapes(op0.raw_buffer(), ops.raw_buffer()...);
    scalar_loop_nest_impl(fn, *(TypedBufferT<T> *)op0.raw_buffer(), *(TypedBufferT<Ts> *)ops.raw_buffer()...);
}

// This helper is similar to the above, but it only implements steps 3 and 4.
template<int FnRank, typename Fn, typename T, typename... Ts>
void loop_nest(Fn &&fn, HalideBuffer<T> op0, HalideBuffer<Ts>... ops) {
    pad_to_rank(FnRank, op0, ops...);
    loop_nest_impl<FnRank>(fn, *op0.raw_buffer(), *ops.raw_buffer()...);
}

// Check if and b are aliases of the same buffer.
bool is_alias(const halide_buffer_t *a, const halide_buffer_t *b) {
    return std::max(a->begin(), b->begin()) < std::min(a->end(), b->end());
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

// A type safe power of two.
struct power_of_two {
    int value;

    explicit power_of_two(int value)
        : value(value) {
    }
};

// Represents a number as mantissa*2^exponent/2^(bits - 1), where bits = 8*sizeof(T)
// This is very similar to a float.
template<typename T>
class IntFloat {
    T mantissa_;
    T exponent_;

    static constexpr int log2_one = sizeof(T) * 8 - 1;
    static constexpr int64_t one = 1LL << log2_one;

public:
    IntFloat()
        : mantissa_(0), exponent_(0) {
    }

    IntFloat(float x) {
        int exponent;
        float mantissa_float = std::frexp(x, &exponent);
        int64_t mantissa_long = (int64_t)std::round(mantissa_float * one);
        assert(std::abs(mantissa_long) <= one);
        if (mantissa_long == one) {
            mantissa_long >>= 1;
            ++exponent;
        }
        mantissa_ = mantissa_long;
        exponent_ = exponent;
    }

    // Multiply this value by a constant power of 2.
    IntFloat operator*=(power_of_two x) {
        exponent_ += x.value;
        return *this;
    }

    T mantissa() const {
        if (exponent_ < -log2_one) {
            return 0;
        } else {
            return mantissa_;
        }
    }

    T exponent() const {
        if (exponent_ < -log2_one) {
            return -log2_one;
        } else if (exponent_ > log2_one) {
            return log2_one;
        } else {
            return exponent_;
        }
    }
};

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
        HLOG(FATAL) << "Unsupported quantized activation function type.";
    }
    return {std::max(min, 0), std::min(max, 255)};
}

Interval get_output_range(ActivationFunction activation, const QuantizationInfo &quantization) {
    const int output_zero = quantization.uniform_zero();
    assert(output_zero >= 0 && output_zero <= 255);

    const float output_scale = quantization.uniform_scale();

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
    IntFloat<int32_t> c;
};

MultiplyParams get_quantized_multiply_params(const QuantizationInfo &a, const QuantizationInfo &b, const QuantizationInfo &c) {
    MultiplyParams result;
    result.a_zero = a.uniform_zero();
    result.b_zero = b.uniform_zero();
    result.c_zero = c.uniform_zero();

    const float a_scale = a.uniform_scale();
    const float b_scale = b.uniform_scale();
    const float c_scale = c.uniform_scale();
    result.c = IntFloat<int32_t>(a_scale * b_scale / c_scale);

    return result;
}

void add_uint8(const HalideBuffer<const void> &in1, const QuantizationInfo &in1q, int in1sign,
               const HalideBuffer<const void> &in2, const QuantizationInfo &in2q, int in2sign,
               const HalideBuffer<void> &out, const QuantizationInfo &outq,
               ActivationFunction activation = ActivationFunction::None) {
    const int in1_zero = in1q.uniform_zero();
    const int in2_zero = in2q.uniform_zero();
    const int out_zero = outq.uniform_zero();

    const float in1_scale = in1q.uniform_scale() * (1 << add_output_shift);
    const float in2_scale = in2q.uniform_scale() * (1 << add_output_shift);
    const float out_scale = outq.uniform_scale() * (1 << add_input_shift);

    const int in1_multiplier = std::lround(in1_scale / out_scale) * in1sign;
    const int in2_multiplier = std::lround(in2_scale / out_scale) * in2sign;

    const auto out_range = get_output_range(activation, outq);

    auto add_rank2 = [&](halide_buffer_t *in1_buf, halide_buffer_t *in2_buf, halide_buffer_t *out_buf) {
        add_uint8_uint8(in1_buf, in1_zero, in1_multiplier, in2_buf, in2_zero, in2_multiplier,
                        out_zero, out_range.min, out_range.max, out_buf);
    };
    elementwise_loop_nest<2>(add_rank2, in1, in2, out);
}

void mul_uint8(const HalideBuffer<const void> &in1, const QuantizationInfo &in1q,
               const HalideBuffer<const void> &in2, const QuantizationInfo &in2q,
               const HalideBuffer<void> &out, const QuantizationInfo &outq,
               ActivationFunction activation = ActivationFunction::None) {
    const int in1_zero = in1q.uniform_zero();
    const int in2_zero = in2q.uniform_zero();
    const int out_zero = outq.uniform_zero();

    const float in1_scale = in1q.uniform_scale();
    const float in2_scale = in2q.uniform_scale();
    const float out_scale = outq.uniform_scale();

    IntFloat<int32_t> multiplier(in1_scale * in2_scale / out_scale);
    multiplier *= power_of_two(-2 * mul_input_shift);
    assert(multiplier.exponent() <= 0);

    const auto out_range = get_output_range(activation, outq);

    auto mul_rank2 = [&](halide_buffer_t *in1_buf, halide_buffer_t *in2_buf, halide_buffer_t *out_buf) {
        mul_uint8_uint8_uint8(in1_buf, in1_zero, in2_buf, in2_zero,
                              out_zero, multiplier.mantissa(), -multiplier.exponent(),
                              out_range.min, out_range.max, out_buf);
    };
    elementwise_loop_nest<2>(mul_rank2, in1, in2, out);
}

bool try_requantize(const HalideBuffer<const void> &in, const QuantizationInfo &inq,
                    HalideBuffer<void> out, const QuantizationInfo &outq,
                    ActivationFunction activation = ActivationFunction::None) {
    if (in.type() != out.type()) {
        HLOG(ERROR) << "requantize: input and output types must match";
        return false;
    }

    if (in.type() == halide_type_of<uint8_t>() &&
        out.type() == halide_type_of<uint8_t>()) {
        // TODO: Maybe a dedicated pipeline for this would be better. It
        // could be a little faster, and avoid some quantization error.
        add_uint8(in, inq, 1, in, inq, 0, out, outq, activation);
        return true;
    }

    return false;
}

// Input and output buffer types must match.
// If the input and output buffers are quantized, we always call requantize.
// If not, we simply copy.
bool requantize_or_copy(const HalideBuffer<const void> &in, const QuantizationInfo &inq,
                        HalideBuffer<void> out, const QuantizationInfo &outq,
                        ActivationFunction activation = ActivationFunction::None) {
    if (in.type() != out.type()) {
        HLOG(ERROR) << "requantize_or_copy: input and output types must match";
        return false;
    }
    if (try_requantize(in, inq, out, outq, activation)) {
        return true;
    }

    if (!is_alias(in.raw_buffer(), out.raw_buffer())) {
        out.copy_from(in);
    }
    return true;
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
        HLOG(FATAL) << UnaryOp::to_string(op) << " is not an activation function";
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
        HLOG(FATAL) << "Unsupported binary op\n";
        return nullptr;
    }
}

namespace {

template<typename T>
T as_scalar(const HalideBuffer<const void> &buf) {
    return *(const T *)buf.data();
}

double dequantize_scalar(const Tensor *t) {
    assert(t->rank() == 0);

    const QuantizationInfo &q = t->quantization();
    float scale = q.scale.empty() ? 1.0f : q.scale.front();
    int zero = q.zero.empty() ? 0 : q.zero.front();

    const auto &buf = t->buffer();
    switch (buf.type().element_of().as_u32()) {
    case halide_type_of<uint8_t>().as_u32():
        return (as_scalar<uint8_t>(buf) - zero) * scale;
    case halide_type_of<int8_t>().as_u32():
        return (as_scalar<int8_t>(buf) - zero) * scale;
    case halide_type_of<uint16_t>().as_u32():
        return (as_scalar<uint16_t>(buf) - zero) * scale;
    case halide_type_of<int16_t>().as_u32():
        return (as_scalar<int16_t>(buf) - zero) * scale;
    case halide_type_of<uint32_t>().as_u32():
        return (as_scalar<uint32_t>(buf) - zero) * scale;
    case halide_type_of<int32_t>().as_u32():
        return (as_scalar<int32_t>(buf) - zero) * scale;
    case halide_type_of<float>().as_u32():
        return (as_scalar<float>(buf) - zero) * scale;
    case halide_type_of<double>().as_u32():
        return (as_scalar<double>(buf) - zero) * scale;
    default:
        HLOG(FATAL) << "Unsupported type " << buf.type();
        return std::numeric_limits<double>::quiet_NaN();
    }
}

template<typename TResult, typename TOperand>
TResult implement_binary(BinaryOp::Operator op, TOperand a, TOperand b) {
    switch (op) {
    case BinaryOp::Add:
        return a + b;
    case BinaryOp::Sub:
        return a - b;
    case BinaryOp::Mul:
        return a * b;
    case BinaryOp::Less:
        return a < b;
    case BinaryOp::LessEqual:
        return a <= b;
    case BinaryOp::Equal:
        return a == b;
    case BinaryOp::NotEqual:
        return a != b;
    default:
        HLOG(FATAL) << "Unknown binary operator " << BinaryOp::to_string(op);
        return TResult();
    }
}

template<typename TOperand, typename TResult>
bool try_scalar_binary_op(BinaryOp::Operator op, const TensorPtr &a, const TensorPtr &b, const TensorPtr &result) {
    if (a->type() == halide_type_of<TOperand>() &&
        b->type() == halide_type_of<TOperand>() &&
        result->type() == halide_type_of<TResult>()) {
        const auto &a_buf = a->buffer<const TOperand>();
        const auto &b_buf = b->buffer<const TOperand>();
        const auto &result_buf = result->buffer<TResult>();

        // This is really slow, only intended to support scalar operations.
        const auto scalar_op = [&](TOperand a_scalar, TOperand b_scalar, TResult &result_scalar) {
            result_scalar = implement_binary<TResult, TOperand>(op, a_scalar, b_scalar);
        };
        scalar_elementwise_loop_nest(scalar_op, a_buf, b_buf, result_buf);
        return true;
    }
    return false;
}

}  // namespace

void BinaryOp::execute() {
    const TensorPtr &in1 = input(0);
    const TensorPtr &in2 = input(1);
    const TensorPtr &out = output();

    if (in1->type() == halide_type_of<uint8_t>() &&
        in2->type() == halide_type_of<uint8_t>() &&
        out->type() == halide_type_of<uint8_t>()) {
        const auto &in1_buf = in1->buffer();
        const auto &in2_buf = in2->buffer();
        const auto &out_buf = out->buffer();

        switch (op_) {
        case Add:
        case Sub:
            add_uint8(in1_buf, in1->quantization(), 1, in2_buf, in2->quantization(), op_ == Add ? 1 : -1, out_buf, out->quantization(), activation_);
            return;
        case Mul:
            mul_uint8(in1_buf, in1->quantization(), in2_buf, in2->quantization(), out_buf, out->quantization(), activation_);
            return;
        default:
            break;
        }
    } else {
        // This is really slow, only intended to support scalar operations.
        if (try_scalar_binary_op<int32_t, int32_t>(op_, in1, in2, out)) {
            return;
        }

        // TODO: these can be useful for debugging pipelines that use op variants we don't fully support yet
        // (e.g. float32) -- leaving this here (but commented out) as a useful reference, but *please*
        // don't add any permanent usage here at this time (the ops almost certainly need to be written in Halide).
        //
        // if (try_scalar_binary_op<float, float>(op_, in1, in2, out)) {
        //     return;
        // }
        // // This is for the LESS, etc operators, which may store results in uint8 rather than bool
        // if (try_scalar_binary_op<float, uint8_t>(op_, in1, in2, out)) {
        //     return;
        // }

        if (out->type() == halide_type_of<bool>() && out->rank() == 0) {
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
    }
    HLOG(FATAL)
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
    if (is_no_op_) {
        return;
    }
    const auto &output_buf = output()->buffer();

    int concatenated_i = 0;
    for (int i = 0; i < input_count(); i++) {
        auto input_buf = input(i)->buffer();
        assert(input_buf.dim(axis_).min() == 0);
        input_buf.translate(axis_, concatenated_i);
        concatenated_i += input_buf.dim(axis_).extent();

        auto output_crop = output_buf;
        crop_to_union(output_crop, input_buf);

        bool copied = requantize_or_copy(input_buf, input(i)->quantization(), output_crop, output()->quantization());
        HCHECK(copied);
    }
}

halide_type_t ConvOp::filter_type() const {
    if (input()->type() == halide_type_of<uint8_t>() &&
        output()->type() == halide_type_of<uint8_t>()) {
        const halide_filter_metadata_t *metadata = conv_u8_u8_u8_metadata();
        return metadata->arguments[2].type;
    } else if (input()->type() == halide_type_of<uint8_t>() &&
               output()->type() == halide_type_of<int16_t>()) {
        const halide_filter_metadata_t *metadata = conv_u8_u8_i16_metadata();
        return metadata->arguments[2].type;
    } else {
        HLOG(FATAL) << "Unsupported type " << output()->type() << "\n";
        return halide_type_t(halide_type_int, 0, 0);
    }
}

BoundsMap ConvOp::map_bounds(int input_idx, int output_idx) const {
    assert(vector_reduction_ > 0);
    assert(vector_tile_ > 0);

#ifdef CONV_R16
    const int unroll_reduction = filter()->extent(0) >= 16 ? 16 : 4;
#else
    const int unroll_reduction = 4;
#endif
    if (input_idx == 0) {
        BoundsMap result(input()->rank(), output()->rank());
        result
            .constant(0, align_up(input()->extent(0), unroll_reduction))
            .elementwise(input()->rank() - 1, input()->rank() - 1);
        for (int i = 1; i < input()->rank() - 1; i++) {
            result.downsample(i, i, stride_[i - 1], Interval(0, dilation_[i - 1] * (filter()->extent(i) - 1)));
        }
        return result;
    } else if (input_idx == 1) {
        const int channel_alignment = unroll_reduction / vector_reduction_;
        BoundsMap result(input()->rank() + 2, output()->rank());
        result
            .constant(0, vector_reduction_)
            .constant(1, vector_tile_)
            .constant(2, align_up(ceil_div(filter()->extent(0), vector_reduction_), channel_alignment))
            .upsample(3, 0, vector_tile_);
        for (int i = 1; i < output()->rank() - 1; i++) {
            result.constant(i + 3, filter()->bounds(i));
        }
        return result;
    } else {
        assert(input_idx == 2);
        return BoundsMap(1, output()->rank()).elementwise(0, 0);
    }
}

namespace {

void call_conv2d(halide_buffer_t *input, halide_buffer_t *filter, halide_buffer_t *bias,
                 const MultiplyParams &params, const std::array<int, 2> &stride,
                 const std::array<int, 2> &dilation, const Interval &output_range,
                 halide_buffer_t *output) {
    using Conv2DFn = decltype(&::hannk::conv_u8_u8_u8);

    Conv2DFn fn;
#ifdef CONV_R16
    if (input->dim[0].extent >= 16) {
        // For large reductions, use the big reduction version.
        // TODO: We really ought to be able to do this with GuardWithIf
        // and/or specialize.
        fn = output->type == halide_type_of<int16_t>() ? hannk::conv_r16_u8_u8_i16 : hannk::conv_r16_u8_u8_u8;
    } else
#endif
    {
        fn = output->type == halide_type_of<int16_t>() ? hannk::conv_u8_u8_i16 : hannk::conv_u8_u8_u8;
    }
    fn(input, (uint8_t)params.a_zero, filter, (uint8_t)params.b_zero, bias,
       stride[0], stride[1], dilation[0], dilation[1], params.c.mantissa(),
       -params.c.exponent(), (uint8_t)params.c_zero, output_range.min, output_range.max,
       output);
}

}  // namespace

bool ConvOp::prepare() {
    // Pass minimal sized buffers to learn about the alignment requirements.
    // TODO: need to adapt this to the types of in, filt, out once we support multiple variants
    HalideBuffer<uint8_t> input_buf(nullptr, 1, 1, 1, 1);
    HalideBuffer<int32_t> bias_buf(nullptr, 1);
    HalideBuffer<void> filter_buf(filter_type(), nullptr, 1, 1, 1, 1, 1, 1);
    HalideBuffer<uint8_t> output_buf(nullptr, 1, 1, 1, 1);
    if (conv_u8_u8_u8(input_buf, 0, filter_buf, 0, bias_buf, 1, 1, 1, 1, 0, 0, 0, 0, 0, output_buf) != 0) {
        return false;
    }

    vector_reduction_ = filter_buf.dim(0).extent();
    vector_tile_ = filter_buf.dim(1).extent();
    return true;
}

void ConvOp::execute() {
    const TensorPtr &in = input();
    const TensorPtr &filt = filter();
    const TensorPtr &out = output();

    if (in->type() == halide_type_of<uint8_t>() &&
        (out->type() == halide_type_of<uint8_t>() || out->type() == halide_type_of<int16_t>())) {
        auto input_buf = in->buffer();
        auto filter_buf = filt->buffer();
        auto bias_buf = bias()->buffer();
        auto output_buf = out->buffer();

        MultiplyParams params =
            get_quantized_multiply_params(in->quantization(), filt->quantization(), out->quantization());

        const auto output_range = get_output_range(activation_, out->quantization());

        // Pad with dummy dimensions up to 2D.
        while (input_buf.dimensions() < 4) {
            input_buf.embed(input_buf.dimensions() - 1, 1);
            output_buf.embed(output_buf.dimensions() - 1, 1);
            filter_buf.add_dimension();
        }

        assert(filter_buf.dimensions() == 6);
        const int filter_width = filter_buf.dim(4).extent();
        const int filter_height = filter_buf.dim(5).extent();
        if (filter_width == 1 && filter_height == 1) {
            // For 1x1 filters, we can fuse x and y, which can help avoid overhead for
            // small output sizes.
            // TODO: Maybe we can just treat all of x, y, b as batch dimensions and fuse
            // them all where possible, which might be a further improvement.
            while (can_fuse_xy(FuseType::Pad, input_buf) &&
                   can_fuse_xy(FuseType::Pad, output_buf) &&
                   input_buf.dim(1).extent() == output_buf.dim(1).extent()) {
                fuse_xy(FuseType::Pad, input_buf);
                fuse_xy(FuseType::Pad, output_buf);
            }

            if (output_buf.dim(1).extent() < output_buf.dim(2).extent()) {
                // Some networks have shapes with very small x and large y that we can't fuse.
                // This case is bad for us because we tile the x dimension. It would be better
                // if we tiled y instead. We can do this by just swapping the x and y dimensions.
                input_buf.transpose(1, 2);
                output_buf.transpose(1, 2);
            }
        }

        call_conv2d(input_buf, filter_buf, bias_buf, params, stride_, dilation_, output_range, output_buf);
    } else {
        HLOG(FATAL) << "Unsupported type " << out->type() << "\n";
    }
}

namespace {

// Wrapper to dispatch to the appropriate variant of depthwise_conv.
void call_depthwise_conv_uint8(
    halide_buffer_t *input, halide_buffer_t *filter, halide_buffer_t *bias,
    const MultiplyParams &params, const std::array<int, 2> &stride, const std::array<int, 2> &dilation,
    int input_stride_x, const Interval &output_range, halide_buffer_t *output) {
    if (input_stride_x != 0) {
        depthwise_conv_shallow_uint8(
            input, (uint8_t)params.a_zero, filter, (uint8_t)params.b_zero, bias,
            stride[0], stride[1], dilation[0], dilation[1], input_stride_x, params.c.mantissa(), -params.c.exponent(),
            (uint8_t)params.c_zero, (uint8_t)output_range.min, (uint8_t)output_range.max, output);
    } else if (input->dim[0].extent == 1) {
        depthwise_conv_broadcast_uint8(
            input, (uint8_t)params.a_zero, filter, (uint8_t)params.b_zero, bias,
            stride[0], stride[1], dilation[0], dilation[1], input_stride_x, params.c.mantissa(), -params.c.exponent(),
            (uint8_t)params.c_zero, (uint8_t)output_range.min, (uint8_t)output_range.max, output);
    } else {
        ::hannk::depthwise_conv_uint8(
            input, (uint8_t)params.a_zero, filter, (uint8_t)params.b_zero, bias,
            stride[0], stride[1], dilation[0], dilation[1], input_stride_x, params.c.mantissa(), -params.c.exponent(),
            (uint8_t)params.c_zero, (uint8_t)output_range.min, (uint8_t)output_range.max, output);
    }
}

bool can_be_shallow(int alignment, int extent_0, int extent_1) {
    assert(alignment > 0);
    // This is correct: we want to use shallow when the vector size (ie, alignment)
    // is evenly divisble by the number of channels (ie, extent(0)).
    //
    // To avoid OOB access for tiny buffers, we also check that the fused width
    // is at least one vector wide.
    return (alignment % extent_0) == 0 && (extent_0 * extent_1 >= alignment);
}

}  // namespace

BoundsMap DepthwiseConv2DOp::map_bounds(int input_idx, int output_idx) const {
    assert(output_idx == 0);
    assert(channel_alignment_ > 0);

    if (input_idx == 0) {
        BoundsMap result(4, 4);
        result
            .upsample(0, 0, depth_multiplier_)
            .downsample(1, 1, stride_[0], Interval(0, dilation_[0] * (filter()->extent(1) - 1)))
            .downsample(2, 2, stride_[1], Interval(0, dilation_[1] * (filter()->extent(2) - 1)))
            .elementwise(3, 3);
        if (depth_multiplier_ == 1) {
            if (stride_[0] == 1 &&
                can_be_shallow(channel_alignment_, input()->extent(0), input()->extent(1))) {
                // We can use the shallow version of depthwise here.
            } else {
                result.align_input(0, channel_alignment_);
            }
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

bool DepthwiseConv2DOp::prepare() {
    // Pass minimal sized buffers to learn about the alignment requirements.
    // TODO: need to adapt this to the types of in, filt, out once we support multiple variants
    HalideBuffer<uint8_t> input_buf(nullptr, 1, 1, 1, 1);
    HalideBuffer<int32_t> bias_buf(nullptr, 1);
    HalideBuffer<uint8_t> filter_buf(nullptr, 1, 1, 1);
    HalideBuffer<uint8_t> output_buf(nullptr, 1, 1, 1, 1);
    if (depthwise_conv_uint8(input_buf, 0, filter_buf, 0, bias_buf, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, output_buf) != 0) {
        return false;
    }
    channel_alignment_ = input_buf.dim(0).extent();
    return true;
}

void DepthwiseConv2DOp::execute() {
    const TensorPtr &in = input();
    const TensorPtr &filt = filter();
    const TensorPtr &out = output();

    if (in->type() == halide_type_of<uint8_t>() &&
        filt->type() == halide_type_of<uint8_t>() &&
        out->type() == halide_type_of<uint8_t>()) {
        auto input_buf = in->buffer();
        auto filter_buf = filt->buffer().sliced(3, 0);
        auto bias_buf = bias()->buffer();
        auto output_buf = out->buffer();

        MultiplyParams params =
            get_quantized_multiply_params(in->quantization(), filt->quantization(), out->quantization());

        const auto output_range = get_output_range(activation_, out->quantization());

        // If the number of channels is small and divides the channel alignment,
        // and the stride of the filter in x is 1, we can use the "shallow"
        // version of depthwise conv, which fuses c and x, and passes the stride
        // of x into the pipeline manually.
        int input_stride_x = 0;
        if (stride_[0] == 1 &&
            can_fuse_cx(FuseType::InPlace, input_buf) &&
            can_fuse_cx(FuseType::InPlace, output_buf) &&
            can_be_shallow(channel_alignment_, input_buf.dim(0).extent(), input_buf.dim(1).extent())) {
            input_stride_x = input_buf.dim(1).stride();
            fuse_cx(FuseType::InPlace, input_buf);
            fuse_cx(FuseType::InPlace, output_buf);
        }

        assert(depth_multiplier_ == 1 || depth_multiplier_ >= out->extent(0));
        call_depthwise_conv_uint8(input_buf, filter_buf, bias_buf, params,
                                  stride_, dilation_, input_stride_x, output_range, output_buf);
    } else {
        HLOG(FATAL) << "Unsupported type " << out->type() << "\n";
    }
}

namespace {

// This is a silly wrapper for a list of types that we can compare to
// a list of runtime types, without dynamically allocating any memory.
template<typename... Types>
struct TypeList {
    halide_type_t types[sizeof...(Types)] = {halide_type_of<Types>()...};
    constexpr size_t size() const {
        return sizeof...(Types);
    }
    halide_type_t operator[](int i) {
        return types[i];
    }
};

template<size_t N, typename T>
struct TypeArray {
    constexpr size_t size() const {
        return N;
    }
    halide_type_t operator[](int i) {
        return halide_type_of<T>();
    }
};

template<typename InputTypes, typename OutputTypes>
bool can_use_elementwise_program(const Op *op) {
    InputTypes input_types;
    OutputTypes output_types;
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

}  // namespace

void ElementwiseProgramOp::execute() {
    const auto &in0 = input(0)->buffer();
    const auto &in1 = input(std::min(input_count() - 1, 1))->buffer();
    const auto &in2 = input(std::min(input_count() - 1, 2))->buffer();
    const auto &in3 = input(std::min(input_count() - 1, 3))->buffer();
    const auto &in4 = input(std::min(input_count() - 1, 4))->buffer();
    const auto &out0 = output(0)->buffer();
    const auto &out1 = output(std::min(output_count() - 1, 1))->buffer();
    using arg_ptr = halide_buffer_t *;
    if (can_use_elementwise_program<TypeArray<5, uint8_t>, TypeArray<1, uint8_t>>(this)) {
        auto rank2 = [&](arg_ptr in0, arg_ptr in1, arg_ptr in2, arg_ptr in3, arg_ptr in4, arg_ptr out0) {
            elementwise_5xuint8_1xuint8(in0, in1, in2, in3, in4, program_, out0);
        };
        elementwise_loop_nest<2>(rank2, in0, in1, in2, in3, in4, out0);
        return;
    } else if (can_use_elementwise_program<TypeArray<5, int16_t>, TypeList<uint8_t, int16_t>>(this)) {
        auto rank2 = [&](arg_ptr in0, arg_ptr in1, arg_ptr in2, arg_ptr in3, arg_ptr in4, arg_ptr out0, arg_ptr out1) {
            elementwise_5xint16_1xuint8int16(in0, in1, in2, in3, in4, program_, out0, out1);
        };
        elementwise_loop_nest<2>(rank2, in0, in1, in2, in3, in4, out0, out1);
        return;
    }
    HLOG(FATAL) << "Unsupported elementwise program\n";
}

BoundsMap GatherOp::map_bounds(int input_idx, int output_idx) const {
    if (input_idx == 0) {
        BoundsMap result = BoundsMap::elementwise(output()->rank());
        // We need potentially anything from the dimension we are gathering.
        result.constant(axis_, input()->extent(axis_));
        return result;
    } else {
        assert(input_idx == 1);
        BoundsMap result(1, output()->rank());
        result.elementwise(0, axis_);
        return result;
    }
}

void GatherOp::execute() {
    const HalideBuffer<const void> &in = input(0)->buffer();
    HalideBuffer<const int32_t> indices = input(1)->buffer();
    const HalideBuffer<void> &out = output()->buffer();

    // Haven't yet found an instance of TFLite's Gather op that
    // specifies a nonzero values. Implement and test once we do.
    HCHECK(batch_dims_ == 0) << "TODO: GatherOp doesn't yet support batch_dim != 0";

    if (indices.dimensions() == 0) {
        // Yes, a 0D (scalar) here is documented as legal in TFLite
        indices.embed(0, 0);  // make 1-D
    }

#ifndef NDEBUG
    assert(out.dimensions() == in.dimensions() + indices.dimensions() - 1);
    for (int i = 0; i < out.dimensions(); i++) {
        if (i < axis_) {
            assert(out.dim(i).extent() == in.dim(i).extent());
        } else if (i < axis_ + indices.dimensions()) {
            assert(out.dim(i).extent() == indices.dim(i - axis_).extent());
        } else {
            assert(out.dim(i).extent() == in.dim(i - indices.dimensions() + 1).extent());
        }
    }

    // Negative values for axis_ are handled by the parser
    assert(axis_ >= 0 && axis_ < in.dimensions());
#endif

    // Note that the TFLite Gather op restricts indices to 1D (or 0D),
    // but other NN libraries (eg NNAPI) alloe for it to be multidimensional,
    // so we must copy more robustly.
    //
    // (TODO: support TFLite's GatherNd op, which is similar to this.)
    const int pos_dims = indices.dimensions();
    indices.for_each_element([&](const int *pos) {
        const int index = indices(pos);
        HalideBuffer<const void> in_i = in.sliced(axis_, index);
        HalideBuffer<void> out_i = out.sliced(axis_, pos[0]);
        for (int i = 1; i < pos_dims; i++) {
            out_i = out_i.sliced(axis_, pos[i]);
        }
        out_i.copy_from(in_i);
    });
}

BoundsMap L2NormalizationOp::map_bounds(int input_idx, int output_idx) const {
    assert(input_idx == 0);
    assert(output_idx == 0);
    return BoundsMap(2, 2)
        .constant(0, input()->bounds(0))
        .elementwise(1, 1);
}

void L2NormalizationOp::execute() {
    const TensorPtr &in = input();
    const TensorPtr &out = output();

    // Negative values for axis_ must be normalized by the parser
    assert(axis_ >= 0 && axis_ < in->rank());

    if (in->type() == halide_type_of<uint8_t>() &&
        out->type() == halide_type_of<uint8_t>()) {
        // Make local copies in case we need to transpose them
        HalideBuffer<void> in_buf = in->buffer();
        HalideBuffer<void> out_buf = out->buffer();

        // TODO: we currently assume that the axis-is-0 case is the most common
        // and most important, and optimize for it; the other cases, we just transpose,
        // which currently requires less-efficient specializations in the Halide code.
        // Revisit if this proves too slow in practice.
        if (axis_ != 0) {
            in_buf.transpose(0, axis_);
            out_buf.transpose(0, axis_);
        }

        const int input_zero = in->quantization().uniform_zero();
        assert(input_zero >= 0 && input_zero <= 255);

        assert(out->quantization().uniform_scale() == 1.0f / 128.0f);
        assert(out->quantization().uniform_zero() == 128);

        auto l2_normalization_rank2 = [&](halide_buffer_t *in_buf, halide_buffer_t *out_buf) {
            l2_normalization_uint8(in_buf, input_zero, out_buf);
        };
        loop_nest<2>(l2_normalization_rank2, in_buf, out_buf);
    } else {
        HLOG(FATAL) << "Unsupported type " << out->type() << "\n";
    }
}

BoundsMap PadOp::map_bounds(int input_idx, int output_idx) const {
    assert(output_idx == 0);
    const int rank = output()->rank();
    if (input_idx == 0) {
        if (input(1)) {
            BoundsMap result(rank, rank);
            const auto &padding = input(1)->buffer<const int32_t>();
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
    const TensorPtr &in = input(0);
    const TensorPtr &padding = input(1);
    const TensorPtr &out = output();

    assert(padding->extent(0) == 2);
    assert(padding->extent(1) == in->rank());

    const auto &padding_buf = padding->buffer<const int32_t>();
    if (out->is_dynamic()) {
        const int dims = in->rank();
        Box new_shape = in->bounds();
        for (int d = 0; d < dims; ++d) {
            const int idx = dims - d - 1;
            const int before_padding = padding_buf(0, idx);
            const int after_padding = padding_buf(1, idx);
            assert(before_padding >= 0 && after_padding >= 0);
            new_shape[d].max += before_padding + after_padding;
        }
        out->resize_dynamic(new_shape);
    }

    if (out->type().bytes() == 1) {
        auto input_buf = in->buffer();
        auto output_buf = out->buffer();

        const int dims = input_buf.dimensions();
        for (int d = 0; d < input_buf.dimensions(); d++) {
            const int idx = dims - d - 1;
            input_buf.translate(d, padding_buf(0, idx));
        }

        uint8_t pad_value = in->quantization().uniform_zero();

        // TODO: should we pad_to_rank(4) the input and output bufs before the loop?

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
                pad_to_rank(4, before);
                fill_uint8(pad_value, before);
            } else {
                input_min = output_min;
            }
            if (output_max > input_max) {
                auto after = output_buf.cropped(d, input_max + 1, output_max - input_max);
                pad_to_rank(4, after);
                fill_uint8(pad_value, after);
            } else {
                input_max = output_max;
            }
            output_buf.crop(d, input_min, input_max - input_min + 1);
        }
        if (!is_alias(input_buf, output_buf) ||
            input_buf.dim(0).min() > output_buf.dim(0).min() ||
            input_buf.dim(0).max() < output_buf.dim(0).max()) {
            pad_to_rank(4, input_buf);
            pad_to_rank(4, output_buf);
            copy_uint8_uint8(input_buf, pad_value, output_buf);
        }
    } else {
        HLOG(FATAL) << "Unsupported type " << out->type() << "\n";
    }
}

namespace {

int compute_padding(int stride, int in_size, int filter_size, int out_size) {
    const int effective_filter_size = (filter_size - 1) + 1;
    const int total_padding = std::max(0, ((out_size - 1) * stride + effective_filter_size - in_size));
    return total_padding / 2;
}

}  // namespace

const char *Pool2DOp::to_string(Pool2DOp::Operator op) {
    switch (op) {
    case Average:
        return "Average";
    case Max:
        return "Max";
    default:
        HLOG(FATAL) << "Unsupported pool op\n";
        return nullptr;
    }
}

BoundsMap Pool2DOp::map_bounds(int input_idx, int output_idx) const {
    assert(output_idx == 0);
    return BoundsMap(4, 4)
        .elementwise(0, 0)
        .downsample(1, 1, stride_[0], Interval(0, filter_size_[0] - 1))
        .downsample(2, 2, stride_[1], Interval(0, filter_size_[1] - 1))
        .elementwise(3, 3);
}

void Pool2DOp::execute() {
    const TensorPtr &in = input();
    const TensorPtr &out = output();

    if (in->type() == halide_type_of<uint8_t>() &&
        out->type() == halide_type_of<uint8_t>()) {
        auto input_buf = in->buffer();
        auto output_buf = out->buffer();

        const auto output_range = get_output_range(activation_, out->quantization());

        const int in_width = input_buf.dim(1).extent();
        const int in_height = input_buf.dim(2).extent();
        const int out_width = output_buf.dim(1).extent();
        const int out_height = output_buf.dim(2).extent();
        input_buf.translate(1, compute_padding(stride_[0], in_width, filter_size_[0], out_width));
        input_buf.translate(2, compute_padding(stride_[1], in_height, filter_size_[1], out_height));

        switch (op_) {
        case Average:
            average_pool_uint8(input_buf, stride_[0], stride_[1], filter_size_[0], filter_size_[1],
                               output_range.min, output_range.max, output_buf);
            break;
        case Max:
            max_pool_uint8(input_buf, stride_[0], stride_[1], filter_size_[0], filter_size_[1],
                           output_range.min, output_range.max, output_buf);
            break;
        }
    } else {
        HLOG(FATAL) << "Unsupported type " << out->type() << "\n";
    }
}

const char *ReductionOp::to_string(Operator op) {
    switch (op) {
    case Mean:
        return "Mean";
    default:
        HLOG(FATAL) << "Unsupported reduction operator.\n";
        return nullptr;
    }
}

bool ReductionOp::reducing(int d) const {
    const TensorPtr &in = input(0);
    const TensorPtr &indices = input(1);
    const auto &indices_buf = indices->buffer<const int32_t>();
    for (int i = 0; i < indices_buf.dim(0).extent(); i++) {
        int index = indices_buf(i);
        if (index < 0) {
            index += in->rank();
        }
        index = in->rank() - 1 - index;
        assert(index >= 0 && index < in->rank());
        if (index == d) {
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
    const TensorPtr &in = input();
    const TensorPtr &out = output();

    if (in->type() == halide_type_of<uint8_t>() &&
        out->type() == halide_type_of<uint8_t>()) {
        auto input_buf = in->buffer();
        auto output_buf = out->buffer();
        pad_to_rank(4, input_buf);
        pad_to_rank(4, output_buf);

        if (op_ == Mean) {
            int mins[4] = {0, 0, 0, 0};
            int extents[4] = {1, 1, 1, 1};
            for (int d = 0; d < 4; d++) {
                if (reducing(d)) {
                    mins[d] = input_buf.dim(d).min();
                    extents[d] = input_buf.dim(d).extent();
                }
            }
            mean_uint8(input_buf, mins[0], extents[0], mins[1], extents[1],
                       mins[2], extents[2], mins[3], extents[3], output_buf);
        }
    }
}

// TODO: Maybe this is only a reshape in some dimensions, in which case we might be able to split it.
BoundsMap ReshapeOp::map_bounds(int input_idx, int output_idx) const {
    assert(input_idx == 0);
    assert(output_idx == 0);
    return BoundsMap::all(input()->bounds(), output()->rank());
}

SmallVector<int, max_rank> ReshapeOp::calc_new_shape() const {
    const TensorPtr &in = input();
    const TensorPtr &shape = input(1);

    SmallVector<int, max_rank> new_shape;
    if (shape) {
        const auto &shape_buf = shape->buffer<const int32_t>();
        assert(shape_buf.dimensions() == 1);
        new_shape.assign(shape_buf.begin(), shape_buf.end());
    }
    if (new_shape.size() == 1 && new_shape[0] == 0) {
        // Legacy tflite models use a shape parameter of [0] to indicate scalars,
        // so adjust accordingly.
        new_shape.clear();
    }
    std::reverse(new_shape.begin(), new_shape.end());

    // One of the shape values can be -1, meaning "calculate it for me".
    int output_elements = 1;
    int stretch_dim = -1;
    for (size_t i = 0; i < new_shape.size(); ++i) {
        const int value = new_shape[i];
        if (value == -1) {
            assert(stretch_dim == -1);
            stretch_dim = i;
        } else {
            output_elements *= value;
        }
    }
    if (stretch_dim != -1) {
        new_shape[stretch_dim] = in->number_of_elements() / output_elements;
        output_elements *= new_shape[stretch_dim];
        assert(output_elements == (int)output()->number_of_elements());
    }

    return new_shape;
}

void ReshapeOp::execute() {
    const TensorPtr &in = input();
    const TensorPtr &out = output();

    SmallVector<int, max_rank> new_shape = calc_new_shape();

    if (out->is_dynamic()) {
        Box b;
        for (int i : new_shape) {
            b.emplace_back(0, i - 1);
        }
        out->resize_dynamic(b);
    }

    const auto &input_buf = in->buffer();
    const auto &output_buf = out->buffer();

    assert((int)new_shape.size() == output_buf.dimensions());
    for (int d = 0; d < output_buf.dimensions(); d++) {
        assert(new_shape.at(d) == output_buf.dim(d).extent());
    }

    // TODO: we must verify these match (and fail at runtime if not).
    // That said, we should be able to predict this at parse time
    // (for non-dynamic tensors) and skip the runtime check most of the time.
    assert(input_buf.number_of_elements() == output_buf.number_of_elements());

    assert(in->is_dense());
    assert(out->is_dense());
    if (is_alias(input_buf.raw_buffer(), output_buf.raw_buffer())) {
        assert(input_buf.begin() == output_buf.begin());
        assert(input_buf.end() == output_buf.end());
    } else {
        size_t output_size = output_buf.number_of_elements() * out->type().bytes();
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
    const TensorPtr &in = input();
    const TensorPtr &out = output();

    if (out->type() == halide_type_of<int32_t>()) {
        HalideBuffer<int32_t> out_buf = out->buffer<int32_t>();
        assert(out_buf.dimensions() == 1);
        for (int i = out_buf.dim(0).min(); i <= out_buf.dim(0).max(); i++) {
            out_buf(i) = in->extent(i);
        }
    } else {
        HLOG(FATAL) << "Unsupported type " << out->type() << "\n";
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
    const TensorPtr &in = input();
    const TensorPtr &out = output();

    // Negative values for axis_ must be normalized by the parser
    assert(axis_ >= 0 && axis_ < in->rank());

    if (in->type() == halide_type_of<uint8_t>() &&
        out->type() == halide_type_of<uint8_t>()) {
        // Make local copies in case we need to transpose them
        HalideBuffer<void> in_buf = in->buffer();
        HalideBuffer<void> out_buf = out->buffer();

        // TODO: we currently assume that the axis-is-0 case is the most common
        // and most important, and optimize for it; the other cases, we just transpose,
        // which currently requires less-efficient specializations in the Halide code.
        // Revisit if this proves too slow in practice.
        if (axis_ != 0) {
            in_buf.transpose(0, axis_);
            out_buf.transpose(0, axis_);
        }

        // We don't need the input zero point because this op exploits the
        // identity exp(x_i)/sum(exp(x_i)) == exp(x_i + C)/sum(exp(x_i + C))
        const int output_zero = out->quantization().uniform_zero();
        assert(output_zero >= 0 && output_zero <= 255);

        // It's a easier to compute 2^(x*(B*log2(e))) than e^(x*B).
        const float beta2 = beta_ * std::log2(std::exp(1.0f));
        IntFloat<int16_t> input_multiplier(beta2 * in->quantization().uniform_scale());
        input_multiplier *= power_of_two(-softmax_input_shift);
        assert(input_multiplier.exponent() <= 0);

        IntFloat<int16_t> output_multiplier(out->quantization().uniform_scale());
        // TODO: Debug why this extra factor of 2 is needed. There's something
        // wrong with the fixed point tricks in the implementation.
        output_multiplier *= power_of_two(1);
        assert(output_multiplier.exponent() <= 0);

        auto softmax_rank2 = [&](halide_buffer_t *in_buf, halide_buffer_t *out_buf) {
            softmax_uint8(in_buf, input_multiplier.mantissa(), -input_multiplier.exponent(),
                          output_zero, output_multiplier.mantissa(), -output_multiplier.exponent(), out_buf);
        };
        loop_nest<2>(softmax_rank2, in_buf, out_buf);
    } else {
        HLOG(FATAL) << "Unsupported type " << out->type() << "\n";
    }
}

namespace {

void DepthToSpace(HalideBuffer<const void> input, int block_size, HalideBuffer<void> output) {
    const int output_depth = output.dim(0).extent();
    split(1, block_size, output);
    split(3, block_size, output);
    split(0, output_depth * block_size, input);
    split(0, output_depth, input);
    output.transpose(2, 3);
    output.copy_from(input);
}

void SpaceToDepth(HalideBuffer<const void> input, int block_size, HalideBuffer<void> output) {
    const int input_depth = input.dim(0).extent();
    split(1, block_size, input);
    split(3, block_size, input);
    split(0, input_depth * block_size, output);
    split(0, input_depth, output);
    input.transpose(2, 3);
    output.copy_from(input);
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
    const TensorPtr &in = input();
    const TensorPtr &out = output();

    if (in->type() == out->type()) {
        const auto &in_buf = in->buffer();
        const auto &out_buf = out->buffer();

        if (block_size_ > 0) {
            SpaceToDepth(in_buf, block_size_, out_buf);
        } else {
            DepthToSpace(in_buf, -block_size_, out_buf);
        }
    } else {
        HLOG(FATAL) << "Unsupported types " << in->type() << " " << out->type() << "\n";
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
    if (is_no_op_) {
        return;
    }
    const auto &input_buf = input()->buffer();

    int concatenated_i = 0;
    for (int i = 0; i < output_count(); i++) {
        HalideBuffer<void> output_buf = output(i)->buffer();
        assert(output_buf.dim(axis_).min() == 0);

        output_buf.translate(axis_, concatenated_i);
        bool copied = requantize_or_copy(input_buf, input()->quantization(), output_buf, output(i)->quantization());
        HCHECK(copied);

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
    const TensorPtr &in = input();
    const TensorPtr &out = output();

    if (in->type() == halide_type_of<uint8_t>()) {
        auto input_buf = in->buffer();
        auto output_buf = out->buffer();

        int input_zero = in->quantization().uniform_zero();
        int output_zero = out->quantization().uniform_zero();

        while (input_buf.dimensions() < 4) {
            input_buf.embed(input_buf.dimensions() - 1, 0);
            output_buf.add_dimension();
        }

        tile_conv_filter_uint8(input_buf, input_zero, output_zero, output_buf);
    } else {
        HLOG(FATAL) << "Unsupported type " << in->type() << "\n";
    }
}

BoundsMap TransposeOp::map_bounds(int input_idx, int output_idx) const {
    assert(output_idx == 0);
    if (input_idx == 0) {
        // TODO: Maybe we can do better here for dimensions that aren't reordered.
        return BoundsMap::all(input(0)->bounds(), output()->rank());
    } else {
        assert(input_idx == 1);
        return BoundsMap::all({Interval(0, output()->rank() - 1)}, output()->rank());
    }
}

void TransposeOp::execute() {
    auto in_buf = input(0)->buffer();
    const auto &dims_buf = input(1)->buffer<const int32_t>();
    auto out_buf = output()->buffer();

    // Adjust the ordering of the output to match the input.
    const int transpose_rank = in_buf.dimensions();
    assert(dims_buf.dim(0).extent() == transpose_rank);
    std::vector<int> order(transpose_rank);
    for (int i = 0; i < dims_buf.dim(0).extent(); i++) {
        order[transpose_rank - 1 - i] = transpose_rank - 1 - dims_buf(i);
    }
    out_buf.transpose(order);

    // Copy the buffers.
    // TODO: This is slow if one of the transposed dimensions is the dimension with stride 1.
    out_buf.copy_from(in_buf);
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
        HLOG(FATAL) << "Unsupported unary op\n";
        return nullptr;
    }
}

void UnaryOp::execute() {
    const TensorPtr &in = input();
    const TensorPtr &out = output();

    if (in->type() == halide_type_of<uint8_t>() && out->type() == halide_type_of<uint8_t>()) {
        auto in_buf = in->buffer();
        auto out_buf = out->buffer();

        const int input_zero = in->quantization().uniform_zero();
        assert(input_zero >= 0 && input_zero <= 255);
        const float in_scale = in->quantization().uniform_scale();

        const int left_shift = 6;

        std::array<int16_t, 64> program_buffer;
        if (op_ == Logistic) {
            IntFloat<int16_t> in_multiplier(in_scale);
            in_multiplier *= power_of_two(-left_shift);
            assert(in_multiplier.exponent() <= 0);

            assert(out->quantization().uniform_scale() == 1.0f / 256.0f);
            assert(out->quantization().uniform_zero() == 0);

            // Build a program to implement the logistic op.
            ElementwiseAssembler p(program_buffer);
            auto input_zeroed = p.sub(p.input(0), input_zero);
            auto input_scaled = p.mul_shift(input_zeroed, in_multiplier.mantissa(), 15 - left_shift);
            auto result = p.logistic(8, input_scaled, -in_multiplier.exponent());
            auto program_buf = p.assemble({result});

            auto logistic_rank2 = [&](halide_buffer_t *in_buf, halide_buffer_t *out_buf) {
                elementwise_5xuint8_1xuint8(in_buf, in_buf, in_buf, in_buf, in_buf, program_buf, out_buf);
            };
            elementwise_loop_nest<2>(logistic_rank2, in_buf, out_buf);
            return;
        } else if (op_ == Tanh) {
            IntFloat<int16_t> in_multiplier(in_scale);
            in_multiplier *= power_of_two(-left_shift);
            assert(in_multiplier.exponent() <= 0);

            assert(out->quantization().uniform_scale() == 1.0f / 128.0f);
            assert(out->quantization().uniform_zero() == 128);

            // Build a program to implement the tanh op.
            ElementwiseAssembler p(program_buffer);
            auto input_zeroed = p.sub(p.input(0), input_zero);
            auto input_scaled = p.mul_shift(input_zeroed, in_multiplier.mantissa(), 15 - left_shift);
            auto result = p.add(p.tanh(7, input_scaled, -in_multiplier.exponent()), 128);
            auto program_buf = p.assemble({result});

            auto tanh_rank2 = [&](halide_buffer_t *in_buf, halide_buffer_t *out_buf) {
                elementwise_5xuint8_1xuint8(in_buf, in_buf, in_buf, in_buf, in_buf, program_buf, out_buf);
            };
            elementwise_loop_nest<2>(tanh_rank2, in_buf, out_buf);
            return;
        } else if (op_ == Negate) {
            add_uint8(in_buf, in->quantization(), -1, in_buf, in->quantization(), 0, out_buf, out->quantization());
            return;
        } else if (op_ == Square) {
            mul_uint8(in_buf, in->quantization(), in_buf, in->quantization(), out_buf, out->quantization());
            return;
        } else if (op_ == Relu || op_ == Relu6 || op_ == ReluN1To1) {
            bool copied = try_requantize(in_buf, in->quantization(), out_buf, out->quantization(), to_activation(op_));
            HCHECK(copied);
            return;
        }
    }
    HLOG(FATAL)
        << "Unsupported unary op " << to_string(op_)
        << " for types " << in->type() << ", " << out->type();
}

BoundsMap UpsampleChannelsOp::map_bounds(int input_idx, int output_idx) const {
    assert(input_idx == 0);
    assert(output_idx == 0);
    int rank = output(output_idx)->rank();
    assert(rank == input(input_idx)->rank());
    return BoundsMap::elementwise(rank).upsample(0, 0, factor_);
}

void UpsampleChannelsOp::execute() {
    const TensorPtr &in = input();
    const TensorPtr &out = output();

    if (in->type() == halide_type_of<uint8_t>() && out->type() == halide_type_of<uint8_t>()) {
        auto in_buf = in->buffer();
        auto out_buf = out->buffer();
        upsample_channels_uint8(in_buf, factor_, out_buf);
        return;
    }
    HLOG(FATAL)
        << "Unsupported UpsampleChannels op for types " << in->type() << ", " << out->type();
}

#define ACCEPT_AND_MUTATE_IMPL(OP)                                  \
    void OP::accept_impl(OpVisitor *v) const {                      \
        v->visit(this);                                             \
    }                                                               \
    Op::OpMutatorFn OP::mutate_impl() const {                       \
        return [](OpPtr op, OpMutator *m) -> OpPtr {                \
            std::unique_ptr<OP> o(static_cast<OP *>(op.release())); \
            return m->visit(std::move(o));                          \
        };                                                          \
    }

ACCEPT_AND_MUTATE_IMPL(BinaryOp)
ACCEPT_AND_MUTATE_IMPL(ConcatenationOp)
ACCEPT_AND_MUTATE_IMPL(ConvOp)
ACCEPT_AND_MUTATE_IMPL(DepthwiseConv2DOp)
ACCEPT_AND_MUTATE_IMPL(ElementwiseProgramOp)
ACCEPT_AND_MUTATE_IMPL(GatherOp)
ACCEPT_AND_MUTATE_IMPL(L2NormalizationOp)
ACCEPT_AND_MUTATE_IMPL(PadOp)
ACCEPT_AND_MUTATE_IMPL(Pool2DOp)
ACCEPT_AND_MUTATE_IMPL(ShapeOp)
ACCEPT_AND_MUTATE_IMPL(SoftmaxOp)
ACCEPT_AND_MUTATE_IMPL(SpaceDepthOp)
ACCEPT_AND_MUTATE_IMPL(SplitOp)
ACCEPT_AND_MUTATE_IMPL(ReductionOp)
ACCEPT_AND_MUTATE_IMPL(ReshapeOp)
ACCEPT_AND_MUTATE_IMPL(TileConvFilterOp)
ACCEPT_AND_MUTATE_IMPL(TransposeOp)
ACCEPT_AND_MUTATE_IMPL(UpsampleChannelsOp)
ACCEPT_AND_MUTATE_IMPL(UnaryOp)

ACCEPT_AND_MUTATE_IMPL(OpGroup)

#undef ACCEPT_AND_MUTATE_IMPL

void OpVisitor::visit(const OpGroup *op) {
    for (int i = 0; i < op->op_count(); i++) {
        op->op(i)->accept(this);
    }
}

OpPtr OpMutator::visit(std::unique_ptr<OpGroup> op) {
    std::vector<TensorPtr> inputs = op->inputs();
    std::vector<TensorPtr> outputs = op->outputs();

    const int old_op_count = op->op_count();

    std::vector<OpPtr> ops_new;
    ops_new.reserve(old_op_count);
    for (int i = 0; i < old_op_count; i++) {
        OpPtr sub_op_old = op->take_op(i);
        assert(sub_op_old != nullptr);
        OpPtr sub_op_new = mutate(std::move(sub_op_old));
        if (sub_op_new != nullptr) {
            ops_new.push_back(std::move(sub_op_new));
        }
    }
    // TODO: we don't bother trying to optimize for an unchanged op here. Is it worthwhile?
    // TODO: verify that inputs and outputs are still correct. Or recalculate from scratch?
    return make_op<OpGroup>(inputs, outputs, std::move(ops_new));
}

}  // namespace hannk
