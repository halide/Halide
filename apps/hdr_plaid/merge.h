/*
    Adapted (with permission) from https://github.com/timothybrooks/hdr-plus
*/

#ifndef HDRPLAID_MERGE_H_
#define HDRPLAID_MERGE_H_

#include "Halide.h"

/*
 * merge -- fully merges aligned frames in the temporal and spatial
 * dimension to produce one denoised bayer frame.
 */
Halide::Func merge(Halide::Func imgs, Halide::Expr width, Halide::Expr height, Halide::Expr frames, Halide::Func alignment, bool skip_schedule);

#endif