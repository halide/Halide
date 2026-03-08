#ifndef HALIDE_INTERNAL_PERFORMANCE_COUNTER_H
#define HALIDE_INTERNAL_PERFORMANCE_COUNTER_H

#include <cstdint>

// -----------------------------------------------------------------------------
// Platform detection macros
// -----------------------------------------------------------------------------
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <chrono>
#define HALIDE_ARCH_X86
#elif defined(__aarch64__) || defined(_M_ARM64)
#define HALIDE_ARCH_ARM64
#elif defined(__arm__) || defined(_M_ARM)
#define HALIDE_ARCH_ARM32
#else
#define HALIDE_ARCH_FALLBACK
#include <chrono>
#endif

// Include compiler-specific intrinsic headers
#if defined(_MSC_VER) && !defined(HALIDE_ARCH_FALLBACK)
#include <intrin.h>
#elif defined(HALIDE_ARCH_X86)
#include <x86intrin.h>
#endif

namespace Halide {
namespace Internal {

/**
 * Returns the lower 32 bits of a high-resolution, low-overhead hardware cycle counter.
 * This is meant strictly for fine-grained relative profiling.
 */
inline uint64_t performance_counter() {
#if defined(HALIDE_ARCH_X86)
    return __rdtsc();

#elif defined(HALIDE_ARCH_ARM64)
#if defined(_MSC_VER)
    return _ReadStatusReg(ARM64_CNTVCT);
#else
    uint64_t val;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(val));
    return val;
#endif

#elif defined(HALIDE_ARCH_ARM32)
    uint32_t val;
    // Read the 32-bit generic timer counter (CNTVCT equivalent on ARMv7)
    __asm__ volatile("mrc p15, 1, %0, c14, c0, 2" : "=r"(val));
    return val;

#else
    // Fallback using standard C++ chrono (nanosecond resolution)
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
#endif
}

/**
 * Returns the frequency of the performance counter in Megahertz (MHz).
 * Used to convert cycle deltas into real wall-time for Chrome Tracing.
 */
inline double performance_counter_frequency() {
#if defined(HALIDE_ARCH_X86)
    static const double frequency_mhz = []() -> double {
#if defined(HALIDE_OS_MAC)
        // macOS exposes the exact TSC frequency via sysctl
        uint64_t tsc_freq = 0;
        size_t size = sizeof(tsc_freq);
        if (sysctlbyname("machdep.tsc.frequency", &tsc_freq, &size, nullptr, 0) == 0) {
            return static_cast<double>(tsc_freq) / 1e6;
        }
#endif

        // Windows & Linux: Standard C++11 calibration loop using the OS's high-precision
        // wall-clock. We spin for 10 milliseconds to get a near-perfect TSC delta.
        auto t_start = std::chrono::steady_clock::now();
        uint64_t r_start = __rdtsc();

        auto t_end = t_start;
        // Spin without sleeping to prevent OS context-switch jitter
        do {
            t_end = std::chrono::steady_clock::now();
        } while (std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count() < 10);

        uint64_t r_end = __rdtsc();

        // Use the exact nanosecond elapsed time for the division calculation
        double elapsed_sec = std::chrono::duration_cast<std::chrono::nanoseconds>(t_end - t_start).count() / 1e9;
        return static_cast<double>(r_end - r_start) / elapsed_sec / 1e6;
    }();
    return frequency_mhz;
#elif defined(HALIDE_ARCH_ARM64)
// ARM64 exposes the system counter frequency natively.
#if defined(_MSC_VER)
    uint64_t hz = _ReadStatusReg(ARM64_CNTFRQ);
    return static_cast<double>(hz) / 1e6;
#else
    uint64_t hz;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(hz));
    return static_cast<double>(hz) / 1e6;
#endif

#elif defined(HALIDE_ARCH_ARM32)
    // Read the Generic Timer Frequency register (CNTFRQ) on ARMv7
    uint32_t hz;
    __asm__ volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(hz));
    return static_cast<double>(hz) / 1e6;

#else
    // The fallback chrono implementation returns nanoseconds.
    // 1 tick = 1 nanosecond = 1e9 Hz = 1000 MHz.
    return 1000.0;
#endif
}

}  // namespace Internal
}  // namespace Halide

#endif  // HALIDE_INTERNAL_PERFORMANCE_COUNTER_H
