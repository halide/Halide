#include "ConstantBounds.h"
#include "IR.h"
#include <cfloat>
#include <map>
#include <mutex>
#include <sstream>
#include <tuple>

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
        const StructTypeInfo *info = struct_type();
        return info ? info->total_bytes : 0;
    }
    return (bits() + 7) / 8;
}

Type Type::Struct(const std::vector<StructField> &fields) {
    user_assert(!fields.empty()) << "Type::Struct requires at least one field.\n";

    // Deliberately leaked: like halide_handle_cplusplus_type, this info table
    // must remain valid for the lifetime of the program, since the intern
    // table holds a (non-owning) pointer to it and Types get freely copied
    // and compared throughout compilation.
    auto *info = new StructTypeInfo();
    info->fields = fields;
    info->offsets.reserve(fields.size());

    int offset = 0;
    for (const auto &f : fields) {
        user_assert(!f.type.is_struct() || f.type.struct_type() != nullptr)
            << "Struct field \"" << f.name << "\" has an invalid nested struct type.\n";
        user_assert(f.array_extent.value_or(1) > 0)
            << "Struct field \"" << f.name << "\" has a non-positive array extent.\n";
        info->offsets.push_back(offset);
        offset += f.type.bytes() * f.array_extent.value_or(1);
    }
    info->total_bytes = offset;

    // A struct has its own honest type code; its byte size lives in the interned
    // StructTypeInfo (and is carried in the ABI's reserved field, see to_abi()).
    // type_bits is not meaningful for a struct -- it's set to a byte's worth so
    // the erased ABI tag is well-formed, but bytes() is the real size.
    Type t(StructKind, 8, 1);
    t.metadata_index_ = Internal::intern_struct_type(info);
    return t;
}

bool Type::same_struct_type(const Type &other) const {
    const StructTypeInfo *a = struct_type();
    const StructTypeInfo *b = other.struct_type();

    if (a == b) {
        return true;
    }
    if (a == nullptr || b == nullptr) {
        return false;
    }
    return a->fields == b->fields;
}

