#include "model.h"
#include "app_util.h"

#include <cmath>
#include <list>

namespace interpret_nn {

size_t sizeof_tensor_type(TensorType t) {
    switch (t) {
    case TensorType::Float32:
        return 4;
    case TensorType::Float16:
        return 2;
    case TensorType::Int32:
        return 4;
    case TensorType::UInt8:
        return 1;
    case TensorType::Int64:
        return 8;
    case TensorType::Int16:
        return 2;
    case TensorType::Complex64:
        return 16;
    case TensorType::Int8:
        return 1;
    case TensorType::Float64:
        return 8;
    case TensorType::Complex128:
        return 32;
    // case TensorType::String:  fallthru
    // case TensorType::Bool:    fallthru
    default:
        APP_FATAL << "Unknown size of type";
        return 0;
    }
}

const char *to_string(TensorType t) {
    switch (t) {
    case TensorType::Float32:
        return "float32";
    case TensorType::Float16:
        return "float16";
    case TensorType::Int32:
        return "int32";
    case TensorType::UInt8:
        return "uint8";
    case TensorType::UInt64:
        return "uint64";
    case TensorType::Int64:
        return "int64";
    case TensorType::Int16:
        return "int16";
    case TensorType::Complex64:
        return "complex64";
    case TensorType::Int8:
        return "int8";
    case TensorType::Float64:
        return "float64";
    case TensorType::Complex128:
        return "complex128";
    case TensorType::String:
        return "string";
    case TensorType::Bool:
        return "bool";
    default:
        APP_FATAL << "Unhandled interpret_nn::TensorType";
        return "";
    }
}

halide_type_t to_halide_type(TensorType t) {
    switch (t) {
    case TensorType::Bool:
        return halide_type_t(halide_type_uint, 1);
    case TensorType::Float16:
        return halide_type_t(halide_type_float, 16);
    case TensorType::Float32:
        return halide_type_t(halide_type_float, 32);
    case TensorType::Float64:
        return halide_type_t(halide_type_float, 64);
    case TensorType::Int16:
        return halide_type_t(halide_type_int, 16);
    case TensorType::Int32:
        return halide_type_t(halide_type_int, 32);
    case TensorType::Int64:
        return halide_type_t(halide_type_int, 64);
    case TensorType::Int8:
        return halide_type_t(halide_type_int, 8);
    case TensorType::UInt8:
        return halide_type_t(halide_type_uint, 8);
    case TensorType::UInt64:
        return halide_type_t(halide_type_uint, 64);

    case TensorType::Complex64:
    case TensorType::Complex128:
    case TensorType::String:
    default:
        APP_FATAL << "Unhandled type in to_halide_type";
        return halide_type_t();
    }
}

Tensor *apply(const TensorMap& map, const Tensor* t) {
    auto i = map.find(t);
    if (i != map.end()) {
        return i->second;
    }
    // TODO: Try to do this without const_cast?
    return const_cast<Tensor*>(t);
}

Model::Model(const Model& copy) {
    // First, just copy all the tensors (shared pointers).
    tensors = copy.tensors;

    // Next, clone the non-allocated tensors. These might get intermediate state
    // while being executed.
    TensorMap map;
    for (auto& i : tensors) {
        if (!i->IsAllocated()) {
            auto cloned = std::make_shared<Tensor>(*i);
            map[i.get()] = cloned.get();
            i = cloned;
        }
    }

    // Now copy the ops, using the tensor map we made above.
    for (const auto& i : copy.ops) {
        ops.push_back(i->Clone(map));
    }
}

void Model::Dump(std::ostream &os) {
    os << "Tensors: " << std::endl;
    for (const auto &i : tensors) {
        i->Dump(os);
    }

    os << "Ops: " << std::endl;
    for (const auto &i : ops) {
        i->Dump(os);
    }
    os << std::endl;
}

void Tensor::Allocate() {
    size_t shape_size = 1;
    for (halide_dimension_t &i : shape_) {
        if (i.stride != 0) {
            APP_CHECK((size_t) i.stride == shape_size);
        } else {
            i.stride = shape_size;
        }
        shape_size *= i.extent;
    }
    shape_size *= sizeof_tensor_type(Type());
    if (data_.empty()) {
        data_.resize(shape_size);
    } else {
        APP_CHECK(data_.size() == shape_size);
    }
}

void Tensor::Dump(std::ostream &os) const {
    os << "  " << to_string(Type()) << " x " << Shape()
       << (IsAllocated() ? " allocated " : " ") << Name() << std::endl;
}

}  // namespace interpret_nn
