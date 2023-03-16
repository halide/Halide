#if !defined(__has_attribute)
#define __has_attribute(x) 0
#endif

#if !defined(__has_builtin)
#define __has_builtin(x) 0
#endif

namespace {

// We can't use std::array because that has its own overload of operator<, etc,
// which will interfere with ours.
template<typename ElementType, size_t Lanes>
struct CppVector {
    ElementType elements[Lanes];

    HALIDE_ALWAYS_INLINE
    ElementType &operator[](size_t i) {
        return elements[i];
    }

    HALIDE_ALWAYS_INLINE
    const ElementType operator[](size_t i) const {
        return elements[i];
    }

    HALIDE_ALWAYS_INLINE
    ElementType *data() {
        return elements;
    }

    HALIDE_ALWAYS_INLINE
    const ElementType *data() const {
        return elements;
    }
};

template<typename ElementType_, size_t Lanes_>
class CppVectorOps {
public:
    using ElementType = ElementType_;
    static constexpr size_t Lanes = Lanes_;

    using Vec = CppVector<ElementType, Lanes>;
    using Mask = CppVector<uint8_t, Lanes>;

    CppVectorOps() = delete;

    static Vec broadcast(const ElementType v) {
        Vec r;
        for (size_t i = 0; i < Lanes; i++) {
            r[i] = v;
        }
        return r;
    }

    static Vec ramp(const ElementType base, const ElementType stride) {
        Vec r;
        for (size_t i = 0; i < Lanes; i++) {
            r[i] = base + stride * i;
        }
        return r;
    }

    static Vec load(const void *base, int32_t offset) {
        Vec r;
        memcpy(r.data(), ((const ElementType *)base + offset), sizeof(ElementType) * Lanes);
        return r;
    }

    static Vec load_gather(const void *base, const CppVector<int32_t, Lanes> &offset) {
        Vec r;
        for (size_t i = 0; i < Lanes; i++) {
            r[i] = ((const ElementType *)base)[offset[i]];
        }
        return r;
    }

    static Vec load_predicated(const void *base, const CppVector<int32_t, Lanes> &offset, const Mask &predicate) {
        Vec r;
        for (size_t i = 0; i < Lanes; i++) {
            if (predicate[i]) {
                r[i] = ((const ElementType *)base)[offset[i]];
            }
        }
        return r;
    }

    static void store(const Vec &v, void *base, int32_t offset) {
        memcpy(((ElementType *)base + offset), v.data(), sizeof(ElementType) * Lanes);
    }

    static void store_scatter(const Vec &v, void *base, const CppVector<int32_t, Lanes> &offset) {
        for (size_t i = 0; i < Lanes; i++) {
            ((ElementType *)base)[offset[i]] = v[i];
        }
    }

    static void store_predicated(const Vec &v, void *base, const CppVector<int32_t, Lanes> &offset, const Mask &predicate) {
        for (size_t i = 0; i < Lanes; i++) {
            if (predicate[i]) {
                ((ElementType *)base)[offset[i]] = v[i];
            }
        }
    }

    template<int... Indices, typename InputVec>
    static Vec shuffle(const InputVec &a) {
        static_assert(sizeof...(Indices) == Lanes, "shuffle() requires an exact match of lanes");
        Vec r = {a[Indices]...};
        return r;
    }

    static Vec replace(const Vec &v, size_t i, const ElementType b) {
        Vec r = v;
        r[i] = b;
        return r;
    }

    template<typename OtherVec>
    static Vec convert_from(const OtherVec &src) {
        Vec r;
        for (size_t i = 0; i < Lanes; i++) {
            r[i] = static_cast<ElementType>(src[i]);
        }
        return r;
    }

    static Vec max(const Vec &a, const Vec &b) {
        Vec r;
        for (size_t i = 0; i < Lanes; i++) {
            r[i] = ::halide_cpp_max(a[i], b[i]);
        }
        return r;
    }

    static Vec min(const Vec &a, const Vec &b) {
        Vec r;
        for (size_t i = 0; i < Lanes; i++) {
            r[i] = ::halide_cpp_min(a[i], b[i]);
        }
        return r;
    }

