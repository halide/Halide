/*
    Adapted (with permission) from https://github.com/timothybrooks/hdr-plus
*/

#include "merge.h"

#include "Halide.h"
#include "align.h"
#include "point.h"
#include "util.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

/*
 * merge_temporal -- combines aligned tiles in the temporal dimension by
 * weighting various frames based on their L1 distance to the reference frame's
 * tile. Thresholds L1 scores so that tiles above a certain distance are completely
 * discounted, and tiles below a certain distance are assumed to be perfectly aligned.
 */
Func merge_temporal(Func imgs, Expr width, Expr height, Expr frames, Func alignment, bool skip_schedule) {

    Func weight("merge_temporal_weights");
    Func total_weight("merge_temporal_total_weights");
    Func output("merge_temporal_output");

    Var ix, iy, tx, ty, n;
    RDom r0(0, 16, 0, 16);                          // reduction over pixels in downsampled tile
    RDom r1(1, frames - 1);                 // reduction over alternate images

    // mirror input with overlapping edges

    Func imgs_mirror = BoundaryConditions::mirror_interior(imgs, 0, width, 0, height);

    // downsampled layer for computing L1 distances

    Func layer = box_down2(imgs_mirror, "merge_layer", skip_schedule);

    // alignment offset, indicies and pixel value expressions; used twice in different reductions

    Point offset;
    Expr al_x, al_y, ref_val, alt_val;

    // expressions for summing over pixels in each tile

    offset = clamp(P(alignment(tx, ty, n)), P(MIN_OFFSET, MIN_OFFSET), P(MAX_OFFSET, MAX_OFFSET));

    al_x = idx_layer(tx, r0.x) + offset.x / 2;
    al_y = idx_layer(ty, r0.y) + offset.y / 2;

    ref_val = layer(idx_layer(tx, r0.x), idx_layer(ty, r0.y), 0);
    alt_val = layer(al_x, al_y, n);

    // constants for determining strength and robustness of temporal merge

    float factor = 8.f;                         // factor by which inverse function is elongated
    int min_dist = 10;                          // pixel L1 distance below which weight is maximal
    int max_dist = 300;                         // pixel L1 distance above which weight is zero

    // average L1 distance in tile and distance normalized to min and factor

    Expr dist = sum(abs(i32(ref_val) - i32(alt_val))) / 256;

    Expr norm_dist = max(1, i32(dist) / factor - min_dist / factor);

    // weight for each tile in temporal merge; inversely proportional to reference and alternate tile L1 distance

    weight(tx, ty, n) = select(norm_dist > (max_dist - min_dist), 0.f, 1.f / norm_dist);

    // total weight for each tile in a temporal stack of images

    total_weight(tx, ty) = sum(weight(tx, ty, r1)) + 1.f;              // additional 1.f accounting for reference image

    // expressions for summing over images at each pixel

    offset = P(alignment(tx, ty, r1));

    al_x = idx_im(tx, ix) + offset.x;
    al_y = idx_im(ty, iy) + offset.y;

    ref_val = imgs_mirror(idx_im(tx, ix), idx_im(ty, iy), 0);
    alt_val = imgs_mirror(al_x, al_y, r1);

    // temporal merge function using weighted pixel values

    output(ix, iy, tx, ty) = sum(weight(tx, ty, r1) * alt_val / total_weight(tx, ty)) + ref_val / total_weight(tx, ty);

    ///////////////////////////////////////////////////////////////////////////
    // schedule
    ///////////////////////////////////////////////////////////////////////////
    if (!skip_schedule) {
        weight.compute_root().parallel(ty).vectorize(tx, 16);
        total_weight.compute_root().parallel(ty).vectorize(tx, 16);
        output.compute_root().parallel(ty).vectorize(ix, 32);
    }
    return output;
}

/*
 * merge_spatial -- smoothly blends between half-overlapped tiles in the spatial
 * domain using a raised cosine filter.
 */
Func merge_spatial(Func input, bool skip_schedule) {

    Func weight("raised_cosine_weights");
    Func output("merge_spatial_output");

    Var v, x, y;

    // (modified) raised cosine window for determining pixel weights

    float pi = 3.141592f;
    weight(v) = 0.5f - 0.5f * cos(2 * pi * (v + 0.5f) / T_SIZE);

    // tile weights based on pixel position

    Expr weight_00 = weight(idx_0(x)) * weight(idx_0(y));
    Expr weight_10 = weight(idx_1(x)) * weight(idx_0(y));
    Expr weight_01 = weight(idx_0(x)) * weight(idx_1(y));
    Expr weight_11 = weight(idx_1(x)) * weight(idx_1(y));

    // values of pixels from each overlapping tile

    Expr val_00 = input(idx_0(x), idx_0(y), tile_0(x), tile_0(y));
    Expr val_10 = input(idx_1(x), idx_0(y), tile_1(x), tile_0(y));
    Expr val_01 = input(idx_0(x), idx_1(y), tile_0(x), tile_1(y));
    Expr val_11 = input(idx_1(x), idx_1(y), tile_1(x), tile_1(y));

    // spatial merge function using weighted pixel values

    output(x, y) = u16(weight_00 * val_00
                     + weight_10 * val_10
                     + weight_01 * val_01
                     + weight_11 * val_11);

    ///////////////////////////////////////////////////////////////////////////
    // schedule
    ///////////////////////////////////////////////////////////////////////////
    if (!skip_schedule) {
        weight.compute_root().vectorize(v, 32);
        output.compute_root().parallel(y).vectorize(x, 32);
    }
    return output;
}

/*
 * merge -- fully merges aligned frames in the temporal and spatial
 * dimension to produce one denoised bayer frame.
 */
Func merge(Func imgs, Expr width, Expr height, Expr frames, Func alignment, bool skip_schedule) {

    Func merge_temporal_output = merge_temporal(imgs, width, height, frames, alignment, skip_schedule);

    return merge_spatial(merge_temporal_output, skip_schedule);
}
