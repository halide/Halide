#ifndef HALIDE_TYPE_H
#define HALIDE_TYPE_H

#include "Error.h"
#include "Float16.h"
#include "Util.h"
#include "runtime/HalideRuntime.h"
#include <cstdint>
#include <optional>

/** \file
 * Defines halide types
 */

/** A set of types to represent a C++ function signature. This allows
 * two things.  First, proper prototypes can be provided for Halide
 * generated functions, giving better compile time type
 * checking. Second, C++ name mangling can be done to provide link
 * time type checking for both Halide generated functions and calls
 * from Halide to external functions.
 *
 * These are intended to be constexpr producible.
 *
 * halide_handle_traits has to go outside the Halide namespace due to template
 * resolution rules. TODO(zalman): Do all types need to be in global namespace?
 */
//@{

/** A structure to represent the (unscoped) name of a C++ composite type for use
 * as a single argument (or return value) in a function signature.
 *
 * Currently does not support the restrict qualifier, references, or
 * r-value references.  These features cannot be used in extern
 * function calls from Halide or in the generated function from
 * Halide, but their applicability seems limited anyway.
 *
 * Although this is in the global namespace, it should be considered "Halide Internal"
 * and subject to change; code outside Halide should avoid referencing it.
 */
struct halide_cplusplus_type_name {
    /// An enum to indicate whether a C++ type is non-composite, a struct, class, or union
    enum CPPTypeType {
        Simple,       ///< "int"
        Struct,       ///< "struct Foo"
        Class,        ///< "class Foo"
        Union,        ///< "union Foo"
        Enum,         ///< "enum Foo"
    } cpp_type_type;  // Note: order is reflected in map_to_name table in CPlusPlusMangle.cpp

    std::string name;

    halide_cplusplus_type_name(CPPTypeType cpp_type_type, const std::string &name)
        : cpp_type_type(cpp_type_type), name(name) {
    }

    bool operator==(const halide_cplusplus_type_name &rhs) const {
        return cpp_type_type == rhs.cpp_type_type &&
               name == rhs.name;
    }

    bool operator!=(const halide_cplusplus_type_name &rhs) const {
        return !(*this == rhs);
    }

    bool operator<(const halide_cplusplus_type_name &rhs) const {
        return cpp_type_type < rhs.cpp_type_type ||
               (cpp_type_type == rhs.cpp_type_type &&
                name < rhs.name);
    }
};

/** A structure to represent the fully scoped name of a C++ composite
 * type for use in generating function signatures that use that type.
 *
 * This is intended to be a constexpr usable type.
 *
 * Although this is in the global namespace, it should be considered "Halide Internal"
 * and subject to change; code outside Halide should avoid referencing it.
 */
struct halide_handle_cplusplus_type {
    halide_cplusplus_type_name inner_name;
    std::vector<std::string> namespaces;
    std::vector<halide_cplusplus_type_name> enclosing_types;

    /// One set of modifiers on a type.
    /// The const/volatile/restrict properties are "inside" the pointer property.
    enum Modifier : uint8_t {
        Const = 1 << 0,            ///< Bitmask flag for "const"
        Volatile = 1 << 1,         ///< Bitmask flag for "volatile"
        Restrict = 1 << 2,         ///< Bitmask flag for "restrict"
        Pointer = 1 << 3,          ///< Bitmask flag for a pointer "*"
        FunctionTypedef = 1 << 4,  ///< Bitmask flag for a function typedef; when this is set, Pointer should also always be set
    };

    /// Qualifiers and indirections on type. 0 is innermost.
    std::vector<uint8_t> cpp_type_modifiers;

    /// References are separate because they only occur at the outermost level.
    /// No modifiers are needed for references as they are not allowed to apply
    /// to the reference itself. (This isn't true for restrict, but that is a C++
    /// extension anyway.) If modifiers are needed, the last entry in the above
    /// array would be the modifiers for the reference.
    enum ReferenceType : uint8_t {
        NotReference = 0,
        LValueReference = 1,  // "&"
        RValueReference = 2,  // "&&"
    };
    ReferenceType reference_type;

