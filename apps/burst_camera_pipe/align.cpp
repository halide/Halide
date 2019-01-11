/*
    Adapted (with permission) from https://github.com/timothybrooks/hdr-plus
*/

#include "align.h"

#include <string>
#include "Halide.h"
#include "point.h"
#include "util.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

/*
 * align_layer -- determines the best offset for tiles of the image at a given resolution provided the offsets for
 * the layer above.
 */
Func align_layer(Func layer, Func prev_alignment, Point prev_min, Point prev_max, bool skip_schedule) {

    Func scores(layer.name() + "_scores");
    Func alignment(layer.name() + "_alignment");

    Var xi, yi, tx, ty, n;
    RDom r0(0, 16, 0, 16);              // reduction over pixels in tile
    RDom r1(-4, 8, -4, 8);              // reduction over search region; extent clipped to 8 for SIMD vectorization

    // offset from the alignment of the previous layer, scaled to this layer. Clamp to bound the amount of memory Halide
    // allocates for the current alignment layer.

    Point prev_offset = DOWNSAMPLE_RATE * clamp(P(prev_alignment(prev_tile(tx), prev_tile(ty), n)), prev_min, prev_max);

    // indices into layer at a specific tile indices and offsets

    Expr x0 = idx_layer(tx, r0.x);
    Expr y0 = idx_layer(ty, r0.y);

    Expr x = x0 + prev_offset.x + xi;
    Expr y = y0 + prev_offset.y + yi;

    // values and L1 distance between reference and alternate layers at specific pixel

    Expr ref_val = layer(x0, y0, 0);
    Expr alt_val = layer(x, y, n);

    Expr dist = abs(i32(ref_val) - i32(alt_val));

    // sum of L1 distances over each pixel in a tile, for the offset specified by xi, yi

    scores(xi, yi, tx, ty, n) = sum(dist);

    // alignment offset for each tile (offset where score is minimum)

    alignment(tx, ty, n) = P(argmin(scores(r1.x, r1.y, tx, ty, n))) + prev_offset;

    ///////////////////////////////////////////////////////////////////////////
    // schedule
    ///////////////////////////////////////////////////////////////////////////
    if (!skip_schedule) {
        scores.compute_at(alignment, tx).vectorize(xi, 8);
        alignment.compute_root().parallel(ty).vectorize(tx, 16);
    }

    return alignment;
}

/*
 * align -- Aligns multiple raw RGGB frames of a scene in T_SIZE x T_SIZE tiles which overlap
 * by T_SIZE_2 in each dimension. align(imgs)(tile_x, tile_y, n) is a point representing the x and y offset
 * for a tile in layer n that most closely matches that tile in the reference (relative to the reference tile's location)
 */
Func align(Func imgs, Halide::Expr width, Halide::Expr height, bool skip_schedule) {

    Func alignment_3("layer_3_alignment");
    Func alignment("alignment");

    Var tx, ty, n;

    // mirror input with overlapping edges

    Func imgs_mirror = BoundaryConditions::mirror_interior(imgs, 0, width, 0, height);

    // downsampled layers for alignment

    Func layer_0 = box_down2(imgs_mirror, "layer_0", skip_schedule);
    Func layer_1 = gauss_down4(layer_0, "layer_1", skip_schedule);
    Func layer_2 = gauss_down4(layer_1, "layer_2", skip_schedule);

    // min and max search regions

    Point min_search = P(-4, -4);
    Point max_search = P(3, 3);

    Point min_3 = P(0, 0);
    Point min_2 = DOWNSAMPLE_RATE * min_3 + min_search;
    Point min_1 = DOWNSAMPLE_RATE * min_2 + min_search;

    Point max_3 = P(0, 0);
    Point max_2 = DOWNSAMPLE_RATE * max_3 + max_search;
    Point max_1 = DOWNSAMPLE_RATE * max_2 + max_search;

    // initial alignment of previous layer is 0, 0

    alignment_3(tx, ty, n) = P(0, 0);

    // hierarchal alignment functions

    Func alignment_2 = align_layer(layer_2, alignment_3, min_3, max_3, skip_schedule);
    Func alignment_1 = align_layer(layer_1, alignment_2, min_2, max_2, skip_schedule);
    Func alignment_0 = align_layer(layer_0, alignment_1, min_1, max_1, skip_schedule);

    // number of tiles in the x and y dimensions

    Expr num_tx = width / T_SIZE_2 - 1;
    Expr num_ty = height / T_SIZE_2 - 1;

    // final alignment offsets for the original mosaic image; tiles outside of the bounds use the nearest alignment offset

    alignment(tx, ty, n) = 2 * P(alignment_0(tx, ty, n));

    Func alignment_repeat = BoundaryConditions::repeat_edge(alignment, 0, num_tx, 0, num_ty);

    return alignment_repeat;
}
