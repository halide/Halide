#include "runtime_internal.h"

#define SYS_arch_prctl 158

extern "C" long syscall(long sysno, ...) throw();

extern "C" WEAK int halide_amx_req_perm(void *user_context) {
    constexpr int XFEATURE_XTILECFG = 17;
    constexpr int XFEATURE_XTILEDATA = 18;
    constexpr int ARCH_REQ_XCOMP_PERM = 0x1023;

    // xgetbv instruction should always be present on CPUs with AMX
    long long res = __builtin_ia32_xgetbv(0);

    // if AMX is not supported by the OS these bits are not set
    if (!(res & (1 << XFEATURE_XTILECFG))) {
        return -2;
    }

    if (!(res & (1 << XFEATURE_XTILEDATA))) {
        return -2;
    }

    // we must request permission to use AMX
    long ret = syscall(SYS_arch_prctl, ARCH_REQ_XCOMP_PERM, XFEATURE_XTILEDATA);

    if (ret) {
        return -1;
    }

    return 0;
}
