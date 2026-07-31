#include "Halide.h"

namespace {

// A tiny pipeline with a compute_root intermediate whose extent depends on the
// (runtime) output extent, so it is heap-allocated and therefore exercises the
// runtime's halide_malloc / halide_free (and thus the runtime's custom-malloc
// state). Used by runtime_prefixes_iso_aottest.cpp to check that separately
// namespaced runtimes keep independent state.
class RuntimePrefixesIso : public Halide::Generator<RuntimePrefixesIso> {
public:
    Output<Buffer<int32_t, 1>> output{"output"};

    void generate() {
        Var x;
        Func producer;
        producer(x) = x * 2;
        output(x) = producer(x) + producer(x + 1);
        producer.compute_root();
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(RuntimePrefixesIso, runtime_prefixes_iso)
