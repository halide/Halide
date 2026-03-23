"""
Simple blur.
"""

import halide as hl
import enum


class BlurGPUSchedule(enum.Enum):
    # Fully inlining schedule.
    Inline = 0
    # Schedule caching intermedia result of blur_x.
    Cache = 1
    # Schedule enabling sliding window opt within each work-item or cuda
    # thread.
    Slide = 2
    # The same as above plus vectorization per work-item.
    SlideVectorize = 3


_GPU_SCHEDULE_ENUM_MAP = {
    "inline": BlurGPUSchedule.Inline,
    "cache": BlurGPUSchedule.Cache,
    "slide": BlurGPUSchedule.Slide,
    "slide_vector": BlurGPUSchedule.SlideVectorize,
}


@hl.generator()
class blur:
    gpu_schedule = hl.GeneratorParam("slide_vector")
    gpu_tile_x = hl.GeneratorParam(32)
    gpu_tile_y = hl.GeneratorParam(8)

    # Note: although this is declared as operating on uint16 images,
    # it will produce incorrect results if more than 14-bit images are used.
    input_buf = hl.InputBuffer(hl.UInt(16), 2)
    blur_y = hl.OutputBuffer(hl.UInt(16), 2)

    def generate(self):
        g = self

        x, y, xi, yi = hl.vars("x y xi yi")

        # The algorithm
        clamped = hl.BoundaryConditions.repeat_edge(g.input_buf)

        blur_x = hl.Func("blur_x")
        blur_x[x, y] = (clamped[x, y] + clamped[x + 1, y] + clamped[x + 2, y]) // 3
        g.blur_y[x, y] = (blur_x[x, y] + blur_x[x, y + 1] + blur_x[x, y + 2]) // 3

        # How to schedule it
        if g.target().has_gpu_feature():
            # GPU schedule.

            # This will raise an exception for unknown strings, which is what
            # we want
            schedule_enum = _GPU_SCHEDULE_ENUM_MAP[g.gpu_schedule]

            if schedule_enum == BlurGPUSchedule.Inline:
                # - Fully inlining.
                g.blur_y.gpu_tile(x, y, xi, yi, g.gpu_tile_x, g.gpu_tile_y)

            elif schedule_enum == BlurGPUSchedule.Cache:
                # - Cache blur_x calculation.
                g.blur_y.gpu_tile(x, y, xi, yi, g.gpu_tile_x, g.gpu_tile_y)
                blur_x.compute_at(g.blur_y, x).gpu_threads(x, y)

            elif schedule_enum == BlurGPUSchedule.Slide:
                # - Instead of caching blur_x calculation explicitly, the
                #   alternative is to allow each work-item in OpenCL or thread
                #   in CUDA to calculate more rows of blur_y so that temporary
                #   blur_x calculation is re-used implicitly. This achieves
                #   the similar schedule of sliding window.
                y_inner = hl.Var("y_inner")
                (
                    g.blur_y.split(y, y, y_inner, g.gpu_tile_y)
                    .reorder(y_inner, x)
                    .unroll(y_inner)
                    .gpu_tile(x, y, xi, yi, g.gpu_tile_x, 1)
                )

            elif schedule_enum == BlurGPUSchedule.SlideVectorize:
                # Vectorization factor.
                factor = 2
                y_inner = hl.Var("y_inner")
                (
                    g.blur_y.vectorize(x, factor)
                    .split(y, y, y_inner, g.gpu_tile_y)
                    .reorder(y_inner, x)
                    .unroll(y_inner)
                    .gpu_tile(x, y, xi, yi, g.gpu_tile_x, 1)
                )

        elif g.target().has_feature(hl.TargetFeature.HVX):
            # Hexagon schedule.
            # TODO: Try using a schedule like the CPU one below.
            vector_size = 128

            (
                g.blur_y.compute_root()
                .hexagon()
                .prefetch(g.input_buf, y, y, 2)
                .split(y, y, yi, 128)
                .parallel(y)
                .vectorize(x, vector_size * 2)
            )
            (
                blur_x.store_at(g.blur_y, y)
                .compute_at(g.blur_y, yi)
                .vectorize(x, vector_size)
            )
        else:
            # CPU schedule.
            # Compute blur_x as needed at each vector of the output.
            # Halide will store blur_x in a circular buffer so its
            # results can be re-used.
            vector_size = g.natural_vector_size(g.input_buf.type())
            g.blur_y.split(y, y, yi, 32).parallel(y).vectorize(x, vector_size)
            (
                blur_x.store_at(g.blur_y, y)
                .compute_at(g.blur_y, x)
                .vectorize(x, vector_size)
            )


if __name__ == "__main__":
    hl.main()
