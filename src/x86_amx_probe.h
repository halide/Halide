#ifndef HALIDE_X86_AMX_PROBE_H
#define HALIDE_X86_AMX_PROBE_H

#if defined(__linux__) && defined(__x86_64__)
#include <asm/prctl.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>
#ifndef ARCH_REQ_XCOMP_PERM
#define ARCH_REQ_XCOMP_PERM 0x1023
#endif
#ifndef XFEATURE_XTILEDATA
#define XFEATURE_XTILEDATA 18
#endif
#endif

namespace Halide::Internal {

#if defined(__linux__) && defined(__x86_64__) && defined(SYS_arch_prctl)
inline thread_local sigjmp_buf x86_amx_probe_jump_buffer;

inline void x86_amx_probe_sigill_handler(int) {
    siglongjmp(x86_amx_probe_jump_buffer, 1);
}
#endif

inline bool x86_amx_is_usable() {
#if defined(__linux__) && defined(__x86_64__) && defined(SYS_arch_prctl)
    // This temporarily installs a SIGILL handler, so it should only be called
    // from one-time initialization paths. Current call sites are serialized by
    // get_host_target()'s static initialization and halide_cpu_features_initialized_lock.
    // It is not safe to call from another signal handler or from contexts that
    // might nest this probe.

    if (syscall(SYS_arch_prctl, ARCH_REQ_XCOMP_PERM, XFEATURE_XTILEDATA) != 0) {
        return false;
    }

    struct sigaction signal_action = {};
    struct sigaction previous_signal_action = {};
    signal_action.sa_handler = x86_amx_probe_sigill_handler;
    sigemptyset(&signal_action.sa_mask);
    if (sigaction(SIGILL, &signal_action, &previous_signal_action) != 0) {
        return false;
    }

    bool ok = false;
    if (sigsetjmp(x86_amx_probe_jump_buffer, 1) == 0) {
        // tilerelease %tmm0
        __asm__ __volatile__(".byte 0xc4, 0xe2, 0x78, 0x49, 0xc0" ::: "memory");
        ok = true;
    }

    sigaction(SIGILL, &previous_signal_action, nullptr);
    return ok;
#else
    return true;
#endif
}

}  // namespace Halide::Internal

#endif