bool Type::operator<(const Type &other) const {
    if (std::tie(type_code, type_bits, type_lanes) <
        std::tie(other.type_code, other.type_bits, other.type_lanes)) {
        return true;
    }
    if (std::tie(other.type_code, other.type_bits, other.type_lanes) <
        std::tie(type_code, type_bits, type_lanes)) {
        return false;
    }
    if (code() == Handle) {
        return handle_type() < other.handle_type();
    }
    if (is_struct()) {
        // Equal (type_code, type_bits, type_lanes) above already implies both
        // are structs here, so we only order by field layout.
        if (same_struct_type(other)) {
            // Consistent with operator==: two struct types with identical
            // field lists always compare as neither-less-than-the-other, even
            // if they're backed by different interned StructTypeInfo tables.
            return false;
        }
        return struct_type()->fields < other.struct_type()->fields;
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

namespace Internal {
namespace {

/** A process-wide table that maps externally-owned, program-lifetime metadata
 * pointers (handle-type descriptors, struct-type layouts) to small 1-based
 * indices, so a `Type` can reference them in 4 bytes instead of an 8-byte
 * inline pointer. Index 0 is reserved to mean "none". Pointers are deduped by
 * identity (the pointees are stable, and distinct pointers with equal content
 * are reconciled by the deep comparisons in Type). Entries are never removed. */
template<typename T>
class InternTable {
    std::mutex mutex;
    std::vector<const T *> table;

public:
    uint32_t intern(const T *ptr) {
        if (ptr == nullptr) {
            return 0;
        }
        std::scoped_lock lock(mutex);
        for (size_t i = 0; i < table.size(); i++) {
            if (table[i] == ptr) {
                return (uint32_t)(i + 1);  // +1: index 0 is reserved for null
            }
        }
        table.push_back(ptr);
        internal_assert(table.size() < (size_t)0xffffffffu) << "intern table overflow";
        return (uint32_t)table.size();
    }

    const T *get(uint32_t index) {
        if (index == 0) {
            return nullptr;
        }
        std::scoped_lock lock(mutex);
        internal_assert(index <= table.size()) << "invalid intern index";
        return table[index - 1];
    }
};

InternTable<halide_handle_cplusplus_type> &handle_type_intern_table() {
    static InternTable<halide_handle_cplusplus_type> t;
    return t;
}

InternTable<StructTypeInfo> &struct_type_intern_table() {
    static InternTable<StructTypeInfo> t;
    return t;
}

}  // namespace

uint32_t intern_handle_type(const halide_handle_cplusplus_type *handle_type) {
    // The overwhelmingly common case (a non-handle type) is the nullptr fast
    // path inside intern(), which costs just a null check and no lock.
    return handle_type_intern_table().intern(handle_type);
}

const halide_handle_cplusplus_type *get_interned_handle_type(uint32_t index) {
    return handle_type_intern_table().get(index);
}

uint32_t intern_struct_type(const StructTypeInfo *struct_type) {
    internal_assert(struct_type != nullptr) << "intern_struct_type(nullptr)";
    return struct_type_intern_table().intern(struct_type);
}

const StructTypeInfo *get_interned_struct_type(uint32_t index) {
    return struct_type_intern_table().get(index);
}

uint32_t intern_opaque_struct_type(int total_bytes) {
    // Reconstructing a struct Type from its ABI form recovers only the size,
    // not the field layout. Memoize one field-opaque StructTypeInfo per size so
    // repeated round-trips (e.g. Buffer::type()) don't leak a table entry each.
    static std::mutex m;
    static std::map<int, const StructTypeInfo *> by_size{};
    const StructTypeInfo *info = nullptr;
    {
        std::scoped_lock lock(m);
        auto it = by_size.find(total_bytes);
        if (it == by_size.end()) {
            // A single opaque byte-blob field of the right length; deliberately
            // leaked, like every other interned StructTypeInfo.
            auto *fresh = new StructTypeInfo();
            fresh->fields = {StructField{"", UInt(8), total_bytes > 0 ? std::optional<int>(total_bytes) : std::nullopt}};
            fresh->offsets = {0};
            fresh->total_bytes = total_bytes;
            by_size[total_bytes] = fresh;
            info = fresh;
        } else {
            info = it->second;
        }
    }
    return intern_struct_type(info);
}
}  // namespace Internal

bool Type::same_handle_type(const Type &other) const {
    const halide_handle_cplusplus_type *first = handle_type();
    const halide_handle_cplusplus_type *second = other.handle_type();

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

    if (type.is_struct()) {
        // The C backend byte-addresses struct storage (field access is lowered
        // to byte loads/stores), so a struct's C element type is a raw byte;
        // callers scale allocations by Type::bytes() for the true size.
        oss << "uint8_t";
    } else if (type.is_bfloat()) {
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

        // Resolve the interned handle metadata once rather than re-lookup per use.
        const halide_handle_cplusplus_type *ht = type.handle_type();

        // If there is no type info or is generating C (not C++) and
        // the type is a class or in an inner scope, just use void *.
        if (ht == nullptr ||
            (!c_plus_plus &&
             (!ht->namespaces.empty() ||
              !ht->enclosing_types.empty() ||
              ht->inner_name.cpp_type_type == halide_cplusplus_type_name::Class))) {
            oss << "void *";
        } else {
            if (ht->inner_name.cpp_type_type ==
                halide_cplusplus_type_name::Struct) {
                oss << "struct ";
            }

            if (!ht->namespaces.empty() ||
                !ht->enclosing_types.empty()) {
                oss << "::";
                for (const auto &ns : ht->namespaces) {
                    oss << ns << "::";
                }
                for (const auto &enclosing_type : ht->enclosing_types) {
                    oss << enclosing_type.name << "::";
                }
            }
            oss << ht->inner_name.name;
            if (ht->reference_type == halide_handle_cplusplus_type::LValueReference) {
                oss << " &";
            } else if (ht->reference_type == halide_handle_cplusplus_type::RValueReference) {
                oss << " &&";
            }
            for (auto modifier : ht->cpp_type_modifiers) {
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
