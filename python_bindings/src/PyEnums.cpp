#include "PyEnums.h"

namespace Halide {
namespace PythonBindings {

void define_enums(py::module &m) {
    py::enum_<Argument::Kind>(m, "ArgumentKind")
        .value("InputScalar", Argument::Kind::InputScalar)
        .value("InputBuffer", Argument::Kind::InputBuffer)
        .value("OutputBuffer", Argument::Kind::OutputBuffer);

    py::enum_<DeviceAPI>(m, "DeviceAPI")
        .value("None", DeviceAPI::None)
        .value("Host", DeviceAPI::Host)
        .value("Default_GPU", DeviceAPI::Default_GPU)
        .value("CUDA", DeviceAPI::CUDA)
        .value("OpenCL", DeviceAPI::OpenCL)
        .value("GLSL", DeviceAPI::GLSL)
        .value("OpenGLCompute", DeviceAPI::OpenGLCompute)
        .value("Metal", DeviceAPI::Metal)
        .value("Hexagon", DeviceAPI::Hexagon);

    py::enum_<LinkageType>(m, "LinkageType")
        .value("External", LinkageType::External)
        .value("ExternalPlusMetadata", LinkageType::ExternalPlusMetadata)
        .value("Internal", LinkageType::Internal)
    ;

    py::enum_<LoopAlignStrategy>(m, "LoopAlignStrategy")
        .value("AlignStart", LoopAlignStrategy::AlignStart)
        .value("AlignEnd", LoopAlignStrategy::AlignEnd)
        .value("NoAlign", LoopAlignStrategy::NoAlign)
        .value("Auto", LoopAlignStrategy::Auto)
    ;

    py::enum_<MemoryType>(m, "MemoryType")
        .value("Auto", MemoryType::Auto)
        .value("Heap", MemoryType::Heap)
        .value("Stack", MemoryType::Stack)
        .value("Register", MemoryType::Register)
        .value("GPUShared", MemoryType::GPUShared)
    ;

    py::enum_<NameMangling>(m, "NameMangling")
        .value("Default", NameMangling::Default)
        .value("C", NameMangling::C)
        .value("CPlusPlus", NameMangling::CPlusPlus)
    ;

    py::enum_<PrefetchBoundStrategy>(m, "PrefetchBoundStrategy")
        .value("Clamp", PrefetchBoundStrategy::Clamp)
        .value("GuardWithIf", PrefetchBoundStrategy::GuardWithIf)
        .value("NonFaulting", PrefetchBoundStrategy::NonFaulting)
    ;

    py::enum_<StmtOutputFormat>(m, "StmtOutputFormat")
        .value("Text", StmtOutputFormat::Text)
        .value("HTML", StmtOutputFormat::HTML);

    py::enum_<TailStrategy>(m, "TailStrategy")
        .value("RoundUp", TailStrategy::RoundUp)
        .value("GuardWithIf", TailStrategy::GuardWithIf)
        .value("ShiftInwards", TailStrategy::ShiftInwards)
        .value("Auto", TailStrategy::Auto)
    ;

    py::enum_<Target::OS>(m, "TargetOS")
        .value("OSUnknown", Target::OS::OSUnknown)
        .value("Linux", Target::OS::Linux)
        .value("Windows", Target::OS::Windows)
        .value("OSX", Target::OS::OSX)
        .value("Android", Target::OS::Android)
        .value("IOS", Target::OS::IOS)
        .value("QuRT", Target::OS::QuRT)
        .value("NoOS", Target::OS::NoOS)
        .value("wasmrt", Target::OS::WebAssemblyRuntime);

    py::enum_<Target::Arch>(m, "TargetArch")
        .value("ArchUnknown", Target::Arch::ArchUnknown)
        .value("X86", Target::Arch::X86)
        .value("ARM", Target::Arch::ARM)
        .value("MIPS", Target::Arch::MIPS)
        .value("Hexagon", Target::Arch::Hexagon)
        .value("POWERPC", Target::Arch::POWERPC)
        .value("WebAssembly", Target::Arch::WebAssembly);

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
        .value("CLAtomics64", Target::Feature::CLAtomics64)
        .value("OpenGL", Target::Feature::OpenGL)
        .value("OpenGLCompute", Target::Feature::OpenGLCompute)
        .value("EGL", Target::Feature::EGL)
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
        .value("D3D12Compute", Target::Feature::D3D12Compute)
        .value("StrictFloat", Target::Feature::StrictFloat)
        .value("LegacyBufferWrappers", Target::Feature::LegacyBufferWrappers)
        .value("TSAN", Target::Feature::TSAN)
        .value("ASAN", Target::Feature::ASAN)
        .value("CheckUnsafePromises", Target::Feature::CheckUnsafePromises)
        .value("HexagonDma", Target::Feature::HexagonDma)
        .value("EmbedBitcode", Target::Feature::EmbedBitcode)
        .value("EnableLLVMLoopOpt", Target::Feature::EnableLLVMLoopOpt)
        .value("DisableLLVMLoopOpt", Target::Feature::DisableLLVMLoopOpt)
        .value("WasmSimd128", Target::Feature::WasmSimd128)
        .value("WasmSignExt", Target::Feature::WasmSignExt)
        .value("SVE", Target::Feature::SVE)
        .value("SVE2", Target::Feature::SVE2)
        .value("FeatureEnd", Target::Feature::FeatureEnd);

    py::enum_<halide_type_code_t>(m, "TypeCode")
        .value("Int", Type::Int)
        .value("UInt", Type::UInt)
        .value("Float", Type::Float)
        .value("Handle", Type::Handle);

    py::enum_<Output>(m, "Output")
        .value("assembly", Output::assembly)
        .value("bitcode", Output::bitcode)
        .value("c_header", Output::c_header)
        .value("c_source", Output::c_source)
        .value("cpp_stub", Output::cpp_stub)
        .value("featurization", Output::featurization)
        .value("llvm_assembly", Output::llvm_assembly)
        .value("object", Output::object)
        .value("python_extension", Output::python_extension)
        .value("pytorch_wrapper", Output::pytorch_wrapper)
        .value("registration", Output::registration)
        .value("schedule", Output::schedule)
        .value("static_library", Output::static_library)
        .value("stmt", Output::stmt)
        .value("stmt_html", Output::stmt_html);
}

}  // namespace PythonBindings
}  // namespace Halide
