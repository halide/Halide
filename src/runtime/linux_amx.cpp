#include "HalideRuntime.h"
#include "printer.h"
#include "runtime_internal.h"

#define SYS_arch_prctl 158

extern "C" int syscall(long sysno, ...) throw();

extern "C" WEAK int halide_enable_amx() {
    constexpr int XFEATURE_XTILECFG = 17;
    constexpr int XFEATURE_XTILEDATA = 18;
    constexpr int ARCH_REQ_XCOMP_PERM = 0x1023;

    // xgetbv instruction should always be present on CPUs with AMX
    long long res = __builtin_ia32_xgetbv(0);

    // if AMX is not supported by the OS these bits are not set
    if (!(res & (1 << XFEATURE_XTILECFG))) {
        error(nullptr) << "XTILECFG not available for AMX instructions.\n";
        return -2;
    }

    if (!(res & (1 << XFEATURE_XTILEDATA))) {
        error(nullptr) << "XTILEDATA not available for AMX instruction.\n";
        return -2;
    }

    // we must request permission to use AMX
    auto ret = syscall(SYS_arch_prctl, ARCH_REQ_XCOMP_PERM, XFEATURE_XTILEDATA);

    if (ret) {
        error(nullptr) << "Failed to enable AMX instructions: " << ret << '\n';
        return -1;
    }

    debug(nullptr) << "AMX permissions acquired\n";

    return 0;
}
