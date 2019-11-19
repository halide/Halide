#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    // Loads libgradient_autoscheduler.so (or gradient_autoscheduler.dll),
    // which is presumed to be in current library search path
    load_plugin("gradient_autoscheduler");
    Pipeline::set_default_autoscheduler_name("Li2018");

    MachineParams params(32, 16000000, 40);
    Target target;

    Var x("x"), y("y");

    { // Simple 1D pointwise operations. Should inline.
        Func in("in");
        in(x) = cast<float>(x);
        Func f0("f0");
        f0(x) = 2.f * in(x);
        Func f1("f1");
        f1(x) = sin(f0(x));
        Func f2("f2");
        f2(x) = f1(x) * f1(x);

        f2.set_estimate(x, 0, 10000);

        AutoSchedulerResults result =
            Pipeline(f2).auto_schedule(target, params);
        std::cout << "Schedule for 1D pointwise operations:" << std::endl;
        std::cout << result.schedule_source << std::endl;
        std::cout << std::endl;
    }

    { // Simple 2D pointwise operations. Should inline.
        Func in("in");
        in(x, y) = cast<float>(x + y);
        Func f0("f0");
        f0(x, y) = 2.f * in(x, y);
        Func f1("f1");
        f1(x, y) = sin(f0(x, y));
        Func f2("f2");
        f2(x, y) = f1(x, y) * f1(x, y);

        f2.set_estimate(x, 0, 1000)
          .set_estimate(y, 0, 1000);

        AutoSchedulerResults result =
            Pipeline(f2).auto_schedule(target, params);
        std::cout << "Schedule for 2D pointwise operations:" << std::endl;
        std::cout << result.schedule_source << std::endl;
        std::cout << std::endl;
    }

    { // 1D Convolution.
        Func in("in");
        in(x) = cast<float>(x);
        RDom r(0, 5);
        Func f0("f0");
        f0(x) += in(x + r) / 5.f;

        f0.set_estimate(x, 0, 1000);

        AutoSchedulerResults result =
            Pipeline(f0).auto_schedule(target, params);
        std::cout << "Schedule for 1D convolution:" << std::endl;
        std::cout << result.schedule_source << std::endl;
        std::cout << std::endl;
    }

    { // 2D Convolution.
        Func in("in");
        in(x, y) = cast<float>(x + y);
        RDom r(0, 5, 0, 5);
        Func f0("f0");
        f0(x, y) += in(x + r.x, y + r.y) / 25.f;

        f0.set_estimate(x, 0, 1000)
          .set_estimate(y, 0, 1000);

        AutoSchedulerResults result =
            Pipeline(f0).auto_schedule(target, params);
        std::cout << "Schedule for 2D convolution:" << std::endl;
        std::cout << result.schedule_source << std::endl;
        std::cout << std::endl;
    }

    { // 1D Histogram.
        Func in("in");
        in(x) = x % 10;
        RDom r(0, 1000);
        Func hist("hist");
        hist(x) = 0;
        hist(clamp(in(r), 0, 10)) += 1;

        hist.set_estimate(x, 0, 10);

        AutoSchedulerResults result =
            Pipeline(hist).auto_schedule(target, params);
        std::cout << "Schedule for 1D histogram:" << std::endl;
        std::cout << result.schedule_source << std::endl;
        std::cout << std::endl;
    }

    { // 2D Histogram.
        Func in("in");
        in(x, y) = (x + y) % 10;
        RDom r(0, 1000, 0, 1000);
        Func hist("hist");
        hist(x) = 0;
        hist(clamp(in(r.x, r.y), 0, 10)) += 1;

        hist.set_estimate(x, 0, 10);

        AutoSchedulerResults result =
            Pipeline(hist).auto_schedule(target, params);
        std::cout << "Schedule for 2D histogram:" << std::endl;
        std::cout << result.schedule_source << std::endl;
        std::cout << std::endl;
    }

    { // 2D Histogram, but the domain is much larger.
        Func in("in");
        in(x, y) = (x + y) % 10000;
        RDom r(0, 1000, 0, 1000);
        Func hist("hist");
        hist(x) = 0;
        hist(clamp(in(r.x, r.y), 0, 10000)) += 1;

        hist.set_estimate(x, 0, 10000);

        AutoSchedulerResults result =
            Pipeline(hist).auto_schedule(target, params);
        std::cout << "Schedule for 2D histogram with larger domain:" << std::endl;
        std::cout << result.schedule_source << std::endl;
        std::cout << std::endl;
    }

    { // Test for conjunction use of bound and estimates.
        Func in("in");
        in(x, y) = cast<float>(x + y);
        Func f0("f0");
        f0(x, y) = 2.f * in(x, y);
        Func f1("f1");
        f1(x, y) = sin(f0(x, y));
        Func f2("f2");
        f2(x, y) = f1(x, y) * f1(x, y);

        f2.bound(x, 0, 4);
        // make sure it also works if we reverse the estimate order
        f2.set_estimate(y, 0, 1024)
          .set_estimate(x, 0, 4);

        AutoSchedulerResults result =
            Pipeline(f2).auto_schedule(target, params);
        std::cout << "Schedule for 2D pointwise operations with small x dimension:" << std::endl;
        std::cout << result.schedule_source << std::endl;
        std::cout << std::endl;
    }
    return 0;
}
