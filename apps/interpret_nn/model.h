#ifndef MODEL_H
#define MODEL_H

#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "HalideBuffer.h"
#include "app_util.h"

namespace interpret_nn {

template<class T, class... Args>
std::unique_ptr<T> make_unique(Args &&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

inline std::ostream &operator<<(std::ostream &s,
                                const halide_dimension_t &dim) {
    return s << "{" << dim.min << ", " << dim.extent << ", " << dim.stride << "}";
}

template<typename T>
inline std::ostream &operator<<(std::ostream &s, const std::vector<T> &v) {
    s << "{";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i > 0) {
            s << ", ";
        }
        s << v[i];
    }
    return s << "}";
}

template<typename T>
using HalideBuffer = Halide::Runtime::Buffer<T>;

// TODO: renamed to "TensorType" to avoid some warnings between this and the Type()
// method and to avoid confusion with Halide::Type. Yuck. Need a better name.
enum class TensorType {
    Bool,
    Complex128,
    Complex64,
    Float16,
    Float32,
    Float64,
    Int16,
    Int32,
    Int64,
    Int8,
    String,
    UInt8,
};

size_t SizeOfTensorType(TensorType t);

const char *TensorTypeToString(TensorType t);
halide_type_t TensorTypeToHalideType(TensorType t);

template<typename T>
bool IsType(TensorType t) {
    if (std::is_const<T>::value) {
        return IsType<typename std::remove_const<T>::type>(t);
    }
    return false;
}

template<>
inline bool IsType<void>(TensorType t) {
    return true;
}
template<>
inline bool IsType<float>(TensorType t) {
    return t == TensorType::Float32;
}
template<>
inline bool IsType<int32_t>(TensorType t) {
    return t == TensorType::Int32;
}
template<>
inline bool IsType<uint8_t>(TensorType t) {
    return t == TensorType::UInt8;
}
template<>
inline bool IsType<int64_t>(TensorType t) {
    return t == TensorType::Int64;
}
template<>
inline bool IsType<std::string>(TensorType t) {
    return t == TensorType::String;
}
template<>
inline bool IsType<bool>(TensorType t) {
    return t == TensorType::Bool;
}
template<>
inline bool IsType<int16_t>(TensorType t) {
    return t == TensorType::Int16;
}
template<>
inline bool IsType<int8_t>(TensorType t) {
    return t == TensorType::Int8;
}
template<>
inline bool IsType<double>(TensorType t) {
    return t == TensorType::Float64;
}
// TODO
//template<> inline bool IsType<float16>(TensorType t) { return t == TensorType::Float16; }

struct QuantizationInfo {
    std::vector<float> scale;
    std::vector<int32_t> zero;
    int32_t dimension;
};

inline std::ostream &operator<<(std::ostream &s,
                                const QuantizationInfo &q) {
    return s << "{" << q.scale << ", " << q.zero << ", " << q.dimension << "}";
}

using CropShape = std::vector<std::pair<int, int>>;

inline CropShape WithoutStrides(const std::vector<halide_dimension_t> &shape) {
    CropShape result;
    result.reserve(shape.size());
    for (const halide_dimension_t &i : shape) {
        result.emplace_back(i.min, i.extent);
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

    Tensor(const Tensor& copy) = default;

    interpret_nn::TensorType Type() const {
        return type_;
    }
    const std::string &Name() const {
        return name_;
    }
    const std::vector<halide_dimension_t> &Shape() const {
        return shape_;
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
    HalideBuffer<T> Data(const CropShape &crop) {
        HalideBuffer<T> buf = Data<T>();
        buf.crop(crop);
        return buf;
    }

    template<class T>
    HalideBuffer<const T> Data(const CropShape &crop) const {
        HalideBuffer<const T> buf = Data<T>();
        buf.crop(crop);
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
Tensor *Map(const TensorMap& map, const Tensor* t);

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
    virtual CropShape GetFullCrop() {
        if (OutputCount() == 1) {
            return WithoutStrides(Output(0)->Shape());
        } else {
            APP_FATAL << "More than one output requires GetFullCrop override.";
            return CropShape();
        }
    }

    // Get the bounds required of all inputs and outputs given a crop.
    struct Bounds {
        std::vector<CropShape> inputs;
        std::vector<CropShape> outputs;
    };
    virtual Bounds InferBounds(const CropShape &crop) const = 0;

    // Execute the op on a given crop.
    virtual void Execute(const CropShape &crop) = 0;

    // Given a crop, split the crop into smaller crops appropriate for this op.
    virtual std::vector<CropShape> Split(const CropShape &crop) const {
        return {crop};
    }

    virtual std::unique_ptr<Op> Clone(const TensorMap& tensor_map) const = 0;

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
