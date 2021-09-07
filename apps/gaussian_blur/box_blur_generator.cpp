// This file defines a generator for a first order IIR low pass filter
// for a 2D image.

#include "Halide.h"

using namespace Halide;
using namespace Halide::BoundaryConditions;

class BoxBlur : public Generator<BoxBlur> {
public:
    Input<Buffer<uint8_t>> input{"input", 2};
    Input<int> radius{"radius"};
    Input<int> out_width{"out_width"}, out_height{"out_height"};
    Output<Buffer<uint8_t>> intermediate{"intermediate", 2};
    Output<Buffer<uint8_t>> output{"output", 2};

    Var x{"x"}, y{"y"};

    Func blur_cols_transpose(Func in, Expr height, bool first_pass) {
        Expr diameter = 2 * radius + 1;
        Expr inv_diameter = 1.f / diameter;
        RDom r_init(-radius, diameter);
        RDom ry(1, height - 1);

        Func wrap("wrap");
        wrap(x, y) = in(x, y);

        // Transpose the input
        Func transpose("transpose");
        transpose(x, y) = wrap(y, x);

        // Blur in y
        std::vector<Func> blurs, dithered;
        for (Type t : {UInt(16), UInt(32)}) {

            const bool should_dither = true;

            auto normalize = [&](Expr num) {
                if (!should_dither) {
                    // Exact integer division using tricks in the spirit of Hacker's Delight.
                    Type wide = t.with_bits(t.bits() * 2);
                    Expr shift = 31 - count_leading_zeros(diameter);
                    Expr wide_one = cast(wide, 1);
                    Expr mul = (wide_one << (t.bits() + shift + 1)) / diameter - (1 << t.bits()) + 1;
                    num += diameter / 2;
                    Expr e = cast(wide, num);
                    e *= mul;
                    e = e >> t.bits();
                    e = cast(t, e);
                    e += (num - e) / 2;
                    e = e >> shift;
                    e = cast<uint8_t>(e);
                    return e;
                } else {
                    return cast<uint8_t>(floor(num * inv_diameter + random_float()));
                }
            };

            Func blur{"blur_" + std::to_string(t.bits())};
            blur(x, y) = undef(t);
            blur(x, 0) = cast(t, 0);
            blur(x, 0) += cast(t, transpose(x, r_init));

            // Derivative of a box
            Expr v =
                (cast(Int(16), transpose(x, ry + radius)) -
                 transpose(x, ry - radius - 1));

            // It's a 9-bit signed integer. Sign-extend then treat it as a
            // uint16/32 with wrap-around. We know that the result can't
            // possibly be negative in the end, so this gives us an extra
            // bit of headroom while accumulating.
            v = cast(t, cast(Int(t.bits()), v));

            blur(x, ry) = blur(x, ry - 1) + v;

            blurs.push_back(blur);

            Func dither;
            dither(x, y) = normalize(blur(x, y));
            dithered.push_back(dither);
        }

        const int vec = get_target().natural_vector_size<uint16_t>();

        Func out;
        out(x, y) = select(diameter < 256, dithered[0](x, y), dithered[1](x, y));

        // Schedule.  Split the transpose into tiles of
        // rows. Parallelize strips.
        Var xo, yo, xi, yi, xoo;
        out
            .compute_root()
            .split(x, xoo, xo, vec * 2)
            .split(xo, xo, xi, vec)
            .reorder(xi, y, xo, xoo)
            .vectorize(xi)
            .parallel(xoo);

        // Run the filter on each row of tiles (which corresponds to a strip of
        // columns in the input).
        for (int i = 0; i < 2; i++) {
            Func blur = blurs[i];
            Func dither = dithered[i];
            blur.compute_at(out, xo)
                .store_in(MemoryType::Stack);

            blur.update(0).vectorize(x);
            blur.update(1).vectorize(x);

            // Vectorize computations within the strips.
            blur.update(2)
                .reorder(x, ry)
                .vectorize(x);

            dither
                .compute_at(out, y)
                .vectorize(x);
        }

        transpose
            .compute_at(out, xo)
            .store_in(MemoryType::Stack)
            .split(y, yo, yi, vec)
            .unroll(x)
            .vectorize(yi);

        wrap
            .compute_at(transpose, yo)
            .store_in(MemoryType::Register)
            .vectorize(x)
            .unroll(y);

        out.specialize(diameter < 256);

        return out;
    }

    void generate() {
        // First, blur the columns of the input.
        Func blury_T = blur_cols_transpose(input, out_width, true);

        intermediate = blury_T;

        // Blur the columns again (the rows of the original).
        Func blur = blur_cols_transpose(blury_T, out_height, false);

        output = blur;
    }
};

HALIDE_REGISTER_GENERATOR(BoxBlur, box_blur)

