#ifndef DENORMAL_DISABLER_H_
#define DENORMAL_DISABLER_H_

#ifdef __SSE__
#include <xmmintrin.h>
#endif

class DenormalDisabler {
public:
    DenormalDisabler() {
        // Record how denormals are currently handled so we can restore the current
        // behavior when we exit the current scope and the class destructor is
        // called. Note that the current code relies on the CSR register, which is
        // available on all the x86 cpus that support SSE instructions.
        // AFAIK, flushing denormals to zero is the standard behavior on NVidia
        // GPUs.
#ifdef __SSE__
        // Enable FTZ and DAZ if needed.
        csr_ = _mm_getcsr();
        unsigned int optimized_fp = csr_ | DAZ | FTZ;
        if (csr_ != optimized_fp) {
            _mm_setcsr(optimized_fp);
            need_restore_ = true;
        }
#endif
    }

    ~DenormalDisabler() {
#ifdef __SSE__
        // Restore the state of the FTZ and DAZ bits if needed.
        if (need_restore_) {
            _mm_setcsr(csr_);
        }
#endif
    }

private:
    unsigned int csr_ = 0;
    bool need_restore_ = false;
    // Interpret denormal as zero (DAZ) bit
    static constexpr unsigned int DAZ = 0x0040;
    // Flush denormal to zero (FTZ) bit
    static constexpr unsigned int FTZ = 0x8000;
};

#endif