    static Vec select(const Mask &cond, const Vec &true_value, const Vec &false_value) {
        Vec r;
        for (size_t i = 0; i < Lanes; i++) {
            r[i] = cond[i] ? true_value[i] : false_value[i];
        }
        return r;
    }

    static Mask logical_or(const Vec &a, const Vec &b) {
        CppVector<uint8_t, Lanes> r;
        for (size_t i = 0; i < Lanes; i++) {
            r[i] = a[i] || b[i] ? 0xff : 0x00;
        }
        return r;
    }

    static Mask logical_and(const Vec &a, const Vec &b) {
        CppVector<uint8_t, Lanes> r;
        for (size_t i = 0; i < Lanes; i++) {
            r[i] = a[i] && b[i] ? 0xff : 0x00;
        }
        return r;
    }

    static Mask lt(const Vec &a, const Vec &b) {
        CppVector<uint8_t, Lanes> r;
        for (size_t i = 0; i < Lanes; i++) {
            r[i] = a[i] < b[i] ? 0xff : 0x00;
        }
        return r;
    }

    static Mask le(const Vec &a, const Vec &b) {
        CppVector<uint8_t, Lanes> r;
        for (size_t i = 0; i < Lanes; i++) {
            r[i] = a[i] <= b[i] ? 0xff : 0x00;
        }
        return r;
    }

    static Mask gt(const Vec &a, const Vec &b) {
        CppVector<uint8_t, Lanes> r;
        for (size_t i = 0; i < Lanes; i++) {
            r[i] = a[i] > b[i] ? 0xff : 0x00;
        }
        return r;
    }

    static Mask ge(const Vec &a, const Vec &b) {
        CppVector<uint8_t, Lanes> r;
        for (size_t i = 0; i < Lanes; i++) {
            r[i] = a[i] >= b[i] ? 0xff : 0x00;
        }
        return r;
    }

    static Mask eq(const Vec &a, const Vec &b) {
        CppVector<uint8_t, Lanes> r;
        for (size_t i = 0; i < Lanes; i++) {
            r[i] = a[i] == b[i] ? 0xff : 0x00;
        }
        return r;
    }

    static Mask ne(const Vec &a, const Vec &b) {
        CppVector<uint8_t, Lanes> r;
        for (size_t i = 0; i < Lanes; i++) {
            r[i] = a[i] != b[i] ? 0xff : 0x00;
        }
        return r;
    }
};

template<typename ElementType, size_t Lanes>
CppVector<ElementType, Lanes> operator~(const CppVector<ElementType, Lanes> &v) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = ~v[i];
    }
    return r;
}

template<typename ElementType, size_t Lanes>
CppVector<ElementType, Lanes> operator!(const CppVector<ElementType, Lanes> &v) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = !v[i];
    }
    return r;
}

template<typename ElementType, size_t Lanes>
CppVector<ElementType, Lanes> operator+(const CppVector<ElementType, Lanes> &a, const CppVector<ElementType, Lanes> &b) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = a[i] + b[i];
    }
    return r;
}

template<typename ElementType, size_t Lanes>
CppVector<ElementType, Lanes> operator-(const CppVector<ElementType, Lanes> &a, const CppVector<ElementType, Lanes> &b) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = a[i] - b[i];
    }
    return r;
}

template<typename ElementType, size_t Lanes>
CppVector<ElementType, Lanes> operator*(const CppVector<ElementType, Lanes> &a, const CppVector<ElementType, Lanes> &b) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = a[i] * b[i];
    }
    return r;
}

template<typename ElementType, size_t Lanes>
CppVector<ElementType, Lanes> operator/(const CppVector<ElementType, Lanes> &a, const CppVector<ElementType, Lanes> &b) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = a[i] / b[i];
    }
    return r;
}

template<typename ElementType, size_t Lanes>
CppVector<ElementType, Lanes> operator%(const CppVector<ElementType, Lanes> &a, const CppVector<ElementType, Lanes> &b) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = a[i] % b[i];
    }
    return r;
}