class BoxBlurLog : public Generator<BoxBlurLog> {
public:
    Input<Buffer<uint8_t>> input{"input", 2};
    Input<int> radius{"radius"};
    Output<Buffer<uint8_t>> output{"output", 2};

    void generate() {
        Expr diameter = cast<uint32_t>(2 * radius + 1);
        Var x, y;
        Func clamped = BoundaryConditions::repeat_edge(input);

        Func in16;
        in16(x, y) = cast<uint16_t>(clamped(x, y));

        // Assume diameter < 256
        std::vector<Func> horiz_blurs, vert_blurs;
        Expr result = in16(x, y - radius);
        Expr offset = -radius + 1;
        Func prev = in16;
        for (int i = 0; i < 8; i++) {
            Func next("blur_y_" + std::to_string(1 << i));
            next(x, y) = prev(x, y) + prev(x, y + (1 << i));
            prev = next;
            vert_blurs.push_back(next);

            Expr use_this = ((diameter >> (i + 1)) & 1) == 1;
            result += select(use_this, next(x, y + offset), 0);
            offset += select(use_this, (1 << i), 0);
        }

        Func blur_y;
        blur_y(x, y) = cast<uint8_t>(clamp((result + diameter / 2) / diameter, 0, 255));

        horiz_blurs.push_back(blur_y);

        result = blur_y(x - radius, y);
        offset = -radius + 1;
        prev = blur_y;
        for (int i = 0; i < 8; i++) {
            Func next("blur_x_" + std::to_string(1 << i));
            next(x, y) = prev(x, y) + prev(x + (1 << i), y);
            prev = next;
            horiz_blurs.push_back(next);

            Expr use_this = ((diameter >> (i + 1)) & 1) == 1;
            result += select(use_this, next(x + offset, y), 0);
            offset += select(use_this, (1 << i), 0);
        }

        output(x, y) = cast<uint8_t>(clamp((result + diameter / 2) / diameter, 0, 255));

        Var yi, yo;
        output
            .vectorize(x, natural_vector_size<uint8_t>())
            .split(y, yo, yi, 64, TailStrategy::GuardWithIf)
            .parallel(yo);

        clamped.compute_at(output, yo).vectorize(_0, natural_vector_size<uint8_t>());

        for (Func b : vert_blurs) {
            b
                .compute_at(output, yo)
                .store_in(MemoryType::Stack)
                .vectorize(x, natural_vector_size<uint16_t>());
        }

        for (Func b : horiz_blurs) {
            b
                .compute_at(output, yi)
                .store_in(MemoryType::Stack)
                .vectorize(x, natural_vector_size<uint16_t>());
        }
    }
};

HALIDE_REGISTER_GENERATOR(BoxBlurLog, box_blur_log)

class BoxBlurPyramid : public Generator<BoxBlurPyramid> {
public:
    Input<Buffer<>> input{"input", 2};
    Input<int> diameter{"diameter"};
    Input<int> width{"width"};
    Input<int> tx_stride{"tx_stride"};
    Output<Buffer<>> output{"output", 3};

