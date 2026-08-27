#!/usr/bin/python3

# Halide tutorial lesson 19: Wrapper Funcs

# This lesson demonstrates how to use Func.in_ and ImageParam.in_ to
# schedule a Func differently in different places, and to stage loads
# from a Func or an ImageParam.

import halide as hl


def main():
    # First we'll declare some Vars to use below.
    x, y, xo, yo, xi, yi = (
        hl.Var("x"),
        hl.Var("y"),
        hl.Var("xo"),
        hl.Var("yo"),
        hl.Var("xi"),
        hl.Var("yi"),
    )

    # This lesson will be about "wrapping" a Func or an ImageParam using the
    # Func.in_ and ImageParam.in_ directives. (They're named in_ rather than
    # in, since in is a reserved word in Python.)
    if True:
        # Consider a simple two-stage pipeline:
        f, g = hl.Func("f_local"), hl.Func("g_local")
        f[x, y] = x + y
        g[x, y] = 2 * f[x, y] + 3

        f.compute_root()

        # This produces the following loop nests:
        # for y:
        #   for x:
        #     f(x, y) = x + y
        # for y:
        #   for x:
        #     g(x, y) = 2 * f(x, y) + 3

        # Using Func.in_, we can interpose a new Func in between f
        # and g using the schedule alone:
        f_in_g = f.in_(g)
        f_in_g.compute_root()

        # Equivalently, we could also chain the schedules like so:
        # f.in_(g).compute_root()

        # This produces the following three loop nests:
        # for y:
        #   for x:
        #     f(x, y) = x + y
        # for y:
        #   for x:
        #     f_in_g(x, y) = f(x, y)
        # for y:
        #   for x:
        #     g(x, y) = 2 * f_in_g(x, y) + 3

        g.realize([5, 5])

        # See figures/lesson_19_wrapper_local.mp4 for a visualization, below.

        # The schedule directive f.in_(g) replaces all calls to 'f'
        # inside 'g' with a wrapper Func and then returns that
        # wrapper. Essentially, it rewrites the original pipeline
        # above into the following:
        if True:
            f_in_g, f, g = hl.Func("f_in_g"), hl.Func("f"), hl.Func("g")
            f[x, y] = x + y
            f_in_g[x, y] = f[x, y]
            g[x, y] = 2 * f_in_g[x, y] + 3

            f.compute_root()
            f_in_g.compute_root()
            g.compute_root()

        # In isolation, such a transformation seems pointless, but it
        # can be used for a variety of scheduling tricks.

    if True:
        # In the schedule above, only the calls to 'f' made by 'g'
        # are replaced. Other calls made to f would still call 'f'
        # directly. If we wish to globally replace all calls to 'f'
        # with a single wrapper, we simply say f.in_().

        # Consider a three stage pipeline, with two consumers of f:
        f, g, h = hl.Func("f_global"), hl.Func("g_global"), hl.Func("h_global")
        f[x, y] = x + y
        g[x, y] = 2 * f[x, y]
        h[x, y] = 3 + g[x, y] - f[x, y]
        f.compute_root()
        g.compute_root()
        h.compute_root()

        # We will replace all calls to 'f' inside both 'g' and 'h'
        # with calls to a single wrapper:
        f.in_().compute_root()

        # The equivalent loop nests are:
        # for y:
        #   for x:
        #     f(x, y) = x + y
        # for y:
        #   for x:
        #     f_in(x, y) = f(x, y)
        # for y:
        #   for x:
        #     g(x, y) = 2 * f_in(x, y)
        # for y:
        #   for x:
        #     h(x, y) = 3 + g(x, y) - f_in(x, y)

        h.realize([5, 5])

        # See figures/lesson_19_wrapper_global.mp4 for a
        # visualization of what this did, below.

    if True:
        # We could also give g and h their own unique wrappers of
        # f. This time we'll schedule them each inside the loop nests
        # of the consumer, which is not something we could do with a
        # single global wrapper.

        f, g, h = hl.Func("f_unique"), hl.Func("g_unique"), hl.Func("h_unique")
        f[x, y] = x + y
        g[x, y] = 2 * f[x, y]
        h[x, y] = 3 + g[x, y] - f[x, y]

        f.compute_root()
        g.compute_root()
        h.compute_root()

        f.in_(g).compute_at(g, y)
        f.in_(h).compute_at(h, y)

        # This creates the loop nests:
        # for y:
        #   for x:
        #     f(x, y) = x + y
        # for y:
        #   for x:
        #     f_in_g(x, y) = f(x, y)
        #   for x:
        #     g(x, y) = 2 * f_in_g(x, y)
        # for y:
        #   for x:
        #     f_in_h(x, y) = f(x, y)
        #   for x:
        #     h(x, y) = 3 + g(x, y) - f_in_h(x, y)

        h.realize([5, 5])
        # See figures/lesson_19_wrapper_unique.mp4 for a visualization, below.

    if True:
        # So far this may seem like a lot of pointless copying of
        # memory. Func.in_ can be combined with other scheduling
        # directives for a variety of purposes. The first we will
        # examine is creating distinct realizations of a Func for
        # several consumers and scheduling each differently.

        # We'll start with nearly the same pipeline.
        f, g, h = hl.Func("f_sched"), hl.Func("g_sched"), hl.Func("h_sched")
        f[x, y] = x + y
        g[x, y] = 2 * f[x, y]
        # h will use a far-away region of f
        h[x, y] = 3 + g[x, y] - f[x + 93, y - 87]

        # This time we'll inline f.
        # f.compute_root()
        g.compute_root()
        h.compute_root()

        f.in_(g).compute_at(g, y)
        f.in_(h).compute_at(h, y)

        # g and h now call f via distinct wrappers. The wrappers are
        # scheduled, but f is not, which means that f is inlined into
        # its two wrappers. They will each independently compute the
        # region of f required by their consumer. If we had scheduled
        # f compute_root, we'd be computing the bounding box of the
        # region required by g and the region required by h, which
        # would mostly be unused data.

        # We can also schedule each of these wrappers
        # differently. For scheduling purposes, wrappers inherit the
        # pure vars of the Func they wrap, so we use the same x and y
        # that we used when defining f:
        f.in_(g).vectorize(x, 4)
        f.in_(h).split(x, xo, xi, 2).reorder(xo, xi)

        # Note that calling f.in_(g) a second time returns the wrapper
        # already created by the first call, it doesn't make a new one.

        h.realize([8, 8])
        # See figures/lesson_19_wrapper_vary_schedule.mp4 for a
        # visualization, below.

        # Note that because f is inlined into its two wrappers, it is
        # the wrappers that do the work of computing f, rather than
        # just loading from an existing computed realization.

    if True:
        # Func.in_ is useful to stage loads from a Func via some
        # smaller intermediate buffer, perhaps on the stack or in
        # shared GPU memory.

        # Consider a pipeline that transposes some compute_root'd Func:

        f, g = hl.Func("f_transpose"), hl.Func("g_transpose")
        f[x, y] = hl.sin(((x + y) * hl.sqrt(y)) / 10)
        f.compute_root()

        g[x, y] = f[y, x]

        # The execution strategy we want is to load an 4x4 tile of f
        # into registers, transpose it in-register, and then write it
        # out as an 4x4 tile of g. We will use Func.in_ to express this:

        f_tile = f.in_(g)

        # We now have a three stage pipeline:
        # f -> f_tile -> g

        # f_tile will load vectors of f, and store them transposed
        # into registers. g will then write this data back to main
        # memory.
        g.tile(x, y, xo, yo, xi, yi, 4, 4).vectorize(xi).unroll(yi)

        # We will compute f_transpose at tiles of g, and use
        # Func.reorder_storage to state that f_transpose should be
        # stored column-major, so that the loads to it done by g can
        # be dense vector loads.
        f_tile.compute_at(g, xo).reorder_storage(y, x).vectorize(x).unroll(y)

        # We take care to make sure f_transpose is only ever accessed
        # at constant indices. The full unrolling/vectorization of
        # all loops that exist inside its compute_at level has this
        # effect. Allocations that are only ever accessed at constant
        # indices can be promoted into registers.

        g.realize([16, 16])
        # See figures/lesson_19_transpose.mp4 for a visualization, below.

    if True:
        # ImageParam.in_ behaves the same way as Func.in_, and you
        # can use it to stage loads in similar ways. Instead of
        # transposing again, we'll use ImageParam.in_ to stage tiles
        # of an input image into GPU shared memory, effectively using
        # shared/local memory as an explicitly-managed cache.

        img = hl.ImageParam(hl.Int(32), 2)

        # We will compute a small blur of the input.
        blur = hl.Func("blur")
        blur[x, y] = (
            img[x - 1, y - 1]
            + img[x, y - 1]
            + img[x + 1, y - 1]
            + img[x - 1, y]
            + img[x, y]
            + img[x + 1, y]
            + img[x - 1, y + 1]
            + img[x, y + 1]
            + img[x + 1, y + 1]
        )

        blur.compute_root().gpu_tile(x, y, xo, yo, xi, yi, 8, 8)

        # The wrapper Func created by ImageParam.in_ has pure vars
        # named _0, _1, etc. Schedule it per tile of "blur", and map
        # _0 and _1 to gpu threads.
        img.in_(blur).compute_at(blur, xo).gpu_threads(hl._0, hl._1)

        # Without Func.in_, computing an 8x8 tile of blur would do
        # 8*8*9 loads to global memory. With Func.in_, the wrapper
        # does 10*10 loads to global memory up front, and then blur
        # does 8*8*9 loads to shared/local memory.

        # Select an appropriate GPU API, as we did in lesson 12.
        target = hl.get_host_target()
        if target.os == hl.TargetOS.OSX:
            target = target.with_feature(hl.TargetFeature.Metal)
        else:
            target = target.with_feature(hl.TargetFeature.OpenCL)

        # This check isn't strictly necessary, but it allows a more graceful
        # exit if running on a system that doesn't have the expected drivers
        # and/or hardware present.
        if not hl.host_supports_target_device(target):
            print(
                "Requested GPU is not supported; skipping this test. "
                "(Do you have the proper hardware and/or driver installed?)"
            )
            return 0

        # Create an interesting input image to use.
        input = hl.Buffer(hl.Int(32), [258, 258])
        input.set_min([-1, -1])
        for yy in range(input.top(), input.bottom() + 1):
            for xx in range(input.left(), input.right() + 1):
                input[xx, yy] = xx * 17 + yy % 4

        img.set(input)
        blur.compile_jit(target)
        out = blur.realize([256, 256])

        # Check the output is what we expected
        for yy in range(out.top(), out.bottom() + 1):
            for xx in range(out.left(), out.right() + 1):
                val = out[xx, yy]
                expected = (
                    input[xx - 1, yy - 1]
                    + input[xx, yy - 1]
                    + input[xx + 1, yy - 1]
                    + input[xx - 1, yy]
                    + input[xx, yy]
                    + input[xx + 1, yy]
                    + input[xx - 1, yy + 1]
                    + input[xx, yy + 1]
                    + input[xx + 1, yy + 1]
                )
                assert val == expected, f"out({xx}, {yy}) = {val} instead of {expected}"

    if True:
        # Func.in_ can also be used to group multiple stages of a
        # Func into the same loop nest. Consider the following
        # pipeline, which computes a value per pixel, then sweeps
        # from left to right and back across each scanline.
        f, g = hl.Func("f_group"), hl.Func("g_group")

        # Initialize f
        f[x, y] = hl.sin(x - y)
        r = hl.RDom([(1, 7)])

        # Sweep from left to right
        f[r, y] = (f[r, y] + f[r - 1, y]) / 2

        # Sweep from right to left
        f[7 - r, y] = (f[7 - r, y] + f[8 - r, y]) / 2

        # Then we do something with a complicated access pattern: A
        # 45 degree rotation with wrap-around
        g[x, y] = f[(x + y) % 8, (x - y) % 8]

        # f should be scheduled compute_root, because its consumer
        # accesses it in a complicated way. But that means all stages
        # of f are computed in separate loop nests:

        # for y:
        #   for x:
        #     f(x, y) = sin(x - y)
        # for y:
        #   for r:
        #     f(r, y) = (f(r, y) + f(r - 1, y)) / 2
        # for y:
        #   for r:
        #     f(7 - r, y) = (f(7 - r, y) + f(8 - r, y)) / 2
        # for y:
        #   for x:
        #     g(x, y) = f((x + y) % 8, (x - y) % 8);

        # We can get better locality if we schedule the work done by
        # f to share a common loop over y. We can do this by
        # computing f at scanlines of a wrapper like so:

        f.in_(g).compute_root()
        f.compute_at(f.in_(g), y)

        # f has the default schedule for a Func with update stages,
        # which is to be computed at the innermost loop of its
        # consumer, which is now the wrapper f.in_(g). This therefore
        # generates the following loop nest, which has better
        # locality:

        # for y:
        #   for x:
        #     f(x, y) = sin(x - y)
        #   for r:
        #     f(r, y) = (f(r, y) + f(r - 1, y)) / 2
        #   for r:
        #     f(7 - r, y) = (f(7 - r, y) + f(8 - r, y)) / 2
        #   for x:
        #     f_in_g(x, y) = f(x, y)
        # for y:
        #   for x:
        #     g(x, y) = f_in_g((x + y) % 8, (x - y) % 8);

        # We'll additionally vectorize the initialization of, and
        # then transfer of pixel values from f into its wrapper:
        f.vectorize(x, 4)
        f.in_(g).vectorize(x, 4)

        g.realize([8, 8])
        # See figures/lesson_19_group_updates.mp4 for a visualization, below.

    print("Success!")
    return 0


if __name__ == "__main__":
    main()