template<typename ElementType, size_t Lanes, typename OtherElementType>
CppVector<ElementType, Lanes> operator<<(const CppVector<ElementType, Lanes> &a, const CppVector<OtherElementType, Lanes> &b) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = a[i] << b[i];
    }
    return r;
}

template<typename ElementType, size_t Lanes, typename OtherElementType>
CppVector<ElementType, Lanes> operator>>(const CppVector<ElementType, Lanes> &a, const CppVector<OtherElementType, Lanes> &b) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = a[i] >> b[i];
    }
    return r;
}

template<typename ElementType, size_t Lanes>
CppVector<ElementType, Lanes> operator&(const CppVector<ElementType, Lanes> &a, const CppVector<ElementType, Lanes> &b) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = a[i] & b[i];
    }
    return r;
}

template<typename ElementType, size_t Lanes>
CppVector<ElementType, Lanes> operator|(const CppVector<ElementType, Lanes> &a, const CppVector<ElementType, Lanes> &b) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = a[i] | b[i];
    }
    return r;
}

template<typename ElementType, size_t Lanes>
CppVector<ElementType, Lanes> operator^(const CppVector<ElementType, Lanes> &a, const CppVector<ElementType, Lanes> &b) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = a[i] ^ b[i];
    }
    return r;
}

template<typename ElementType, size_t Lanes>
CppVector<ElementType, Lanes> operator+(const CppVector<ElementType, Lanes> &a, const ElementType b) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = a[i] + b;
    }
    return r;
}

template<typename ElementType, size_t Lanes>
CppVector<ElementType, Lanes> operator-(const CppVector<ElementType, Lanes> &a, const ElementType b) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = a[i] - b;
    }
    return r;
}

template<typename ElementType, size_t Lanes>
CppVector<ElementType, Lanes> operator*(const CppVector<ElementType, Lanes> &a, const ElementType b) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = a[i] * b;
    }
    return r;
}

template<typename ElementType, size_t Lanes>
CppVector<ElementType, Lanes> operator/(const CppVector<ElementType, Lanes> &a, const ElementType b) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = a[i] / b;
    }
    return r;
}

template<typename ElementType, size_t Lanes>
CppVector<ElementType, Lanes> operator%(const CppVector<ElementType, Lanes> &a, const ElementType b) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = a[i] % b;
    }
    return r;
}

template<typename ElementType, size_t Lanes>
CppVector<ElementType, Lanes> operator>>(const CppVector<ElementType, Lanes> &a, const ElementType b) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = a[i] >> b;
    }
    return r;
}

template<typename ElementType, size_t Lanes>
CppVector<ElementType, Lanes> operator<<(const CppVector<ElementType, Lanes> &a, const ElementType b) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = a[i] << b;
    }
    return r;
}

template<typename ElementType, size_t Lanes>
CppVector<ElementType, Lanes> operator&(const CppVector<ElementType, Lanes> &a, const ElementType b) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = a[i] & b;
    }
    return r;
}

template<typename ElementType, size_t Lanes>
CppVector<ElementType, Lanes> operator|(const CppVector<ElementType, Lanes> &a, const ElementType b) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = a[i] | b;
    }
    return r;
}

template<typename ElementType, size_t Lanes>
CppVector<ElementType, Lanes> operator^(const CppVector<ElementType, Lanes> &a, const ElementType b) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = a[i] ^ b;
    }
    return r;
}

template<typename ElementType, size_t Lanes>
CppVector<ElementType, Lanes> operator+(const ElementType a, const CppVector<ElementType, Lanes> &b) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = a + b[i];
    }
    return r;
}

template<typename ElementType, size_t Lanes>
CppVector<ElementType, Lanes> operator-(const ElementType a, const CppVector<ElementType, Lanes> &b) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = a - b[i];
    }
    return r;
}

template<typename ElementType, size_t Lanes>
CppVector<ElementType, Lanes> operator*(const ElementType a, const CppVector<ElementType, Lanes> &b) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = a * b[i];
    }
    return r;
}

template<typename ElementType, size_t Lanes>
CppVector<ElementType, Lanes> operator/(const ElementType a, const CppVector<ElementType, Lanes> &b) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = a / b[i];
    }
    return r;
}