    void generate() {
        Var x("x"), y("y"), ty("ty"), tx("tx"), yo("yo"), yi("yi"), xo("xo"), xi("xi");

        // Figure out our intermediate accumulator types
        Type small_sum_type, large_sum_type, diff_type;
        int max_count_for_small_sum, max_count_for_large_sum;
        const int bits = input.type().bits();
        if (input.type().is_float()) {
            small_sum_type = input.type();
            diff_type = input.type();
            large_sum_type = input.type().with_bits(bits * 2);
            // This is approximate. If we wanted an exact float blur,
            // this should be set to 1.
            max_count_for_small_sum = 256;
            max_count_for_large_sum = 0x7fffffff;
        } else {
            small_sum_type = UInt(bits * 2);
            diff_type = Int(bits * 2);
            large_sum_type = UInt(bits * 4);
            max_count_for_small_sum = 1 << bits;
            max_count_for_large_sum = std::min(0x7fffffffULL, 1ULL << (bits * 3));
        }

        const int N = 8;

        const int vec = bits <= 16 ? 16 : 8;
        Func input_clamped;
        input_clamped(x, y) = input(x, clamp(likely(y), 0, input.dim(1).max()));

        Func input_tiled;
        input_tiled(x, tx, y) = input_clamped(x + tx * tx_stride, y);

        // We use slightly different algorithms as a function of the
        // max diameter supported. They get muxed together at the end.

        // For large radius, we'll downsample in y by a factor proportionate
        // to sqrt(diameter) ahead of time. We pick sqrt(diameter)
        // because it equalizes the number of samples taken inside the
        // low res and high res images, giving the best computational
        // complexity.

        // We'll put a lower bound on the downsampling factor so that
        // the intermediate downsampled image is likely to be
        // in-cache.
        Expr down_factor = N;
        Expr group_factor = clamp(diameter / (2 * N), 1, 32);

        Expr coarse_offset = 0;

        Func down_y_1("down_y_1"), down_y_2("down_y_2");
        RDom r_down_1(0, down_factor);
        down_y_1(x, y) += cast(small_sum_type, input_clamped(x, y * down_factor + r_down_1 - coarse_offset));

        RDom r_down_2(1, group_factor - 1);
        down_y_2(x, y, ty) = undef(small_sum_type);
        down_y_2(x, 0, ty) = down_y_1(x, ty * group_factor);
        down_y_2(x, r_down_2, ty) = down_y_1(x, ty * group_factor + r_down_2) + down_y_2(x, r_down_2 - 1, ty);

        Func down_y("down_y");
        down_y(x, y, ty) = down_y_2(x, y, ty);

        // The maximum diameter at which we should just use a direct
        // blur in x, instead of a sum-scan.  Tuned
        // empirically.
        const int max_diameter_direct_blur_x = 8;

        // The maximum diameter at which we can get away with
        // low-precision accumulators for the blur in x and the blur
        // in y. Must be <= 16 for uint8 inputs or we'll get
        // overflow.
        const int max_diameter_low_bit_blur_x = std::sqrt(max_count_for_small_sum);

        // The maximum diameter at which we should do a direct blur in
        // y for the first scanline of each strip. Above this we use
        // the precomputed downsampled-in-y Func. Tuned empirically.
        const int max_diameter_direct_blur_y = 80;

        // The maximum diameter at which we can use a low-precision
        // accumulator for the blur in y. Must be <= 256 for uint8
        // inputs or we'll get overflow
        const int max_diameter_low_bit_blur_y = max_count_for_small_sum;

        std::set<int> max_diameters{max_diameter_direct_blur_x,
                                    max_diameter_low_bit_blur_x,
                                    max_diameter_direct_blur_y,
                                    max_diameter_low_bit_blur_y,
                                    max_count_for_large_sum};

        std::vector<Expr> results, conditions;
        for (int max_diameter : max_diameters) {

            Func blur_y_init("blur_y_init");
            Func blur_y("blur_y");

            // Slice the footprint of the vertical blur into three pieces.
            Expr fine_start_1 = ty * N;
            Expr fine_end_2 = ty * N + diameter;

            Expr coarse_start = (fine_start_1 - 1 + coarse_offset) / down_factor + 1;
            Expr coarse_end = (fine_end_2 + coarse_offset) / down_factor;

            Expr fine_end_1 = coarse_start * down_factor - coarse_offset;
            Expr fine_start_2 = coarse_end * down_factor - coarse_offset;

            Expr coarse_pieces = coarse_end - coarse_start;
            Expr fine_pieces_1 = fine_end_1 - fine_start_1;
            Expr fine_pieces_2 = fine_end_2 - fine_start_2;

            // How many complete groups of aligned group_factor-sized
            // groups of coarse pieces does our coarse component span?
            // There will also be partial groups at the start and end.
            Expr coarse_group_start = coarse_start / group_factor;
            Expr coarse_group_end = coarse_end / group_factor;
            Expr coarse_group_pieces = coarse_group_end - coarse_group_start;

            // The group index of the coarse piece just before the
            // start of the filter footprint, assuming coarse_start is
            // not a multiple of group_factor.
            Expr partial_group_1_idx = max(0, coarse_start / group_factor);

            // The group index of the last coarse piece.
            Expr partial_group_2_idx = max(0, (coarse_end - 1) / group_factor);

            // The within-group index of the first coarse piece just
            // before the start of the filter footprint.
            Expr partial_group_1_subidx = (coarse_start % group_factor) - 1;

            // The within-group index of the last coarse piece in the
            // footprint.
            Expr partial_group_2_subidx = (coarse_end % group_factor) - 1;

            // An empirically-tuned threshold for when it starts making
            // sense to use the downsampled-in-y input to boost the
            // initial blur.
            const bool use_down_y = max_diameter > max_diameter_direct_blur_y;

            RDom ry_init_fine_1(0, down_factor - 1);
            ry_init_fine_1.where(ry_init_fine_1 < fine_pieces_1);

            RDom ry_init_coarse(0, (diameter - 1) / (down_factor * group_factor) + 1);
            ry_init_coarse.where(ry_init_coarse < coarse_group_pieces);

            RDom ry_init_fine_2(0, down_factor - 1);
            ry_init_fine_2.where(ry_init_fine_2 < fine_pieces_2);

            RDom ry_init_full(0, diameter);

            Type blur_y_t = (max_diameter <= max_diameter_low_bit_blur_y) ? small_sum_type : large_sum_type;

            if (use_down_y) {
                // Start with the two partial coarse groups.
                blur_y_init(x, tx, ty) =
                    (cast(blur_y_t, select(partial_group_2_subidx >= 0,
                                           down_y(x + tx * tx_stride, max(0, partial_group_2_subidx), partial_group_2_idx), 0)) -
                     select(partial_group_1_subidx >= 0, down_y(x + tx * tx_stride, max(0, partial_group_1_subidx), partial_group_1_idx), 0));

                // Now add entire coarse groups. Each group is sum-scanned, so
                // to add the entire group we can just access its last
                // element.
                Expr dy = coarse_group_start + ry_init_coarse;
                blur_y_init(x, tx, ty) += cast(blur_y_t, down_y(x + tx * tx_stride, group_factor - 1, dy));

                // Now add individual scanlines at the start and
                // end. We do the ones at the start last, because
                // we're about to subtract them to do the sliding
                // window update, so we get better temporal locality
                // that way.
                blur_y_init(x, tx, ty) += cast(blur_y_t, input_tiled(x, tx, fine_start_2 + ry_init_fine_2));
                blur_y_init(x, tx, ty) += cast(blur_y_t, input_tiled(x, tx, fine_start_1 + ry_init_fine_1));
            } else {
                blur_y_init(x, tx, ty) += cast(blur_y_t, input_tiled(x, tx, ty * N + ry_init_full));
            }

            // Compute the other in-between scanlines by incrementally
            // updating that one in a sliding window.
            Func diff_y("diff_y");
            diff_y(x, tx, ty, y) =
                (cast(diff_type, input_tiled(x, tx, ty * N + y + diameter)) -
                 input_tiled(x, tx, ty * N + y));

            RDom ry_scan(0, N - 1);
            blur_y(x, tx, ty, y) = undef(blur_y_t);
            blur_y(x, tx, ty, 0) = blur_y_init(x, tx, ty);
            blur_y(x, tx, ty, ry_scan + 1) =
                (blur_y(x, tx, ty, ry_scan) + cast(blur_y_t, diff_y(x, tx, ty, ry_scan)));

            // For large diameter, we do the blur in x using the regular
            // sliding window approach.

            const bool use_blur_x_direct = max_diameter <= max_diameter_direct_blur_x;

            Type blur_x_t = (max_diameter <= max_diameter_low_bit_blur_x) ? small_sum_type : large_sum_type;

            const int integrate_vec = natural_vector_size(blur_y_t);

            Func integrate_x("integrate_x");
            integrate_x(x, tx, ty, y) = undef(blur_x_t);
            integrate_x(-1, tx, ty, y) = cast(blur_x_t, 0);
            RDom rx_scan(0, integrate_vec, 0, ((width + diameter) / integrate_vec));
            Expr rx = rx_scan.x + integrate_vec * rx_scan.y;
            integrate_x(rx, tx, ty, y) =
                (integrate_x(rx - 1, tx, ty, y) +
                 blur_y(rx, tx, ty, y));
            RDom rx_tail(((width + diameter) / integrate_vec) * integrate_vec, (width + diameter) % integrate_vec);
            rx = clamp(rx_tail, 0, width + diameter - 2);
            integrate_x(rx, tx, ty, y) =
                (integrate_x(rx - 1, tx, ty, y) +
                 blur_y(rx, tx, ty, y));

            Func blur_x("blur_x");
            blur_x(x, tx, ty, y) = integrate_x(x + diameter - 1, tx, ty, y) - integrate_x(x - 1, tx, ty, y);

            Func blur_y_untiled("blur_y_untiled");
            blur_y_untiled(x, tx, y) = blur_y(x, tx, y / N, y % N);

            // For small diameter, we do it directly and stay in 16-bit
            Func blur_x_direct("blur_x_direct");
            RDom rx_direct(0, diameter);
            blur_x_direct(x, tx, y) += blur_y_untiled(x + rx_direct, tx, y);

            auto norm = [&](Expr e) {
                if (e.type().bits() <= 32) {
                    // TODO: This is a bit suspect for blurs that produce between 23 and 32 bits.
                    e = cast<float>(e);
                } else {
                    e = cast<double>(e);
                }
                Expr den = cast(e.type(), diameter * diameter);
                Expr result = e * (1 / den);
                if (!input.type().is_float()) {
                    result = round(result);
                }
                return cast(input.type(), result);
            };

            Func normalize("normalize");
            normalize(x, tx, y) = norm(blur_x(x, tx, y / N, y % N));

            if (use_blur_x_direct) {
                results.push_back(norm(blur_x_direct(x, tx, y)));
            } else {
                results.push_back(normalize(x, tx, y));
            }
            Expr condition = diameter <= max_diameter;
            conditions.push_back(condition);

            if (use_blur_x_direct) {
                blur_y
                    .store_in(MemoryType::Register)
                    .compute_at(blur_y.in(), xo);
                blur_y.update(0)
                    .vectorize(x);
                blur_y.update(1)
                    .vectorize(x)
                    .unroll(ry_scan);

                blur_y.in()
                    .compute_at(output, yo)
                    .split(x, xo, x, vec)
                    .reorder(y, x, xo)
                    .vectorize(x)
                    .bound_extent(ty, 1)
                    .unroll(y);

                blur_x_direct
                    .store_in(MemoryType::Register)
                    .compute_at(output, xo)
                    .bound_extent(x, vec)
                    .vectorize(x)
                    .update()
                    .reorder(x, y, rx_direct)
                    .vectorize(x);
            } else {
                normalize
                    .store_in(MemoryType::Register)
                    .compute_at(output, xo)
                    .reorder_storage(y, x)
                    .bound_extent(y, N)
                    .bound_extent(x, vec)
                    .vectorize(y)
                    .unroll(x);
                normalize.in()
                    .store_in(MemoryType::Register)
                    .compute_at(output, xo)
                    .bound_extent(y, N)
                    .bound_extent(x, vec)
                    .bound_extent(tx, 1)
                    .vectorize(y)
                    .unroll(x);

                integrate_x
                    .compute_at(output, yo)
                    .reorder_storage(y, x, ty);
                integrate_x.update(0)
                    .vectorize(y);
                integrate_x.update(1)
                    .vectorize(y);

                RVar rxo;
                integrate_x
                    .update(1)
                    .reorder(y, rx_scan.x, rx_scan.y, ty)
                    .rename(rx_scan.y, rxo)
                    .unroll(rx_scan.x);
                integrate_x
                    .update(2)
                    .reorder(y, rx_tail.x, ty)
                    .rename(rx_tail.x, rxo);

                blur_y
                    .compute_at(integrate_x, rxo)
                    .store_in(MemoryType::Register);

                blur_y.update(0)
                    .vectorize(x);
                blur_y.update(1)
                    .vectorize(x)
                    .unroll(ry_scan);

                diff_y.compute_at(integrate_x, rxo)
                    .store_in(MemoryType::Register)
                    .vectorize(x)
                    .unroll(y);

                blur_y.in()
                    .compute_at(integrate_x, rxo)
                    .store_in(MemoryType::Register)
                    .reorder_storage(y, x, ty)
                    .vectorize(x)
                    .unroll(y);
            }

            blur_y_init
                .bound_extent(ty, 1)
                .compute_at(output, yo)
                .vectorize(x, vec, TailStrategy::GuardWithIf)
                .align_storage(x, vec);
            if (use_down_y) {
                blur_y_init
                    .vectorize(x, vec * 2, TailStrategy::GuardWithIf);
                blur_y_init.update(0)
                    .reorder(x, ry_init_coarse, ty)
                    .vectorize(x, vec * 2, TailStrategy::GuardWithIf);
                blur_y_init.update(1)
                    .reorder(x, ry_init_fine_2, ty)
                    .vectorize(x, vec * 2, TailStrategy::GuardWithIf);
                blur_y_init.update(2)
                    .reorder(x, ry_init_fine_1, ty)
                    .vectorize(x, vec * 2, TailStrategy::GuardWithIf);
            } else {
                blur_y_init.update(0)
                    .reorder(x, ry_init_full, ty)
                    .vectorize(x, vec * 2, TailStrategy::GuardWithIf);
            }

            for (Func f : {blur_y, blur_y_init}) {
                f.specialize(condition);
                f.specialize_fail("unreachable");
                if (use_blur_x_direct) {
                    // f.store_in(MemoryType::Register);
                }
            }
        }

        Expr result = results.back();

        for (size_t i = conditions.size() - 1; i > 0; i--) {
            result = select(conditions[i - 1], results[i - 1], result);
        }

        output(x, tx, y) = result;

        down_y.compute_root()
            .split(x, tx, x, 1024, TailStrategy::GuardWithIf)
            .reorder(y, x, tx, ty)
            .fuse(tx, ty, ty)
            .parallel(ty)
            .unroll(y)
            .split(x, xo, xi, natural_vector_size(small_sum_type), TailStrategy::RoundUp)
            .vectorize(xi);

        /*
        down_y_1.compute_at(down_y, xi).unroll(y).update().unroll(r_down_1).unroll(y);
        down_y_2.compute_at(down_y, xi).update().unroll(r_down_2);
        */

        down_y_1.compute_at(down_y, ty)
            .vectorize(x, natural_vector_size(small_sum_type), TailStrategy::GuardWithIf)
            .update()
            .vectorize(x, natural_vector_size(small_sum_type), TailStrategy::GuardWithIf)
            .unroll(r_down_1);
        down_y_2
            .compute_at(down_y, xi)
            .update(1)
            .unroll(r_down_2);

        output.dim(0).set_bounds(0, width);
        output.dim(1).set_min(0);
        output.dim(2).set_min(0);
        input.dim(0).set_min(0);
        input.dim(1).set_min(0);

        output.specialize(diameter <= max_diameter_direct_blur_x)
            .split(y, ty, y, N, TailStrategy::GuardWithIf)
            .split(y, yo, yi, N)
            .split(x, xo, xi, vec, TailStrategy::GuardWithIf)
            .reorder(xi, xo, yi, yo, tx, ty)
            .vectorize(xi)
            .fuse(tx, ty, ty)
            .parallel(ty);

        output
            .split(y, ty, y, N, TailStrategy::GuardWithIf)
            .split(y, yo, yi, N)
            .split(x, xo, xi, vec, TailStrategy::GuardWithIf)
            .reorder(xi, yi, xo, yo, tx, ty)
            .vectorize(xi)
            .unroll(yi)
            .fuse(tx, ty, ty)
            .parallel(ty);

        for (size_t i = conditions.size() - 1; i > 0; i--) {
            output.specialize(conditions[i - 1]);
        }
        add_requirement(conditions.back(), "Unsupported diameter");

        add_requirement(diameter > 0);
        add_requirement(diameter % 2 == 1);
    }
};

