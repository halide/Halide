#ifndef INTERPRET_NN_H_
#define INTERPRET_NN_H_

#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "HalideBuffer.h"
#include "halide_app_assert.h"

namespace interpret_nn {

inline std::ostream &operator<<(std::ostream &s,
                                const halide_dimension_t &dim) {
    return s << "{" << dim.min << ", " << dim.extent << ", " << dim.stride << "}";
}

inline std::ostream &operator<<(std::ostream &s,
                                const std::vector<halide_dimension_t> &shape) {
    s << "{";
    for (halide_dimension_t i : shape) {
        s << i << ",";
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

template<typename T>
bool IsType(TensorType t) {
    return IsType<typename std::remove_const<T>::type>(t);
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
        halide_app_assert(IsType<T>(type_));
        return HalideBuffer<T>(
            reinterpret_cast<T *>(data_.data()),
            shape_.size(), shape_.data());
    }

    template<class T>
    HalideBuffer<const T> Data() const {
        halide_app_assert(IsType<T>(type_));
        return HalideBuffer<const T>(reinterpret_cast<const T *>(data_.data()),
                                     shape_.size(), shape_.data());
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

    bool is_allocated() const {
        return !data_.empty();
    }
    void allocate();
    void free() {
        data_.resize(0);
    }
};

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
            halide_app_error << "More than one output requires GetFullCrop override.";
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
    const Tensor *Output() const {
        return Output(0);
    }
    Tensor *Input(int idx) {
        return inputs_[idx];
    }
    Tensor *Output(int idx) {
        return outputs_[idx];
    }
    Tensor *Output() {
        return Output(0);
    }
};

struct Model {
    std::vector<std::unique_ptr<Tensor>> tensors;
    std::vector<std::unique_ptr<Op>> ops;

    void Dump(std::ostream &os);
};

struct ScheduleOptions {
    // How much parallelism to enable.
    int parallelism = 0;

    // How much memory to try to fit the working set into.
    int target_working_set_size_bytes = 0;
};

class ModelInterpreter {
    Model *model_;

    // The schedule is a list of ops with crops to run the ops on.
    struct ScheduledOp {
        Op *op;
        CropShape crop;
    };
    std::vector<ScheduledOp> schedule_;

    // Can ops a and b be reordered with respect to each other?
    static bool CanReorder(const ScheduledOp &a, const ScheduledOp &b);

    // Compute the cost of the memory accesses between a producer from and
    // a consumer to. The cost is related to the size of the memory accessed,
    // and the distance between the regions of memory accessed.
    static float Distance(const ScheduledOp &from, const ScheduledOp &to);

    void ScheduleNaive();
    void Schedule(ScheduleOptions options);

public:
    explicit ModelInterpreter(Model *m,
                              ScheduleOptions options = ScheduleOptions())
        : model_(m) {
        Schedule(options);
    }

    Tensor *GetTensor(const std::string &name) {
        return nullptr;
    }

    void Execute();
};

}  // namespace interpret_nn

#endif  // INTERPRET_NN_H_