template<typename ElementType, size_t Lanes>
CppVector<ElementType, Lanes> operator%(const ElementType a, const CppVector<ElementType, Lanes> &b) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = a % b[i];
    }
    return r;
}

template<typename ElementType, size_t Lanes>
CppVector<ElementType, Lanes> operator>>(const ElementType a, const CppVector<ElementType, Lanes> &b) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = a >> b[i];
    }
    return r;
}

template<typename ElementType, size_t Lanes>
CppVector<ElementType, Lanes> operator<<(const ElementType a, const CppVector<ElementType, Lanes> &b) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = a << b[i];
    }
    return r;
}

template<typename ElementType, size_t Lanes>
CppVector<ElementType, Lanes> operator&(const ElementType a, const CppVector<ElementType, Lanes> &b) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = a & b[i];
    }
    return r;
}

template<typename ElementType, size_t Lanes>
CppVector<ElementType, Lanes> operator|(const ElementType a, const CppVector<ElementType, Lanes> &b) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = a | b[i];
    }
    return r;
}

template<typename ElementType, size_t Lanes>
CppVector<ElementType, Lanes> operator^(const ElementType a, const CppVector<ElementType, Lanes> &b) {
    CppVector<ElementType, Lanes> r;
    for (size_t i = 0; i < Lanes; i++) {
        r[i] = a ^ b[i];
    }
    return r;
}

#if __has_attribute(ext_vector_type) || __has_attribute(vector_size)

#if __has_attribute(ext_vector_type)
// Clang
template<typename ElementType, size_t Lanes>
using NativeVector __attribute__((ext_vector_type(Lanes), aligned(sizeof(ElementType)))) = ElementType;
#elif __has_attribute(vector_size) || defined(__GNUC__)
// GCC
template<typename ElementType, size_t Lanes>
using NativeVector __attribute__((vector_size(Lanes * sizeof(ElementType)), aligned(sizeof(ElementType)))) = ElementType;
#else
#error
#endif

template<typename T>
struct NativeVectorComparisonType {
    using type = void;
};

template<>
struct NativeVectorComparisonType<int8_t> { using type = char; };

template<>
struct NativeVectorComparisonType<int16_t> { using type = int16_t; };

template<>
struct NativeVectorComparisonType<int32_t> { using type = int32_t; };

template<>
struct NativeVectorComparisonType<int64_t> { using type = int64_t; };

template<>
struct NativeVectorComparisonType<uint8_t> { using type = char; };

template<>
struct NativeVectorComparisonType<uint16_t> { using type = int16_t; };

template<>
struct NativeVectorComparisonType<uint32_t> { using type = int32_t; };

template<>
struct NativeVectorComparisonType<uint64_t> { using type = int64_t; };

template<>
struct NativeVectorComparisonType<float> { using type = int32_t; };

template<>
struct NativeVectorComparisonType<double> { using type = int64_t; };

template<typename ElementType_, size_t Lanes_>
class NativeVectorOps {
public:
    using ElementType = ElementType_;
    static constexpr size_t Lanes = Lanes_;

    using Vec = NativeVector<ElementType, Lanes>;
    using Mask = NativeVector<uint8_t, Lanes>;

    NativeVectorOps() = delete;

    static Vec broadcast(const ElementType v) {
        const Vec zero = {};  // Zero-initialized native vector.
        return v - zero;
    }

    static Vec ramp(const ElementType base, const ElementType stride) {
        Vec r;
        for (size_t i = 0; i < Lanes; i++) {
            r[i] = base + stride * i;
        }
        return r;
    }

    static Vec load(const void *base, int32_t offset) {
        Vec r;
        // We only require Vec to be element-aligned, so we can't safely just read
        // directly from memory (might segfault). Use memcpy for safety.
        //
        // If Vec is a non-power-of-two (e.g. uint8x48), the actual implementation
        // might be larger (e.g. it might really be a uint8x64). Only copy the amount
        // that is in the logical type, to avoid possible overreads.
        memcpy(&r, ((const ElementType *)base + offset), sizeof(ElementType) * Lanes);
        return r;
    }

