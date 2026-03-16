#include "fuzz_main.h"
#include "fuzz_helpers.h"

namespace {

template<typename T>
T initialize_rng() {
    constexpr auto kStateWords = T::state_size * sizeof(typename T::result_type) / sizeof(uint32_t);
    std::vector<uint32_t> random(kStateWords);
    std::generate(random.begin(), random.end(), std::random_device{});
    std::seed_seq seed_seq(random.begin(), random.end());
    return T{seed_seq};
}
}  // namespace

namespace Halide {

int fuzz_main(int argc, char **argv, FuzzFunction main_fn) {
    auto seed_generator = initialize_rng<FuzzingContext::RandomEngine>();
    auto seed = seed_generator();

    FuzzingContext ctx{seed};
    return main_fn(ctx);
}

}  // namespace Halide
