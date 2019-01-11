#include "runtime_internal.h"
#include "HalideRuntimeQurt.h"

extern "C" {
__attribute__((weak)) void HP_profile(unsigned int marker, unsigned char enable);
WEAK uint32_t halide_qurt_sysmon_enabled = 0;
WEAK volatile uint32_t halide_qurt_sysmon_marker = 0;
WEAK volatile uint32_t halide_sysmon_lock = 0;

// Generate a sysmon marker with the specifid ID
WEAK void halide_sysmon_marker(uint32_t next_marker) {
    if (!halide_qurt_sysmon_enabled) {
        return;
    }
    if (halide_qurt_sysmon_marker == next_marker) {
        return;                         // Don't redundantly set marker
    }

    while (!__sync_bool_compare_and_swap(&halide_sysmon_lock, 0, 1)) { }
    uint32_t last_marker = halide_qurt_sysmon_marker;   // Get value after lock
    if (last_marker != next_marker) {   // Only change if we have a new ID
        halide_qurt_sysmon_marker = next_marker;
        if (last_marker != 0) {
            HP_profile(last_marker, 0); // Stop last marker
        }
        if (next_marker != 0) {
            HP_profile(next_marker, 1); // Start new marker
        }
    }
    __sync_synchronize();
    halide_sysmon_lock = 0;
}

WEAK void halide_sysmon_start(void) {
    halide_qurt_sysmon_enabled = (HP_profile != NULL);
}
WEAK void halide_sysmon_stop(void) {
    halide_sysmon_marker(0);
    halide_qurt_sysmon_enabled = 0;
}
WEAK void halide_set_sysmon_marker(uint32_t marker) {
    halide_qurt_sysmon_marker = marker;
}
}
