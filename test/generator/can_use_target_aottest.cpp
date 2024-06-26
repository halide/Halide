#include "HalideRuntime.h"
#include <stdio.h>

#if defined(_MSC_VER) || defined(__x86_64__) || defined(__i386__)
#define TESTING_ON_X86 1
#else
#define TESTING_ON_X86 0
#endif

#if TESTING_ON_X86
#if defined(_MSC_VER)
#include <intrin.h>
static void cpuid(int info[4], int infoType, int extra) {
    __cpuidex(info, infoType, extra);
}
#elif defined(_LP64)
static void cpuid(int info[4], int infoType, int extra) {
    __asm__ __volatile__(
        "cpuid                 \n\t"
        : "=a"(info[0]), "=b"(info[1]), "=c"(info[2]), "=d"(info[3])
        : "0"(infoType), "2"(extra));
}
#else
static void cpuid(int info[4], int infoType, int extra) {
    // We save %ebx in case it's the PIC register
    __asm__ __volatile__(
        "mov{l}\t{%%}ebx, %1  \n\t"
        "cpuid                 \n\t"
        "xchg{l}\t{%%}ebx, %1  \n\t"
        : "=a"(info[0]), "=r"(info[1]), "=c"(info[2]), "=d"(info[3])
        : "0"(infoType), "2"(extra));
}
#endif
#endif  // TESTING_ON_X86

struct HostFeatures {
    void set(halide_target_feature_t i) {
        bits[i / 64] |= ((uint64_t)1) << (i % 64);
    }

    bool test(halide_target_feature_t i) const {
        return (bits[i / 64] & ((uint64_t)1) << (i % 64)) != 0;
    }

    static constexpr int kWordCount = (halide_target_feature_end + 63) / (sizeof(uint64_t) * 8);
    uint64_t bits[kWordCount] = {0};
};

int main(int argc, char **argv) {

#if TESTING_ON_X86
    int info[4];
    cpuid(info, 1, 0);

    HostFeatures host_features;
    if (info[2] & (1 << 28)) host_features.set(halide_target_feature_avx);
    if (info[2] & (1 << 19)) host_features.set(halide_target_feature_sse41);
    if (info[2] & (1 << 29)) host_features.set(halide_target_feature_f16c);
    if (info[2] & (1 << 12)) host_features.set(halide_target_feature_fma);

    printf("host_features are: ");
    for (int i = 0; i < host_features.kWordCount; i++) {
        printf("%x %x\n", (unsigned)host_features.bits[i], (unsigned)(host_features.bits[i] >> 32));
    }

    // First, test that the host features are usable. If not, something is wrong.
    if (!halide_can_use_target_features(host_features.kWordCount, host_features.bits)) {
        printf("Failure!\n");
        return 1;
    }

    // Now start subtracting features; we should still be usable.
    // Note that this always ends with testing features=0, which should always pass.
    for (int i = 0; i < (int)halide_target_feature_end; i++) {
        const int word = i / 64;
        const int bit = i % 64;
        if (host_features.bits[word] & (1ULL << bit)) {
            host_features.bits[word] &= ~(1ULL << bit);
            printf("host_features are: %x %x\n", (unsigned)host_features.bits[word], (unsigned)(host_features.bits[word] >> 32));
            if (!halide_can_use_target_features(host_features.kWordCount, host_features.bits)) {
                printf("Failure!\n");
                return 1;
            }
        }
    }
#else
    printf("Warning: this test is not meaningful when run on non-x86 systems.");
#endif
    printf("Success!\n");
    return 0;
}
