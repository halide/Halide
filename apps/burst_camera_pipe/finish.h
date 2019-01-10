/*
    Adapted (with permission) from https://github.com/timothybrooks/hdr-plus
*/

#ifndef BURST_CAMERA_PIPE_FINISH_H_
#define BURST_CAMERA_PIPE_FINISH_H_

#include "Halide.h"

struct WhiteBalance {
  Halide::Expr r;
  Halide::Expr g0;
  Halide::Expr g1;
  Halide::Expr b;
};

/*
 * finish -- Applies a series of standard local and global image processing
 * operations to an input mosaicked image, producing a pleasant color output.
 * Input pecifies black-level, white-level and white balance. Additionally,
 * tone mapping is applied to the image, as specified by the input compression
 * and gain amounts. This produces natural-looking brightened shadows, without
 * blowing out highlights. The output values are 8-bit.
 */
Halide::Func finish(Halide::Func input, Halide::Expr width, Halide::Expr height, Halide::Expr bp, Halide::Expr wp,
  const WhiteBalance &wb, Halide::Expr c, Halide::Expr g, bool skip_schedule);

#endif