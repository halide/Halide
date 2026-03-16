#ifndef HALIDE_FUZZ_HELPERS_H_
#define HALIDE_FUZZ_HELPERS_H_

#define HALIDE_FUZZER_BACKEND_STDLIB 0
#define HALIDE_FUZZER_BACKEND_LIBFUZZER 1

#ifndef HALIDE_FUZZER_BACKEND
#error "HALIDE_FUZZER_BACKEND not defined, defaulting to libFuzzer"
#endif

///////////////////////////////////////////////////////////////////////////////

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

#if HALIDE_FUZZER_BACKEND == HALIDE_FUZZER_BACKEND_LIBFUZZER
#include "fuzzer/FuzzedDataProvider.h"  // IWYU pragma: export
#elif HALIDE_FUZZER_BACKEND == HALIDE_FUZZER_BACKEND_STDLIB
#include "fuzz_main.h"
#include <random>
#endif

namespace Halide {

#if HALIDE_FUZZER_BACKEND == HALIDE_FUZZER_BACKEND_LIBFUZZER
class FuzzingContext : public FuzzedDataProvider {
public:
    using FuzzedDataProvider::FuzzedDataProvider;
    template<typename T>
    T PickValueInVector(std::vector<T> &vec) {
        return vec[ConsumeIntegralInRange<std::size_t>(0, vec.size() - 1)];
    }
};
#elif HALIDE_FUZZER_BACKEND == HALIDE_FUZZER_BACKEND_STDLIB
// IMPORTANT: we don't use std::*_distribution because they are not portable across standard libraries
class FuzzingContext {
public:
    using RandomEngine = std::mt19937_64;
    using SeedType = RandomEngine::result_type;

    explicit FuzzingContext(SeedType seed) : rng(seed) {
    }

    template<typename T>
    T ConsumeIntegral() {
        return static_cast<T>(rng());
    }

    template<typename T>
    T ConsumeIntegralInRange(T min, T max) {
        // If this proves too slow, there are smarter things we can do:
        // https://lemire.me/blog/2019/06/06/nearly-divisionless-random-integer-generation-on-various-systems/
        if (max < min) {
            return min;
        }
        return min + rng() % (max - min + 1);
    }

    bool ConsumeBool() {
        return rng() & 1;
    }

    template<typename T>
    T PickValueInVector(std::vector<T> &vec) {
        return vec[ConsumeIntegralInRange(static_cast<std::size_t>(0), vec.size() - 1)];
    }

    template<typename T>
    auto PickValueInArray(T &array) -> decltype(auto) {
        return array[ConsumeIntegralInRange(static_cast<std::size_t>(0), std::size(array) - 1)];
    }

private:
    RandomEngine rng;
};
#endif

}  // namespace Halide

#if HALIDE_FUZZER_BACKEND == HALIDE_FUZZER_BACKEND_LIBFUZZER
#define FUZZ_TEST(name, signature)                                            \
    static int name##_fuzz_test(signature);                                   \
    extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) { \
        FuzzingContext fdp(data, size);                                       \
        return name##_fuzz_test(fdp);                                         \
    }                                                                         \
    static int name##_fuzz_test(signature)
#elif HALIDE_FUZZER_BACKEND == HALIDE_FUZZER_BACKEND_STDLIB
#define FUZZ_TEST(name, signature)                              \
    static int name##_fuzz_test(signature);                     \
    int main(int argc, char **argv) {                           \
        return Halide::fuzz_main(argc, argv, name##_fuzz_test); \
    }                                                           \
    static int name##_fuzz_test(signature)
#endif

#endif  // HALIDE_FUZZ_HELPERS_H_
