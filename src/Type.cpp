#include "IR.h"
#include <cfloat>
#include <sstream>

namespace Halide {

using std::ostringstream;

namespace {
uint64_t max_uint(int bits) {
    uint64_t max_val = 0xffffffffffffffffULL;
    return max_val >> (64 - bits);
}

int64_t max_int(int bits) {
    int64_t max_val = 0x7fffffffffffffffLL;
    return max_val >> (64 - bits);
}

int64_t min_int(int bits) {
    return -max_int(bits) - 1;
}

}  // namespace

/** Return an expression which is the maximum value of this type */
Halide::Expr Type::max() const {
    if (is_vector()) {
        return Internal::Broadcast::make(element_of().max(), lanes());
    } else if (is_int()) {
        return Internal::IntImm::make(*this, max_int(bits()));
    } else if (is_uint()) {
        return Internal::UIntImm::make(*this, max_uint(bits()));
    } else {
        internal_assert(is_float());
        if (bits() == 16) {
            return Internal::FloatImm::make(*this, 65504.0);
        } else if (bits() == 32) {
            return Internal::FloatImm::make(*this, std::numeric_limits<float>::infinity());
        } else if (bits() == 64) {
            return Internal::FloatImm::make(*this, std::numeric_limits<double>::infinity());
        } else {
            internal_error
                << "Unknown float type: " << (*this) << "\n";
            return 0;
        }
    }
}

/** Return an expression which is the minimum value of this type */
Halide::Expr Type::min() const {
    if (is_vector()) {
        return Internal::Broadcast::make(element_of().min(), lanes());
    } else if (is_int()) {
        return Internal::IntImm::make(*this, min_int(bits()));
    } else if (is_uint()) {
        return Internal::UIntImm::make(*this, 0);
    } else {
        internal_assert(is_float());
        if (bits() == 16) {
            return Internal::FloatImm::make(*this, -65504.0);
        } else if (bits() == 32) {
            return Internal::FloatImm::make(*this, -std::numeric_limits<float>::infinity());
        } else if (bits() == 64) {
            return Internal::FloatImm::make(*this, -std::numeric_limits<double>::infinity());
        } else {
            internal_error
                << "Unknown float type: " << (*this) << "\n";
            return 0;
        }
    }
}

bool Type::is_max(int64_t x) const {
    return x > 0 && is_max((uint64_t)x);
}

bool Type::is_max(uint64_t x) const {
    if (is_int()) {
        return x == (uint64_t)max_int(bits());
    } else if (is_uint()) {
        return x == max_uint(bits());
    } else {
        return false;
    }
}

bool Type::is_min(int64_t x) const {
    if (is_int()) {
        return x == min_int(bits());
    } else if (is_uint()) {
        return x == 0;
    } else {
        return false;
    }
}

bool Type::is_min(uint64_t x) const {
    return false;
}

bool Type::can_represent(Type other) const {
    if (*this == other) {
        return true;
    }
    if (lanes() != other.lanes()) {
        return false;
    }
    if (is_int()) {
        return ((other.is_int() && other.bits() <= bits()) ||
                (other.is_uint() && other.bits() < bits()));
    } else if (is_uint()) {
        return other.is_uint() && other.bits() <= bits();
    } else if (is_bfloat()) {
        return (other.is_bfloat() && other.bits() <= bits());
    } else if (is_float()) {
        if (other.is_bfloat()) {
            return bits() > other.bits();
        } else {
            return ((other.is_float() && other.bits() <= bits()) ||
                    (bits() == 64 && other.bits() <= 32) ||
                    (bits() == 32 && other.bits() <= 16));
        }
    } else {
        return false;
    }
}

bool Type::can_represent(int64_t x) const {
    if (is_int()) {
        return x >= min_int(bits()) && x <= max_int(bits());
    } else if (is_uint()) {
        return x >= 0 && (uint64_t)x <= max_uint(bits());
    } else if (is_bfloat()) {
        switch (bits()) {
        case 16:
            // Round-trip from int64_t to bfloat16_t and back to see
            // if the value was preserved. This round-tripping must be
            // done via float in both directions, which gives us the
            // following ridiculous chain of casts:
            return (int64_t)(float)(bfloat16_t)(float)x == x;
        default:
            return false;
        }
    } else if (is_float()) {
        switch (bits()) {
        case 16:
            return (int64_t)(float)(float16_t)(float)x == x;
        case 32:
            return (int64_t)(float)x == x;
        case 64:
            return (int64_t)(double)x == x;
        default:
            return false;
        }
    } else {
        return false;
    }
}

bool Type::can_represent(uint64_t x) const {
    if (is_int()) {
        return x <= (uint64_t)(max_int(bits()));
    } else if (is_uint()) {
        return x <= max_uint(bits());
    } else if (is_bfloat()) {
        switch (bits()) {
        case 16:
            return (uint64_t)(float)(bfloat16_t)(float)x == x;
        default:
            return false;
        }
    } else if (is_float()) {
        switch (bits()) {
        case 16:
            return (uint64_t)(float)(float16_t)(float)x == x;
        case 32:
            return (uint64_t)(float)x == x;
        case 64:
            return (uint64_t)(double)x == x;
        default:
            return false;
        }
    } else {
        return false;
    }
}

bool Type::can_represent(double x) const {
    if (is_int()) {
        int64_t i = Internal::safe_numeric_cast<int64_t>(x);
        return (x >= min_int(bits())) && (x <= max_int(bits())) && (x == (double)i);
    } else if (is_uint()) {
        uint64_t u = Internal::safe_numeric_cast<uint64_t>(x);
        return (x >= 0) && (x <= max_uint(bits())) && (x == (double)u);
    } else if (is_bfloat()) {
        switch (bits()) {
        case 16:
            return (double)(bfloat16_t)x == x;
        default:
            return false;
        }
    } else if (is_float()) {
        switch (bits()) {
        case 16:
            return (double)(float16_t)x == x;
        case 32:
            return (double)(float)x == x;
        case 64:
            return true;
        default:
            return false;
        }
    } else {
        return false;
    }
}

bool Type::same_handle_type(const Type &other) const {
    const halide_handle_cplusplus_type *first = handle_type;
    const halide_handle_cplusplus_type *second = other.handle_type;

    if (first == second) {
        return true;
    }

    if (first == nullptr) {
        first = halide_handle_traits<void *>::type_info();
    }
    if (second == nullptr) {
        second = halide_handle_traits<void *>::type_info();
    }

    return first->inner_name == second->inner_name &&
           first->namespaces == second->namespaces &&
           first->enclosing_types == second->enclosing_types &&
           first->cpp_type_modifiers == second->cpp_type_modifiers &&
           first->reference_type == second->reference_type;
}

std::string type_to_c_type(Type type, bool include_space, bool c_plus_plus) {
    bool needs_space = true;
    ostringstream oss;

    if (type.is_bfloat()) {
        oss << "bfloat" << type.bits() << "_t";
    } else if (type.is_float()) {
        if (type.bits() == 32) {
            oss << "float";
        } else if (type.bits() == 64) {
            oss << "double";
        } else {
            oss << "float" << type.bits() << "_t";
        }
        if (type.is_vector()) {
            oss << type.lanes();
        }
    } else if (type.is_handle()) {
        needs_space = false;

        // If there is no type info or is generating C (not C++) and
        // the type is a class or in an inner scope, just use void *.
        if (type.handle_type == nullptr ||
            (!c_plus_plus &&
             (!type.handle_type->namespaces.empty() ||
              !type.handle_type->enclosing_types.empty() ||
              type.handle_type->inner_name.cpp_type_type == halide_cplusplus_type_name::Class))) {
            oss << "void *";
        } else {
            if (type.handle_type->inner_name.cpp_type_type ==
                halide_cplusplus_type_name::Struct) {
                oss << "struct ";
            }

            if (!type.handle_type->namespaces.empty() ||
                !type.handle_type->enclosing_types.empty()) {
                oss << "::";
                for (const auto &ns : type.handle_type->namespaces) {
                    oss << ns << "::";
                }
                for (const auto &enclosing_type : type.handle_type->enclosing_types) {
                    oss << enclosing_type.name << "::";
                }
            }
            oss << type.handle_type->inner_name.name;
            if (type.handle_type->reference_type == halide_handle_cplusplus_type::LValueReference) {
                oss << " &";
            } else if (type.handle_type->reference_type == halide_handle_cplusplus_type::RValueReference) {
                oss << " &&";
            }
            for (auto modifier : type.handle_type->cpp_type_modifiers) {
                if (modifier & halide_handle_cplusplus_type::Const) {
                    oss << " const";
                }
                if (modifier & halide_handle_cplusplus_type::Volatile) {
                    oss << " volatile";
                }
                if (modifier & halide_handle_cplusplus_type::Restrict) {
                    oss << " restrict";
                }
                if ((modifier & halide_handle_cplusplus_type::Pointer) &&
                    !(modifier & halide_handle_cplusplus_type::FunctionTypedef)) {
                    oss << " *";
                }
            }
        }
    } else {
        // This ends up using different type names than OpenCL does
        // for the integer vector types. E.g. uint16x8_t rather than
        // OpenCL's short8. Should be fine as CodeGen_C introduces
        // typedefs for them and codegen always goes through this
        // routine or its override in CodeGen_OpenCL to make the
        // names. This may be the better bet as the typedefs are less
        // likely to collide with built-in types (e.g. the OpenCL
        // ones for a C compiler that decides to compile OpenCL).
        // This code also supports arbitrary vector sizes where the
        // OpenCL ones must be one of 2, 3, 4, 8, 16, which is too
        // restrictive for already existing architectures.
        switch (type.bits()) {
        case 1:
            // bool vectors are always emitted as uint8 in the C++ backend
            if (type.is_vector()) {
                oss << "uint8x" << type.lanes() << "_t";
            } else {
                oss << "bool";
            }
            break;
        default:
            if (type.is_uint()) {
                oss << "u";
            }
            oss << "int" << type.bits();
            if (type.is_vector()) {
                oss << "x" << type.lanes();
            }
            oss << "_t";
        }
    }
    if (include_space && needs_space) {
        oss << " ";
    }
    return oss.str();
}

}  // namespace Halide
