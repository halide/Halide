#include "Target.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>

#include "../../src/Target.h"

#include <vector>
#include <string>

void defineTarget()
{
    using Halide::Target;
    namespace h = Halide;
    namespace p = boost::python;
    using p::self;

    p::class_<Target>("Target",
                      "A struct representing a target machine and os to generate code for.",
                      p::init<>())

            // not all constructors (yet) exposed
            //Target(OS o, Arch a, int b, std::vector<Feature> initial_features = std::vector<Feature>())
            .def(self == self)
            .def(self != self)
            .def("has_gpu_feature", &Target::has_gpu_feature, p::arg("self"),
                 "Is OpenCL or CUDA enabled in this target? "
                 "I.e. is Func::gpu_tile and similar going to work? "
                 "We do not include OpenGL, because it is not capable of gpgpu, "
                 "and is not scheduled via Func::gpu_tile.")

            // not all methods (yet) exposed
            ;

    p::def("get_host_target", &h::get_host_target,
           "Return the target corresponding to the host machine.");

    p::def("get_target_from_environment", &h::get_target_from_environment,
           "Return the target that Halide will use. If HL_TARGET is set it "
           "uses that. Otherwise calls \ref get_host_target");

    p::def("get_jit_target_from_environment", &h::get_jit_target_from_environment,
           "Return the target that Halide will use for jit-compilation. If "
           "HL_JIT_TARGET is set it uses that. Otherwise calls \\ref "
           "get_host_target. Throws an error if the architecture, bit width, "
           "and OS of the target do not match the host target, so this is only "
           "useful for controlling the feature set.");

    return;
}
