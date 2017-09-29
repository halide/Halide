// Halide tutorial lesson 21: Auto-Scheduler

// This lesson demonstrates how to use the auto-scheduler to generate a
// copy-pastable CPU schedule that can be subsequently improved upon.

// On linux or os x, you can compile and run it like so:

// g++ lesson_21_auto_scheduler_generate.cpp ../tools/GenGen.cpp -g -std=c++11 -fno-rtti -I ../include -L ../bin -lHalide -lpthread -ldl -o lesson_21_generate
// export LD_LIBRARY_PATH=../bin   # For linux
// export DYLD_LIBRARY_PATH=../bin # For OS X
// ./lesson_21_generate -o . -f conv_layer target=host
// g++ lesson_21_auto_scheduler_run.cpp brighten_*.o -ldl -lpthread -o lesson_21_run
// ./lesson_21_run

// If you have the entire Halide source tree, you can also build it by
// running:
//    make tutorial_lesson_21_auto_scheduler_run
// in a shell with the current directory at the top of the halide
// source tree.

#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// We will define a generator to auto-schedule.
class AutoScheduled : public Halide::Generator<AutoScheduled> {
public:
    GeneratorParam<bool>  auto_schedule{"auto_schedule", false};

    Input<Buffer<float>>  input{"input", 4};
    Input<Buffer<float>>  filter{"filter", 4};
    Input<Buffer<float>>  bias{"bias", 1};
    Input<float>          min_value{"min_value"};

    Output<Buffer<float>> output1{"output1", 4};
    Output<Buffer<float>> output2{"output2", 4};

    void generate() {
        RDom r(filter.dim(0).min(), filter.dim(0).extent(),
               filter.dim(1).min(), filter.dim(1).extent(),
               filter.dim(2).min(), filter.dim(2).extent());

        f(x, y, z, n) = bias(z);
        f(x, y, z, n) += filter(r.x, r.y, r.z, z) * input(x + r.x, y + r.y, r.z, n);
        output1(x, y, z, n) = max(0, f(x, y, z, n));
        output2(x, y, z, n) = min(min_value, f(x, y, z, n));
    }

