#include "PyEnums.h"

namespace Halide {
namespace PythonBindings {

void define_enums(py::module &m) {
    py::enum_<Argument::Kind>(m, "ArgumentKind")
        .value("InputScalar", Argument::Kind::InputScalar)
        .value("InputBuffer", Argument::Kind::InputBuffer)
        .value("OutputBuffer", Argument::Kind::OutputBuffer);

    py::enum_<Internal::ArgInfoKind>(m, "ArgInfoKind")
        .value("Scalar", Internal::ArgInfoKind::Scalar)
        .value("Buffer", Internal::ArgInfoKind::Buffer)
        .value("Function", Internal::ArgInfoKind::Function);

    py::enum_<Internal::ArgInfoDirection>(m, "ArgInfoDirection")
        .value("Input", Internal::ArgInfoDirection::Input)
        .value("Output", Internal::ArgInfoDirection::Output);

    py::enum_<DeviceAPI>(m, "DeviceAPI")
        .value("None", DeviceAPI::None)
        .value("Host", DeviceAPI::Host)
        .value("Default_GPU", DeviceAPI::Default_GPU)
        .value("CUDA", DeviceAPI::CUDA)
        .value("OpenCL", DeviceAPI::OpenCL)
        .value("OpenGLCompute", DeviceAPI::OpenGLCompute)
        .value("Metal", DeviceAPI::Metal)
        .value("Hexagon", DeviceAPI::Hexagon);

    py::enum_<LinkageType>(m, "LinkageType")
        .value("External", LinkageType::External)
        .value("ExternalPlusMetadata", LinkageType::ExternalPlusMetadata)
        .value("ExternalPlusArgv", LinkageType::ExternalPlusArgv)
        .value("Internal", LinkageType::Internal);

    py::enum_<LoopAlignStrategy>(m, "LoopAlignStrategy")
        .value("AlignStart", LoopAlignStrategy::AlignStart)
        .value("AlignEnd", LoopAlignStrategy::AlignEnd)
        .value("NoAlign", LoopAlignStrategy::NoAlign)
        .value("Auto", LoopAlignStrategy::Auto);

    py::enum_<MemoryType>(m, "MemoryType")
        .value("Auto", MemoryType::Auto)
        .value("Heap", MemoryType::Heap)
        .value("Stack", MemoryType::Stack)
        .value("Register", MemoryType::Register)
        .value("GPUShared", MemoryType::GPUShared)
        .value("GPUTexture", MemoryType::GPUTexture)
        .value("LockedCache", MemoryType::LockedCache)
        .value("VTCM", MemoryType::VTCM);

    py::enum_<NameMangling>(m, "NameMangling")
        .value("Default", NameMangling::Default)
        .value("C", NameMangling::C)
        .value("CPlusPlus", NameMangling::CPlusPlus);

    py::enum_<PrefetchBoundStrategy>(m, "PrefetchBoundStrategy")
        .value("Clamp", PrefetchBoundStrategy::Clamp)
        .value("GuardWithIf", PrefetchBoundStrategy::GuardWithIf)
        .value("NonFaulting", PrefetchBoundStrategy::NonFaulting);

    py::enum_<StmtOutputFormat>(m, "StmtOutputFormat")
        .value("Text", StmtOutputFormat::Text)
        .value("HTML", StmtOutputFormat::HTML);

    py::enum_<TailStrategy>(m, "TailStrategy")
        .value("RoundUp", TailStrategy::RoundUp)
        .value("GuardWithIf", TailStrategy::GuardWithIf)
        .value("ShiftInwards", TailStrategy::ShiftInwards)
        .value("Auto", TailStrategy::Auto);

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
        .value("Hexagon", Target::Arch::Hexagon)
        .value("POWERPC", Target::Arch::POWERPC)
        .value("RISCV", Target::Arch::RISCV)
        .value("WebAssembly", Target::Arch::WebAssembly);

    // Please keep sorted.
    py::enum_<Target::Processor>(m, "TargetProcessorTune")
        .value("TuneAMDFam10", Target::Processor::AMDFam10)
        .value("TuneBdVer1", Target::Processor::BdVer1)
        .value("TuneBdVer2", Target::Processor::BdVer2)
        .value("TuneBdVer3", Target::Processor::BdVer3)
        .value("TuneBdVer4", Target::Processor::BdVer4)
        .value("TuneBtVer1", Target::Processor::BtVer1)
        .value("TuneBtVer2", Target::Processor::BtVer2)
        .value("TuneGeneric", Target::Processor::ProcessorGeneric)
        .value("TuneK8", Target::Processor::K8)
        .value("TuneK8_SSE3", Target::Processor::K8_SSE3)
        .value("TuneZnVer1", Target::Processor::ZnVer1)
        .value("TuneZnVer2", Target::Processor::ZnVer2)
        .value("TuneZnVer3", Target::Processor::ZnVer3);

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
        .value("OpenGLCompute", Target::Feature::OpenGLCompute)
        .value("EGL", Target::Feature::EGL)
        .value("UserContext", Target::Feature::UserContext)
        .value("Profile", Target::Feature::Profile)
        .value("NoRuntime", Target::Feature::NoRuntime)
        .value("Metal", Target::Feature::Metal)
        .value("CPlusPlusMangling", Target::Feature::CPlusPlusMangling)
        .value("LargeBuffers", Target::Feature::LargeBuffers)
        .value("HVX", Target::Feature::HVX)
        .value("HVX_128", Target::Feature::HVX_128)
        .value("HVX_v62", Target::Feature::HVX_v62)
        .value("HVX_v65", Target::Feature::HVX_v65)
        .value("HVX_v66", Target::Feature::HVX_v66)
        .value("FuzzFloatStores", Target::Feature::FuzzFloatStores)
        .value("SoftFloatABI", Target::Feature::SoftFloatABI)
        .value("MSAN", Target::Feature::MSAN)
        .value("AVX512", Target::Feature::AVX512)
        .value("AVX512_KNL", Target::Feature::AVX512_KNL)
        .value("AVX512_Skylake", Target::Feature::AVX512_Skylake)
        .value("AVX512_Cannonlake", Target::Feature::AVX512_Cannonlake)
        .value("AVX512_SapphireRapids", Target::Feature::AVX512_SapphireRapids)
        .value("TraceLoads", Target::Feature::TraceLoads)
        .value("TraceStores", Target::Feature::TraceStores)
        .value("TraceRealizations", Target::Feature::TraceRealizations)
        .value("D3D12Compute", Target::Feature::D3D12Compute)
        .value("StrictFloat", Target::Feature::StrictFloat)
        .value("TSAN", Target::Feature::TSAN)
        .value("ASAN", Target::Feature::ASAN)
        .value("CheckUnsafePromises", Target::Feature::CheckUnsafePromises)
        .value("HexagonDma", Target::Feature::HexagonDma)
        .value("EmbedBitcode", Target::Feature::EmbedBitcode)
        .value("EnableLLVMLoopOpt", Target::Feature::EnableLLVMLoopOpt)
        .value("WasmSimd128", Target::Feature::WasmSimd128)
        .value("WasmSignExt", Target::Feature::WasmSignExt)
        .value("WasmSatFloatToInt", Target::Feature::WasmSatFloatToInt)
        .value("WasmThreads", Target::Feature::WasmThreads)
        .value("WasmBulkMemory", Target::Feature::WasmBulkMemory)
        .value("SVE", Target::Feature::SVE)
        .value("SVE2", Target::Feature::SVE2)
        .value("ARMDotProd", Target::Feature::ARMDotProd)
        .value("ARMFp16", Target::Feature::ARMFp16)
        .value("LLVMLargeCodeModel", Target::Feature::LLVMLargeCodeModel)
        .value("RVV", Target::Feature::RVV)
        .value("ARMv81a", Target::Feature::ARMv81a)
        .value("SanitizerCoverage", Target::Feature::SanitizerCoverage)
        .value("ProfileByTimer", Target::Feature::ProfileByTimer)
        .value("SPIRV", Target::Feature::SPIRV)
        .value("Semihosting", Target::Feature::Semihosting)
        .value("FeatureEnd", Target::Feature::FeatureEnd);

    py::enum_<halide_type_code_t>(m, "TypeCode")
        .value("Int", Type::Int)
        .value("UInt", Type::UInt)
        .value("Float", Type::Float)
        .value("Handle", Type::Handle);

    py::enum_<OutputFileType>(m, "OutputFileType")
        .value("assembly", OutputFileType::assembly)
        .value("bitcode", OutputFileType::bitcode)
        .value("c_header", OutputFileType::c_header)
        .value("c_source", OutputFileType::c_source)
        .value("cpp_stub", OutputFileType::cpp_stub)
        .value("featurization", OutputFileType::featurization)
        .value("function_info_header", OutputFileType::function_info_header)
        .value("llvm_assembly", OutputFileType::llvm_assembly)
        .value("object", OutputFileType::object)
        .value("python_extension", OutputFileType::python_extension)
        .value("pytorch_wrapper", OutputFileType::pytorch_wrapper)
        .value("registration", OutputFileType::registration)
        .value("schedule", OutputFileType::schedule)
        .value("static_library", OutputFileType::static_library)
        .value("stmt", OutputFileType::stmt)
        .value("stmt_html", OutputFileType::stmt_html)
        .value("compiler_log", OutputFileType::compiler_log);
}

}  // namespace PythonBindings
}  // namespace Halide
