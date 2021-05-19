#ifndef HANNK_MODEL_H
#define HANNK_MODEL_H

#include <array>
#include <iostream>
#include <list>
#include <map>
#include <string>
#include <vector>

#include "HalideBuffer.h"
#include "interpreter/interval.h"
#include "util/buffer_util.h"
#include "util/error_util.h"

namespace hannk {

struct QuantizationInfo {
    std::vector<float> scale;
    std::vector<int32_t> zero;
    int32_t dimension = -1;

    bool operator==(const QuantizationInfo &r) const {
        return dimension == r.dimension && scale == r.scale && zero == r.zero;
    }

    float uniform_scale() const {
        assert(scale.size() == 1);
        return scale[0];
    }

    int32_t uniform_zero() const {
        assert(zero.size() == 1);
        return zero[0];
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
    explicit TensorStorage(HalideBuffer<void> buffer);
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
using OpPtr = std::unique_ptr<Op>;
using TensorPtr = std::shared_ptr<Tensor>;

template<class T, class... Args>
std::unique_ptr<T> make_op(Args &&...args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

class Tensor {
    std::string name_;
    HalideBuffer<void> buffer_;
    QuantizationInfo quantization_;
    // If true, this Tensor should be considered constant and should not be mutated.
    // (It may actually refer to read-only external memory, or it may simply be marked this may as
    // the result of a transform.)
    bool is_constant_ = false;
    // If true, this Tensor's storage is externally owned and must not be freed.
    bool is_external_ = false;
    // If true, the Tensor is one of the inputs to the Model.
    bool is_input_ = false;
    // If true, the Tensor is one of the outputs to the Model.
    bool is_output_ = false;
    // If true, this Tensor is 'dynamic' (i.e., it's an output whose size
    // is calculated during evaluation, rather than ahead of time).  It is an error
    // for a Tensor to be dynamic if it is also constant.
    // Currently only used in conjunction with the TFLite delegate.
    bool is_dynamic_ = false;

    // If both is_external_ and is_dynamic_ are true, this function is used by resize() to
    // reallocate the buffer storage. It returns the new host pointer to be used (or null
    // in the event of an error). Note that this is a 'single-shot' method: it
    // should only be used a single time, and nulled out after use, as it may have
    // captured state that isn't guaranteed to outlast the next call to Interpreter::execute().
    std::function<void *(const Box &new_shape)> external_dynamic_resizer_fn_;

    // Possibly shared storage for this tensor.
    std::shared_ptr<TensorStorage> storage_;
    // The offset of this tensor into the storage buffer.
    SmallVector<int, max_rank> storage_offset_;

    // A list of ops that use this tensor as an output or an input, respectively.
    std::list<Op *> producers_;
    std::list<Op *> consumers_;

    std::shared_ptr<TensorStorage> storage();

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

    int number_of_elements() const {
        return buffer_.number_of_elements();
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

    void set_constant() {
        assert(!is_dynamic());
        is_constant_ = true;
    }

    bool is_external() const {
        return is_external_;
    }

    void set_external() {
        is_external_ = true;
    }

    void set_external_host(void *host);
    void set_external_dynamic_resizer(const std::function<void *(const Box &new_shape)> &fn);

    bool is_dynamic() const {
        return is_dynamic_;
    }

    void set_dynamic() {
        assert(!is_constant());
        is_dynamic_ = true;
    }

    bool is_input() const {
        return is_input_;
    }

    bool is_output() const {
        return is_output_;
    }

    void set_input() {
        is_input_ = true;
    }

    void set_output() {
        is_output_ = true;
    }

    template<class T = void>
    const HalideBuffer<T> &buffer() {
        return buffer_.as<T>();
    }

    template<class T = void>
    const HalideBuffer<const T> &buffer() const {
        return buffer_.as_const().as<const T>();
    }

    halide_buffer_t *raw_buffer() {
        return buffer_.raw_buffer();
    }

    bool is_allocated() const;
    void allocate();

    void resize(const Box &new_shape);

    bool is_alias() const;
    void set_alias_of(const TensorPtr &t, const SmallVector<int, max_rank> &offset = {});

    void add_consumer(Op *op);
    void add_producer(Op *op);
    void remove_consumer(Op *op);
    void remove_producer(Op *op);

    const std::list<Op *> &producers() const {
        return producers_;
    }
    const std::list<Op *> &consumers() const {
        return consumers_;
    }

    void replace_all_consumers_with(const TensorPtr &other);

    void dump(std::ostream &os) const;
};

// A mapping from old tensors to new tensors, when cloning an op.
using TensorMap = std::map<const TensorPtr, TensorPtr>;

// Apply a tensor map to a list of tensors. This is used to support
// cloning ops referring to different tensors.
const TensorPtr &apply(TensorMap &map, const TensorPtr &t);

// A mapping from an output x to required input coordinates [min, max].
// [min, max] = (x / inv_stride) * stride + bounds
struct DimMap {
    Interval pre_bounds;
    int stride;
    int inv_stride;
    Interval bounds;

    DimMap()
        : pre_bounds(0, 0), stride(0), inv_stride(1), bounds(0, -1) {
    }
    DimMap(int stride, int inv_stride, const Interval &bounds)
        : pre_bounds(0, 0), stride(stride), inv_stride(inv_stride), bounds(bounds) {
    }

    Interval evaluate(Interval x) const {
        Interval result = x;
        result += pre_bounds;
        result /= inv_stride;
        result *= stride;
        result += bounds;
        return result;
    }
    Interval evaluate(int at) const {
        return evaluate(Interval(at));
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
    DimMap &constant(const Interval &bounds) {
        stride = 0;
        inv_stride = 1;
        this->bounds = bounds;
        return *this;
    }

    DimMap &constant(int extent) {
        return constant(Interval(0, extent - 1));
    }

    DimMap &downsample(int factor, const Interval &filter) {
        stride = factor;
        inv_stride = 1;
        bounds = filter;
        return *this;
    }

    DimMap &upsample(int factor, const Interval &filter) {
        stride = 1;
        inv_stride = factor;
        bounds = filter;
        return *this;
    }

    DimMap &downsample(int factor) {
        return downsample(factor, Interval(0, 0));
    }

    DimMap &upsample(int factor) {
        return upsample(factor, Interval(0, 0));
    }

    DimMap &elementwise(int offset = 0) {
        return upsample(1);
    }

    DimMap &stencil(const Interval &filter) {
        return upsample(1, filter);
    }

    DimMap &align(int alignment) {
        pre_bounds.max += alignment - 1;
        stride *= alignment;
        inv_stride *= alignment;
        bounds.min = align_down(bounds.min, alignment);
        bounds.max = align_up(bounds.max + 1, alignment) - 1;
        return *this;
    }
};

class BoundsMap {
    int dims_in_;
    int dims_out_;
    DimMap data_[max_rank * (max_rank + 1)];

public:
    BoundsMap(int dims_in, int dims_out)
        : dims_in_(dims_in), dims_out_(dims_out) {
        assert(dims_in <= max_rank);
        assert(dims_out <= max_rank);
    }

    DimMap &at(int dim_in, int dim_out) {
        return data_[dim_in * (max_rank + 1) + dim_out];
    }
    const DimMap &at(int dim_in, int dim_out) const {
        return data_[dim_in * (max_rank + 1) + dim_out];
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
            result = Union(result, at(dim_in, i).evaluate(output[i]));
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
        at(dim_in, dim_out).elementwise(offset);
        return *this;
    }

    BoundsMap &stencil(int dim_in, int dim_out, const Interval &filter) {
        at(dim_in, dim_out).stencil(filter);
        return *this;
    }

    BoundsMap &upsample(int dim_in, int dim_out, int factor) {
        at(dim_in, dim_out).upsample(factor);
        return *this;
    }

    BoundsMap &upsample(int dim_in, int dim_out, int factor, const Interval &filter) {
        at(dim_in, dim_out).upsample(factor, filter);
        return *this;
    }

    BoundsMap &downsample(int dim_in, int dim_out, int factor) {
        at(dim_in, dim_out).downsample(factor);
        return *this;
    }

    BoundsMap &downsample(int dim_in, int dim_out, int factor, const Interval &filter) {
        at(dim_in, dim_out).downsample(factor, filter);
        return *this;
    }

    BoundsMap &constant(int dim_in, int extent) {
        at(dim_in).constant(extent);
        return *this;
    }

    BoundsMap &constant(int dim_in, const Interval &bounds) {
        at(dim_in).constant(bounds);
        return *this;
    }

    BoundsMap &align_input(int dim_in, int alignment) {
        for (int i = 0; i < dims_out_ + 1; i++) {
            at(dim_in, i).align(alignment);
        }
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
    std::vector<TensorPtr> inputs_;
    std::vector<TensorPtr> outputs_;

protected:
    Op(std::vector<TensorPtr> inputs, std::vector<TensorPtr> outputs);

public:
    virtual ~Op();

    // Get the bounds required of all inputs and outputs given a crop.
    virtual BoundsMap map_bounds(int input_idx, int output_idx) const = 0;
    BoundsMap map_bounds(int input_idx) const {
        if (output_count() == 1) {
            return map_bounds(input_idx, 0);
        } else {
            HLOG(FATAL) << "More than one output requires get_full_crop override.";
            return BoundsMap(0, 0);
        }
    }

    // Execute the op on a given crop.
    virtual void execute() = 0;

    // Clone this op, replacing tensors using the mapping in tensor_map.
    virtual OpPtr clone(TensorMap &tensor_map) const = 0;

    virtual void accept(OpVisitor *v) = 0;

    virtual void dump(std::ostream &os) const = 0;

    int input_count() const {
        return inputs_.size();
    }
    int output_count() const {
        return outputs_.size();
    }
    const TensorPtr &input(int idx) const {
        return inputs_[idx];
    }
    const TensorPtr &output(int idx) const {
        return outputs_[idx];
    }
    const TensorPtr &input() const {
        return input(0);
    }
    const TensorPtr &output() const {
        return output(0);
    }
    const TensorPtr &input(int idx) {
        return inputs_[idx];
    }
    const TensorPtr &output(int idx) {
        return outputs_[idx];
    }
    const TensorPtr &input() {
        return input(0);
    }
    const TensorPtr &output() {
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

class OpGroup : public Op {
    std::vector<OpPtr> ops_;

public:
    OpGroup(std::vector<TensorPtr> inputs, std::vector<TensorPtr> outputs, std::vector<OpPtr> ops = {})
        : Op(std::move(inputs), std::move(outputs)), ops_(std::move(ops)) {
    }

    void add(OpPtr to_insert, const Op *before = nullptr);
    void remove(const Op *op);

    BoundsMap map_bounds(int input_idx, int output_idx) const;

    void execute();

    int op_count() const {
        return ops_.size();
    }
    Op *op(int i) {
        return ops_[i].get();
    }
    const Op *op(int i) const {
        return ops_[i].get();
    }

    OpPtr clone(TensorMap &tensor_map) const;
    void accept(OpVisitor *v);
    void dump(std::ostream &os) const;
};

}  // namespace hannk

#endif  // HANNK_MODEL_H
