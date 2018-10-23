#include "hexagon_standalone.h"

extern "C" {

// Provide an implementation of qurt to redirect to the appropriate
// simulator calls.
int qurt_hvx_lock(int mode) {
    SIM_ACQUIRE_HVX;
    if (mode == 0) {
        SIM_CLEAR_HVX_DOUBLE_MODE;
    } else {
        SIM_SET_HVX_DOUBLE_MODE;
    }
    return 0;
}

int qurt_hvx_unlock() {
    SIM_RELEASE_HVX;
    return 0;
}

}  // extern "C"
