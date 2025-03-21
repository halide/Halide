// Always use assert, even if llvm-config defines NDEBUG
#ifdef NDEBUG
#undef NDEBUG
#include <assert.h>
#define NDEBUG
#else
#include <cassert>
#endif

#ifndef HALIDE_UTIL_H
#define HALIDE_UTIL_H

/** \file
 * Various utility functions used internally Halide. */

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "runtime/HalideRuntime.h"

#ifdef Halide_STATIC_DEFINE
#define HALIDE_EXPORT
#else
#if defined(_MSC_VER)
// Halide_EXPORTS is quietly defined by CMake when building a shared library
#ifdef Halide_EXPORTS
#define HALIDE_EXPORT __declspec(dllexport)
#else
#define HALIDE_EXPORT __declspec(dllimport)
#endif
#else
#define HALIDE_EXPORT __attribute__((visibility("default")))
#endif
#endif

// If we're in user code, we don't want certain functions to be inlined.
#if defined(COMPILING_HALIDE) || defined(BUILDING_PYTHON)
#define HALIDE_NO_USER_CODE_INLINE
#else
#define HALIDE_NO_USER_CODE_INLINE HALIDE_NEVER_INLINE
#endif

// Clang uses __has_feature() for sanitizers...
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define HALIDE_INTERNAL_USING_ASAN
#endif
#if __has_feature(memory_sanitizer)
#define HALIDE_INTERNAL_USING_MSAN
#endif
#if __has_feature(thread_sanitizer)
#define HALIDE_INTERNAL_USING_TSAN
#endif
#if __has_feature(coverage_sanitizer)
#define HALIDE_INTERNAL_USING_COVSAN
#endif
#if __has_feature(undefined_behavior_sanitizer)
#define HALIDE_INTERNAL_USING_UBSAN
#endif
#endif

// ...but GCC/MSVC don't like __has_feature, so handle them separately.
// (Only AddressSanitizer for now, not sure if any others are well-supported
// outside of Clang.
#if defined(__SANITIZE_ADDRESS__) && !defined(HALIDE_INTERNAL_USING_ASAN)
#define HALIDE_INTERNAL_USING_ASAN
#endif

