#ifndef HALIDE_RUNTIME_LINUX_THREAD_ID_COMMON_H
#define HALIDE_RUNTIME_LINUX_THREAD_ID_COMMON_H

#include "HalideRuntime.h"
#include "runtime_internal.h"

#ifndef SYS_GETTID
#error "SYS_GETTID must be defined before including linux_thread_id_common.h"
#endif

extern "C" {

extern int syscall(int num, ...);

WEAK int32_t halide_current_thread_id() {
    const int32_t id = syscall(SYS_GETTID);
    return id ? id : 1;
}
}

#endif  // HALIDE_RUNTIME_LINUX_THREAD_ID_COMMON_H
