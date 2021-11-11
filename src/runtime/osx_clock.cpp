#include "HalideRuntime.h"

struct mach_timebase_info {
    uint32_t numer;
    uint32_t denom;
};

typedef struct mach_timebase_info *mach_timebase_info_t;
typedef struct mach_timebase_info mach_timebase_info_data_t;

typedef int kern_return_t;

namespace Halide {
namespace Runtime {
namespace Internal {
WEAK bool halide_reference_clock_inited = false;
WEAK uint64_t halide_reference_clock = 0;
WEAK mach_timebase_info_data_t halide_timebase_info;
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

extern "C" {

extern uint64_t mach_absolute_time(void);
extern kern_return_t mach_timebase_info(mach_timebase_info_t info);

WEAK int halide_start_clock(void *user_context) {
    // Guard against multiple calls
    if (!halide_reference_clock_inited) {
        mach_timebase_info(&halide_timebase_info);
        halide_reference_clock = mach_absolute_time();
        halide_reference_clock_inited = true;
    }

    return 0;
}

WEAK int64_t halide_current_time_ns(void *user_context) {
    // It is an error to call halide_current_time_ns() if halide_start_clock() has never been called
    halide_debug_assert(user_context, halide_reference_clock_inited);

    uint64_t now = mach_absolute_time();
    return (now - halide_reference_clock) * halide_timebase_info.numer / halide_timebase_info.denom;
}

extern int usleep(int);
WEAK void halide_sleep_ms(void *user_context, int ms) {
    usleep(ms * 1000);
}
}