HALIDE_REGISTER_GENERATOR(BoxBlurPyramid, box_blur_pyramid)

#if 0

template<typename TOutput, typename TSum>
class BoxBlurTiled : public Generator<BoxBlurTiled<TOutput, TSum>> {
    template<typename T>
    using Input = GeneratorInput<T>;

    template<typename T>
    using Output = GeneratorOutput<T>;

public:
    Input<Buffer<TOutput>> input{"input", 2};
    Input<int> diameter{"diameter"};
    Input<int> tile_extent_x{"tile_extent_x"};
    Input<int> tile_stride_x{"tile_stride_x"};
    Input<int> tile_extent_y{"tile_extent_y"};
    Input<int> tile_stride_y{"tile_stride_y"};
    Output<Func> output{"output", {halide_type_of<TOutput>(), halide_type_of<TSum>}, 4};

    void generate() {
        Var x("x"), y("y"), ty("ty"), tx("tx");

        Func input_tiled;
        input_tiled(x, y, tx, ty) = input(x + tx * tile_stride_x, y + ty * tile_stride_y);

        // Figure out our intermediate accumulator types
        Type small_sum_type, large_sum_type, diff_type;
        int max_count_for_small_sum, max_count_for_large_sum;
        const int bits = input.type().bits();
        if (input.type().is_float()) {
            small_sum_type = input.type();
            diff_type = input.type();
            large_sum_type = input.type().with_bits(bits * 2);
            // This is approximate. If we wanted an exact float blur,
            // this should be set to 1.
            max_count_for_small_sum = 256;
            max_count_for_large_sum = 0x7fffffff;
        } else {
            small_sum_type = UInt(bits * 2);
            diff_type = Int(bits * 2);
            large_sum_type = UInt(bits * 4);
            max_count_for_small_sum = 1 << bits;
            max_count_for_large_sum = std::min(0x7fffffffULL, 1ULL << (bits * 3));
        }

        Func blur_y_init("blur_y_init");
        Func blur_y("blur_y");

        auto norm = [&](Expr e) {
            if (e.type().bits() <= 32) {
                // TODO: This is a bit suspect for blurs that produce between 23 and 32 bits.
                e = cast<float>(e);
            } else {
                e = cast<double>(e);
            }
            Expr den = cast(e.type(), diameter * diameter);
            Expr result = e * (1 / den);
            if (!input.type().is_float()) {
                result = round(result);
            }
            return cast(input.type(), result);
        };

        // TODO: resurrect downsample-in-y strategy for computing blur_y_init

        RDom ry_init_full(0, diameter);
        blur_y_init(x, tx, ty) += cast(blur_y_t, input_tiled(x, ry_init_full, tx, ty));

        output(x, 0, tx, ty) = {norm(blur_y_init(x, tx, ty), blur_y_init(x, tx, ty))};

        // const int vec = natural_vector_size<TOutput>();
        RDom r(1, tile_extent_x - 1, 1, tile_extent_y - 1);

        Expr new_blur =
            output(r.x, r.y - 1, tx, ty)[1] -
            output(r.x - 1, r.y - 1, tx, ty)[1] +
            output(r.x - 1, r.y, tx, ty)[1] +
            ((cast(diff_type, input_tiled(r.x + diameter - 1, r.y + diameter - 1, tx, ty)) -
              input_tiled(r.x + diameter - 1, r.y - 1, tx, ty)) -
             (cast(diff_type, input_tiled(r.x, r.y + diameter - 1, tx, ty)) -
              input_tiled(r.x - 1, r.y - 1, tx, ty)));

        output(r.x, r.y, tx, ty) =
            {
                norm(new_blur_y_sum_x - output(r.x - diameter, r.y, tx, ty)),
                new_blur_y_sum_x};

        // Compute the other in-between scanlines by incrementally
        // updating that one in a sliding window.
        Func diff_y("diff_y");
        diff_y(x, y, tx, ty) =
            (cast(diff_type, input_tiled(x, y + diameter, tx, ty)) -
             input_tiled(x, y, tx, ty));

        RDom ry_scan(0, tile_extent_y - 1);
        blur_y(x, y, tx, ty) = undef(blur_y_t);
        blur_y(x, 0, tx, ty) = blur_y_init(x, tx, ty);
        blur_y(x, ry_scan + 1, tx, ty) =
            (blur_y(x, ry_scan, tx, ty) + cast(blur_y_t, diff_y(x, ry_scan, tx, ty)));

        // TODO: resurrect blur-x-direct
        Type blur_x_t = (max_diameter <= max_diameter_low_bit_blur_x) ? small_sum_type : large_sum_type;

        const int integrate_vec = natural_vector_size(blur_y_t);

        Func integrate_x("integrate_x");
        integrate_x(x, tx, ty, y) = undef(blur_x_t);
        integrate_x(-1, tx, ty, y) = cast(blur_x_t, 0);
        RDom rx_scan(0, integrate_vec, 0, ((width + diameter) / integrate_vec));
        Expr rx = rx_scan.x + integrate_vec * rx_scan.y;
        integrate_x(rx, tx, ty, y) =
            (integrate_x(rx - 1, tx, ty, y) +
             blur_y(rx, tx, ty, y));
        RDom rx_tail(((width + diameter) / integrate_vec) * integrate_vec, (width + diameter) % integrate_vec);
        rx = clamp(rx_tail, 0, width + diameter - 2);
        integrate_x(rx, tx, ty, y) =
            (integrate_x(rx - 1, tx, ty, y) +
             blur_y(rx, tx, ty, y));

        Func blur_x("blur_x");
        blur_x(x, tx, ty, y) = integrate_x(x + diameter - 1, tx, ty, y) - integrate_x(x - 1, tx, ty, y);

        Func blur_y_untiled("blur_y_untiled");
        blur_y_untiled(x, tx, y) = blur_y(x, tx, y / N, y % N);

        // For small diameter, we do it directly and stay in 16-bit
        Func blur_x_direct("blur_x_direct");
        RDom rx_direct(0, diameter);
        blur_x_direct(x, tx, y) += blur_y_untiled(x + rx_direct, tx, y);

        Func normalize("normalize");
        normalize(x, tx, y) = norm(blur_x(x, tx, y / N, y % N));

        if (use_blur_x_direct) {
            results.push_back(norm(blur_x_direct(x, tx, y)));
        } else {
            results.push_back(normalize(x, tx, y));
        }
        Expr condition = diameter <= max_diameter;
        conditions.push_back(condition);

        if (use_blur_x_direct) {
            blur_y
                .store_in(MemoryType::Register)
                .compute_at(blur_y.in(), xo);
            blur_y.update(0)
                .vectorize(x);
            blur_y.update(1)
                .vectorize(x)
                .unroll(ry_scan);

            blur_y.in()
                .compute_at(output, yo)
                .split(x, xo, x, vec)
                .reorder(y, x, xo)
                .vectorize(x)
                .bound_extent(ty, 1)
                .unroll(y);

            blur_x_direct
                .store_in(MemoryType::Register)
                .compute_at(output, xo)
                .bound_extent(x, vec)
                .vectorize(x)
                .update()
                .reorder(x, y, rx_direct)
                .vectorize(x);
        } else {
            normalize
                .store_in(MemoryType::Register)
                .compute_at(output, xo)
                .reorder_storage(y, x)
                .bound_extent(y, N)
                .bound_extent(x, vec)
                .vectorize(y)
                .unroll(x);
            normalize.in()
                .store_in(MemoryType::Register)
                .compute_at(output, xo)
                .bound_extent(y, N)
                .bound_extent(x, vec)
                .bound_extent(tx, 1)
                .vectorize(y)
                .unroll(x);

            integrate_x
                .compute_at(output, yo)
                .reorder_storage(y, x, ty);
            integrate_x.update(0)
                .vectorize(y);
            integrate_x.update(1)
                .vectorize(y);

            RVar rxo;
            integrate_x
                .update(1)
                .reorder(y, rx_scan.x, rx_scan.y, ty)
                .rename(rx_scan.y, rxo)
                .unroll(rx_scan.x);
            integrate_x
                .update(2)
                .reorder(y, rx_tail.x, ty)
                .rename(rx_tail.x, rxo);

            blur_y
                .compute_at(integrate_x, rxo)
                .store_in(MemoryType::Register);

            blur_y.update(0)
                .vectorize(x);
            blur_y.update(1)
                .vectorize(x)
                .unroll(ry_scan);

            diff_y.compute_at(integrate_x, rxo)
                .store_in(MemoryType::Register)
                .vectorize(x)
                .unroll(y);

            blur_y.in()
                .compute_at(integrate_x, rxo)
                .store_in(MemoryType::Register)
                .reorder_storage(y, x, ty)
                .vectorize(x)
                .unroll(y);
        }

        blur_y_init
            .bound_extent(ty, 1)
            .compute_at(output, yo)
            .vectorize(x, vec, TailStrategy::GuardWithIf)
            .align_storage(x, vec);
        if (use_down_y) {
            blur_y_init
                .vectorize(x, vec * 2, TailStrategy::GuardWithIf);
            blur_y_init.update(0)
                .reorder(x, ry_init_coarse, ty)
                .vectorize(x, vec * 2, TailStrategy::GuardWithIf);
            blur_y_init.update(1)
                .reorder(x, ry_init_fine_2, ty)
                .vectorize(x, vec * 2, TailStrategy::GuardWithIf);
            blur_y_init.update(2)
                .reorder(x, ry_init_fine_1, ty)
                .vectorize(x, vec * 2, TailStrategy::GuardWithIf);
        } else {
            blur_y_init.update(0)
                .reorder(x, ry_init_full, ty)
                .vectorize(x, vec * 2, TailStrategy::GuardWithIf);
        }

        for (Func f : {blur_y, blur_y_init}) {
            f.specialize(condition);
            f.specialize_fail("unreachable");
            if (use_blur_x_direct) {
                // f.store_in(MemoryType::Register);
            }
        }
    }

