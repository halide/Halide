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
#include "interpreter/tensor.h"
#include "util/buffer_util.h"
#include "util/error_util.h"
#include "util/small_vector.h"

namespace hannk {

class Op;
using OpPtr = std::unique_ptr<Op>;

template<class T, class... Args>
std::unique_ptr<T> make_op(Args &&...args) {
    return std::make_unique<T>(std::forward<Args>(args)...);
}

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

    bool is_input(const TensorPtr &t) const;
    bool is_output(const TensorPtr &t) const;

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

    void accept(OpVisitor *v);
    void dump(std::ostream &os) const;
};

}  // namespace hannk

#endif  // HANNK_MODEL_H
