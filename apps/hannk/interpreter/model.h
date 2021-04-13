#ifndef HANNK_MODEL_H
#define HANNK_MODEL_H

#include <iostream>
#include <list>
#include <map>
#include <string>
#include <vector>

#include "HalideBuffer.h"
#include "interval.h"
#include "util/error_util.h"

namespace hannk {

template<class T, class... Args>
std::unique_ptr<T> make_unique(Args &&...args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

template<typename T>
using HalideBuffer = Halide::Runtime::Buffer<T>;

struct QuantizationInfo {
    std::vector<float> scale;
    std::vector<int32_t> zero;
    int32_t dimension = -1;

    bool operator==(const QuantizationInfo &r) const {
        return dimension == r.dimension && scale == r.scale && zero == r.zero;
    }
};

inline std::ostream &operator<<(std::ostream &s, const QuantizationInfo &q) {
    return s << "{" << q.scale << ", " << q.zero << ", " << q.dimension << "}";
}

// Storage for a tensor. This can be shared among several tensors aliasing
// the same memory. All aliases use the strides of the buffer in this storage
// buffer.
class TensorStorage {
    HalideBuffer<void> buffer_;

public:
    TensorStorage();
    TensorStorage(HalideBuffer<void> buffer);
    TensorStorage &operator=(const TensorStorage &) = delete;
    TensorStorage(TensorStorage &&) = default;
    TensorStorage &operator=(TensorStorage &&) = default;

    // Grow the bounds of the storage to accommodate a new user.
    // The type and dimensionality must match the existing storage.
    void add_use(halide_type_t, const Box &bounds);

    halide_type_t type() const {
        return buffer_.type();
    }

    int rank() const {
        return buffer_.dimensions();
    }

    template<class T = void>
    const HalideBuffer<T> &buffer() {
        return buffer_.as<T>();
    }

    template<class T = void>
    const HalideBuffer<const T> &buffer() const {
        return buffer_.as_const().as<const T>();
    }

    bool is_allocated() const;
    void allocate();
};

class Op;
class Tensor;
using TensorPtr = std::shared_ptr<Tensor>;

class Tensor {
    std::string name_;
    HalideBuffer<void> buffer_;
    QuantizationInfo quantization_;
    bool is_constant_ = false;
    bool is_input_ = false;
    bool is_output_ = false;

    // Possibly shared storage for this tensor.
    std::shared_ptr<TensorStorage> storage_;
    // The offset of this tensor into the storage buffer.
    std::vector<int> storage_offset_;

    // A list of ops that use this tensor as an output or an input, respectively.
    std::list<Op *> producers_;
    std::list<Op *> consumers_;

public:
    Tensor() = delete;
    Tensor(std::string name, HalideBuffer<void> buffer, QuantizationInfo quantization = QuantizationInfo());
    Tensor(std::string name, halide_type_t type, const Box &bounds, QuantizationInfo quantization = QuantizationInfo());
    Tensor(const Tensor &copy);
    Tensor(Tensor &&) = default;
    Tensor &operator=(const Tensor &) = delete;
    Tensor &operator=(Tensor &&) = default;

    halide_type_t type() const {
        return buffer_.type();
    }

    const std::string &name() const {
        return name_;
    }

    Box bounds() const {
        const int dimensions = buffer_.dimensions();

        Box result;
        result.reserve(dimensions);
        for (int d = 0; d < dimensions; d++) {
            const auto &dim = buffer_.dim(d);
            result.emplace_back(dim.min(), dim.max());
        }
        return result;
    }

    Interval bounds(int i) const {
        const auto &d = buffer_.dim(i);
        return Interval(d.min(), d.max());
    }

    int extent(int i) const {
        return buffer_.dim(i).extent();
    }

    int rank() const {
        return buffer_.dimensions();
    }

    const QuantizationInfo &quantization() const {
        return quantization_;
    }

    bool is_constant() const {
        return is_constant_;
    }

    void set_constant(bool constant = true) {
        is_constant_ = constant;
    }

    bool is_input() const {
        return is_input_;
    }

    bool is_output() const {
        return is_output_;
    }

    void set_input(bool is_input) {
        is_input_ = is_input;
    }

    void set_output(bool is_output) {
        is_output_ = is_output;
    }

    template<class T = void>
    const HalideBuffer<T> &buffer() {
        return buffer_.as<T>();
    }

    template<class T = void>
    const HalideBuffer<const T> &buffer() const {
        return buffer_.as_const().as<const T>();
    }

    template<class T = void>
    HalideBuffer<T> buffer(const Box &crop) {
        HalideBuffer<T> buf = buffer_.as<T>();
        for (int i = 0; i < (int)crop.size(); i++) {
            buf.crop(i, crop[i].min, crop[i].extent());
        }
        return buf;
    }

    template<class T = void>
    HalideBuffer<const T> buffer(const Box &crop) const {
        HalideBuffer<const T> buf = buffer_.as<const T>();
        for (int i = 0; i < (int)crop.size(); i++) {
            buf.crop(i, crop[i].min, crop[i].extent());
        }
        return buf;
    }

    bool is_allocated() const;
    void allocate();

    std::shared_ptr<TensorStorage> storage();

    void set_alias_of(TensorPtr t, std::vector<int> offset = {});

    void add_consumer(Op *op);
    void add_producer(Op *op);
    void remove_consumer(Op *op);
    void remove_producer(Op *op);

    const std::list<Op *> &producers() const { return producers_; }
    const std::list<Op *> &consumers() const { return consumers_; }

    void replace_all_consumers_with(TensorPtr other);

    void dump(std::ostream &os) const;
};

// A mapping from old tensors to new tensors, when cloning an op.
using TensorMap = std::map<const TensorPtr , TensorPtr >;

// Apply a tensor map to a list of tensors. This is used to support
// cloning ops referring to different tensors.
TensorPtr apply(TensorMap &map, const TensorPtr t);

// Required properties of a crop in a particular dimension.
struct SplitInfo {
    // Required alignment of crops in this dimension.
    int alignment;

    // Minimum extent of crops in this dimension.
    int min;

    // The default is not allowing any splits.
    SplitInfo()
        : SplitInfo(0, 1) {
    }
    SplitInfo(int alignment, int min)
        : alignment(alignment), min(min) {
    }

    static SplitInfo no_split() {
        return SplitInfo(0, 1);
    }
    static SplitInfo any_split() {
        return SplitInfo(1, 1);
    }
    static SplitInfo guard_with_if(int factor) {
        return SplitInfo(factor, 1);
    }
    static SplitInfo shift_inwards(int factor) {
        return SplitInfo(1, factor);
    }
    static SplitInfo round_up(int factor) {
        return SplitInfo(factor, factor);
    }
};

// A mapping from an output x to required input coordinates [min, max].
// [min, max] = x * stride / inv_stride + bounds
struct DimMap {
    int stride;
    int inv_stride;
    Interval bounds;

    DimMap(int stride, int inv_stride, const Interval &bounds)
        : stride(stride), inv_stride(inv_stride), bounds(bounds) {
    }

    Interval evaluate(Interval result) const {
        result *= stride;
        result /= inv_stride;
        result += bounds;
        return result;
    }
    Interval evaluate(int at) const {
        Interval result(at);
        result *= stride;
        result /= inv_stride;
        result += bounds;
        return result;
    }

    bool is_elementwise() const {
        return stride == 1 && inv_stride == 1 && bounds.extent() == 1;
    }

    bool is_upsample() const {
        return stride == 1 && inv_stride > 1;
    }

    bool is_downsample() const {
        return stride > 1 && inv_stride == 1;
    }

    bool is_constant() const {
        return stride == 0;
    }

    // A dependency where the input `bounds` do not depend on the output.
    static DimMap constant(int extent) {
        return DimMap(0, 1, Interval(0, extent - 1));
    }

    static DimMap constant(const Interval &bounds) {
        return DimMap(0, 1, bounds);
    }

    static DimMap elementwise(int offset = 0) {
        return DimMap(1, 1, Interval(offset));
    }

    static DimMap stencil(const Interval &filter) {
        return DimMap(1, 1, filter);
    }

    static DimMap downsample(int factor, const Interval &filter) {
        return DimMap(factor, 1, filter);
    }

    static DimMap upsample(int factor) {
        return DimMap(1, factor, Interval(0, 0));
    }

    static DimMap upsample(int factor, const Interval &filter) {
        return DimMap(1, factor, filter);
    }
};

class BoundsMap {
    int dims_in_;
    int dims_out_;
    std::vector<DimMap> data_;

public:
    BoundsMap(int dims_in, int dims_out)
        : dims_in_(dims_in), dims_out_(dims_out), data_(dims_in * (dims_out + 1), {0, 1, {0, 0}}) {
    }

    DimMap &at(int dim_in, int dim_out) {
        return data_[dim_in * (dims_out_ + 1) + dim_out];
    }
    const DimMap &at(int dim_in, int dim_out) const {
        return data_[dim_in * (dims_out_ + 1) + dim_out];
    }

    DimMap &at(int dim_in) {
        DimMap &result = at(dim_in, dims_out_);
        assert(result.stride == 0);
        return result;
    }
    const DimMap &at(int dim_in) const {
        const DimMap &result = at(dim_in, dims_out_);
        assert(result.stride == 0);
        return result;
    }

    Interval evaluate(int dim_in, const Box &output) const {
        Interval result = at(dim_in).bounds;
        for (int i = 0; i < (int)output.size(); i++) {
            result += at(dim_in, i).evaluate(output[i]);
        }
        return result;
    }

    Box evaluate(const Box &output) const {
        Box input(dims_in_);
        for (int i = 0; i < dims_in_; i++) {
            input[i] = evaluate(i, output);
        }
        return input;
    }

    // Check if this bounds map is solely an elementwise mapping from dim_in to dim_out.
    bool is_elementwise(int dim_in, int dim_out) const {
        bool result = true;
        for (int i = 0; i < dims_out_ + 1; i++) {
            const DimMap &map = at(dim_in, i);
            result = result && (i == dim_out ? map.is_elementwise() : map.is_constant());
        }
        return result;
    }

    bool is_constant(int dim_in, int dim_out) const {
        bool result = true;
        for (int i = 0; i < dims_out_ + 1; i++) {
            const DimMap &map = at(dim_in, i);
            result = result && (i == dim_out ? map.is_elementwise() : map.is_constant());
        }
        return result;
    }

    // Add bounds for an elementwise mapping of x of dim_in to y of dim_out,
    // where x maps to y + offset.
    BoundsMap &elementwise(int dim_in, int dim_out, int offset = 0) {
        at(dim_in, dim_out) = DimMap::elementwise(offset);
        return *this;
    }

    BoundsMap &stencil(int dim_in, int dim_out, const Interval &filter) {
        at(dim_in, dim_out) = DimMap::stencil(filter);
        return *this;
    }

    BoundsMap &upsample(int dim_in, int dim_out, int factor) {
        at(dim_in, dim_out) = DimMap::upsample(factor);
        return *this;
    }

    BoundsMap &upsample(int dim_in, int dim_out, int factor, const Interval &filter) {
        at(dim_in, dim_out) = DimMap::upsample(factor, filter);
        return *this;
    }

    BoundsMap &downsample(int dim_in, int dim_out, int factor, const Interval &filter) {
        at(dim_in, dim_out) = DimMap::downsample(factor, filter);
        return *this;
    }

    BoundsMap &constant(int dim_in, int extent) {
        at(dim_in) = DimMap::constant(extent);
        return *this;
    }

    BoundsMap &constant(int dim_in, const Interval &bounds) {
        at(dim_in) = DimMap::constant(bounds);
        return *this;
    }

    // Producing an element of the output requires the corresponding element of
    // the input.
    static BoundsMap elementwise(int rank) {
        BoundsMap result(rank, rank);
        for (int i = 0; i < rank; i++) {
            result.elementwise(i, i);
        }
        return result;
    }

    // Producing any point of any output dimension requires all of the input.
    static BoundsMap all(const Box &bounds_in, int dims_out) {
        BoundsMap result(bounds_in.size(), dims_out);
        for (int j = 0; j < (int)bounds_in.size(); j++) {
            result.constant(j, bounds_in[j]);
        }
        return result;
    }
};

class OpVisitor;

class Op {
private:
    std::vector<TensorPtr > inputs_;
    std::vector<TensorPtr > outputs_;

protected:
    Op(std::vector<TensorPtr > inputs, std::vector<TensorPtr > outputs);

public:
    virtual ~Op();

    // Get the bounds required of all inputs and outputs given a crop.
    virtual BoundsMap map_bounds(int input_idx, int output_idx) const = 0;
    BoundsMap map_bounds(int input_idx) const {
        if (output_count() == 1) {
            return map_bounds(input_idx, 0);
        } else {
            LOG(FATAL) << "More than one output requires get_full_crop override.";
            return BoundsMap(0, 0);
        }
    }

    // Execute the op on a given crop.
    virtual void execute(const Box &crop) = 0;

    // Get information about how crops of this op can be split.
    virtual std::vector<SplitInfo> get_split_info() const {
        return {};
    }

    // Clone this op, replacing tensors using the mapping in tensor_map.
    virtual std::unique_ptr<Op> clone(TensorMap &tensor_map) const = 0;

    virtual void accept(OpVisitor *v) = 0;

    virtual void dump(std::ostream &os) const = 0;

    int input_count() const {
        return inputs_.size();
    }
    int output_count() const {
        return outputs_.size();
    }
    const TensorPtr input(int idx) const {
        return inputs_[idx];
    }
    const TensorPtr output(int idx) const {
        return outputs_[idx];
    }
    const TensorPtr input() const {
        return input(0);
    }
    const TensorPtr output() const {
        return output(0);
    }
    TensorPtr input(int idx) {
        return inputs_[idx];
    }
    TensorPtr output(int idx) {
        return outputs_[idx];
    }
    TensorPtr input() {
        return input(0);
    }
    TensorPtr output() {
        return output(0);
    }

    void set_input(int idx, TensorPtr t);
    void set_output(int idx, TensorPtr t);
    void set_input(TensorPtr t);
    void set_output(TensorPtr t);

    // Movable but not copyable.
    Op() = delete;
    Op(const Op &) = delete;
    Op &operator=(const Op &) = delete;
    Op(Op &&) = delete;
    Op &operator=(Op &&) = delete;
};

struct Model {
    std::vector<std::unique_ptr<Op>> ops;

    // Add a tensor after an existing tensor.
    void insert(std::unique_ptr<Op> to_insert, const Op *before = nullptr);
    void remove(const Op *op);

    void accept(OpVisitor *v);

    void dump(std::ostream &os);

    // Models can be copied. Tensors that are allocated will be
    // shared, tensors that are not allocated will be cloned.
    Model(const Model &);
    Model() = default;
    Model(Model &&) = default;
    Model &operator=(Model &&) = default;

    Model &operator=(const Model &) = delete;
};

}  // namespace hannk

#endif  // HANNK_MODEL_H
