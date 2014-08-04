#ifndef HALIDE_TYPE_H
#define HALIDE_TYPE_H

#include <stdint.h>

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
 * These are intended to be constexpr producable, but we don't depend
 * on C++11 yet. In C++14, it is possible these will be replaced with
 * introspection/reflection facilities.
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
 */
struct halide_cplusplus_type_name {
    /// An enum to indicate whether a C++ type is non-composite, a struct, class, or union
    enum CPPTypeType {
      Simple, ///< "int"
      Struct, ///< "struct Foo"
      Class,  ///< "class Foo"
      Union,  ///< "union Foo" TODO: Do we need unions
      Enum,   ///< "enum Foo" TODO: Do we need enums
    } cpp_type_type;

    enum CPPTypeQualifiers {
      Const = 1,    ///< flag for "const"
      Volatile = 2, ///< flag for "volatile"
    };
    int32_t cpp_type_qualifiers; /// Bitset indicating which qualifiers are present on type

    std::string name;

    halide_cplusplus_type_name(CPPTypeType cpp_type_type, const std::string &name)
        : cpp_type_type(cpp_type_type), name(name) {
    }

    bool operator==(const halide_cplusplus_type_name &rhs) const {
         return cpp_type_type == rhs.cpp_type_type &&
                 name == rhs.name;
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
 * This is intended to be a constexpr usable type, but we don't depend
 * on C++11 yet. In C++14, it is possible this will be replaced with
 * introspection/reflection facilities.
 *
 * TODO(zalman): Decide if this needs a field to indicate const/non-const.
 */
struct halide_handle_cplusplus_type {
    halide_cplusplus_type_name inner_name;
    std::vector<std::string> namespaces;
    std::vector<halide_cplusplus_type_name> enclosing_types;

    halide_handle_cplusplus_type(const halide_cplusplus_type_name &inner_name,
                                 const std::vector<std::string> &namespaces = std::vector<std::string>(),
                                 const std::vector<halide_cplusplus_type_name> &enclosing_types = std::vector<halide_cplusplus_type_name>())
        : inner_name(inner_name), namespaces(namespaces), enclosing_types(enclosing_types) {
    }
};

/** A type traits template to provide a halide_handle_cplusplus_type
 * value from a C++ type.
 *
 * Note the type represented is implicitly a pointer.
 * TODO(zalman): Figure out if we need to represent refs
 *
 * A NULL pointer of type halide_handle_traits represents "void *".
 * This is chosen for compactness or representation as Type is a very
 * widely used data structure.
 */
template<typename T>
struct halide_handle_traits {
    // NULL here means "void *". This trait must return a pointer to a
    // global structure. I.e. it should never be freed.
    static const halide_handle_cplusplus_type *type_info() { return NULL; }
};
//@}

namespace Halide {

struct Expr;

/** Types in the halide type system. They can be ints, unsigned ints,
 * or floats of various bit-widths (the 'bits' field). They can also
 * be vectors of the same (by setting the 'width' field to something
 * larger than one). Front-end code shouldn't use vector
 * types. Instead vectorize a function. */
struct Type {
    /** The basic type code: signed integer, unsigned integer, or floating point */
    enum {Int,  //!< signed integers
          UInt, //!< unsigned integers
          Float, //!< floating point numbers
          Handle //!< opaque pointer type (void *)
    } code;

    /** The number of bits of precision of a single scalar value of this type. */
    int bits;

    /** The number of bytes required to store a single scalar value of this type. Ignores vector width. */
    int bytes() const {return (bits + 7) / 8;}

    /** How many elements (if a vector type). Should be 1 for scalar types. */
    int width;

    /** Type to be printed when declaring handles of this type. */
    const halide_handle_cplusplus_type *handle_type;

    /** Is this type boolean (represented as UInt(1))? */
    bool is_bool() const {return code == UInt && bits == 1;}

    /** Is this type a vector type? (width > 1) */
    bool is_vector() const {return width > 1;}

    /** Is this type a scalar type? (width == 1) */
    bool is_scalar() const {return width == 1;}

    /** Is this type a floating point type (float or double). */
    bool is_float() const {return code == Float;}

    /** Is this type a signed integer type? */
    bool is_int() const {return code == Int;}

    /** Is this type an unsigned integer type? */
    bool is_uint() const {return code == UInt;}

    /** Is this type an opaque handle type (void *) */
    bool is_handle() const {return code == Handle;}

    /** Check that the type name of two handles matches. */
    bool same_handle_type(const Type &other) const {
        const halide_handle_cplusplus_type *first = handle_type;
        const halide_handle_cplusplus_type *second = other.handle_type;

        if (first == second) {
            return true;
        }

        static halide_handle_cplusplus_type void_type(halide_cplusplus_type_name(halide_cplusplus_type_name::Simple, "void"));

        if (first == NULL) {
            first = &void_type;
        }
        if (second == NULL) {
            second = &void_type;
        }

        return first->inner_name == second->inner_name &&
               first->namespaces == second->namespaces &&
               first->enclosing_types == second->enclosing_types;
    }

    /** Compare two types for equality */
    bool operator==(const Type &other) const {
        return code == other.code && bits == other.bits && width == other.width &&
            (code != Handle || same_handle_type(other));
    }

    /** Compare two types for inequality */
    bool operator!=(const Type &other) const {
        return code != other.code || bits != other.bits || width != other.width ||
            (code == Handle && !same_handle_type(other));
    }

    /** Produce a vector of this type, with 'width' elements */
    Type vector_of(int w) const {
        Type type = {code, bits, w, NULL};
        return type;
    }

    /** Produce the type of a single element of this vector type */
    Type element_of() const {
        Type type = {code, bits, 1, NULL};
        return type;
    }

    /** Can this type represent all values of another type? */
    bool can_represent(Type other) const;

    /** Return an integer which is the maximum value of this type. */
    int imax() const;

    /** Return an expression which is the maximum value of this type */
    Expr max() const;

    /** Return an integer which is the minimum value of this type */
    int imin() const;

    /** Return an expression which is the minimum value of this type */
    Expr min() const;
};

/** Constructing a signed integer type */
inline Type Int(int bits, int width = 1) {
    Type t;
    t.code = Type::Int;
    t.bits = bits;
    t.width = width;
    return t;
}

/** Constructing an unsigned integer type */
inline Type UInt(int bits, int width = 1) {
    Type t;
    t.code = Type::UInt;
    t.bits = bits;
    t.width = width;
    return t;
}

/** Construct a floating-point type */
inline Type Float(int bits, int width = 1) {
    Type t;
    t.code = Type::Float;
    t.bits = bits;
    t.width = width;
    return t;
}

/** Construct a boolean type */
inline Type Bool(int width = 1) {
    return UInt(1, width);
}

/** Construct a handle type */
inline Type Handle(int width = 1, const halide_handle_cplusplus_type *handle_type = NULL) {
    Type t;
    t.code = Type::Handle;
    t.bits = 64; // All handles are 64-bit for now
    t.width = width;
    t.handle_type = handle_type;
    return t;
}

namespace {
template<typename T>
struct type_of_helper;

template<typename T>
struct type_of_helper<T *> {
    operator Type() {
      return Handle(1, halide_handle_traits<T>::type_info());
    }
};

template<>
struct type_of_helper<float> {
    operator Type() {return Float(32);}
};

template<>
struct type_of_helper<double> {
    operator Type() {return Float(64);}
};

template<>
struct type_of_helper<uint8_t> {
    operator Type() {return UInt(8);}
};

template<>
struct type_of_helper<uint16_t> {
    operator Type() {return UInt(16);}
};

template<>
struct type_of_helper<uint32_t> {
    operator Type() {return UInt(32);}
};

template<>
struct type_of_helper<uint64_t> {
    operator Type() {return UInt(64);}
};

template<>
struct type_of_helper<int8_t> {
    operator Type() {return Int(8);}
};

template<>
struct type_of_helper<int16_t> {
    operator Type() {return Int(16);}
};

template<>
struct type_of_helper<int32_t> {
    operator Type() {return Int(32);}
};

template<>
struct type_of_helper<int64_t> {
    operator Type() {return Int(64);}
};

template<>
struct type_of_helper<bool> {
    operator Type() {return Bool();}
};
}

/** Construct the halide equivalent of a C type */
template<typename T> Type type_of() {
    return Type(type_of_helper<T>());
}

}

#endif
