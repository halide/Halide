#include "Target.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>

#include "Halide.h"

#include "Expr.h"

#include <string>
#include <vector>

namespace h = Halide;
namespace p = boost::python;

void target_set_features(h::Target &t, p::list features, bool value) {
    auto features_vec = python_collection_to_vector<h::Target::Feature>(features);
    t.set_features(features_vec, value);
}

void defineTarget() {
    using Halide::Target;

    auto target_class =
        p::class_<Target>("Target",
                          "A struct representing a target machine and os to generate code for.",
                          p::init<>())

            // not all constructors (yet) exposed
            //Target(OS o, Arch a, int b, std::vector<Feature> initial_features = std::vector<Feature>())
            .def(p::self == p::self)
            .def(p::self != p::self)

            .def_readwrite("os", &Target::os)
            .def_readwrite("arch", &Target::arch)
            .def_readwrite("bits", &Target::bits,
                           "The bit-width of the target machine. Must be 0 for unknown, or 32 or 64.")

            .def("has_gpu_feature", &Target::has_gpu_feature, p::arg("self"),
                 "Is OpenCL or CUDA enabled in this target? "
                 "I.e. is Func::gpu_tile and similar going to work? "
                 "We do not include OpenGL, because it is not capable of gpgpu, "
                 "and is not scheduled via Func::gpu_tile.")

            .def("to_string", &Target::to_string, p::arg("self"),
                 "Convert the Target into a string form that can be reconstituted "
                 "by merge_string(), which will always be of the form\n"
                 "  arch-bits-os-feature1-feature2...featureN.\n"
                 "Note that is guaranteed that t2.from_string(t1.to_string()) == t1, "
                 "but not that from_string(s).to_string() == s (since there can be "
                 "multiple strings that parse to the same Target)... "
                 "*unless* t1 contains 'unknown' fields (in which case you'll get a string "
                 "that can't be parsed, which is intentional).")

            .def("set_feature", &Target::set_feature,
                 (p::arg("self"), p::arg("f"), p::arg("value") = true))
            .def("set_features", &target_set_features,
                 (p::arg("self"), p::arg("features_to_set"), p::arg("value") = true))

        // not all methods (yet) exposed

        ;

    p::enum_<Target::OS>("TargetOS",
                         "The operating system used by the target. Determines which "
                         "system calls to generate.")
        .value("OSUnknown", Target::OS::OSUnknown)
        .value("Linux", Target::OS::Linux)
        .value("Windows", Target::OS::Windows)
        .value("OSX", Target::OS::OSX)
        .value("Android", Target::OS::Android)
        .value("IOS", Target::OS::IOS)
        .export_values();

    p::enum_<Target::Arch>("TargetArch",
                           "The architecture used by the target. Determines the "
                           "instruction set to use.")
        .value("ArchUnknown", Target::Arch::ArchUnknown)
        .value("X86", Target::Arch::X86)
        .value("ARM", Target::Arch::ARM)
        .value("MIPS", Target::Arch::MIPS)
        .value("POWERPC", Target::Arch::POWERPC)
        .export_values();

    p::enum_<Target::Feature>("TargetFeature",
                              "Optional features a target can have.")
        .value("JIT", Target::Feature::JIT)
        .value("Debug", Target::Feature::Debug)
        .value("NoAsserts", Target::Feature::NoAsserts)
        .value("NoBoundsQuery", Target::Feature::NoBoundsQuery)
        .value("Profile", Target::Feature::Profile)

        .value("SSE41", Target::Feature::SSE41)
        .value("AVX", Target::Feature::AVX)
        .value("AVX2", Target::Feature::AVX2)
        .value("FMA", Target::Feature::FMA)
        .value("FMA4", Target::Feature::FMA4)
        .value("F16C", Target::Feature::F16C)

        .value("ARMv7s", Target::Feature::ARMv7s)
        .value("NoNEON", Target::Feature::NoNEON)

        .value("VSX", Target::Feature::VSX)
        .value("POWER_ARCH_2_07", Target::Feature::POWER_ARCH_2_07)

        .value("CUDA", Target::Feature::CUDA)
        .value("CUDACapability30", Target::Feature::CUDACapability30)
        .value("CUDACapability32", Target::Feature::CUDACapability32)
        .value("CUDACapability35", Target::Feature::CUDACapability35)
        .value("CUDACapability50", Target::Feature::CUDACapability50)

        .value("OpenCL", Target::Feature::OpenCL)
        .value("CLDoubles", Target::Feature::CLDoubles)

        .value("OpenGL", Target::Feature::OpenGL)
        .value("UserContext", Target::Feature::UserContext)
        .value("Matlab", Target::Feature::Matlab)
        .value("Metal", Target::Feature::Metal)
        .value("FeatureEnd", Target::Feature::FeatureEnd)

        .export_values();

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
