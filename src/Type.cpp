#include "ConstantBounds.h"
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

bool StructField::operator==(const StructField &other) const {
    return name == other.name && type == other.type && array_extent == other.array_extent;
}

bool StructField::operator<(const StructField &other) const {
    if (name != other.name) {
        return name < other.name;
    }
    if (type != other.type) {
        return type < other.type;
    }
    return array_extent < other.array_extent;
}

int StructTypeInfo::find_field(const std::string &name) const {
    for (size_t i = 0; i < fields.size(); i++) {
        if (fields[i].name == name) {
            return (int)i;
        }
    }
    return -1;
}

int Type::bytes() const {
    if (is_struct()) {
        return struct_type->total_bytes;
    }
    return (bits() + 7) / 8;
}

Type Type::Struct(const std::vector<StructField> &fields) {
    user_assert(!fields.empty()) << "Type::Struct requires at least one field.\n";

    // Deliberately leaked: like halide_handle_cplusplus_type, this side table
    // must remain valid for the lifetime of the program, since Types
    // (including this pointer) get freely copied and compared throughout
    // compilation.
    auto *info = new StructTypeInfo();
    info->fields = fields;
    info->offsets.reserve(fields.size());

    int offset = 0;
    for (const auto &f : fields) {
        user_assert(!f.type.is_struct() || f.type.struct_type != nullptr)
            << "Struct field \"" << f.name << "\" has an invalid nested struct type.\n";
        user_assert(f.array_extent.value_or(1) > 0)
            << "Struct field \"" << f.name << "\" has a non-positive array extent.\n";
        info->offsets.push_back(offset);
        offset += f.type.bytes() * f.array_extent.value_or(1);
    }
    info->total_bytes = offset;

    Type t(Type::UInt, 8, 1);
    t.struct_type = info;
    return t;
}

bool Type::same_struct_type(const Type &other) const {
    const StructTypeInfo *a = struct_type;
    const StructTypeInfo *b = other.struct_type;

    if (a == b) {
        return true;
    }
    if (a == nullptr || b == nullptr) {
        return false;
    }
    return a->fields == b->fields;
}

bool Type::operator<(const Type &other) const {
    if (type < other.type) {
        return true;
    }
    if (code() == Handle) {
        return handle_type < other.handle_type;
    }
    if (is_struct() || other.is_struct()) {
        if (is_struct() != other.is_struct()) {
            // One is a struct type and the other isn't (despite having the
            // same halide_type_t tag, since a struct's tag is plain
            // UInt(8)): order by pointer identity. This can't disagree with
            // operator== (which always returns false for a
            // struct-vs-non-struct pair), so any consistent order is fine.
            return struct_type < other.struct_type;
        }
        if (same_struct_type(other)) {
            // Consistent with operator==: two struct types with identical
            // field lists always compare as neither-less-than-the-other,
            // even if they're backed by different (independently
            // allocated) StructTypeInfo side tables.
            return false;
        }
        return struct_type->fields < other.struct_type->fields;
    }
    return false;
}

/** Return an expression which is the maximum value of this type */
Halide::Expr Type::max() const {
    user_assert(!is_struct()) << "Type::max() is not defined for a struct type: " << *this << "\n";
    if (is_vector()) {
        return Internal::Broadcast::make(element_of().max(), lanes());
    } else if (is_int()) {
        return Internal::IntImm::make(*this, max_int(bits()));
    } else if (is_uint()) {
        return Internal::UIntImm::make(*this, max_uint(bits()));
    } else {
        internal_assert(is_float()) << "Type::max() is not defined for " << *this << "\n";
        if (bits() == 16) {
            return Internal::FloatImm::make(*this, (double)float16_t::make_infinity());
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
    user_assert(!is_struct()) << "Type::min() is not defined for a struct type: " << *this << "\n";
    if (is_vector()) {
        return Internal::Broadcast::make(element_of().min(), lanes());
    } else if (is_int()) {
        return Internal::IntImm::make(*this, min_int(bits()));
    } else if (is_uint()) {
        return Internal::UIntImm::make(*this, 0);
    } else {
        internal_assert(is_float()) << "Type::min() is not defined for " << *this << "\n";
        if (bits() == 16) {
            return Internal::FloatImm::make(*this, (double)float16_t::make_negative_infinity());
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
    if (is_struct()) {
        return false;
    } else if (is_int()) {
        return x == (uint64_t)max_int(bits());
    } else if (is_uint()) {
        return x == max_uint(bits());
    } else {
        return false;
    }
}

bool Type::is_min(int64_t x) const {
    if (is_struct()) {
        return false;
    } else if (is_int()) {
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
    if (is_struct() || other.is_struct()) {
        // Two non-identical struct types (or a struct and a non-struct)
        // never "represent" each other, regardless of what is_uint()/bits()
        // report for the struct's erased ABI tag.
        return false;
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
            return (other.is_float() && other.bits() <= bits()) ||
                   (bits() == 64 && other.bits() <= 32) ||
                   (bits() == 32 && other.bits() <= 16) ||
                   (bits() == 16 && other.bits() <= 8);
        }
    } else {
        return false;
    }
}

bool Type::can_represent(const Internal::ConstantInterval &in) const {
    return in.is_bounded() && can_represent(in.min) && can_represent(in.max);
}

bool Type::can_represent(int64_t x) const {
    if (is_struct()) {
        return false;
    } else if (is_int()) {
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
    if (is_struct()) {
        return false;
    } else if (is_int()) {
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
    if (is_struct()) {
        return false;
    } else if (is_int()) {
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