    static Vec load_gather(const void *base, const NativeVector<int32_t, Lanes> offset) {
        Vec r;
        for (size_t i = 0; i < Lanes; i++) {
            r[i] = ((const ElementType *)base)[offset[i]];
        }
        return r;
    }

    static Vec load_predicated(const void *base, const NativeVector<int32_t, Lanes> offset, const NativeVector<uint8_t, Lanes> predicate) {
        Vec r;
        for (size_t i = 0; i < Lanes; i++) {
            if (predicate[i]) {
                r[i] = ((const ElementType *)base)[offset[i]];
            }
        }
        return r;
    }
    static void store(const Vec v, void *base, int32_t offset) {
        // We only require Vec to be element-aligned, so we can't safely just write
        // directly from memory (might segfault). Use memcpy for safety.
        //
        // If Vec is a non-power-of-two (e.g. uint8x48), the actual implementation
        // might be larger (e.g. it might really be a uint8x64). Only copy the amount
        // that is in the logical type, to avoid possible overreads.
        memcpy(((ElementType *)base + offset), &v, sizeof(ElementType) * Lanes);
    }

    static void store_scatter(const Vec v, void *base, const NativeVector<int32_t, Lanes> offset) {
        for (size_t i = 0; i < Lanes; i++) {
            ((ElementType *)base)[offset[i]] = v[i];
        }
    }

    static void store_predicated(const Vec v, void *base, const NativeVector<int32_t, Lanes> offset, const NativeVector<uint8_t, Lanes> predicate) {
        for (size_t i = 0; i < Lanes; i++) {
            if (predicate[i]) {
                ((ElementType *)base)[offset[i]] = v[i];
            }
        }
    }

    template<int... Indices, typename InputVec>
    static Vec shuffle(const InputVec a) {
        static_assert(sizeof...(Indices) == Lanes, "shuffle() requires an exact match of lanes");
#if __has_builtin(__builtin_shufflevector)
        // Exists in clang and gcc >= 12. Gcc's __builtin_shuffle can't
        // be used, because it can't handle changing the number of vector
        // lanes between input and output.
        return __builtin_shufflevector(a, a, Indices...);
#else
        Vec r = {a[Indices]...};
        return r;
#endif
    }

    static Vec replace(Vec v, size_t i, const ElementType b) {
        v[i] = b;
        return v;
    }

    template<typename OtherVec>
    static Vec convert_from(const OtherVec src) {
#if __has_builtin(__builtin_convertvector)
        // Don't use __builtin_convertvector for float->int: it appears to have
        // different float->int rounding behavior in at least some situations;
        // for now we'll use the much-slower-but-correct explicit C++ code.
        // (https://github.com/halide/Halide/issues/2080)
        constexpr bool is_float_to_int = std::is_floating_point<OtherVec>::value &&
                                         std::is_integral<Vec>::value;
        if (!is_float_to_int) {
            return __builtin_convertvector(src, Vec);
        }
#endif
        // Fallthru for float->int, or degenerate compilers that support native vectors
        // but not __builtin_convertvector (Intel?)
        Vec r;
        for (size_t i = 0; i < Lanes; i++) {
            r[i] = static_cast<ElementType>(src[i]);
        }
        return r;
    }

    static Vec max(const Vec a, const Vec b) {
#if defined(__GNUC__) && !defined(__clang__)
        // TODO: GCC doesn't seem to recognize this pattern, and scalarizes instead
        return a > b ? a : b;
#else
        // Clang doesn't do ternary operator for vectors, but recognizes this pattern
        Vec r;
        for (size_t i = 0; i < Lanes; i++) {
            r[i] = a[i] > b[i] ? a[i] : b[i];
        }
        return r;
#endif
    }

    static Vec min(const Vec a, const Vec b) {
#if defined(__GNUC__) && !defined(__clang__)
        // TODO: GCC doesn't seem to recognize this pattern, and scalarizes instead
        return a < b ? a : b;
#else
        // Clang doesn't do ternary operator for vectors, but recognizes this pattern
        Vec r;
        for (size_t i = 0; i < Lanes; i++) {
            r[i] = a[i] < b[i] ? a[i] : b[i];
        }
        return r;
#endif
    }

