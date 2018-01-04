#include "PyTarget.h"

namespace Halide {
namespace PythonBindings {

namespace {

// Helper class that registers a converter that can auto-convert
// a python [List] into a std::vector<T>, greatly simplifying
// many API bindings. (Conversions that cannot be done will throw a ValueError.)
// TODO: move into a general helper header; other files will want to use this
template<typename T>
struct PythonListToVectorConverter {
    static void *convertible(PyObject *obj) {
        return PyList_Check(obj) ? obj : nullptr;
    }

    static void construct(PyObject *obj, py::converter::rvalue_from_python_stage1_data* data) {
        // The contents of this method are unspeakably evil, but apparently this is how
        // this sort of thing is done in Boost.Python. *shrug*
        py::list list(py::handle<>(py::borrowed(obj)));

        void* storage = ((py::converter::rvalue_from_python_storage<std::vector<T>>*)data)->storage.bytes;
        auto *v = new (storage) std::vector<T>();

        const size_t c = py::len(list);
        v->reserve(c);
        for (size_t i = 0; i < c; i++) {
            v->push_back(py::extract<T>(list[i]));
        }
        data->convertible = storage;
    }

    PythonListToVectorConverter() {
        py::converter::registry::push_back(
            &convertible,
            &construct,
            py::type_id<std::vector<T>>());
    }
};

std::string target_repr(const Target &t) {
    std::ostringstream o;
    o << "<halide.Target " << t.to_string() << ">";
    return o.str();
}

}  // namespace

void define_target() {
    // Register a converter that auto-converts from a python list to a vector<Feature>
    PythonListToVectorConverter<Target::Feature> converter;

    // Disambiguate some ambigious methods
    int (Target::*natural_vector_size_method)(Type) const = &Target::natural_vector_size;
    bool (Target::*supports_type1_method)(const Type &t) const = &Target::supports_type;
    bool (Target::*supports_type2_method)(const Type &t, DeviceAPI device) const = &Target::supports_type;

    auto target_class =
        py::class_<Target>("Target", py::init<>())
            .def(py::init<std::string>(py::args("self", "name")))
            .def(py::init<Target::OS, Target::Arch, int>(py::args("self", "os", "arch", "bits")))
            .def(py::init<Target::OS, Target::Arch, int, std::vector<Target::Feature>>(py::args("self", "os", "arch", "bits", "features")))

            .def(py::self == py::self)
            .def(py::self != py::self)

            .def_readwrite("os", &Target::os)
            .def_readwrite("arch", &Target::arch)
            .def_readwrite("bits", &Target::bits)

            .def("__repr__", &target_repr, py::arg("self"))
            .def("__str__", &Target::to_string, py::arg("self"))
            .def("to_string", &Target::to_string, py::arg("self"))

            .def("has_feature", &Target::has_feature, py::arg("self"))
            .def("features_any_of", &Target::features_any_of, (py::arg("self"), py::arg("features")))
            .def("features_all_of", &Target::features_all_of, (py::arg("self"), py::arg("features")))

            .def("set_feature", &Target::set_feature, (py::arg("self"), py::arg("f"), py::arg("value") = true))
            .def("set_features", &Target::set_features, (py::arg("self"), py::arg("features"), py::arg("value") = true))
            .def("with_feature", &Target::with_feature, (py::arg("self"), py::arg("f")))
            .def("without_feature", &Target::without_feature, (py::arg("self"), py::arg("f")))
            .def("has_gpu_feature", &Target::has_gpu_feature, py::arg("self"))
            .def("supports_type", supports_type1_method, (py::arg("self"), py::arg("type")))
            .def("supports_type", supports_type2_method, (py::arg("self"), py::arg("type"), py::arg("device")))
            .def("supports_device_api", &Target::supports_device_api, (py::arg("self"), py::arg("device")))
            .def("natural_vector_size", natural_vector_size_method, (py::arg("self"), py::arg("type")))
            .def("has_large_buffers", &Target::has_large_buffers, py::arg("self"))
            .def("maximum_buffer_size", &Target::maximum_buffer_size, py::arg("self"))
            .def("supported", &Target::supported, py::arg("self"))
        ;

    py::enum_<Target::OS>("TargetOS")
        .value("OSUnknown", Target::OS::OSUnknown)
        .value("Linux", Target::OS::Linux)
        .value("Windows", Target::OS::Windows)
        .value("OSX", Target::OS::OSX)
        .value("Android", Target::OS::Android)
        .value("IOS", Target::OS::IOS)
        .value("QuRT", Target::OS::QuRT)
        .value("NoOS", Target::OS::NoOS);

    py::enum_<Target::Arch>("TargetArch")
        .value("ArchUnknown", Target::Arch::ArchUnknown)
        .value("X86", Target::Arch::X86)
        .value("ARM", Target::Arch::ARM)
        .value("MIPS", Target::Arch::MIPS)
        .value("Hexagon", Target::Arch::Hexagon)
        .value("POWERPC", Target::Arch::POWERPC);

    py::enum_<Target::Feature>("TargetFeature")
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

    py::def("validate_target_string", &Target::validate_target_string);

    py::def("get_host_target", &get_host_target);
    py::def("get_target_from_environment", &get_target_from_environment);
    py::def("get_jit_target_from_environment", &get_jit_target_from_environment);
    py::def("target_feature_for_device_api", &target_feature_for_device_api);
}

}  // namespace PythonBindings
}  // namespace Halide
