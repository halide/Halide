#ifndef FCAM_DEMOSAIC_H
#define FCAM_DEMOSAIC_H

/** \file
 * Converting RAW data to RGB24 by demosiacking and gamma correcting. */
#include "HalideBuffer.h"

namespace FCam {

/** Demosaic, white balance, and gamma correct a raw frame, and
 * return a slightly smaller RGB24 format image. At least four
 * pixels are lost from each side of the image, more if necessary
 * to maintain the following constraint on the output size: The
 * output will have a width that is a multiple of 40, and a height
 * which is a multiple of 24. In order to color correct, this uses
 * the frame's shot's custom color matrix if it exists. Otherwise,
 * it uses the frame's platform's \ref Platform::rawToRGBColorMatrix
 * method to retrieve the correct white-balanced color conversion
 * matrix. */
void demosaic(Halide::Runtime::Buffer<uint16_t> input,
              Halide::Runtime::Buffer<uint8_t> out,
              float colorTemp = 3700.0f,
              float contrast = 50.0f,
              bool denoise = true, int blackLevel = 25,
              int whiteLevel = 1023,
              float gamma = 2.2f);
}

#endif