    Expr result = results.back();

    for (size_t i = conditions.size() - 1; i > 0; i--) {
        result = select(conditions[i - 1], results[i - 1], result);
    }

    output(x, tx, y) = result;

    down_y.compute_root()
        .split(x, tx, x, 1024, TailStrategy::GuardWithIf)
        .reorder(y, x, tx, ty)
        .fuse(tx, ty, ty)
        .parallel(ty)
        .unroll(y)
        .split(x, xo, xi, natural_vector_size(small_sum_type), TailStrategy::RoundUp)
        .vectorize(xi);

    /*
    down_y_1.compute_at(down_y, xi).unroll(y).update().unroll(r_down_1).unroll(y);
    down_y_2.compute_at(down_y, xi).update().unroll(r_down_2);
    */

    down_y_1.compute_at(down_y, ty)
        .vectorize(x, natural_vector_size(small_sum_type), TailStrategy::GuardWithIf)
        .update()
        .vectorize(x, natural_vector_size(small_sum_type), TailStrategy::GuardWithIf)
        .unroll(r_down_1);
    down_y_2
        .compute_at(down_y, xi)
        .update(1)
        .unroll(r_down_2);

    output.dim(0).set_bounds(0, width);
    output.dim(1).set_min(0);
    output.dim(2).set_min(0);
    input.dim(0).set_min(0);
    input.dim(1).set_min(0);

