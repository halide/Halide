// Always use assert, even if llvm-config defines NDEBUG
#ifdef NDEBUG
#undef NDEBUG
#include <assert.h>
#define NDEBUG
#else
#include <assert.h>
#endif

#ifndef HALIDE_UTIL_H
#define HALIDE_UTIL_H

/** \file
 * Various utility functions used internally Halide. */

#include <vector>
#include <string>
#include <cstring>

// by default, the symbol EXPORT does nothing. In windows dll builds we can define it to __declspec(dllexport)
#if defined(_WIN32) && defined(Halide_SHARED)
#ifdef Halide_EXPORTS
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __declspec(dllimport)
#endif
#else
#define EXPORT
#endif

// If we're in user code, we don't want certain functions to be inlined.
#if defined(COMPILING_HALIDE) || defined(BUILDING_PYTHON)
#define NO_INLINE
#else
#ifdef _WIN32
#define NO_INLINE __declspec(noinline)
#else
#define NO_INLINE __attribute__((noinline))
#endif
#endif

namespace Halide {
namespace Internal {

/** Build small vectors of up to 10 elements. If we used C++11 and
 * had vector initializers, this would not be necessary, but we
 * don't want to rely on C++11 support. */
//@{
template<typename T>
std::vector<T> vec(T a) {
    std::vector<T> v(1);
    v[0] = a;
    return v;
}

template<typename T>
std::vector<T> vec(T a, T b) {
    std::vector<T> v(2);
    v[0] = a;
    v[1] = b;
    return v;
}

template<typename T>
std::vector<T> vec(T a, T b, T c) {
    std::vector<T> v(3);
    v[0] = a;
    v[1] = b;
    v[2] = c;
    return v;
}

template<typename T>
std::vector<T> vec(T a, T b, T c, T d) {
    std::vector<T> v(4);
    v[0] = a;
    v[1] = b;
    v[2] = c;
    v[3] = d;
    return v;
}

template<typename T>
std::vector<T> vec(T a, T b, T c, T d, T e) {
    std::vector<T> v(5);
    v[0] = a;
    v[1] = b;
    v[2] = c;
    v[3] = d;
    v[4] = e;
    return v;
}

template<typename T>
std::vector<T> vec(T a, T b, T c, T d, T e, T f) {
    std::vector<T> v(6);
    v[0] = a;
    v[1] = b;
    v[2] = c;
    v[3] = d;
    v[4] = e;
    v[5] = f;
    return v;
}

template<typename T>
std::vector<T> vec(T a, T b, T c, T d, T e, T f, T g) {
    std::vector<T> v(7);
    v[0] = a;
    v[1] = b;
    v[2] = c;
    v[3] = d;
    v[4] = e;
    v[5] = f;
    v[6] = g;
    return v;
}

template<typename T>
std::vector<T> vec(T a, T b, T c, T d, T e, T f, T g, T h) {
    std::vector<T> v(8);
    v[0] = a;
    v[1] = b;
    v[2] = c;
    v[3] = d;
    v[4] = e;
    v[5] = f;
    v[6] = g;
    v[7] = h;
    return v;
}

template<typename T>
std::vector<T> vec(T a, T b, T c, T d, T e, T f, T g, T h, T i) {
    std::vector<T> v(9);
    v[0] = a;
    v[1] = b;
    v[2] = c;
    v[3] = d;
    v[4] = e;
    v[5] = f;
    v[6] = g;
    v[7] = h;
    v[8] = i;
    return v;
}

template<typename T>
std::vector<T> vec(T a, T b, T c, T d, T e, T f, T g, T h, T i, T j) {
    std::vector<T> v(10);
    v[0] = a;
    v[1] = b;
    v[2] = c;
    v[3] = d;
    v[4] = e;
    v[5] = f;
    v[6] = g;
    v[7] = h;
    v[8] = i;
    v[9] = j;
    return v;
}
// @}

/** Convert an integer to a string. */
EXPORT std::string int_to_string(int x);

/** An aggressive form of reinterpret cast used for correct type-punning. */
template<typename DstType, typename SrcType>
DstType reinterpret_bits(const SrcType &src) {
    assert(sizeof(SrcType) == sizeof(DstType));
    DstType dst;
    memcpy(&dst, &src, sizeof(SrcType));
    return dst;
}

/** Make a unique name for an object based on the name of the stack
 * variable passed in. If introspection isn't working or there are no
 * debug symbols, just uses unique_name with the given prefix. */
EXPORT std::string make_entity_name(void *stack_ptr, const std::string &type, char prefix);

/** Generate a unique name starting with the given character. It's
 * unique relative to all other calls to unique_name done by this
 * process. Not thread-safe. */
EXPORT std::string unique_name(char prefix);

/** Generate a unique name starting with the given string.  Not
 * thread-safe. */
EXPORT std::string unique_name(const std::string &name, bool user = true);

/** Test if the first string starts with the second string */
EXPORT bool starts_with(const std::string &str, const std::string &prefix);

/** Test if the first string ends with the second string */
EXPORT bool ends_with(const std::string &str, const std::string &suffix);

/** Replace all matches of the second string in the first string with the last string */
EXPORT std::string replace_all(std::string &str, const std::string &find, const std::string &replace);
    
/** Return the final token of the name string using the given delimiter. */
EXPORT std::string base_name(const std::string &name, char delim = '.');

/** Split the source string using 'delim' as the divider. */
EXPORT std::vector<std::string> split_string(const std::string &source, const std::string &delim);

}
}

#endif
