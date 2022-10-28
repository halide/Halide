"""
Local Laplacian.
"""

import halide as hl

# Just declare these at global scope, for simplicity
x, y, c, k = hl.vars("x y c k")


def _func_list(name, size):
    """Return a list containing `size` Funcs, named `name_n` for n in 0..size-1."""
    return [hl.Func("%s_%d" % (name, i)) for i in range(size)]


def _downsample(f):
    """Downsample with a 1 3 3 1 filter"""
    downx, downy = hl.funcs("downx downy")
    downx[x, y, hl._] = (
        f[2 * x - 1, y, hl._]
        + 3.0 * (f[2 * x, y, hl._] + f[2 * x + 1, y, hl._])
        + f[2 * x + 2, y, hl._]
    ) / 8.0
    downy[x, y, hl._] = (
        downx[x, 2 * y - 1, hl._]
        + 3.0 * (downx[x, 2 * y, hl._] + downx[x, 2 * y + 1, hl._])
        + downx[x, 2 * y + 2, hl._]
    ) / 8.0
    return downy


def _upsample(f):
    """Upsample using bilinear interpolation"""
    upx, upy = hl.funcs("upx upy")
    upx[x, y, hl._] = hl.lerp(
        f[(x + 1) // 2, y, hl._],
        f[(x - 1) // 2, y, hl._],
        ((x % 2) * 2 + 1) / 4.0,
    )
    upy[x, y, hl._] = hl.lerp(
        upx[x, (y + 1) // 2, hl._],
        upx[x, (y - 1) // 2, hl._],
        ((y % 2) * 2 + 1) / 4.0,
    )
    return upy


@hl.alias(local_laplacian_Mullapudi2016={"autoscheduler": "Mullapudi2016"})
@hl.generator()
class local_laplacian:
    pyramid_levels = hl.GeneratorParam(8)

    input_buf = hl.InputBuffer(hl.UInt(16), 3)
    levels = hl.InputScalar(hl.Int(32))
    alpha = hl.InputScalar(hl.Float(32))
    beta = hl.InputScalar(hl.Float(32))
    output_buf = hl.OutputBuffer(hl.UInt(16), 3)

    def generate(self):
        g = self

        # THE ALGORITHM
        J = g.pyramid_levels

        # Make the remapping function as a lookup table.
        fx = hl.f32(x) / 256.0
        remap = hl.Func("remap")
        remap[x] = g.alpha * fx * hl.exp(-fx * fx / 2.0)

        # Set a boundary condition
        clamped = hl.BoundaryConditions.repeat_edge(g.input_buf)

        # Convert to floating point
        floating = hl.Func("floating")
        floating[x, y, c] = clamped[x, y, c] / 65535.0

        # Get the luminance channel
        gray = hl.Func("gray")
        gray[x, y] = (
            hl.f32(0.299) * floating[x, y, 0]
            + hl.f32(0.587) * floating[x, y, 1]
            + hl.f32(0.114) * floating[x, y, 2]
        )

        # Make the processed Gaussian pyramid.
        gPyramid = _func_list("gPyramid", J)
        # Do a lookup into a lut with 256 entires per intensity level
        level = k * (1.0 / (g.levels - 1))
        idx = gray[x, y] * hl.f32(g.levels - 1) * 256.0
        idx = hl.clamp(hl.i32(idx), 0, (g.levels - 1) * 256)
        gPyramid[0][x, y, k] = (
            g.beta * (gray[x, y] - level) + level + remap[idx - 256 * k]
        )
        for j in range(1, J):
            gPyramid[j][x, y, k] = _downsample(gPyramid[j - 1])[x, y, k]

        # Get its laplacian pyramid
        lPyramid = _func_list("lPyramid", J)
        lPyramid[J - 1][x, y, k] = gPyramid[J - 1][x, y, k]
        for j in range(J - 2, -1, -1):
            lPyramid[j][x, y, k] = (
                gPyramid[j][x, y, k] - _upsample(gPyramid[j + 1])[x, y, k]
            )

        # Make the Gaussian pyramid of the input
        inGPyramid = _func_list("inGPyramid", J)
        inGPyramid[0][x, y] = gray[x, y]
        for j in range(1, J):
            inGPyramid[j][x, y] = _downsample(inGPyramid[j - 1])[x, y]

        # Make the laplacian pyramid of the output
        outLPyramid = _func_list("outLPyramid", J)
        for j in range(0, J):
            # Split input pyramid value into integer and floating parts
            level = inGPyramid[j][x, y] * hl.f32(g.levels - 1)
            li = hl.clamp(hl.i32(level), 0, g.levels - 2)
            lf = level - hl.f32(li)
            # Linearly interpolate between the nearest processed pyramid levels
            outLPyramid[j][x, y] = (1.0 - lf) * lPyramid[j][x, y, li] + (
                lf * lPyramid[j][x, y, li + 1]
            )

        # Make the Gaussian pyramid of the output
        outGPyramid = _func_list("outGPyramid", J)
        outGPyramid[J - 1][x, y] = outLPyramid[J - 1][x, y]
        for j in range(J - 2, -1, -1):
            outGPyramid[j][x, y] = (
                _upsample(outGPyramid[j + 1])[x, y] + outLPyramid[j][x, y]
            )

        # Reintroduce color (Connelly: use eps to avoid scaling up noise w/
        # apollo3.png input)
        color = hl.Func("color")
        eps = hl.f32(0.01)
        color[x, y, c] = (
            outGPyramid[0][x, y] * (floating[x, y, c] + eps) / (gray[x, y] + eps)
        )

        # Convert back to 16-bit
        g.output_buf[x, y, c] = hl.u16(hl.clamp(color[x, y, c], 0.0, 1.0) * 65535.0)

        # ESTIMATES
        # (This can be useful in conjunction with RunGen and benchmarks as well
        # as autoschedulers, so we do it in all cases.)
        g.input_buf.set_estimates([(0, 1536), (0, 2560), (0, 3)])
        # Provide estimates on the parameters
        g.levels.set_estimate(8)
        g.alpha.set_estimate(1)
        g.beta.set_estimate(1)
        g.output_buf.set_estimates([(0, 1536), (0, 2560), (0, 3)])

        # THE SCHEDULE
        if g.using_autoscheduler():
            # nothing
            pass
        elif g.target().has_gpu_feature():
            # GPU schedule.
            # 3.19ms on an RTX 2060.
            remap.compute_root()
            xi, yi = hl.vars("xi yi")
            g.output_buf.compute_root().gpu_tile(x, y, xi, yi, 16, 8)
            for j in range(0, J):
                blockw = 16
                blockh = 8
                if j > 3:
                    blockw = 2
                    blockh = 2

                if j > 0:
                    inGPyramid[j].compute_root().gpu_tile(x, y, xi, yi, blockw, blockh)
                    (
                        gPyramid[j]
                        .compute_root()
                        .reorder(k, x, y)
                        .gpu_tile(x, y, xi, yi, blockw, blockh)
                    )

                outGPyramid[j].compute_root().gpu_tile(x, y, xi, yi, blockw, blockh)

        else:
            # CPU schedule.

            # 21.4ms on an Intel i9-9960X using 32 threads at 3.7
            # GHz, using the target x86-64-avx2.

            # This app is dominated by data-dependent loads from
            # memory, so we're better off leaving the AVX-512 units
            # off in exchange for a higher clock, and we benefit from
            # hyperthreading.

            remap.compute_root()
            yo = hl.Var("yo")
            (
                g.output_buf.reorder(c, x, y)
                .split(y, yo, y, 64)
                .parallel(yo)
                .vectorize(x, 8)
            )
            gray.compute_root().parallel(y, 32).vectorize(x, 8)
            for j in range(1, 5):
                inGPyramid[j].compute_root().parallel(y, 32).vectorize(x, 8)
                (
                    gPyramid[j]
                    .compute_root()
                    .reorder_storage(x, k, y)
                    .reorder(k, y)
                    .parallel(y, 8)
                    .vectorize(x, 8)
                )
                (
                    outGPyramid[j]
                    .store_at(g.output_buf, yo)
                    .compute_at(g.output_buf, y)
                    .fold_storage(y, 4)
                    .vectorize(x, 8)
                )

            outGPyramid[0].compute_at(g.output_buf, y).vectorize(x, 8)
            for j in range(5, J):
                inGPyramid[j].compute_root()
                gPyramid[j].compute_root().parallel(k)
                outGPyramid[j].compute_root()


if __name__ == "__main__":
    hl.main()