    void schedule() {
        if (auto_schedule) {
            // To use the auto-scheduler, we need to provide estimates on all
            // the input/output sizes including estimates on all the parameter
            // values; otherwise, the auto-scheduler will throw an assertion.

            // To provide estimates (min and extent values) for each dimension
            // of the input images ('input', 'filter', and 'bias'), we use the
            // set_bounds_estimate() method. set_bounds_estimate() takes in
            // (min, extent) of the corresponding dimension as arguments.
            input.dim(0).set_bounds_estimate(0, 131);
            input.dim(1).set_bounds_estimate(0, 131);
            input.dim(2).set_bounds_estimate(0, 64);
            input.dim(3).set_bounds_estimate(0, 4);

            filter.dim(0).set_bounds_estimate(0, 3);
            filter.dim(1).set_bounds_estimate(0, 3);
            filter.dim(2).set_bounds_estimate(0, 64);
            filter.dim(3).set_bounds_estimate(0, 64);

            bias.dim(0).set_bounds_estimate(0, 64);

            // To provide estimates on the parameter values, we use the
            // set_estimate() method.
            min_value.set_estimate(2.0f);

            // To provide estimates (min and extent values) for each dimension
            // of pipeline outputs, we use the estimate() method. estimate()
            // takes in (dim_name, min, extent) as arguments.
            output1.estimate(x, 0, 128)
                .estimate(y, 0, 128)
                .estimate(z, 0, 64)
                .estimate(n, 0, 4);

            output2.estimate(x, 0, 128)
                .estimate(y, 0, 128)
                .estimate(z, 0, 64)
                .estimate(n, 0, 4);

            // Technically, the estimate values can be anything, but the closer
            // they are to the actual use-case values, the better the generated
            // schedule will be.

            // Now, let's auto-schedule the pipeline by calling auto_schedule_outputs(),
            // which takes in a MachineParams object as an argument. The machine_params
            // argument is optional. If none is specified, the default machine parameters
            // for a generic CPU architecture are going to be used by the auto-scheduler.

            // Let's use some arbitrary but plausible values for the machine parameters.
            MachineParams machine_params(32, 16 * 1024 * 1024, 40);
            // The arguments to MachineParams are the maximum level of parallelism
            // available, the size of the last-level cache (in KB), and the ratio
            // between memory cost and arithmetic cost at the last level case, of
            // the target architecture, in that order.

            // Note that when using the auto-scheduler, no schedule should have
            // been applied to the pipeline; otherwise, the auto-scheduler will
            // throw an error. The current auto-scheduler does not work with
            // partially-scheduled pipeline.
            //
            // Calling auto_schedule_outputs() will apply the generated schedule
            // automatically to members of the pipeline in addition to returning
            // a string representation of the schedule.
            std::string schedule = auto_schedule_outputs(machine_params);
            std::cout << "\nSchedule:\n\n" << schedule << "\n";

            // The generated schedule that is dumped to std::cout is an actual
            // Halide C++ source, which is readily copy-pastable back into
            // this very same source file with little modifications. Programmers
            // can use this as a starting schedule and iteratively improve the
            // schedule. Note that the current auto-scheduler is only able to
            // generate CPU schedule and only does tiling, simple vectorization
            // and parallelization. It doesn't deal with line buffering, storage
            // reordering, or factoring a reduction.
        } else {
            // This is where you would declare the schedule you have or
            // paste the schedule generated by the auto-scheduler.

            // This auto-scheduler will return the following schedule for the
            // estimates and machine parameters declared above when run on
            // this pipeline:
            //
            // Var x_vi("x_vi");
            // Var x_vo("x_vo");
            //
            // Func f0 = pipeline.get_func(3);
            // Func output1 = pipeline.get_func(4);
            // Func output2 = pipeline.get_func(5);
            //
            // {
            //     Var x = f0.args()[0];
            //     Var y = f0.args()[1];
            //     Var z = f0.args()[2];
            //     Var n = f0.args()[3];
            //     RVar r$x(f0.update(0).get_schedule().rvars()[0].var);
            //     RVar r$y(f0.update(0).get_schedule().rvars()[1].var);
            //     RVar r$z(f0.update(0).get_schedule().rvars()[2].var);
            //     f0
            //         .compute_root()
            //         .split(x, x_vo, x_vi, 8)
            //         .vectorize(x_vi)
            //         .parallel(n)
            //         .parallel(z);
            //     f0.update(0)
            //         .split(x, x_vo, x_vi, 8)
            //         .vectorize(x_vi)
            //         .parallel(n)
            //         .parallel(z);
            // }
            // {
            //     Var x = output1.args()[0];
            //     Var y = output1.args()[1];
            //     Var z = output1.args()[2];
            //     Var n = output1.args()[3];
            //     output1
            //         .compute_root()
            //         .split(x, x_vo, x_vi, 8)
            //         .vectorize(x_vi)
            //         .parallel(n)
            //         .parallel(z);
            // }
            // {
            //     Var x = output2.args()[0];
            //     Var y = output2.args()[1];
            //     Var z = output2.args()[2];
            //     Var n = output2.args()[3];
            //     output2
            //         .compute_root()
            //         .split(x, x_vo, x_vi, 8)
            //         .vectorize(x_vi)
            //         .parallel(n)
            //         .parallel(z);
            // }
        }
    }
private:
    Var x{"x"}, y{"y"}, z{"z"}, n{"n"};
    Func f;
};

// As in lesson 15, we register our generator and then compile this
// file along with tools/GenGen.cpp.
HALIDE_REGISTER_GENERATOR(AutoScheduled, auto_schedule_gen)

// After compiling this file, see how to use it in
// lesson_21_auto_scheduler_run.cpp
