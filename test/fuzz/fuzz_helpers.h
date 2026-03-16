#ifndef HALIDE_FUZZ_HELPERS_H_
#define HALIDE_FUZZ_HELPERS_H_

#define HALIDE_FUZZER_BACKEND_STDLIB 0
#define HALIDE_FUZZER_BACKEND_LIBFUZZER 1

#ifndef HALIDE_FUZZER_BACKEND
#warning "HALIDE_FUZZER_BACKEND not defined, defaulting to libFuzzer"
#define HALIDE_FUZZER_BACKEND HALIDE_FUZZER_BACKEND_LIBFUZZER
#endif

///////////////////////////////////////////////////////////////////////////////

#if HALIDE_FUZZER_BACKEND == HALIDE_FUZZER_BACKEND_LIBFUZZER
#include "fuzzer/FuzzedDataProvider.h"  // IWYU pragma: export
#endif

#include <cstddef>
#include <vector>

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
#endif

}  // namespace Halide

#define FUZZ_TEST(name, signature)                                            \
    static int name##_fuzz_test(signature);                                   \
    extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) { \
        FuzzingContext fdp(data, size);                                       \
        return name##_fuzz_test(fdp);                                         \
    }                                                                         \
    static int name##_fuzz_test(signature)

#endif  // HALIDE_FUZZ_HELPERS_H_
