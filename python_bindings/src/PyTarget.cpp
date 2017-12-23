#include "PyTarget.h"

namespace Halide {
namespace PythonBindings {

namespace {

std::string target_repr(const Target &t) {
    std::ostringstream o;
    o << "<halide.Target " << t.to_string() << ">";
    return o.str();
}

}  // namespace

void define_target(py::module &m) {
    // Disambiguate some ambigious methods
    int (Target::*natural_vector_size_method)(Type) const = &Target::natural_vector_size;
    bool (Target::*supports_type1_method)(const Type &t) const = &Target::supports_type;
    bool (Target::*supports_type2_method)(const Type &t, DeviceAPI device) const = &Target::supports_type;

    auto target_class =
        py::class_<Target>(m, "Target")
            .def(py::init<>())
            .def(py::init<const std::string &>())
            .def(py::init<Target::OS, Target::Arch, int>())
            .def(py::init<Target::OS, Target::Arch, int, std::vector<Target::Feature>>())

            .def("__eq__", [](const Target &value, Target *value2) { return value2 && value == *value2; })
            .def("__ne__", [](const Target &value, Target *value2) { return !value2 || value != *value2; })

            .def_readwrite("os", &Target::os)
            .def_readwrite("arch", &Target::arch)
            .def_readwrite("bits", &Target::bits)

            .def("__repr__", &target_repr)
            .def("__str__", &Target::to_string)
            .def("to_string", &Target::to_string)

            .def("has_feature", &Target::has_feature)
            .def("features_any_of", &Target::features_any_of, py::arg("features"))
            .def("features_all_of", &Target::features_all_of, py::arg("features"))

            .def("set_feature", &Target::set_feature, py::arg("f"), py::arg("value") = true)
            .def("set_features", &Target::set_features, py::arg("features"), py::arg("value") = true)
            .def("with_feature", &Target::with_feature, py::arg("feature"))
            .def("without_feature", &Target::without_feature, py::arg("feature"))
            .def("has_gpu_feature", &Target::has_gpu_feature)
            .def("supports_type", supports_type1_method, py::arg("type"))
            .def("supports_type", supports_type2_method, py::arg("type"), py::arg("device"))
            .def("supports_device_api", &Target::supports_device_api, py::arg("device"))
            .def("natural_vector_size", natural_vector_size_method, py::arg("type"))
            .def("has_large_buffers", &Target::has_large_buffers)
            .def("maximum_buffer_size", &Target::maximum_buffer_size)
            .def("supported", &Target::supported)
            .def_static("validate_target_string", &Target::validate_target_string, py::arg("name"));
        ;

    py::enum_<Target::OS>(m, "TargetOS")
        .value("OSUnknown", Target::OS::OSUnknown)
        .value("Linux", Target::OS::Linux)
        .value("Windows", Target::OS::Windows)
        .value("OSX", Target::OS::OSX)
        .value("Android", Target::OS::Android)
        .value("IOS", Target::OS::IOS)
        .value("QuRT", Target::OS::QuRT)
        .value("NoOS", Target::OS::NoOS);

    py::enum_<Target::Arch>(m, "TargetArch")
        .value("ArchUnknown", Target::Arch::ArchUnknown)
        .value("X86", Target::Arch::X86)
        .value("ARM", Target::Arch::ARM)
        .value("MIPS", Target::Arch::MIPS)
        .value("Hexagon", Target::Arch::Hexagon)
        .value("POWERPC", Target::Arch::POWERPC);

    py::enum_<Target::Feature>(m, "TargetFeature")
        .value("JIT", Target::Feature::JIT)
        .value("Debug", Target::Feature::Debug)
        .value("NoAsserts", Target::Feature::NoAsserts)
        .value("NoBoundsQuery", Target::Feature::NoBoundsQuery)
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
        .value("CUDACapability61", Target::Feature::CUDACapability61)
        .value("OpenCL", Target::Feature::OpenCL)
        .value("CLDoubles", Target::Feature::CLDoubles)
        .value("CLHalf", Target::Feature::CLHalf)
        .value("OpenGL", Target::Feature::OpenGL)
        .value("OpenGLCompute", Target::Feature::OpenGLCompute)
        .value("UserContext", Target::Feature::UserContext)
        .value("Matlab", Target::Feature::Matlab)
        .value("Profile", Target::Feature::Profile)
        .value("NoRuntime", Target::Feature::NoRuntime)
        .value("Metal", Target::Feature::Metal)
        .value("MinGW", Target::Feature::MinGW)
        .value("CPlusPlusMangling", Target::Feature::CPlusPlusMangling)
        .value("LargeBuffers", Target::Feature::LargeBuffers)
        .value("HVX_64", Target::Feature::HVX_64)
        .value("HVX_128", Target::Feature::HVX_128)
        .value("HVX_v62", Target::Feature::HVX_v62)
        .value("HVX_v65", Target::Feature::HVX_v65)
        .value("HVX_v66", Target::Feature::HVX_v66)
        .value("HVX_shared_object", Target::Feature::HVX_shared_object)
        .value("FuzzFloatStores", Target::Feature::FuzzFloatStores)
        .value("SoftFloatABI", Target::Feature::SoftFloatABI)
        .value("MSAN", Target::Feature::MSAN)
        .value("AVX512", Target::Feature::AVX512)
        .value("AVX512_KNL", Target::Feature::AVX512_KNL)
        .value("AVX512_Skylake", Target::Feature::AVX512_Skylake)
        .value("AVX512_Cannonlake", Target::Feature::AVX512_Cannonlake)
        .value("TraceLoads", Target::Feature::TraceLoads)
        .value("TraceStores", Target::Feature::TraceStores)
        .value("TraceRealizations", Target::Feature::TraceRealizations)
        .value("FeatureEnd", Target::Feature::FeatureEnd);

    m.def("get_host_target", &get_host_target);
    m.def("get_target_from_environment", &get_target_from_environment);
    m.def("get_jit_target_from_environment", &get_jit_target_from_environment);
    m.def("target_feature_for_device_api", &target_feature_for_device_api);
}

}  // namespace PythonBindings
}  // namespace Halide
