#ifndef HANNK_TENSOR_H
#define HANNK_TENSOR_H

#include <iostream>
#include <list>
#include <string>
#include <vector>

#include "HalideBuffer.h"
#include "util/buffer_util.h"
#include "util/small_vector.h"

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

class Op;

class Tensor;
using TensorPtr = std::shared_ptr<Tensor>;
using TensorOffset = SmallVector<int, max_rank>;
using TensorDimensions = SmallVector<halide_dimension_t, max_rank>;

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
    // If true, this Tensor is 'dynamic' (i.e., it's an output whose size
    // is calculated during evaluation, rather than ahead of time).  It is an error
    // for a Tensor to be dynamic if it is also constant or external.
    // Currently only used in conjunction with the TFLite delegate.
    bool is_dynamic_ = false;

    // A list of ops that use this tensor as an output or an input, respectively.
    std::list<Op *> producers_;
    std::list<Op *> consumers_;

public:
    Tensor() = delete;
    Tensor(std::string name, HalideBuffer<void> buffer, QuantizationInfo quantization = QuantizationInfo());
    Tensor(std::string name, halide_type_t type, const Box &bounds, QuantizationInfo quantization = QuantizationInfo());

    // Not movable, not copyable
    Tensor(const Tensor &copy) = delete;
    Tensor &operator=(const Tensor &) = delete;
    Tensor(Tensor &&) = delete;
    Tensor &operator=(Tensor &&) = delete;

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

    void set_constant(bool constant = true) {
        is_constant_ = constant;
    }

    bool is_external() const {
        return is_external_;
    }

    void set_external(bool external = true) {
        assert(!(external && is_dynamic()));
        is_external_ = external;
    }

    void set_external_host(void *host);

    void set_external_buffer(HalideBuffer<void> external_buffer);

    bool is_dynamic() const {
        return is_dynamic_;
    }

    void set_dynamic(bool dynamic = true) {
        assert(!(dynamic && (is_constant() || is_external())));
        is_dynamic_ = dynamic;
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

}  // namespace hannk

#endif  // HANNK_TENSOR_H
