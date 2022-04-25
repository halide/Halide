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

class TensorStorage {
    friend class Tensor;

    HalideBuffer<void> buffer;

    TensorStorage() = delete;
    TensorStorage(const TensorStorage &) = delete;
    TensorStorage &operator=(const TensorStorage &) = delete;
    TensorStorage(TensorStorage &&) = delete;
    TensorStorage &operator=(TensorStorage &&) = delete;

public:
    // std::make_shared doesn't care about friendship, so just make this public.
    TensorStorage(halide_type_t type, int rank, const halide_dimension_t *dimensions);

    size_t storage_size() const;
};
using TensorStoragePtr = std::shared_ptr<TensorStorage>;

enum class AliasType {
    None,
    Offset,    // The aliased Tensors are translated/cropped views of the same buffer.
               // External tensors cannot be in an offset-alias group.
    Reshaped,  // The aliased Tensors share the same host pointer and have identical elem_count but different shapes
               // At most one External tensor can be in a reshape-alias group.
};

class Tensor {
    std::string name_;
    HalideBuffer<void> buffer_;
    QuantizationInfo quantization_;
    // If true, this Tensor should be considered constant and should not be mutated.
    // (It may actually refer to read-only external memory, or it may simply be marked this may as
    // the result of a transform.)
    bool is_constant_ = false;
    // If true, this Tensor's buffer was externally created and must not be modified,
    // (aside from allowing the buffer's dtor to run normally).
    bool is_external_ = false;
    // If true, this Tensor is 'dynamic' (i.e., it's an output whose size
    // is calculated during evaluation, rather than ahead of time).  It is an error
    // for a Tensor to be dynamic if it is also constant or external.
    // Currently only used in conjunction with the TFLite delegate.
    bool is_dynamic_ = false;

    struct AliasInfo {
        // All the Tensors in this list share their TensorStorage with each other.
        // An arbitrary number of Tensors can alias each other, but at most *one* may be external.
        //
        // Note that this list should never have size() of 0 or 1 (the pointer should simply be null).
        std::vector<std::weak_ptr<Tensor>> aliases;
        AliasType alias_type = AliasType::None;

        AliasInfo() = default;

        explicit AliasInfo(const TensorPtr &t, AliasType a)
            : aliases(1, t), alias_type(a) {
        }

        AliasInfo(const AliasInfo &) = delete;
        AliasInfo &operator=(const AliasInfo &) = delete;
        AliasInfo(AliasInfo &&) = delete;
        AliasInfo &operator=(AliasInfo &&) = delete;
    };
    std::shared_ptr<AliasInfo> alias_info_;

    // Possibly shared storage for this tensor.
    TensorStoragePtr storage_;
    // The offset of this tensor into the storage buffer.
    // Only used if is_alias_ = true and is_reshape_alias_ = false.
    // If storage_offset_.size() < rank(), remaining offset entries are implicitly zero.
    TensorOffset storage_offset_;

    // A list of ops that use this tensor as an output or an input, respectively.
    std::list<Op *> producers_;
    std::list<Op *> consumers_;

    void finish_buffer_allocation();

    bool has_external_alias() const;

public:
    Tensor() = delete;
    Tensor(std::string name, HalideBuffer<void> buffer, QuantizationInfo quantization = QuantizationInfo());
    Tensor(std::string name, halide_type_t type, const Box &bounds, QuantizationInfo quantization = QuantizationInfo());

    // Not movable, not copyable
    Tensor(const Tensor &copy) = delete;
    Tensor &operator=(const Tensor &) = delete;
    Tensor(Tensor &&) = delete;
    Tensor &operator=(Tensor &&) = delete;

    // It's public, but since TensorStorage is a blind struct, this can only
    // be used externally to group Tensors that share the same storage.
    TensorStoragePtr storage();

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
        assert(!(external && alias_type() != AliasType::None));
        is_external_ = external;
    }

    // Requires that set_external() has already been called.
    // external_buffer must have the same dimensions, mins, and extents
    // as the current buffer (but the strides need not match).
    // external_buffer must *not* have a null host pointer.
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

    bool is_allocated() const {
        return buffer_.data() != nullptr;
    }
    void allocate_from_heap();
    void allocate_from_arena_pointer(void *host);

    void resize_dynamic(const Box &new_shape);

    AliasType alias_type() const {
        return alias_info_ != nullptr ? alias_info_->alias_type : AliasType::None;
    }

    // Return true iff 'this' can be an alias of 'source' of the given type.
    bool can_alias(const TensorPtr &source, AliasType alias_type) const;

    // Needs to be static so we can pass via shared_ptr (necessary for weak_ptr usage internally)
    static void make_offset_alias(TensorPtr alias, TensorPtr source, const TensorOffset &offset);
    static void make_reshape_alias(TensorPtr alias, TensorPtr source);

    bool is_dense() const;

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

    void dump(std::ostream &os) const;
};

}  // namespace hannk

#endif  // HANNK_TENSOR_H
