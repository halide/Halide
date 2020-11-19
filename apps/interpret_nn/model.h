#ifndef MODEL_H
#define MODEL_H

#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "HalideBuffer.h"
#include "app_util.h"
#include "interval.h"

namespace interpret_nn {

template<class T, class... Args>
std::unique_ptr<T> make_unique(Args &&...args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

template<typename T>
using HalideBuffer = Halide::Runtime::Buffer<T>;

// TODO: renamed to "TensorType" to avoid some warnings between this and the type()
// method and to avoid confusion with Halide::Type. Yuck. Need a better name.
enum class TensorType {
    // Note that these are deliberately ordered and valued to match tflite's
    // similar enum; there is no reason these types *must* have the same values,
    // but as the values arbitrary otherwise, we might as well match.
    Float32 = 0,
    Float16 = 1,
    Int32 = 2,
    UInt8 = 3,
    Int64 = 4,
    String = 5,
    Bool = 6,
    Int16 = 7,
    Complex64 = 8,
    Int8 = 9,
    Float64 = 10,
    Complex128 = 11,
    UInt64 = 12,
};

size_t sizeof_tensor_type(TensorType t);
const char *to_string(TensorType t);
halide_type_t to_halide_type(TensorType t);

template<typename T>
inline TensorType to_tensor_type() {
    if (std::is_const<T>::value) {
        return to_tensor_type<typename std::remove_const<T>::type>();
    }
    APP_FATAL << "Type is not convertible to TensorType";
    // unreachable
}

template<>
inline TensorType to_tensor_type<float>() {
    return TensorType::Float32;
}
template<>
inline TensorType to_tensor_type<int32_t>() {
    return TensorType::Int32;
}
template<>
inline TensorType to_tensor_type<uint8_t>() {
    return TensorType::UInt8;
}
template<>
inline TensorType to_tensor_type<uint64_t>() {
    return TensorType::UInt64;
}
template<>
inline TensorType to_tensor_type<int64_t>() {
    return TensorType::Int64;
}
template<>
inline TensorType to_tensor_type<std::string>() {
    return TensorType::String;
}
template<>
inline TensorType to_tensor_type<bool>() {
    return TensorType::Bool;
}
template<>
inline TensorType to_tensor_type<int16_t>() {
    return TensorType::Int16;
}
template<>
inline TensorType to_tensor_type<int8_t>() {
    return TensorType::Int8;
}
template<>
inline TensorType to_tensor_type<double>() {
    return TensorType::Float64;
}
// TODO
//template<> inline TensorType to_tensor_type<float16>() { return TensorType::Float16; }

template<typename T>
inline bool is_type(TensorType t) {
    return t == to_tensor_type<T>();
}

struct QuantizationInfo {
    std::vector<float> scale;
    std::vector<int32_t> zero;
    int32_t dimension;
};

inline std::ostream &operator<<(std::ostream &s, const QuantizationInfo &q) {
    return s << "{" << q.scale << ", " << q.zero << ", " << q.dimension << "}";
}

inline Box without_strides(const std::vector<halide_dimension_t> &shape) {
    Box result;
    result.reserve(shape.size());
    for (const halide_dimension_t &i : shape) {
        result.emplace_back(i);
    }
    return result;
}

class Tensor {
    std::string name_;
    TensorType type_;
    std::vector<halide_dimension_t> shape_;
    std::vector<uint8_t> data_;
    QuantizationInfo quantization_;
    bool is_constant_;

public:
    explicit Tensor(std::string name, TensorType type,
                    std::vector<halide_dimension_t> shape,
                    std::vector<uint8_t> data, QuantizationInfo quantization)
        : name_(std::move(name)),
          type_(type),
          shape_(std::move(shape)),
          data_(std::move(data)),
          quantization_(std::move(quantization)) {
        is_constant_ = data_.size() != 0;
    }

    Tensor(const Tensor &copy) = default;

    interpret_nn::TensorType type() const {
        return type_;
    }
    const std::string &name() const {
        return name_;
    }
    const std::vector<halide_dimension_t> &shape() const {
        return shape_;
    }
    const halide_dimension_t &dim(int i) const {
        return shape_[i];
    }
    int rank() const {
        return shape_.size();
    }
    const QuantizationInfo &quantization() const {
        return quantization_;
    }
    bool is_constant() const {
        return is_constant_;
    }

    template<class T>
    HalideBuffer<T> data() {
        if (std::is_void<T>::value) {
            return HalideBuffer<T>(
                to_halide_type(type_),
                reinterpret_cast<T *>(data_.data()),
                shape_.size(), shape_.data());
        } else {
            APP_CHECK(is_type<T>(type_));
            return HalideBuffer<T>(
                reinterpret_cast<T *>(data_.data()),
                shape_.size(), shape_.data());
        }
    }

    template<class T>
    HalideBuffer<const T> data() const {
        if (std::is_void<T>::value) {
            return HalideBuffer<const T>(
                to_halide_type(type_),
                reinterpret_cast<const T *>(data_.data()),
                shape_.size(), shape_.data());
        } else {
            APP_CHECK(is_type<T>(type_));
            return HalideBuffer<const T>(
                reinterpret_cast<const T *>(data_.data()),
                shape_.size(), shape_.data());
        }
    }

    template<class T>
    HalideBuffer<T> data(const Box &crop) {
        HalideBuffer<T> buf = data<T>();
        for (int i = 0; i < (int)crop.size(); i++) {
            buf.crop(i, crop[i].min, crop[i].extent());
        }
        return buf;
    }

    template<class T>
    HalideBuffer<const T> data(const Box &crop) const {
        HalideBuffer<const T> buf = data<T>();
        for (int i = 0; i < (int)crop.size(); i++) {
            buf.crop(i, crop[i].min, crop[i].extent());
        }
        return buf;
    }

    bool is_allocated() const {
        return !data_.empty();
    }
    void allocate();
    void free() {
        data_.resize(0);
    }

    void dump(std::ostream &os) const;

    Tensor() = delete;
    Tensor &operator=(const Tensor &) = delete;
    Tensor(Tensor &&) = default;
    Tensor &operator=(Tensor &&) = default;
};

// A mapping from old tensors to new tensors, when cloning an op.
using TensorMap = std::map<const Tensor *, Tensor *>;

// Apply a tensor map to a list of tensors. This is used to support
// cloning ops referring to different tensors.
Tensor *apply(const TensorMap &map, const Tensor *t);

class Op {
protected:
    std::vector<Tensor *> inputs_;
    std::vector<Tensor *> outputs_;

    explicit Op(std::vector<Tensor *> inputs, std::vector<Tensor *> outputs)
        : inputs_(std::move(inputs)), outputs_(std::move(outputs)) {
    }

public:
    virtual ~Op() = default;

    // Get the shape of the complete output of this op.
    virtual Box get_full_crop() {
        if (output_count() == 1) {
            return without_strides(output(0)->shape());
        } else {
            APP_FATAL << "More than one output requires get_full_crop override.";
            return Box();
        }
    }

    // Get the bounds required of all inputs and outputs given a crop.
    struct Bounds {
        std::vector<Box> inputs;
        std::vector<Box> outputs;
    };
    virtual Bounds infer_bounds(const Box &crop) const = 0;

    // Execute the op on a given crop.
    virtual void execute(const Box &crop) = 0;

    // Given a crop, split the crop into smaller crops appropriate for this op.
    virtual std::vector<Box> split(const Box &crop) const {
        return {crop};
    }

    // Clone this op, replacing tensors using the mapping in tensor_map.
    virtual std::unique_ptr<Op> clone(const TensorMap &tensor_map) const = 0;

    virtual void dump(std::ostream &os) const = 0;

    int input_count() const {
        return inputs_.size();
    }
    int output_count() const {
        return outputs_.size();
    }
    const Tensor *input(int idx) const {
        return inputs_[idx];
    }
    const Tensor *output(int idx) const {
        return outputs_[idx];
    }
    const Tensor *input() const {
        return input(0);
    }
    const Tensor *output() const {
        return output(0);
    }
    Tensor *input(int idx) {
        return inputs_[idx];
    }
    Tensor *output(int idx) {
        return outputs_[idx];
    }
    Tensor *input() {
        return input(0);
    }
    Tensor *output() {
        return output(0);
    }

    // Movable but not copyable.
    Op() = delete;
    Op(const Op &) = delete;
    Op &operator=(const Op &) = delete;
    Op(Op &&) = default;
    Op &operator=(Op &&) = default;
};

struct Model {
    std::vector<std::shared_ptr<Tensor>> tensors;
    std::vector<std::unique_ptr<Op>> ops;

    void dump(std::ostream &os);

    // Models can be copied. Tensors that are allocated will be
    // shared, tensors that are not allocated will be cloned.
    Model(const Model &);
    Model() = default;
    Model(Model &&) = default;
    Model &operator=(Model &&) = default;

    Model &operator=(const Model &) = delete;
};

}  // namespace interpret_nn

#endif  // MODEL_H