namespace Halide {

/** Load a plugin in the form of a dynamic library (e.g. for custom autoschedulers).
 * If the string doesn't contain any . characters, the proper prefix and/or suffix
 * for the platform will be added:
 *
 *   foo -> libfoo.so (Linux/OSX/etc -- note that .dylib is not supported)
 *   foo -> foo.dll (Windows)
 *
 * otherwise, it is assumed to be an appropriate pathname.
 *
 * Any error in loading will assert-fail. */
void load_plugin(const std::string &lib_name);

namespace Internal {

/** Some numeric conversions are UB if the value won't fit in the result;
 * safe_numeric_cast<>() is meant as a drop-in replacement for a C/C++ cast
 * that adds well-defined behavior for the UB cases, attempting to mimic
 * common implementation behavior as much as possible.
 */
template<typename DST, typename SRC,
         typename std::enable_if<std::is_floating_point<SRC>::value>::type * = nullptr>
DST safe_numeric_cast(SRC s) {
    if (std::is_integral<DST>::value) {
        // Treat float -> int as a saturating cast; this is handled
        // in different ways by different compilers, so an arbitrary but safe
        // choice like this is reasonable.
        if (s < (SRC)std::numeric_limits<DST>::min()) {
            return std::numeric_limits<DST>::min();
        }
        if (s > (SRC)std::numeric_limits<DST>::max()) {
            return std::numeric_limits<DST>::max();
        }
    }
    return (DST)s;
}

template<typename DST, typename SRC,
         typename std::enable_if<std::is_integral<SRC>::value>::type * = nullptr>
DST safe_numeric_cast(SRC s) {
    if (std::is_integral<DST>::value) {
        // any-int -> signed-int is technically UB if value won't fit;
        // in practice, common compilers implement such conversions as done below
        // (as verified by exhaustive testing on Clang for x86-64). We could
        // probably continue to rely on that behavior, but making it explicit
        // avoids possible wrather of UBSan and similar debug helpers.
        // (Yes, using sizeof for this comparison is a little odd for the uint->int
        // case, but the intent is to match existing common behavior, which this does.)
        if (std::is_integral<SRC>::value && std::is_signed<DST>::value && sizeof(DST) < sizeof(SRC)) {
            using UnsignedSrc = typename std::make_unsigned<SRC>::type;
            return (DST)(s & (UnsignedSrc)(-1));
        }
    }
    return (DST)s;
}

/** An aggressive form of reinterpret cast used for correct type-punning. */
template<typename DstType, typename SrcType>
DstType reinterpret_bits(const SrcType &src) {
    static_assert(sizeof(SrcType) == sizeof(DstType), "Types must be same size");
    DstType dst;
    memcpy(&dst, &src, sizeof(SrcType));
    return dst;
}

/** Get value of an environment variable. Returns its value
 * is defined in the environment. If the var is not defined, an empty string
 * is returned.
 */
std::string get_env_variable(char const *env_var_name);

/** Get the name of the currently running executable. Platform-specific.
 * If program name cannot be retrieved, function returns an empty string. */
std::string running_program_name();

/** Generate a unique name starting with the given prefix. It's unique
 * relative to all other strings returned by unique_name in this
 * process.
 *
 * The single-character version always appends a numeric suffix to the
 * character.
 *
 * The string version will either return the input as-is (with high
 * probability on the first time it is called with that input), or
 * replace any existing '$' characters with underscores, then add a
 * '$' sign and a numeric suffix to it.
 *
 * Note that unique_name('f') therefore differs from
 * unique_name("f"). The former returns something like f123, and the
 * latter returns either f or f$123.
 */
// @{
std::string unique_name(char prefix);
std::string unique_name(const std::string &prefix);
// @}

/** Test if the first string starts with the second string */
bool starts_with(const std::string &str, const std::string &prefix);

/** Test if the first string ends with the second string */
bool ends_with(const std::string &str, const std::string &suffix);

/** Replace all matches of the second string in the first string with the last string */
std::string replace_all(const std::string &str, const std::string &find, const std::string &replace);

/** Split the source string using 'delim' as the divider. */
std::vector<std::string> split_string(const std::string &source, const std::string &delim);

/** Join the source vector using 'delim' as the divider. */
template<typename T>
std::string join_strings(const std::vector<T> &sources, const std::string &delim) {
    size_t sz = 0;
    if (!sources.empty()) {
        sz += delim.size() * (sources.size() - 1);
    }
    for (const auto &s : sources) {
        sz += s.size();
    }
    std::string result;
    result.reserve(sz);
    bool need_delim = false;
    for (const auto &s : sources) {
        if (need_delim) {
            result += delim;
        }
        result += s;
        need_delim = true;
    }
    return result;
}

/** Perform a left fold of a vector. Returns a default-constructed
 * vector element if the vector is empty. Similar to std::accumulate
 * but with a less clunky syntax. */
template<typename T, typename Fn>
T fold_left(const std::vector<T> &vec, Fn f) {
    T result;
    if (vec.empty()) {
        return result;
    }
    result = vec[0];
    for (size_t i = 1; i < vec.size(); i++) {
        result = f(result, vec[i]);
    }
    return result;
}

/** Returns a right fold of a vector. Returns a default-constructed
 * vector element if the vector is empty. */
template<typename T, typename Fn>
T fold_right(const std::vector<T> &vec, Fn f) {
    T result;
    if (vec.empty()) {
        return result;
    }
    result = vec.back();
    for (size_t i = vec.size() - 1; i > 0; i--) {
        result = f(vec[i - 1], result);
    }
    return result;
}

template<typename... T>
struct meta_and : std::true_type {};

template<typename T1, typename... Args>
struct meta_and<T1, Args...> : std::integral_constant<bool, T1::value && meta_and<Args...>::value> {};

template<typename... T>
struct meta_or : std::false_type {};

template<typename T1, typename... Args>
struct meta_or<T1, Args...> : std::integral_constant<bool, T1::value || meta_or<Args...>::value> {};

template<typename To, typename... Args>
struct all_are_convertible : meta_and<std::is_convertible<Args, To>...> {};

/** Returns base name and fills in namespaces, outermost one first in vector. */
std::string extract_namespaces(const std::string &name, std::vector<std::string> &namespaces);

/** Like extract_namespaces(), but strip and discard the namespaces, returning base name only */
std::string strip_namespaces(const std::string &name);

struct FileStat {
    uint64_t file_size;
    uint32_t mod_time;  // Unix epoch time
    uint32_t uid;
    uint32_t gid;
    uint32_t mode;
};

/** Create a unique file with a name of the form prefixXXXXXsuffix in an arbitrary
 * (but writable) directory; this is typically /tmp, but the specific
 * location is not guaranteed. (Note that the exact form of the file name
 * may vary; in particular, the suffix may be ignored on Windows.)
 * The file is created (but not opened), thus this can be called from
 * different threads (or processes, e.g. when building with parallel make)
 * without risking collision. Note that if this file is used as a temporary
 * file, the caller is responsibly for deleting it. Neither the prefix nor suffix
 * may contain a directory separator.
 */
std::string file_make_temp(const std::string &prefix, const std::string &suffix);

/** Create a unique directory in an arbitrary (but writable) directory; this is
 * typically somewhere inside /tmp, but the specific location is not guaranteed.
 * The directory will be empty (i.e., this will never return /tmp itself,
 * but rather a new directory inside /tmp). The caller is responsible for removing the
 * directory after use.
 */
std::string dir_make_temp();

/** Wrapper for access(). Quietly ignores errors. */
bool file_exists(const std::string &name);

/** assert-fail if the file doesn't exist. useful primarily for testing purposes. */
void assert_file_exists(const std::string &name);

/** assert-fail if the file DOES exist. useful primarily for testing purposes. */
void assert_no_file_exists(const std::string &name);

/** Wrapper for unlink(). Asserts upon error. */
void file_unlink(const std::string &name);

/** Wrapper for unlink(). Quietly ignores errors. */
void file_unlink(const std::string &name);

/** Ensure that no file with this path exists. If such a file
 * exists and cannot be removed, assert-fail. */
void ensure_no_file_exists(const std::string &name);

/** Wrapper for rmdir(). Asserts upon error. */
void dir_rmdir(const std::string &name);

/** Wrapper for stat(). Asserts upon error. */
FileStat file_stat(const std::string &name);

/** Read the entire contents of a file into a vector<char>. The file
 * is read in binary mode. Errors trigger an assertion failure. */
std::vector<char> read_entire_file(const std::string &pathname);

/** Create or replace the contents of a file with a given pointer-and-length
 * of memory. If the file doesn't exist, it is created; if it does exist, it
 * is completely overwritten. Any error triggers an assertion failure. */
void write_entire_file(const std::string &pathname, const void *source, size_t source_len);

inline void write_entire_file(const std::string &pathname, const std::vector<char> &source) {
    write_entire_file(pathname, source.data(), source.size());
}

/** A simple utility class that creates a temporary file in its ctor and
 * deletes that file in its dtor; this is useful for temporary files that you
 * want to ensure are deleted when exiting a certain scope. Since this is essentially
 * just an RAII wrapper around file_make_temp() and file_unlink(), it has the same
 * failure modes (i.e.: assertion upon error).
 */
class TemporaryFile final {
public:
    TemporaryFile(const std::string &prefix, const std::string &suffix)
        : temp_path(file_make_temp(prefix, suffix)) {
    }
    const std::string &pathname() const {
        return temp_path;
    }
    ~TemporaryFile() {
        if (do_unlink) {
            file_unlink(temp_path);
        }
    }
    // You can call this if you want to defeat the automatic deletion;
    // this is rarely what you want to do (since it defeats the purpose
    // of this class), but can be quite handy for debugging purposes.
    void detach() {
        do_unlink = false;
    }

private:
    const std::string temp_path;
    bool do_unlink = true;

public:
    TemporaryFile(const TemporaryFile &) = delete;
    TemporaryFile &operator=(const TemporaryFile &) = delete;
    TemporaryFile(TemporaryFile &&) = delete;
    TemporaryFile &operator=(TemporaryFile &&) = delete;
};

/** Routines to test if math would overflow for signed integers with
 * the given number of bits. */
// @{
bool add_would_overflow(int bits, int64_t a, int64_t b);
bool sub_would_overflow(int bits, int64_t a, int64_t b);
bool mul_would_overflow(int bits, int64_t a, int64_t b);
// @}

/** Routines to perform arithmetic on signed types without triggering signed
 * overflow. If overflow would occur, sets result to zero, and returns
 * false. Otherwise set result to the correct value, and returns true. */
// @{
HALIDE_MUST_USE_RESULT bool add_with_overflow(int bits, int64_t a, int64_t b, int64_t *result);
HALIDE_MUST_USE_RESULT bool sub_with_overflow(int bits, int64_t a, int64_t b, int64_t *result);
HALIDE_MUST_USE_RESULT bool mul_with_overflow(int bits, int64_t a, int64_t b, int64_t *result);
// @}

/** Helper class for saving/restoring variable values on the stack, to allow
 * for early-exit that preserves correctness */
template<typename T>
struct ScopedValue {
    T &var;
    T old_value;
    /** Preserve the old value, restored at dtor time */
    ScopedValue(T &var)
        : var(var), old_value(var) {
    }
    /** Preserve the old value, then set the var to a new value. */
    ScopedValue(T &var, const T &new_value)
        : var(var), old_value(var) {
        var = new_value;
    }
    ~ScopedValue() {
        var = old_value;
    }
    operator T() const {
        return old_value;
    }
    // allow move but not copy
    ScopedValue(const ScopedValue &that) = delete;
    ScopedValue(ScopedValue &&that) noexcept = default;
};

// Helpers for timing blocks of code. Put 'TIC;' at the start and
// 'TOC;' at the end. Timing is reported at the toc via
// debug(0). The calls can be nested and will pretty-print
// appropriately. Took this idea from matlab via Jon Barron.
//
// Note that this uses global state internally, and is not thread-safe
// at all. Only use it for single-threaded debugging sessions.

void halide_tic_impl(const char *file, int line);
void halide_toc_impl(const char *file, int line);
#define HALIDE_TIC Halide::Internal::halide_tic_impl(__FILE__, __LINE__)
#define HALIDE_TOC Halide::Internal::halide_toc_impl(__FILE__, __LINE__)
#ifdef COMPILING_HALIDE
#define TIC HALIDE_TIC
#define TOC HALIDE_TOC
#endif

// statically cast a value from one type to another: this is really just
// some syntactic sugar around static_cast<>() to avoid compiler warnings
// regarding 'bool' in some compliation configurations.
template<typename TO>
struct StaticCast {
    template<typename FROM>
    constexpr static TO value(const FROM &from) {
        if constexpr (std::is_same<TO, bool>::value) {
            return from != 0;
        } else {
            return static_cast<TO>(from);
        }
    }
};

// Like std::is_convertible, but with additional tests for arithmetic types:
// ensure that the value will roundtrip losslessly (e.g., no integer truncation
// or dropping of fractional parts).
template<typename TO>
struct IsRoundtrippable {
    template<typename FROM>
    constexpr static bool value(const FROM &from) {
        if constexpr (std::is_convertible<FROM, TO>::value) {
            if constexpr (std::is_arithmetic<TO>::value &&
                          std::is_arithmetic<FROM>::value &&
                          !std::is_same<TO, FROM>::value) {
                const TO to = static_cast<TO>(from);
                const FROM roundtripped = static_cast<FROM>(to);
                return roundtripped == from;
            } else {
                return true;
            }
        } else {
            return false;
        }
    }
};

template<typename T>
struct reverse_adaptor {
    T &range;
};

template<typename T>
auto begin(reverse_adaptor<T> i) {
    return std::rbegin(i.range);
}

template<typename T>
auto end(reverse_adaptor<T> i) {
    return std::rend(i.range);
}

/**
 * Reverse-order adaptor for range-based for-loops.
 * TODO: Replace with std::ranges::reverse_view when upgrading to C++20.
 */
template<typename T>
reverse_adaptor<T> reverse_view(T &&range) {
    return {range};
}

/** Emit a version of a string that is a valid identifier in C (. is replaced with _)
 * If prefix_underscore is true (the default), an underscore will be prepended if the
 * input starts with an alphabetic character to avoid reserved word clashes.
 */
std::string c_print_name(const std::string &name, bool prefix_underscore = true);

/** Return the LLVM_VERSION against which this libHalide is compiled. This is provided
 * only for internal tests which need to verify behavior; please don't use this outside
 * of Halide tests. */
int get_llvm_version();

}  // namespace Internal

/** Set how much stack the compiler should use for compilation in
 * bytes. This can also be set through the environment variable
 * HL_COMPILER_STACK_SIZE, though this function takes precedence. A
 * value of zero causes the compiler to just use the calling stack for
 * all compilation tasks.
 *
 * Calling this or setting the environment variable should not be
 * necessary. It is provided for three kinds of testing:
 *
 * First, Halide uses it in our internal tests to make sure
 * we're not using a silly amount of stack size on some
 * canary programs to avoid stack usage regressions.
 *
 * Second, if you have a mysterious crash inside a generator, you can
 * set a larger stack size as a way to test if it's a stack
 * overflow. Perhaps our default stack size is not large enough for
 * your program and schedule. Use this call or the environment var as
 * a workaround, and then open a bug with a reproducer at
 * github.com/halide/Halide/issues so that we can determine what's
 * going wrong that is causing your code to use so much stack.
 *
 * Third, perhaps using a side-stack is causing problems with
 * sanitizing, debugging, or profiling tools. If this is a problem,
 * you can set HL_COMPILER_STACK_SIZE to zero to make Halide stay on
 * the main thread's stack.
 */
void set_compiler_stack_size(size_t);

/** The default amount of stack used for lowering and codegen. 32 MB
 * ought to be enough for anyone. */
constexpr size_t default_compiler_stack_size = 32 * 1024 * 1024;

/** Return how much stack size the compiler should use for calls that
 * go through run_with_large_stack below. Currently that's lowering
 * and codegen. If no call to set_compiler_stack_size has been made,
 * this checks the value of the environment variable
 * HL_COMPILER_STACK_SIZE. If that's unset, it returns
 * default_compiler_stack_size, defined above. */
size_t get_compiler_stack_size();

namespace Internal {

/** Call the given action in a platform-specific context that
 * provides at least the stack space returned by
 * get_compiler_stack_size. If that value is zero, just calls the
 * function on the calling thread. Otherwise on Windows this
 * uses a Fiber, and on other platforms it uses swapcontext. */
void run_with_large_stack(const std::function<void()> &action);

/** Portable versions of popcount, count-leading-zeros, and
    count-trailing-zeros. */
// @{
int popcount64(uint64_t x);
int clz64(uint64_t x);
int ctz64(uint64_t x);
// @}

/** Return an integer 2^n, for some n,  which is >= x. Argument x must be > 0. */
inline int64_t next_power_of_two(int64_t x) {
    return static_cast<int64_t>(1) << static_cast<int64_t>(std::ceil(std::log2(x)));
}

template<typename T>
inline T align_up(T x, int n) {
    return (x + n - 1) / n * n;
}

}  // namespace Internal
}  // namespace Halide

#endif
