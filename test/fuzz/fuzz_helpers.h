#ifndef HALIDE_FUZZ_HELPERS_H_
#define HALIDE_FUZZ_HELPERS_H_

#include "fuzzer/FuzzedDataProvider.h"
#include <vector>

namespace Halide {

template<typename T>
inline T pick_value_in_vector(FuzzedDataProvider &fdp, std::vector<T> &vec) {
    return vec[fdp.ConsumeIntegralInRange<size_t>(0, vec.size() - 1)];
}
}  // namespace Halide

#endif  // HALIDE_FUZZ_HELPERS_H_