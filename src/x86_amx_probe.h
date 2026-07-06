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

inline bool x86_amx_is_usable() {
#if defined(__linux__) && defined(__x86_64__) && defined(SYS_arch_prctl)
    // This temporarily installs a SIGILL handler, so it should only be called
    // from one-time initialization paths. Current call sites are serialized by
    // get_host_target()'s static initialization and halide_cpu_features_initialized_lock.
    static thread_local sigjmp_buf jmpbuf;
    auto sigill_handler = [](int) {
        siglongjmp(jmpbuf, 1);
    };

    if (syscall(SYS_arch_prctl, ARCH_REQ_XCOMP_PERM, XFEATURE_XTILEDATA) != 0) {
        return false;
    }

    struct sigaction sa = {};
    struct sigaction old_sa = {};
    sa.sa_handler = sigill_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGILL, &sa, &old_sa) != 0) {
        return false;
    }

    bool ok = false;
    if (sigsetjmp(jmpbuf, 1) == 0) {
        // tilerelease %tmm0
        __asm__ __volatile__(".byte 0xc4, 0xe2, 0x78, 0x49, 0xc0" ::: "memory");
        ok = true;
    }

    sigaction(SIGILL, &old_sa, nullptr);
    return ok;
#else
    return true;
#endif
}

}  // namespace Halide::Internal

#endif
