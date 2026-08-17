#!/usr/bin/python3

# Halide tutorial lesson 21: Auto-Scheduler: generating a schedule

# So far we have written Halide schedules by hand, but it is also possible to
# ask Halide to suggest a reasonable schedule. We call this auto-scheduling.
# This lesson demonstrates how to use the autoscheduler to generate a
# copy-pasteable CPU schedule that can be subsequently improved upon.

import halide as hl


def sum3x3(f, x, y):
    return (
        f[x - 1, y - 1]
        + f[x - 1, y]
        + f[x - 1, y + 1]
        + f[x, y - 1]
        + f[x, y]
        + f[x, y + 1]
        + f[x + 1, y - 1]
        + f[x + 1, y]
        + f[x + 1, y + 1]
    )


# We will define a generator to auto-schedule.
@hl.generator(name="auto_schedule_gen")
class AutoScheduled:
    input_buf = hl.InputBuffer(hl.Float(32), 3)
    factor = hl.InputScalar(hl.Float(32))

    output1 = hl.OutputBuffer(hl.Float(32), 2)
    output2 = hl.OutputBuffer(hl.Float(32), 2)

    def generate(self):
        g = self
        x, y = hl.Var("x"), hl.Var("y")

        # For our algorithm, we'll use Harris corner detection.
        in_b = hl.BoundaryConditions.repeat_edge(g.input_buf)

        gray = hl.Func("gray")
        gray[x, y] = (
            hl.f32(0.299) * in_b[x, y, 0]
            + hl.f32(0.587) * in_b[x, y, 1]
            + hl.f32(0.114) * in_b[x, y, 2]
        )

        Iy = hl.Func("Iy")
        Iy[x, y] = (
            gray[x - 1, y - 1] * hl.f32(-1.0 / 12)
            + gray[x - 1, y + 1] * hl.f32(1.0 / 12)
            + gray[x, y - 1] * hl.f32(-2.0 / 12)
            + gray[x, y + 1] * hl.f32(2.0 / 12)
            + gray[x + 1, y - 1] * hl.f32(-1.0 / 12)
            + gray[x + 1, y + 1] * hl.f32(1.0 / 12)
        )

        Ix = hl.Func("Ix")
        Ix[x, y] = (
            gray[x - 1, y - 1] * hl.f32(-1.0 / 12)
            + gray[x + 1, y - 1] * hl.f32(1.0 / 12)
            + gray[x - 1, y] * hl.f32(-2.0 / 12)
            + gray[x + 1, y] * hl.f32(2.0 / 12)
            + gray[x - 1, y + 1] * hl.f32(-1.0 / 12)
            + gray[x + 1, y + 1] * hl.f32(1.0 / 12)
        )

        Ixx = hl.Func("Ixx")
        Ixx[x, y] = Ix[x, y] * Ix[x, y]
        Iyy = hl.Func("Iyy")
        Iyy[x, y] = Iy[x, y] * Iy[x, y]
        Ixy = hl.Func("Ixy")
        Ixy[x, y] = Ix[x, y] * Iy[x, y]
        Sxx = hl.Func("Sxx")
        Sxx[x, y] = sum3x3(Ixx, x, y)
        Syy = hl.Func("Syy")
        Syy[x, y] = sum3x3(Iyy, x, y)
        Sxy = hl.Func("Sxy")
        Sxy[x, y] = sum3x3(Ixy, x, y)
        det = hl.Func("det")
        det[x, y] = Sxx[x, y] * Syy[x, y] - Sxy[x, y] * Sxy[x, y]
        trace = hl.Func("trace")
        trace[x, y] = Sxx[x, y] + Syy[x, y]
        harris = hl.Func("harris")
        harris[x, y] = det[x, y] - hl.f32(0.04) * trace[x, y] * trace[x, y]
        g.output1[x, y] = harris[x, y]
        g.output2[x, y] = g.factor * harris[x, y]

        # Unlike the C++ Generator API, Python generators don't have a
        # separate schedule() method -- there's just generate(), so the
        # scheduling below runs immediately after the algorithm above.
        if g.using_autoscheduler():
            # The autoscheduler requires estimates on all the input/output
            # sizes and parameter values in order to compare different
            # alternatives and decide on a good schedule.

            # To provide estimates (min and extent values) for each dimension
            # of the input images ('input', 'filter', and 'bias'), we use the
            # set_estimates() method. set_estimates() takes in a list of
            # (min, extent) of the corresponding dimension as arguments.
            g.input_buf.set_estimates([(0, 1024), (0, 1024), (0, 3)])

            # To provide estimates on the parameter values, we use the
            # set_estimate() method.
            g.factor.set_estimate(2.0)

            # To provide estimates (min and extent values) for each dimension
            # of pipeline outputs, we use the set_estimates() method.
            # set_estimates() takes in a list of (min, extent) for each
            # dimension.
            g.output1.set_estimates([(0, 1024), (0, 1024)])
            g.output2.set_estimates([(0, 1024), (0, 1024)])

            # Technically, the estimate values can be anything, but the
            # closer they are to the actual use-case values, the better the
            # generated schedule will be.

            # To auto-schedule the pipeline, we don't have to do anything
            # else: every Generator implicitly has a GeneratorParam named
            # "autoscheduler.name"; if this is set to the name of the
            # Autoscheduler we want to use, Halide will apply it to all of
            # our pipeline's outputs automatically.

            # Every Generator also implicitly has additional, optional
            # GeneratorParams that are dependent on the specific
            # Autoscheduler selected, which allows you to specify
            # characteristics of the machine architecture for the
            # autoscheduler; it's generally specified on the command line.
            # If none is specified, the default machine parameters for a
            # generic CPU architecture will be used by the autoscheduler.

            # Let's see some arbitrary but plausible values for the machine
            # parameters for the Mullapudi2016 Autoscheduler:
            #
            #      autoscheduler=Mullapudi2016
            #      autoscheduler.parallelism=32
            #      autoscheduler.last_level_cache_size=16777216
            #      autoscheduler.balance=40
            #
            # These are the maximum level of parallelism available, the size
            # of the last-level cache (in bytes), and the ratio between the
            # cost of a miss at the last level cache and the cost of
            # arithmetic on the target architecture, in that order.

            # Note that when using the autoscheduler, no schedule should have
            # been applied to the pipeline; otherwise, the autoscheduler will
            # throw an error. The current autoscheduler cannot handle a
            # partially-scheduled pipeline.

            # If HL_DEBUG_CODEGEN is set to 3 or greater, the schedule will
            # be dumped to stdout (along with much other information); a
            # more useful way is to add "schedule" to the -e flag to the
            # Generator. In CMake, this is done by passing the argument
            # SCHEDULE <outvar> to add_halide_library(). See
            # doc/HalideCMakePackage.md for more detail.

            # The generated schedule that is dumped to file is an actual
            # Halide C++ source, which is readily copy-pasteable back into
            # this very same source file with few modifications. Programmers
            # can use this as a starting schedule and iteratively improve
            # the schedule. Note that the current autoscheduler is only able
            # to generate CPU schedules and only does tiling, simple
            # vectorization and parallelization. It doesn't deal with line
            # buffering, storage reordering, or factoring reductions.

            # At the time of writing, the autoscheduler will produce the
            # following schedule for the estimates and machine parameters
            # declared above when run on this pipeline:
            #
            # Var x_i("x_i")
            # Var x_i_vi("x_i_vi")
            # Var x_i_vo("x_i_vo")
            # Var x_o("x_o")
            # Var x_vi("x_vi")
            # Var x_vo("x_vo")
            # Var y_i("y_i")
            # Var y_o("y_o")
            #
            # Func Ix = pipeline.get_func(4)
            # Func Iy = pipeline.get_func(7)
            # Func gray = pipeline.get_func(3)
            # Func harris = pipeline.get_func(14)
            # Func output1 = pipeline.get_func(15)
            # Func output2 = pipeline.get_func(16)
            #
            # {
            #     Var x = Ix.args()[0]
            #     Ix
            #         .compute_at(harris, x_o)
            #         .split(x, x_vo, x_vi, 8)
            #         .vectorize(x_vi)
            # }
            # {
            #     Var x = Iy.args()[0]
            #     Iy
            #         .compute_at(harris, x_o)
            #         .split(x, x_vo, x_vi, 8)
            #         .vectorize(x_vi)
            # }
            # {
            #     Var x = gray.args()[0]
            #     gray
            #         .compute_at(harris, x_o)
            #         .split(x, x_vo, x_vi, 8)
            #         .vectorize(x_vi)
            # }
            # {
            #     Var x = harris.args()[0]
            #     Var y = harris.args()[1]
            #     harris
            #         .compute_root()
            #         .split(x, x_o, x_i, 256)
            #         .split(y, y_o, y_i, 128)
            #         .reorder(x_i, y_i, x_o, y_o)
            #         .split(x_i, x_i_vo, x_i_vi, 8)
            #         .vectorize(x_i_vi)
            #         .parallel(y_o)
            #         .parallel(x_o)
            # }
            # {
            #     Var x = output1.args()[0]
            #     Var y = output1.args()[1]
            #     output1
            #         .compute_root()
            #         .split(x, x_vo, x_vi, 8)
            #         .vectorize(x_vi)
            #         .parallel(y)
            # }
            # {
            #     Var x = output2.args()[0]
            #     Var y = output2.args()[1]
            #     output2
            #         .compute_root()
            #         .split(x, x_vo, x_vi, 8)
            #         .vectorize(x_vi)
            #         .parallel(y)
            # }

        else:
            # This is where you would declare the schedule you have written
            # by hand or paste the schedule generated by the autoscheduler.
            # We will use a naive schedule here to compare the performance of
            # the autoschedule with a basic schedule.
            gray.compute_root()
            Iy.compute_root()
            Ix.compute_root()

            # As discussed earlier, the generated schedule that is dumped to
            # file is an actual Halide C++ source, which is readily
            # copy-pasteable back into this very same source file with few
            # modifications. Or, developers can save the generated schedules
            # to the source directory, and then include the generated
            # schedule here.
            #
            # import tutorial_schedule
            # tutorial_schedule.apply_schedule_auto_schedule_true(g.get_pipeline(), g.target())


if __name__ == "__main__":
    hl.main()
