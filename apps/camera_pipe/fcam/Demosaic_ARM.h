#ifndef FCAM_DEMOSAIC_ARM_H
#define FCAM_DEMOSAIC_ARM_H
//#ifdef FCAM_ARCH_ARM

#include <static_image.h>

// Arm-specific optimized post-processing routines
    
namespace FCam {
void demosaic_ARM(Image<uint16_t> input, Image<uint8_t> out, float colorTemp, float contrast, bool denoise, int blackLevel, float gamma);
}

//#endif
#endif