    static Vec select(const Mask cond, const Vec true_value, const Vec false_value) {
#if defined(__GNUC__) && !defined(__clang__)
        // This should do the correct lane-wise select.
        using T = typename NativeVectorComparisonType<ElementType>::type;
        auto b = NativeVectorOps<T, Lanes>::convert_from(cond);
        return b ? true_value : false_value;
#else
        // Clang doesn't do ternary operator for vectors, but recognizes this pattern
        Vec r;
        for (size_t i = 0; i < Lanes; i++) {
            r[i] = cond[i] ? true_value[i] : false_value[i];
        }
        return r;
#endif
    }

    // The relational operators produce signed-int of same width as input; our codegen expects uint8.
    static Mask logical_or(const Vec a, const Vec b) {
        using T = typename NativeVectorComparisonType<ElementType>::type;
        const NativeVector<T, Lanes> r = a || b;
        return NativeVectorOps<uint8_t, Lanes>::convert_from(r);
    }

    static Mask logical_and(const Vec a, const Vec b) {
        using T = typename NativeVectorComparisonType<ElementType>::type;
        const NativeVector<T, Lanes> r = a && b;
        return NativeVectorOps<uint8_t, Lanes>::convert_from(r);
    }

    static Mask lt(const Vec a, const Vec b) {
        using T = typename NativeVectorComparisonType<ElementType>::type;
        const NativeVector<T, Lanes> r = a < b;
        return NativeVectorOps<uint8_t, Lanes>::convert_from(r);
    }

    static Mask le(const Vec a, const Vec b) {
        using T = typename NativeVectorComparisonType<ElementType>::type;
        const NativeVector<T, Lanes> r = a <= b;
        return NativeVectorOps<uint8_t, Lanes>::convert_from(r);
    }

    static Mask gt(const Vec a, const Vec b) {
        using T = typename NativeVectorComparisonType<ElementType>::type;
        const NativeVector<T, Lanes> r = a > b;
        return NativeVectorOps<uint8_t, Lanes>::convert_from(r);
    }

    static Mask ge(const Vec a, const Vec b) {
        using T = typename NativeVectorComparisonType<ElementType>::type;
        const NativeVector<T, Lanes> r = a >= b;
        return NativeVectorOps<uint8_t, Lanes>::convert_from(r);
    }

    static Mask eq(const Vec a, const Vec b) {
        using T = typename NativeVectorComparisonType<ElementType>::type;
        const NativeVector<T, Lanes> r = a == b;
        return NativeVectorOps<uint8_t, Lanes>::convert_from(r);
    }

    static Mask ne(const Vec a, const Vec b) {
        using T = typename NativeVectorComparisonType<ElementType>::type;
        const NativeVector<T, Lanes> r = a != b;
        return NativeVectorOps<uint8_t, Lanes>::convert_from(r);
    }
};

#endif  // __has_attribute(ext_vector_type) || __has_attribute(vector_size)

}  // namespace

// Dec. 1, 2018: Apparently emscripten compilation runs with the __has_attribute true,
// then fails to handle the vector intrinsics later.
#if !defined(__EMSCRIPTEN__) && (__has_attribute(ext_vector_type) || __has_attribute(vector_size))
#if __GNUC__ && !__clang__
   // GCC only allows powers-of-two; fall back to CppVector for other widths
#define halide_cpp_use_native_vector(type, lanes) ((lanes & (lanes - 1)) == 0)
#else
#define halide_cpp_use_native_vector(type, lanes) (true)
#endif
#else
   // No NativeVector available
#define halide_cpp_use_native_vector(type, lanes) (false)
#endif  // __has_attribute(ext_vector_type) || __has_attribute(vector_size)

// Failsafe to allow forcing non-native vectors in case of unruly compilers
#if HALIDE_CPP_ALWAYS_USE_CPP_VECTORS
#undef halide_cpp_use_native_vector
#define halide_cpp_use_native_vector(type, lanes) (false)
#endif
