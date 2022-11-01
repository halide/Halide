"""
Fast image interpolation using a pyramid.
"""

import halide as hl


def _func_list(name, size):
    """Return a list containing `size` Funcs, named `name_n` for n in 0..size-1."""
    return [hl.Func("%s_%d" % (name, i)) for i in range(size)]


@hl.alias(
    interpolate_Mullapudi2016={"autoscheduler": "Mullapudi2016"},
)
@hl.generator()
class interpolate:
    levels = hl.GeneratorParam(10)

    input_buf = hl.InputBuffer(hl.Float(32), 3)
    output_buf = hl.OutputBuffer(hl.Float(32), 3)

    def generate(self):
        g = self

        x, y, c = hl.vars("x y c")

        # Input must have four color channels - rgba
        g.input_buf.dim(2).set_bounds(0, 4)

        downsampled = _func_list("downsampled", g.levels)
        downx = _func_list("downx", g.levels)
        interpolated = _func_list("interpolated", g.levels)
        upsampled = _func_list("upsampled", g.levels)
        upsampledx = _func_list("upsampledx", g.levels)

        clamped = hl.BoundaryConditions.repeat_edge(g.input_buf)

        downsampled[0][x, y, c] = hl.select(
            c < 3,
            clamped[x, y, c] * clamped[x, y, 3],
            clamped[x, y, 3],
        )

        for l in range(1, g.levels):
            prev = downsampled[l - 1]

            if l == 4:
                # Also add a boundary condition at a middle pyramid level
                # to prevent the footprint of the downsamplings to extend
                # too far off the base image. Otherwise we look 512
                # pixels off each edge.
                w = g.input_buf.width() / (1 << (l - 1))
                h = g.input_buf.height() / (1 << (l - 1))
                prev = hl.lambda_func(
                    x, y, c, prev[hl.clamp(x, 0, w), hl.clamp(y, 0, h), c]
                )

            downx[l][x, y, c] = (
                prev[x * 2 - 1, y, c] + 2 * prev[x * 2, y, c] + prev[x * 2 + 1, y, c]
            ) * 0.25

            downsampled[l][x, y, c] = (
                downx[l][x, y * 2 - 1, c]
                + 2 * downx[l][x, y * 2, c]
                + downx[l][x, y * 2 + 1, c]
            ) * 0.25

        interpolated[g.levels - 1][x, y, c] = downsampled[g.levels - 1][x, y, c]

        for l in range(g.levels - 2, -1, -1):
            upsampledx[l][x, y, c] = (
                interpolated[l + 1][x / 2, y, c]
                + interpolated[l + 1][(x + 1) / 2, y, c]
            ) / 2
            upsampled[l][x, y, c] = (
                upsampledx[l][x, y / 2, c] + upsampledx[l][x, (y + 1) / 2, c]
            ) / 2
            alpha = 1.0 - downsampled[l][x, y, 3]
            interpolated[l][x, y, c] = (
                downsampled[l][x, y, c] + alpha * upsampled[l][x, y, c]
            )

        g.output_buf[x, y, c] = interpolated[0][x, y, c] / interpolated[0][x, y, 3]

        # Schedule
        if g.using_autoscheduler():
            # nothing
            pass
        elif g.target().has_gpu_feature():
            # 0.86ms on a 2060 RTX
            yo, yi, xo, xi, ci, xii, yii = hl.vars("yo yi xo xi ci xii yii")

            (
                g.output_buf.bound(x, 0, g.input_buf.width())
                .bound(y, 0, g.input_buf.height())
                .bound(c, 0, 3)
                .reorder(c, x, y)
                .tile(x, y, xi, yi, 32, 32, hl.TailStrategy.RoundUp)
                .tile(xi, yi, xii, yii, 2, 2)
                .gpu_blocks(x, y)
                .gpu_threads(xi, yi)
                .unroll(xii)
                .unroll(yii)
                .unroll(c)
            )

            for l in range(1, g.levels):
                (
                    downsampled[l]
                    .compute_root()
                    .reorder(c, x, y)
                    .unroll(c)
                    .gpu_tile(x, y, xi, yi, 16, 16)
                )

            for l in range(3, g.levels, 2):
                (
                    interpolated[l]
                    .compute_root()
                    .reorder(c, x, y)
                    .tile(x, y, xi, yi, 32, 32, hl.TailStrategy.RoundUp)
                    .tile(xi, yi, xii, yii, 2, 2)
                    .gpu_blocks(x, y)
                    .gpu_threads(xi, yi)
                    .unroll(xii)
                    .unroll(yii)
                    .unroll(c)
                )

            (
                upsampledx[1]
                .compute_at(g.output_buf, x)
                .reorder(c, x, y)
                .tile(x, y, xi, yi, 2, 1)
                .unroll(xi)
                .unroll(yi)
                .unroll(c)
                .gpu_threads(x, y)
            )

            (
                interpolated[1]
                .compute_at(g.output_buf, x)
                .reorder(c, x, y)
                .tile(x, y, xi, yi, 2, 2)
                .unroll(xi)
                .unroll(yi)
                .unroll(c)
                .gpu_threads(x, y)
            )

            (
                interpolated[2]
                .compute_at(g.output_buf, x)
                .reorder(c, x, y)
                .unroll(c)
                .gpu_threads(x, y)
            )

        else:
            # 4.54ms on an Intel i9-9960X using 16 threads
            xo, xi, yo, yi = hl.vars("xo xi yo yi")
            vec = g.natural_vector_size(hl.Float(32))
            for l in range(1, g.levels - 1):
                # We must refer to the downsampled stages in the
                # upsampling later, so they must all be
                # compute_root or redundantly recomputed, as in
                # the local_laplacian app.
                (
                    downsampled[l]
                    .compute_root()
                    .reorder(x, c, y)
                    .split(y, yo, yi, 8)
                    .parallel(yo)
                    .vectorize(x, vec)
                )

            # downsampled[0] takes too long to compute_root, so
            # we'll redundantly recompute it instead.  Make a
            # separate clone of it in the first downsampled stage
            # so that we can schedule the two versions
            # separately.
            (
                downsampled[0]
                .clone_in(downx[1])
                .store_at(downsampled[1], yo)
                .compute_at(downsampled[1], yi)
                .reorder(c, x, y)
                .unroll(c)
                .vectorize(x, vec)
            )

            (
                g.output_buf.bound(x, 0, g.input_buf.width())
                .bound(y, 0, g.input_buf.height())
                .bound(c, 0, 3)
                .split(x, xo, xi, vec)
                .split(y, yo, yi, 32)
                .reorder(xi, c, xo, yi, yo)
                .unroll(c)
                .vectorize(xi)
                .parallel(yo)
            )

            for l in range(1, g.levels):
                (
                    interpolated[l]
                    .store_at(g.output_buf, yo)
                    .compute_at(g.output_buf, yi)
                    .vectorize(x, vec)
                )

        # Estimates (for autoscheduler; ignored otherwise)
        (
            g.input_buf.dim(0)
            .set_estimate(0, 1536)
            .dim(1)
            .set_estimate(0, 2560)
            .dim(2)
            .set_estimate(0, 4)
        )
        (
            g.output_buf.output_buffer()
            .dim(0)
            .set_estimate(0, 1536)
            .dim(1)
            .set_estimate(0, 2560)
            .dim(2)
            .set_estimate(0, 3)
        )


if __name__ == "__main__":
    hl.main()