    halide_handle_cplusplus_type(const halide_cplusplus_type_name &inner_name,
                                 const std::vector<std::string> &namespaces = {},
                                 const std::vector<halide_cplusplus_type_name> &enclosing_types = {},
                                 const std::vector<uint8_t> &modifiers = {},
                                 ReferenceType reference_type = NotReference)
        : inner_name(inner_name),
          namespaces(namespaces),
          enclosing_types(enclosing_types),
          cpp_type_modifiers(modifiers),
          reference_type(reference_type) {
    }

    template<typename T>
    static halide_handle_cplusplus_type make();
};
//@}

/** halide_c_type_to_name is a utility class used to provide a user-extensible
 * way of naming Handle types.
 *
 * Although this is in the global namespace, it should be considered "Halide Internal"
 * and subject to change; code outside Halide should avoid referencing it
 * directly (use the HALIDE_DECLARE_EXTERN_xxx macros instead).
 */
template<typename T>
struct halide_c_type_to_name {
    static constexpr bool known_type = false;
    static halide_cplusplus_type_name name() {
        return {halide_cplusplus_type_name::Simple, "void"};
    }
};

#define HALIDE_DECLARE_EXTERN_TYPE(TypeType, Type)                \
    template<>                                                    \
    struct halide_c_type_to_name<Type> {                          \
        static constexpr bool known_type = true;                  \
        static halide_cplusplus_type_name name() {                \
            return {halide_cplusplus_type_name::TypeType, #Type}; \
        }                                                         \
    }

#define HALIDE_DECLARE_EXTERN_SIMPLE_TYPE(T) HALIDE_DECLARE_EXTERN_TYPE(Simple, T)
#define HALIDE_DECLARE_EXTERN_STRUCT_TYPE(T) HALIDE_DECLARE_EXTERN_TYPE(Struct, T)
#define HALIDE_DECLARE_EXTERN_CLASS_TYPE(T) HALIDE_DECLARE_EXTERN_TYPE(Class, T)
#define HALIDE_DECLARE_EXTERN_UNION_TYPE(T) HALIDE_DECLARE_EXTERN_TYPE(Union, T)

HALIDE_DECLARE_EXTERN_SIMPLE_TYPE(char);
HALIDE_DECLARE_EXTERN_SIMPLE_TYPE(bool);
HALIDE_DECLARE_EXTERN_SIMPLE_TYPE(int8_t);
HALIDE_DECLARE_EXTERN_SIMPLE_TYPE(uint8_t);
HALIDE_DECLARE_EXTERN_SIMPLE_TYPE(int16_t);
HALIDE_DECLARE_EXTERN_SIMPLE_TYPE(uint16_t);
HALIDE_DECLARE_EXTERN_SIMPLE_TYPE(int32_t);
HALIDE_DECLARE_EXTERN_SIMPLE_TYPE(uint32_t);
HALIDE_DECLARE_EXTERN_SIMPLE_TYPE(int64_t);
HALIDE_DECLARE_EXTERN_SIMPLE_TYPE(uint64_t);
HALIDE_DECLARE_EXTERN_SIMPLE_TYPE(Halide::float16_t);
HALIDE_DECLARE_EXTERN_SIMPLE_TYPE(Halide::bfloat16_t);
HALIDE_DECLARE_EXTERN_SIMPLE_TYPE(halide_task_t);
HALIDE_DECLARE_EXTERN_SIMPLE_TYPE(halide_loop_task_t);
#if HALIDE_CPP_COMPILER_HAS_FLOAT16
HALIDE_DECLARE_EXTERN_SIMPLE_TYPE(_Float16);
#endif
HALIDE_DECLARE_EXTERN_SIMPLE_TYPE(float);
HALIDE_DECLARE_EXTERN_SIMPLE_TYPE(double);
HALIDE_DECLARE_EXTERN_STRUCT_TYPE(halide_buffer_t);
HALIDE_DECLARE_EXTERN_STRUCT_TYPE(halide_dimension_t);
HALIDE_DECLARE_EXTERN_STRUCT_TYPE(halide_device_interface_t);
HALIDE_DECLARE_EXTERN_STRUCT_TYPE(halide_filter_metadata_t);
HALIDE_DECLARE_EXTERN_STRUCT_TYPE(halide_semaphore_t);
HALIDE_DECLARE_EXTERN_STRUCT_TYPE(halide_semaphore_acquire_t);
HALIDE_DECLARE_EXTERN_STRUCT_TYPE(halide_parallel_task_t);

// You can make arbitrary user-defined types be "Known" using the
// macro above. This is useful for making Param<> arguments for
// Generators type safe. e.g.,
//
//    struct MyFunStruct { ... };
//
//    ...
//
//    HALIDE_DECLARE_EXTERN_STRUCT_TYPE(MyFunStruct);
//
//    ...
//
//    class MyGenerator : public Generator<MyGenerator> {
//       Param<const MyFunStruct *> my_struct_ptr;
//       ...
//    };

template<typename T>
/*static*/ halide_handle_cplusplus_type halide_handle_cplusplus_type::make() {
    constexpr bool is_ptr = std::is_pointer_v<T>;
    constexpr bool is_lvalue_reference = std::is_lvalue_reference_v<T>;
    constexpr bool is_rvalue_reference = std::is_rvalue_reference_v<T>;

    using TNoRef = std::remove_reference_t<T>;
    using TNoRefNoPtr = std::remove_pointer_t<TNoRef>;
    constexpr bool is_function_pointer = std::is_pointer_v<TNoRef> &&
                                         std::is_function_v<TNoRefNoPtr>;

    // Don't remove the pointer-ness from a function pointer.
    using TBase = std::conditional_t<is_function_pointer, TNoRef, TNoRefNoPtr>;
    constexpr bool is_const = std::is_const_v<TBase>;
    constexpr bool is_volatile = std::is_volatile_v<TBase>;

    constexpr uint8_t modifiers = static_cast<uint8_t>(
        (is_function_pointer ? FunctionTypedef : 0) |
        (is_ptr ? Pointer : 0) |
        (is_const ? Const : 0) |
        (is_volatile ? Volatile : 0));

    constexpr ReferenceType ref_type =
        (is_lvalue_reference ? LValueReference :
         is_rvalue_reference ? RValueReference :
                               NotReference);

    using TNonCVBase = std::remove_cv_t<TBase>;
    constexpr bool known_type = halide_c_type_to_name<TNonCVBase>::known_type;
    static_assert(!(!known_type && !is_ptr), "Unknown types must be pointers");

    halide_handle_cplusplus_type info = {
        halide_c_type_to_name<TNonCVBase>::name(),
        {},
        {},
        {modifiers},
        ref_type};
    // Pull off any namespaces
    info.inner_name.name = Halide::Internal::extract_namespaces(info.inner_name.name, info.namespaces);
    return info;
}

/** A type traits template to provide a halide_handle_cplusplus_type
 * value from a C++ type.
 *
 * Note the type represented is implicitly a pointer.
 *
 * A NULL pointer of type halide_handle_traits represents "void *".
 * This is chosen for compactness or representation as Type is a very
 * widely used data structure.
 *
 * Although this is in the global namespace, it should be considered "Halide Internal"
 * and subject to change; code outside Halide should avoid referencing it directly.
 */
template<typename T>
struct halide_handle_traits {
    // This trait must return a pointer to a global structure. I.e. it should never be freed.
    // A return value of nullptr here means "void *".
    HALIDE_ALWAYS_INLINE static const halide_handle_cplusplus_type *type_info() {
        if (std::is_pointer_v<T> ||
            std::is_lvalue_reference_v<T> ||
            std::is_rvalue_reference_v<T>) {
            static const halide_handle_cplusplus_type the_info = halide_handle_cplusplus_type::make<T>();
            return &the_info;
        }
        return nullptr;
    }
};

namespace Halide {

namespace Internal {
struct ConstantInterval;

/** Handle (C++ pointer) types carry a pointer to externally-owned metadata
 * describing the pointee type. To keep `Type` small (8 bytes) that metadata is
 * referenced by a 4-byte index into a process-wide intern table rather than by
 * an 8-byte pointer. Index 0 means "no handle metadata" (i.e. void *). The
 * table stores the (non-owning) pointers callers supply — the pointees must
 * outlive all `Type`s, exactly as before. `intern_handle_type(nullptr)` returns
 * 0 without locking, so non-handle type construction pays nothing. */
uint32_t intern_handle_type(const halide_handle_cplusplus_type *handle_type);
const halide_handle_cplusplus_type *get_interned_handle_type(uint32_t index);
}  // namespace Internal

struct Expr;

/** Types in the halide type system. They can be ints, unsigned ints,
 * or floats of various bit-widths (the 'bits' field). They can also
 * be vectors of the same (by setting the 'lanes' field to something
 * larger than one). Front-end code shouldn't use vector
 * types. Instead vectorize a function. */
struct Type {
private:
    halide_type_code_t type_code;
    uint8_t type_bits;
    uint16_t type_lanes;
    // Index into the process-wide handle-type intern table (0 == no handle
    // metadata).
    uint32_t handle_index_ = 0;

public:
    /** Aliases for halide_type_code_t values for legacy compatibility
     * and to match the Halide internal C++ style. */
    // @{
    static constexpr halide_type_code_t Int = halide_type_int;
    static constexpr halide_type_code_t UInt = halide_type_uint;
    static constexpr halide_type_code_t Float = halide_type_float;
    static constexpr halide_type_code_t BFloat = halide_type_bfloat;
    static constexpr halide_type_code_t Handle = halide_type_handle;
    // @}

    /** Exposed so code that needs to reason about the maximum representable
     * lanes count (e.g. overflow checks when combining vectors) can derive
     * it from Type itself instead of hardcoding a width. */
    static constexpr int kLanesBits = 8 * sizeof(type_lanes);

    /** The number of bytes required to store a single scalar value of this type. Ignores vector lanes. */
    int bytes() const {
        return (bits() + 7) / 8;
    }

    // Default ctor initializes everything to predictable-but-unlikely values
    constexpr Type()
        : type_code(Handle), type_bits(0), type_lanes(0) {
    }

    /** Construct a runtime representation of a Halide type from:
     * code: The fundamental type from an enum.
     * bits: The bit size of one element.
     * lanes: The number of vector elements in the type. */
    HALIDE_ALWAYS_INLINE
    Type(halide_type_code_t code, int bits, int lanes, const halide_handle_cplusplus_type *handle_type = nullptr)
        : type_code(code), type_bits((uint8_t)bits), type_lanes((uint16_t)lanes),
          handle_index_(handle_type ? Internal::intern_handle_type(handle_type) : 0) {
        internal_assert(lanes == (int)type_lanes)
            << "Halide only supports vector types with up to 65535 lanes. " << lanes << " lanes requested.";
        internal_assert(bits == (int)type_bits)
            << "Halide only supports types with up to 255 bits. " << bits << " bits requested.";
        internal_assert(code == Handle || !handle_type)
            << "Cannot construct a non-handle Type with handle metadata.\n";
    }

    /** Trivial copy constructor. */
    Type(const Type &that) = default;

    /** Trivial copy assignment operator. */
    Type &operator=(const Type &that) = default;

    /** Construct a (scalar) language Type from an ABI element type. */
    HALIDE_ALWAYS_INLINE
    Type(halide_type_t that, const halide_handle_cplusplus_type *handle_type = nullptr)
        : Type(that.code, that.bits, 1, handle_type) {
    }

    /** Erase this language Type to the ABI/runtime halide_type_t for use in
     * runtime calls, buffer/argument metadata, etc. This is a *checked*
     * erasure: the ABI element type has no lanes, so a vector type
     * (lanes >= 2) has no image here and asserts rather than silently
     * dropping its lanes. */
    HALIDE_ALWAYS_INLINE
    halide_type_t to_abi() const {
        internal_assert(type_lanes < 2)
            << "Cannot erase a vector type with " << type_lanes
            << " lanes to a scalar ABI halide_type_t.\n";
        return halide_type_t(code(), type_bits);
    }

    /** Return the underlying data type of an element as an enum value. */
    HALIDE_ALWAYS_INLINE
    halide_type_code_t code() const {
        return type_code;
    }

    /** Return the bit size of a single element of this type. */
    HALIDE_ALWAYS_INLINE
    int bits() const {
        return type_bits;
    }

    /** Return the number of vector elements in this type. */
    HALIDE_ALWAYS_INLINE
    int lanes() const {
        return type_lanes;
    }

    /** Return Type with same number of bits and lanes, but new_code for a type code. */
    HALIDE_ALWAYS_INLINE
    Type with_code(halide_type_code_t new_code) const {
        Type t = *this;
        t.type_code = new_code;
        t.handle_index_ = new_code != code() ? 0 : handle_index_;  // Changing the type code invalidates any handle metadata.
        return t;
    }

    /** Return Type with same type code and lanes, but new_bits for the number of bits. */
    HALIDE_ALWAYS_INLINE
    Type with_bits(int new_bits) const {
        Type t = *this;
        t.type_bits = (uint8_t)new_bits;
        t.handle_index_ = new_bits != bits() ? 0 : handle_index_;  // Changing the bit width invalidates any handle metadata.
        return t;
    }

    /** Return Type with same type code and number of bits,
     * but new_lanes for the number of vector lanes. */
    HALIDE_ALWAYS_INLINE
    Type with_lanes(int new_lanes) const {
        Type t = *this;
        t.type_lanes = (uint16_t)new_lanes;
        return t;
    }

    /** Return Type with the same type code and number of lanes, but with at least twice as many bits. */
    HALIDE_ALWAYS_INLINE
    Type widen() const {
        if (is_bfloat()) {
            // Widening a bfloat16 should produce a float32.
            return with_code(Float).with_bits(32);
        }
        if (bits() == 1) {
            // Widening a 1-bit type should produce an 8-bit type.
            return with_bits(8);
        } else {
            return with_bits(bits() * 2);
        }
    }

    /** Return Type with the same type code and number of lanes, but with at most half as many bits. */
    HALIDE_ALWAYS_INLINE
    Type narrow() const {
        internal_assert(bits() != 1) << "Attempting to narrow a 1-bit type\n";
        if (bits() == 8) {
            // Narrowing an 8-bit type should produce a 1-bit type.
            return with_bits(1);
        } else {
            return with_bits(bits() / 2);
        }
    }

    /** The externally-owned C++ type metadata for a handle type (null for a
     * plain void * handle or a non-handle type). Backed by a 4-byte intern
     * index rather than stored inline, so this is now an accessor rather than a
     * public field. */
    const halide_handle_cplusplus_type *handle_type() const {
        // Inline the common (non-handle) case so it needs no call or table
        // lookup; only a real handle index reaches the intern table.
        return handle_index_ == 0 ? nullptr : Internal::get_interned_handle_type(handle_index_);
    }

    /** Is this type boolean (represented as UInt(1))? */
    HALIDE_ALWAYS_INLINE
    bool is_bool() const {
        return code() == UInt && bits() == 1;
    }

    /** Is this type a vector type? (lanes() != 1).
     * TODO(abadams): Decide what to do for lanes() == 0. */
    HALIDE_ALWAYS_INLINE
    bool is_vector() const {
        return lanes() != 1;
    }

    /** Is this type a scalar type? (lanes() == 1).
     * TODO(abadams): Decide what to do for lanes() == 0. */
    HALIDE_ALWAYS_INLINE
    bool is_scalar() const {
        return lanes() == 1;
    }

    /** Is this type a floating point type (float or double). */
    HALIDE_ALWAYS_INLINE
    bool is_float() const {
        return code() == Float || code() == BFloat;
    }

    /** Is this type a floating point type (float or double). */
    HALIDE_ALWAYS_INLINE
    bool is_bfloat() const {
        return code() == BFloat;
    }

    /** Is this type a signed integer type? */
    HALIDE_ALWAYS_INLINE
    bool is_int() const {
        return code() == Int;
    }

    /** Is this type an unsigned integer type? */
    HALIDE_ALWAYS_INLINE
    bool is_uint() const {
        return code() == UInt;
    }

    /** Is this type an integer type of any sort? */
    HALIDE_ALWAYS_INLINE
    bool is_int_or_uint() const {
        return code() == Int || code() == UInt;
    }

    /** Is this type an opaque handle type (void *) */
    HALIDE_ALWAYS_INLINE
    bool is_handle() const {
        return code() == Handle;
    }

    // Returns true iff type is a signed integral type where overflow is defined.
    HALIDE_ALWAYS_INLINE
    bool can_overflow_int() const {
        return is_int() && bits() <= 16;
    }

    // Returns true iff type does have a well-defined overflow behavior.
    HALIDE_ALWAYS_INLINE
    bool can_overflow() const {
        return is_uint() || can_overflow_int();
    }

    /** Check that the type name of two handles matches. */
    bool same_handle_type(const Type &other) const;

    /** Compare two types for equality */
    HALIDE_ALWAYS_INLINE
    bool operator==(const Type &other) const {
        return type_code == other.type_code && type_bits == other.type_bits &&
               type_lanes == other.type_lanes && (code() != Handle || same_handle_type(other));
    }

    /** Compare two types for inequality */
    bool operator!=(const Type &other) const {
        return !(*this == other);
    }

    /** Compare a language type to an ABI element type. Equal iff this type is a
     * single element (not a vector) with the same code and bits. */
    bool operator==(const halide_type_t &other) const {
        return type_lanes < 2 && (uint8_t)type_code == (uint8_t)other.code && type_bits == other.bits;
    }

    /** Compare two types for inequality */
    bool operator!=(const halide_type_t &other) const {
        return !(*this == other);
    }

    /** Compare ordering of two types so they can be used in certain containers and algorithms */
    bool operator<(const Type &other) const {
        if (std::tie(type_code, type_bits, type_lanes) <
            std::tie(other.type_code, other.type_bits, other.type_lanes)) {
            return true;
        }
        if (code() == Handle) {
            return handle_type() < other.handle_type();
        }
        return false;
    }

    /** Produce the scalar type (that of a single element) of this vector type */
    HALIDE_ALWAYS_INLINE
    Type element_of() const {
        return with_lanes(1);
    }

    /** Can this type represent all values of another type? */
    bool can_represent(Type other) const;

    /** Can this type represent exactly all integer values of some constant
     * integer range? */
    bool can_represent(const Internal::ConstantInterval &in) const;

    /** Can this type represent a particular constant? */
    // @{
    bool can_represent(double x) const;
    bool can_represent(int64_t x) const;
    bool can_represent(uint64_t x) const;
    // @}

    /** Check if an integer constant value is the maximum or minimum
     * representable value for this type. */
    // @{
    bool is_max(uint64_t) const;
    bool is_max(int64_t) const;
    bool is_min(uint64_t) const;
    bool is_min(int64_t) const;
    // @}

    /** Return an expression which is the maximum value of this type.
     * Returns infinity for types which can represent it. */
    Expr max() const;

    /** Return an expression which is the minimum value of this type.
     * Returns -infinity for types which can represent it. */
    Expr min() const;
};

static_assert(sizeof(Type) == 8, "Halide::Type is a code+bits+lanes triple plus a 4-byte handle-type index");
static_assert(std::is_trivially_copyable_v<Type>, "Type must stay trivially copyable");

/** Constructing a signed integer type */
HALIDE_ALWAYS_INLINE Type Int(int bits, int lanes = 1) {
    return Type(Type::Int, bits, lanes);
}

/** Constructing an unsigned integer type */
HALIDE_ALWAYS_INLINE Type UInt(int bits, int lanes = 1) {
    return Type(Type::UInt, bits, lanes);
}

/** Construct a floating-point type */
HALIDE_ALWAYS_INLINE Type Float(int bits, int lanes = 1) {
    return Type(Type::Float, bits, lanes);
}

/** Construct a floating-point type in the bfloat format. Only 16-bit currently supported. */
HALIDE_ALWAYS_INLINE Type BFloat(int bits, int lanes = 1) {
    return Type(Type::BFloat, bits, lanes);
}

/** Construct a boolean type */
HALIDE_ALWAYS_INLINE Type Bool(int lanes = 1) {
    return UInt(1, lanes);
}

/** Construct a handle type */
HALIDE_ALWAYS_INLINE Type Handle(const halide_handle_cplusplus_type *handle_type = nullptr) {
    return Type(Type::Handle, 64, 1, handle_type);
}

/** Construct the halide equivalent of a C type */
template<typename T>
inline Type type_of() {
    return Type(halide_type_of<T>(), halide_handle_traits<T>::type_info());
}

/** Halide type to a C++ type */
std::string type_to_c_type(Type type, bool include_space, bool c_plus_plus = true);

}  // namespace Halide

#endif
