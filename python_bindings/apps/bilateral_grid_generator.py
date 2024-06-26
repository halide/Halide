"""
Bilateral histogram.
"""

import halide as hl


@hl.alias(
    bilateral_grid_Adams2019={"autoscheduler": "Adams2019"},
    bilateral_grid_Mullapudi2016={"autoscheduler": "Mullapudi2016"},
    bilateral_grid_Li2018={"autoscheduler": "Li2018"},
)
@hl.generator()
class bilateral_grid:
    s_sigma = hl.GeneratorParam(8)

    input_buf = hl.InputBuffer(hl.Float(32), 2)
    r_sigma = hl.InputScalar(hl.Float(32))
    bilateral_grid = hl.OutputBuffer(hl.Float(32), 2)

    def generate(self):
        g = self

        x, y, z, c = hl.vars("x y z c")

        # Add a boundary condition
        clamped = hl.BoundaryConditions.repeat_edge(g.input_buf)

        # Construct the bilateral grid
        r = hl.RDom([(0, g.s_sigma), (0, g.s_sigma)])
        val = clamped[
            x * g.s_sigma + r.x - g.s_sigma // 2,
            y * g.s_sigma + r.y - g.s_sigma // 2,
        ]
        val = hl.clamp(val, 0.0, 1.0)

        zi = hl.i32(val / g.r_sigma + 0.5)

        histogram = hl.Func("histogram")
        histogram[x, y, z, c] = 0.0
        histogram[x, y, zi, c] += hl.mux(c, [val, 1.0])

        # Blur the histogram using a five-tap filter
        blurx, blury, blurz = hl.funcs("blurx blury blurz")
        blurz[x, y, z, c] = (
            histogram[x, y, z - 2, c]
            + histogram[x, y, z - 1, c] * 4
            + histogram[x, y, z, c] * 6
            + histogram[x, y, z + 1, c] * 4
            + histogram[x, y, z + 2, c]
        )
        blurx[x, y, z, c] = (
            blurz[x - 2, y, z, c]
            + blurz[x - 1, y, z, c] * 4
            + blurz[x, y, z, c] * 6
            + blurz[x + 1, y, z, c] * 4
            + blurz[x + 2, y, z, c]
        )
        blury[x, y, z, c] = (
            blurx[x, y - 2, z, c]
            + blurx[x, y - 1, z, c] * 4
            + blurx[x, y, z, c] * 6
            + blurx[x, y + 1, z, c] * 4
            + blurx[x, y + 2, z, c]
        )

        # Take trilinear samples to compute the output
        val = hl.clamp(clamped[x, y], 0.0, 1.0)
        zv = val / g.r_sigma
        zi = hl.i32(zv)
        zf = zv - zi
        xf = hl.f32(x % g.s_sigma) / g.s_sigma
        yf = hl.f32(y % g.s_sigma) / g.s_sigma
        xi = x / g.s_sigma
        yi = y / g.s_sigma

        interpolated = hl.Func("interpolated")
        interpolated[x, y, c] = hl.lerp(
            hl.lerp(
                hl.lerp(blury[xi, yi, zi, c], blury[xi + 1, yi, zi, c], xf),
                hl.lerp(blury[xi, yi + 1, zi, c], blury[xi + 1, yi + 1, zi, c], xf),
                yf,
            ),
            hl.lerp(
                hl.lerp(blury[xi, yi, zi + 1, c], blury[xi + 1, yi, zi + 1, c], xf),
                hl.lerp(
                    blury[xi, yi + 1, zi + 1, c], blury[xi + 1, yi + 1, zi + 1, c], xf
                ),
                yf,
            ),
            zf,
        )

        # Normalize
        g.bilateral_grid[x, y] = interpolated[x, y, 0] / interpolated[x, y, 1]

        # ESTIMATES
        # (This can be useful in conjunction with RunGen and benchmarks as well
        # as auto-schedule, so we do it in all cases.)
        # Provide estimates on the input image
        g.input_buf.set_estimates([(0, 1536), (0, 2560)])
        # Provide estimates on the parameters
        g.r_sigma.set_estimate(0.1)
        # TODO: Compute estimates from the parameter values
        histogram.set_estimate(z, -2, 16)
        blurz.set_estimate(z, 0, 12)
        blurx.set_estimate(z, 0, 12)
        blury.set_estimate(z, 0, 12)
        g.bilateral_grid.set_estimates([(0, 1536), (0, 2560)])

        if g.using_autoscheduler():
            # nothing
            pass
        elif g.target().has_gpu_feature():
            # 0.50ms on an RTX 2060

            xi, yi, zi = hl.vars("xi yi zi")

            # Schedule blurz in 8x8 tiles. This is a tile in
            # grid-space, which means it represents something like
            # 64x64 pixels in the input (if s_sigma is 8).
            blurz.compute_root().reorder(c, z, x, y).gpu_tile(x, y, xi, yi, 8, 8)

            # Schedule histogram to happen per-tile of blurz, with
            # intermediate results in shared memory. This means histogram
            # and blurz makes a three-stage kernel:
            # 1) Zero out the 8x8 set of histograms
            # 2) Compute those histogram by iterating over lots of the input image
            # 3) Blur the set of histograms in z
            histogram.reorder(c, z, x, y).compute_at(blurz, x).gpu_threads(x, y)
            histogram.update().reorder(c, r.x, r.y, x, y).gpu_threads(x, y).unroll(c)

            # Schedule the remaining blurs and the sampling at the end
            # similarly.
            (
                blurx.compute_root()
                .reorder(c, x, y, z)
                .reorder_storage(c, x, y, z)
                .vectorize(c)
                .unroll(y, 2, hl.TailStrategy.RoundUp)
                .gpu_tile(x, y, z, xi, yi, zi, 32, 8, 1, hl.TailStrategy.RoundUp)
            )
            (
                blury.compute_root()
                .reorder(c, x, y, z)
                .reorder_storage(c, x, y, z)
                .vectorize(c)
                .unroll(y, 2, hl.TailStrategy.RoundUp)
                .gpu_tile(x, y, z, xi, yi, zi, 32, 8, 1, hl.TailStrategy.RoundUp)
            )
            g.bilateral_grid.compute_root().gpu_tile(x, y, xi, yi, 32, 8)
            interpolated.compute_at(g.bilateral_grid, xi).vectorize(c)
        else:
            # CPU schedule.

            # 3.98ms on an Intel i9-9960X using 32 threads at 3.7 GHz
            # using target x86-64-avx2. This is a little less
            # SIMD-friendly than some of the other apps, so we
            # benefit from hyperthreading, and don't benefit from
            # AVX-512, which on my machine reduces the clock to 3.0
            # GHz.

            (
                blurz.compute_root()
                .reorder(c, z, x, y)
                .parallel(y)
                .vectorize(x, 8)
                .unroll(c)
            )
            histogram.compute_at(blurz, y)
            histogram.update().reorder(c, r.x, r.y, x, y).unroll(c)
            (
                blurx.compute_root()  #
                .reorder(c, x, y, z)  #
                .parallel(z)  #
                .vectorize(x, 8)  #
                .unroll(c)
            )
            (
                blury.compute_root()
                .reorder(c, x, y, z)
                .parallel(z)
                .vectorize(x, 8)
                .unroll(c)
            )
            g.bilateral_grid.compute_root().parallel(y).vectorize(x, 8)


if __name__ == "__main__":
    hl.main()
