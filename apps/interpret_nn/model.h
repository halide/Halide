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

// TODO: renamed to "TensorType" to avoid some warnings between this and the Type()
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

size_t SizeOfTensorType(TensorType t);

const char *TensorTypeToString(TensorType t);
halide_type_t TensorTypeToHalideType(TensorType t);

template<typename T>
TensorType ToTensorType() {
    if (std::is_const<T>::value) {
        return ToTensorType<typename std::remove_const<T>::type>();
    }
    APP_FATAL << "Type is not convertible to TensorType";
    // unreachable
}

template<>
inline TensorType ToTensorType<float>() {
    return TensorType::Float32;
}
template<>
inline TensorType ToTensorType<int32_t>() {
    return TensorType::Int32;
}
template<>
inline TensorType ToTensorType<uint8_t>() {
    return TensorType::UInt8;
}
template<>
inline TensorType ToTensorType<uint64_t>() {
    return TensorType::UInt64;
}
template<>
inline TensorType ToTensorType<int64_t>() {
    return TensorType::Int64;
}
template<>
inline TensorType ToTensorType<std::string>() {
    return TensorType::String;
}
template<>
inline TensorType ToTensorType<bool>() {
    return TensorType::Bool;
}
template<>
inline TensorType ToTensorType<int16_t>() {
    return TensorType::Int16;
}
template<>
inline TensorType ToTensorType<int8_t>() {
    return TensorType::Int8;
}
template<>
inline TensorType ToTensorType<double>() {
    return TensorType::Float64;
}
// TODO
//template<> inline TensorType ToTensorType<float16>() { return TensorType::Float16; }

template<typename T>
inline bool IsType(TensorType t) {
    return t == ToTensorType<T>();
}

struct QuantizationInfo {
    std::vector<float> scale;
    std::vector<int32_t> zero;
    int32_t dimension;
};

inline std::ostream &operator<<(std::ostream &s,
                                const QuantizationInfo &q) {
    return s << "{" << q.scale << ", " << q.zero << ", " << q.dimension << "}";
}

inline Box WithoutStrides(const std::vector<halide_dimension_t> &shape) {
    Box result;
    result.reserve(shape.size());
    for (const halide_dimension_t &i : shape) {
        result.emplace_back(i.min, i.min + i.extent - 1);
    }
    return result;
}

class Tensor {
    std::string name_;
    TensorType type_;
    std::vector<halide_dimension_t> shape_;
    std::vector<uint8_t> data_;
    QuantizationInfo quantization_;

public:
    explicit Tensor(std::string name, TensorType type,
                    std::vector<halide_dimension_t> shape,
                    std::vector<uint8_t> data, QuantizationInfo quantization)
        : name_(std::move(name)),
          type_(type),
          shape_(std::move(shape)),
          data_(std::move(data)),
          quantization_(std::move(quantization)) {
    }

    Tensor(const Tensor &copy) = default;

    interpret_nn::TensorType Type() const {
        return type_;
    }
    const std::string &Name() const {
        return name_;
    }
    const std::vector<halide_dimension_t> &Shape() const {
        return shape_;
    }
    const halide_dimension_t &Dim(int i) const {
        return shape_[i];
    }
    int Rank() const {
        return shape_.size();
    }
    const QuantizationInfo &Quantization() const {
        return quantization_;
    }

    template<class T>
    HalideBuffer<T> Data() {
        if (std::is_void<T>::value) {
            return HalideBuffer<T>(
                TensorTypeToHalideType(type_),
                reinterpret_cast<T *>(data_.data()),
                shape_.size(), shape_.data());
        } else {
            APP_CHECK(IsType<T>(type_));
            return HalideBuffer<T>(
                reinterpret_cast<T *>(data_.data()),
                shape_.size(), shape_.data());
        }
    }

    template<class T>
    HalideBuffer<const T> Data() const {
        if (std::is_void<T>::value) {
            return HalideBuffer<const T>(
                TensorTypeToHalideType(type_),
                reinterpret_cast<const T *>(data_.data()),
                shape_.size(), shape_.data());
        } else {
            APP_CHECK(IsType<T>(type_));
            return HalideBuffer<const T>(
                reinterpret_cast<const T *>(data_.data()),
                shape_.size(), shape_.data());
        }
    }

    template<class T>
    HalideBuffer<T> Data(const Box &crop) {
        HalideBuffer<T> buf = Data<T>();
        for (int i = 0; i < (int)crop.size(); i++) {
            buf.crop(i, crop[i].min, crop[i].extent());
        }
        return buf;
    }

    template<class T>
    HalideBuffer<const T> Data(const Box &crop) const {
        HalideBuffer<const T> buf = Data<T>();
        for (int i = 0; i < (int)crop.size(); i++) {
            buf.crop(i, crop[i].min, crop[i].extent());
        }
        return buf;
    }

    bool IsAllocated() const {
        return !data_.empty();
    }
    void Allocate();
    void Free() {
        data_.resize(0);
    }

    void Dump(std::ostream &os) const;

    Tensor() = delete;
    Tensor &operator=(const Tensor &) = delete;
    Tensor(Tensor &&) = default;
    Tensor &operator=(Tensor &&) = default;
};

// A mapping from old tensors to new tensors, when cloning an op.
using TensorMap = std::map<const Tensor *, Tensor *>;

// Apply a tensor map to a list of tensors. This is used to support
// cloning ops referring to different tensors.
Tensor *Map(const TensorMap &map, const Tensor *t);

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
    virtual Box GetFullCrop() {
        if (OutputCount() == 1) {
            return WithoutStrides(Output(0)->Shape());
        } else {
            APP_FATAL << "More than one output requires GetFullCrop override.";
            return Box();
        }
    }

    // Get the bounds required of all inputs and outputs given a crop.
    struct Bounds {
        std::vector<Box> inputs;
        std::vector<Box> outputs;
    };
    virtual Bounds InferBounds(const Box &crop) const = 0;

    // Execute the op on a given crop.
    virtual void Execute(const Box &crop) = 0;

    // Given a crop, split the crop into smaller crops appropriate for this op.
    virtual std::vector<Box> Split(const Box &crop) const {
        return {crop};
    }

    virtual std::unique_ptr<Op> Clone(const TensorMap &tensor_map) const = 0;

    virtual void Dump(std::ostream &os) const = 0;

    int InputCount() const {
        return inputs_.size();
    }
    int OutputCount() const {
        return outputs_.size();
    }
    const Tensor *Input(int idx) const {
        return inputs_[idx];
    }
    const Tensor *Output(int idx) const {
        return outputs_[idx];
    }
    const Tensor *Input() const {
        return Input(0);
    }
    const Tensor *Output() const {
        return Output(0);
    }
    Tensor *Input(int idx) {
        return inputs_[idx];
    }
    Tensor *Output(int idx) {
        return outputs_[idx];
    }
    Tensor *Input() {
        return Input(0);
    }
    Tensor *Output() {
        return Output(0);
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

    void Dump(std::ostream &os);

    // Models can be copied. Tensors that are allocated will be
    // shared, tensors that are not allocated will be cloned.
    Model(const Model &);

    Model() = default;
    Model &operator=(const Model &) = delete;
    Model(Model &&) = default;
    Model &operator=(Model &&) = default;
};

}  // namespace interpret_nn

#endif  // MODEL_H
