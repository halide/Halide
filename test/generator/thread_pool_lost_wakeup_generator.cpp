#include "Halide.h"

namespace {

// The test drives the thread pool directly through the runtime API. This
// trivial pipeline just links a runtime into it.
class ThreadPoolLostWakeup : public Halide::Generator<ThreadPoolLostWakeup> {
public:
    Output<Buffer<int, 1>> output{"output"};

    void generate() {
        Var x{"x"};
        output(x) = x;
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(ThreadPoolLostWakeup, thread_pool_lost_wakeup)