    output.specialize(diameter <= max_diameter_direct_blur_x)
        .split(y, ty, y, N, TailStrategy::GuardWithIf)
        .split(y, yo, yi, N)
        .split(x, xo, xi, vec, TailStrategy::GuardWithIf)
        .reorder(xi, xo, yi, yo, tx, ty)
        .vectorize(xi)
        .fuse(tx, ty, ty)
        .parallel(ty);

    output
        .split(y, ty, y, N, TailStrategy::GuardWithIf)
        .split(y, yo, yi, N)
        .split(x, xo, xi, vec, TailStrategy::GuardWithIf)
        .reorder(xi, yi, xo, yo, tx, ty)
        .vectorize(xi)
        .unroll(yi)
        .fuse(tx, ty, ty)
        .parallel(ty);

    for (size_t i = conditions.size() - 1; i > 0; i--) {
        output.specialize(conditions[i - 1]);
    }
    add_requirement(conditions.back(), "Unsupported diameter");

    add_requirement(diameter > 0);
    add_requirement(diameter % 2 == 1);
}
}
;

// TODO: low precision versions for small blurs
using BoxBlurTiledU8 = BoxBlurTile<uint8_t, uint32_t>;
using BoxBlurTiledU16 = BoxBlurTile<uint16_t, uint64_t>;
using BoxBlurTiledF32 = BoxBlurTile<double, double>;
HALIDE_REGISTER_GENERATOR(BoxBlurTiledU8, box_blur_tiled_u8)
HALIDE_REGISTER_GENERATOR(BoxBlurTiledU16, box_blur_tiled_u16)
HALIDE_REGISTER_GENERATOR(BoxBlurTiledF32, box_blur_tiled_f32)

#endif
