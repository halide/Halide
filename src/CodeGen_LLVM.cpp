#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>

#include "CPlusPlusMangle.h"
#include "CSE.h"
#include "CodeGen_ARM.h"
#include "CodeGen_GPU_Host.h"
#include "CodeGen_Hexagon.h"
#include "CodeGen_Internal.h"
#include "CodeGen_LLVM.h"
#include "CodeGen_MIPS.h"
#include "CodeGen_PowerPC.h"
#include "CodeGen_RISCV.h"
#include "CodeGen_WebAssembly.h"
#include "CodeGen_X86.h"
#include "CompilerLogger.h"
#include "Debug.h"
#include "Deinterleave.h"
#include "EmulateFloat16Math.h"
#include "ExprUsesVar.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "IntegerDivisionTable.h"
#include "JITModule.h"
#include "LLVM_Headers.h"
#include "LLVM_Runtime_Linker.h"
#include "Lerp.h"
#include "MatlabWrapper.h"
#include "Pipeline.h"
#include "Simplify.h"
#include "Util.h"

#if !(__cplusplus > 199711L || _MSC_VER >= 1800)

// VS2013 isn't fully C++11 compatible, but it supports enough of what Halide
// needs for now to be an acceptable minimum for Windows.
#error "Halide requires C++11 or VS2013+; please upgrade your compiler."

#endif

namespace Halide {

std::unique_ptr<llvm::Module> codegen_llvm(const Module &module, llvm::LLVMContext &context) {
    std::unique_ptr<Internal::CodeGen_LLVM> cg(Internal::CodeGen_LLVM::new_for_target(module.target(), context));
    return cg->compile(module);
}

namespace Internal {

using namespace llvm;
using std::map;
using std::ostringstream;
using std::pair;
using std::string;
using std::vector;

// Define a local empty inline function for each target
// to disable initialization.
#define LLVM_TARGET(target)                    \
    inline void Initialize##target##Target() { \
    }
#include <llvm/Config/Targets.def>
#undef LLVM_TARGET

#define LLVM_ASM_PARSER(target)                   \
    inline void Initialize##target##AsmParser() { \
    }
#include <llvm/Config/AsmParsers.def>
#undef LLVM_ASM_PARSER

#define LLVM_ASM_PRINTER(target)                   \
    inline void Initialize##target##AsmPrinter() { \
    }
#include <llvm/Config/AsmPrinters.def>
#undef LLVM_ASM_PRINTER

#define InitializeTarget(target)          \
    LLVMInitialize##target##Target();     \
    LLVMInitialize##target##TargetInfo(); \
    LLVMInitialize##target##TargetMC();   \
    llvm_##target##_enabled = true;

#define InitializeAsmParser(target) \
    LLVMInitialize##target##AsmParser();

#define InitializeAsmPrinter(target) \
    LLVMInitialize##target##AsmPrinter();

// Override above empty init function with macro for supported targets.
#ifdef WITH_ARM
#define InitializeARMTarget() InitializeTarget(ARM)
#define InitializeARMAsmParser() InitializeAsmParser(ARM)
#define InitializeARMAsmPrinter() InitializeAsmPrinter(ARM)
#endif

#ifdef WITH_PTX
#define InitializeNVPTXTarget() InitializeTarget(NVPTX)
#define InitializeNVPTXAsmParser() InitializeAsmParser(NVPTX)
#define InitializeNVPTXAsmPrinter() InitializeAsmPrinter(NVPTX)
#endif

#ifdef WITH_AMDGPU
#define InitializeAMDGPUTarget() InitializeTarget(AMDGPU)
#define InitializeAMDGPUAsmParser() InitializeAsmParser(AMDGPU)
#define InitializeAMDGPUAsmPrinter() InitializeAsmParser(AMDGPU)
#endif

#ifdef WITH_AARCH64
#define InitializeAArch64Target() InitializeTarget(AArch64)
#define InitializeAArch64AsmParser() InitializeAsmParser(AArch64)
#define InitializeAArch64AsmPrinter() InitializeAsmPrinter(AArch64)
#endif

#ifdef WITH_HEXAGON
#define InitializeHexagonTarget() InitializeTarget(Hexagon)
#define InitializeHexagonAsmParser() InitializeAsmParser(Hexagon)
#define InitializeHexagonAsmPrinter() InitializeAsmPrinter(Hexagon)
#endif

#ifdef WITH_MIPS
#define InitializeMipsTarget() InitializeTarget(Mips)
#define InitializeMipsAsmParser() InitializeAsmParser(Mips)
#define InitializeMipsAsmPrinter() InitializeAsmPrinter(Mips)
#endif

#ifdef WITH_POWERPC
#define InitializePowerPCTarget() InitializeTarget(PowerPC)
#define InitializePowerPCAsmParser() InitializeAsmParser(PowerPC)
#define InitializePowerPCAsmPrinter() InitializeAsmPrinter(PowerPC)
#endif

#ifdef WITH_RISCV
#define InitializeRISCVTarget() InitializeTarget(RISCV)
#define InitializeRISCVAsmParser() InitializeAsmParser(RISCV)
#define InitializeRISCVAsmPrinter() InitializeAsmPrinter(RISCV)
#endif

#ifdef WITH_X86
#define InitializeX86Target() InitializeTarget(X86)
#define InitializeX86AsmParser() InitializeAsmParser(X86)
#define InitializeX86AsmPrinter() InitializeAsmPrinter(X86)
#endif

#ifdef WITH_WEBASSEMBLY
#define InitializeWebAssemblyTarget() InitializeTarget(WebAssembly)
#define InitializeWebAssemblyAsmParser() InitializeAsmParser(WebAssembly)
#define InitializeWebAssemblyAsmPrinter() InitializeAsmPrinter(WebAssembly)
#endif

namespace {

// Get the LLVM linkage corresponding to a Halide linkage type.
llvm::GlobalValue::LinkageTypes llvm_linkage(LinkageType t) {
    // TODO(dsharlet): For some reason, marking internal functions as
    // private linkage on OSX is causing some of the static tests to
    // fail. Figure out why so we can remove this.
    return llvm::GlobalValue::ExternalLinkage;

    // switch (t) {
    // case LinkageType::ExternalPlusMetadata:
    // case LinkageType::External:
    //     return llvm::GlobalValue::ExternalLinkage;
    // default:
    //     return llvm::GlobalValue::PrivateLinkage;
    // }
}

// A local helper to make an llvm value type representing
// alignment. Can't be declared in a header without introducing a
// dependence on the LLVM headers.
#if LLVM_VERSION >= 100
llvm::Align make_alignment(int a) {
    return llvm::Align(a);
}
#else
int make_alignment(int a) {
    return a;
}
#endif

}  // namespace

CodeGen_LLVM::CodeGen_LLVM(Target t)
    : function(nullptr), context(nullptr),
      builder(nullptr),
      value(nullptr),
      very_likely_branch(nullptr),
      default_fp_math_md(nullptr),
      strict_fp_math_md(nullptr),
      target(t),
      void_t(nullptr), i1_t(nullptr), i8_t(nullptr),
      i16_t(nullptr), i32_t(nullptr), i64_t(nullptr),
      f16_t(nullptr), f32_t(nullptr), f64_t(nullptr),
      halide_buffer_t_type(nullptr),
      metadata_t_type(nullptr),
      argument_t_type(nullptr),
      scalar_value_t_type(nullptr),
      device_interface_t_type(nullptr),
      pseudostack_slot_t_type(nullptr),

      // Vector types. These need an LLVMContext before they can be initialized.
      i8x8(nullptr),
      i8x16(nullptr),
      i8x32(nullptr),
      i16x4(nullptr),
      i16x8(nullptr),
      i16x16(nullptr),
      i32x2(nullptr),
      i32x4(nullptr),
      i32x8(nullptr),
      i64x2(nullptr),
      i64x4(nullptr),
      f32x2(nullptr),
      f32x4(nullptr),
      f32x8(nullptr),
      f64x2(nullptr),
      f64x4(nullptr),

      // Wildcards for pattern matching
      wild_i8x8(Variable::make(Int(8, 8), "*")),
      wild_i16x4(Variable::make(Int(16, 4), "*")),
      wild_i32x2(Variable::make(Int(32, 2), "*")),

      wild_u8x8(Variable::make(UInt(8, 8), "*")),
      wild_u16x4(Variable::make(UInt(16, 4), "*")),
      wild_u32x2(Variable::make(UInt(32, 2), "*")),

      wild_i8x16(Variable::make(Int(8, 16), "*")),
      wild_i16x8(Variable::make(Int(16, 8), "*")),
      wild_i32x4(Variable::make(Int(32, 4), "*")),
      wild_i64x2(Variable::make(Int(64, 2), "*")),

      wild_u8x16(Variable::make(UInt(8, 16), "*")),
      wild_u16x8(Variable::make(UInt(16, 8), "*")),
      wild_u32x4(Variable::make(UInt(32, 4), "*")),
      wild_u64x2(Variable::make(UInt(64, 2), "*")),

      wild_i8x32(Variable::make(Int(8, 32), "*")),
      wild_i16x16(Variable::make(Int(16, 16), "*")),
      wild_i32x8(Variable::make(Int(32, 8), "*")),
      wild_i64x4(Variable::make(Int(64, 4), "*")),

      wild_u8x32(Variable::make(UInt(8, 32), "*")),
      wild_u16x16(Variable::make(UInt(16, 16), "*")),
      wild_u32x8(Variable::make(UInt(32, 8), "*")),
      wild_u64x4(Variable::make(UInt(64, 4), "*")),

      wild_f32x2(Variable::make(Float(32, 2), "*")),

      wild_f32x4(Variable::make(Float(32, 4), "*")),
      wild_f64x2(Variable::make(Float(64, 2), "*")),

      wild_f32x8(Variable::make(Float(32, 8), "*")),
      wild_f64x4(Variable::make(Float(64, 4), "*")),

      wild_u1x_(Variable::make(UInt(1, 0), "*")),
      wild_i8x_(Variable::make(Int(8, 0), "*")),
      wild_u8x_(Variable::make(UInt(8, 0), "*")),
      wild_i16x_(Variable::make(Int(16, 0), "*")),
      wild_u16x_(Variable::make(UInt(16, 0), "*")),
      wild_i32x_(Variable::make(Int(32, 0), "*")),
      wild_u32x_(Variable::make(UInt(32, 0), "*")),
      wild_i64x_(Variable::make(Int(64, 0), "*")),
      wild_u64x_(Variable::make(UInt(64, 0), "*")),
      wild_f32x_(Variable::make(Float(32, 0), "*")),
      wild_f64x_(Variable::make(Float(64, 0), "*")),

      // Bounds of types
      min_i8(Int(8).min()),
      max_i8(Int(8).max()),
      max_u8(UInt(8).max()),

      min_i16(Int(16).min()),
      max_i16(Int(16).max()),
      max_u16(UInt(16).max()),

      min_i32(Int(32).min()),
      max_i32(Int(32).max()),
      max_u32(UInt(32).max()),

      min_i64(Int(64).min()),
      max_i64(Int(64).max()),
      max_u64(UInt(64).max()),

      min_f32(Float(32).min()),
      max_f32(Float(32).max()),

      min_f64(Float(64).min()),
      max_f64(Float(64).max()),

      inside_atomic_mutex_node(false),
      emit_atomic_stores(false),

      destructor_block(nullptr),
      strict_float(t.has_feature(Target::StrictFloat)) {
    initialize_llvm();
}

namespace {

template<typename T>
CodeGen_LLVM *make_codegen(const Target &target,
                           llvm::LLVMContext &context) {
    CodeGen_LLVM *ret = new T(target);
    ret->set_context(context);
    return ret;
}

}  // namespace

void CodeGen_LLVM::set_context(llvm::LLVMContext &context) {
    this->context = &context;
}

CodeGen_LLVM *CodeGen_LLVM::new_for_target(const Target &target,
                                           llvm::LLVMContext &context) {
    // The awkward mapping from targets to code generators
    if (target.features_any_of({Target::CUDA,
                                Target::OpenCL,
                                Target::OpenGL,
                                Target::OpenGLCompute,
                                Target::Metal,
                                Target::D3D12Compute})) {
#ifdef WITH_X86
        if (target.arch == Target::X86) {
            return make_codegen<CodeGen_GPU_Host<CodeGen_X86>>(target, context);
        }
#endif
#if defined(WITH_ARM) || defined(WITH_AARCH64)
        if (target.arch == Target::ARM) {
            return make_codegen<CodeGen_GPU_Host<CodeGen_ARM>>(target, context);
        }
#endif
#ifdef WITH_MIPS
        if (target.arch == Target::MIPS) {
            return make_codegen<CodeGen_GPU_Host<CodeGen_MIPS>>(target, context);
        }
#endif
#ifdef WITH_POWERPC
        if (target.arch == Target::POWERPC) {
            return make_codegen<CodeGen_GPU_Host<CodeGen_PowerPC>>(target, context);
        }
#endif
#ifdef WITH_WEBASSEMBLY
        if (target.arch == Target::WebAssembly) {
            return make_codegen<CodeGen_GPU_Host<CodeGen_WebAssembly>>(target, context);
        }
#endif
#ifdef WITH_RISCV
        if (target.arch == Target::RISCV) {
            return make_codegen<CodeGen_GPU_Host<CodeGen_RISCV>>(target, context);
        }
#endif
        user_error << "Invalid target architecture for GPU backend: "
                   << target.to_string() << "\n";
        return nullptr;

    } else if (target.arch == Target::X86) {
        return make_codegen<CodeGen_X86>(target, context);
    } else if (target.arch == Target::ARM) {
        return make_codegen<CodeGen_ARM>(target, context);
    } else if (target.arch == Target::MIPS) {
        return make_codegen<CodeGen_MIPS>(target, context);
    } else if (target.arch == Target::POWERPC) {
        return make_codegen<CodeGen_PowerPC>(target, context);
    } else if (target.arch == Target::Hexagon) {
        return make_codegen<CodeGen_Hexagon>(target, context);
    } else if (target.arch == Target::WebAssembly) {
        return make_codegen<CodeGen_WebAssembly>(target, context);
    } else if (target.arch == Target::RISCV) {
        return make_codegen<CodeGen_RISCV>(target, context);
    }

    user_error << "Unknown target architecture: "
               << target.to_string() << "\n";
    return nullptr;
}

void CodeGen_LLVM::initialize_llvm() {
    static std::once_flag init_llvm_once;
    std::call_once(init_llvm_once, []() {
        // You can hack in command-line args to llvm with the
        // environment variable HL_LLVM_ARGS, e.g. HL_LLVM_ARGS="-print-after-all"
        std::string args = get_env_variable("HL_LLVM_ARGS");
        if (!args.empty()) {
            vector<std::string> arg_vec = split_string(args, " ");
            vector<const char *> c_arg_vec;
            c_arg_vec.push_back("llc");
            for (const std::string &s : arg_vec) {
                c_arg_vec.push_back(s.c_str());
            }
            cl::ParseCommandLineOptions((int)(c_arg_vec.size()), &c_arg_vec[0], "Halide compiler\n");
        }

        InitializeNativeTarget();
        InitializeNativeTargetAsmPrinter();
        InitializeNativeTargetAsmParser();

#define LLVM_TARGET(target) \
    Initialize##target##Target();
#include <llvm/Config/Targets.def>
#undef LLVM_TARGET

#define LLVM_ASM_PARSER(target) \
    Initialize##target##AsmParser();
#include <llvm/Config/AsmParsers.def>
#undef LLVM_ASM_PARSER

#define LLVM_ASM_PRINTER(target) \
    Initialize##target##AsmPrinter();
#include <llvm/Config/AsmPrinters.def>
#include <utility>
#undef LLVM_ASM_PRINTER
    });
}

void CodeGen_LLVM::init_context() {
    // Ensure our IRBuilder is using the current context.
    delete builder;
    builder = new IRBuilder<>(*context);

    // Branch weights for very likely branches
    llvm::MDBuilder md_builder(*context);
    very_likely_branch = md_builder.createBranchWeights(1 << 30, 0);
    default_fp_math_md = md_builder.createFPMath(0.0);
    strict_fp_math_md = md_builder.createFPMath(0.0);
    builder->setDefaultFPMathTag(default_fp_math_md);
    llvm::FastMathFlags fast_flags;
    fast_flags.setNoNaNs();
    fast_flags.setNoInfs();
    fast_flags.setNoSignedZeros();
    // Don't use approximate reciprocals for division. It's too inaccurate even for Halide.
    // fast_flags.setAllowReciprocal();
    // Theoretically, setAllowReassoc could be setUnsafeAlgebra for earlier versions, but that
    // turns on all the flags.
    fast_flags.setAllowReassoc();
    fast_flags.setAllowContract(true);
    fast_flags.setApproxFunc();
    builder->setFastMathFlags(fast_flags);

    // Define some types
    void_t = llvm::Type::getVoidTy(*context);
    i1_t = llvm::Type::getInt1Ty(*context);
    i8_t = llvm::Type::getInt8Ty(*context);
    i16_t = llvm::Type::getInt16Ty(*context);
    i32_t = llvm::Type::getInt32Ty(*context);
    i64_t = llvm::Type::getInt64Ty(*context);
    f16_t = llvm::Type::getHalfTy(*context);
    f32_t = llvm::Type::getFloatTy(*context);
    f64_t = llvm::Type::getDoubleTy(*context);

    i8x8 = VectorType::get(i8_t, 8);
    i8x16 = VectorType::get(i8_t, 16);
    i8x32 = VectorType::get(i8_t, 32);
    i16x4 = VectorType::get(i16_t, 4);
    i16x8 = VectorType::get(i16_t, 8);
    i16x16 = VectorType::get(i16_t, 16);
    i32x2 = VectorType::get(i32_t, 2);
    i32x4 = VectorType::get(i32_t, 4);
    i32x8 = VectorType::get(i32_t, 8);
    i64x2 = VectorType::get(i64_t, 2);
    i64x4 = VectorType::get(i64_t, 4);
    f32x2 = VectorType::get(f32_t, 2);
    f32x4 = VectorType::get(f32_t, 4);
    f32x8 = VectorType::get(f32_t, 8);
    f64x2 = VectorType::get(f64_t, 2);
    f64x4 = VectorType::get(f64_t, 4);
}

void CodeGen_LLVM::init_module() {
    init_context();

    // Start with a module containing the initial module for this target.
    module = get_initial_module_for_target(target, context);
}

void CodeGen_LLVM::add_external_code(const Module &halide_module) {
    for (const ExternalCode &code_blob : halide_module.external_code()) {
        if (code_blob.is_for_cpu_target(get_target())) {
            add_bitcode_to_module(context, *module, code_blob.contents(), code_blob.name());
        }
    }
}

CodeGen_LLVM::~CodeGen_LLVM() {
    delete builder;
}

bool CodeGen_LLVM::llvm_X86_enabled = false;
bool CodeGen_LLVM::llvm_ARM_enabled = false;
bool CodeGen_LLVM::llvm_Hexagon_enabled = false;
bool CodeGen_LLVM::llvm_AArch64_enabled = false;
bool CodeGen_LLVM::llvm_NVPTX_enabled = false;
bool CodeGen_LLVM::llvm_Mips_enabled = false;
bool CodeGen_LLVM::llvm_PowerPC_enabled = false;
bool CodeGen_LLVM::llvm_AMDGPU_enabled = false;
bool CodeGen_LLVM::llvm_WebAssembly_enabled = false;
bool CodeGen_LLVM::llvm_RISCV_enabled = false;

namespace {

struct MangledNames {
    string simple_name;
    string extern_name;
    string argv_name;
    string metadata_name;
};

MangledNames get_mangled_names(const std::string &name,
                               LinkageType linkage,
                               NameMangling mangling,
                               const std::vector<LoweredArgument> &args,
                               const Target &target) {
    std::vector<std::string> namespaces;
    MangledNames names;
    names.simple_name = extract_namespaces(name, namespaces);
    names.extern_name = names.simple_name;
    names.argv_name = names.simple_name + "_argv";
    names.metadata_name = names.simple_name + "_metadata";

    if (linkage != LinkageType::Internal &&
        ((mangling == NameMangling::Default &&
          target.has_feature(Target::CPlusPlusMangling)) ||
         mangling == NameMangling::CPlusPlus)) {
        std::vector<ExternFuncArgument> mangle_args;
        for (const auto &arg : args) {
            if (arg.kind == Argument::InputScalar) {
                mangle_args.emplace_back(make_zero(arg.type));
            } else if (arg.kind == Argument::InputBuffer ||
                       arg.kind == Argument::OutputBuffer) {
                mangle_args.emplace_back(Buffer<>());
            }
        }
        names.extern_name = cplusplus_function_mangled_name(names.simple_name, namespaces, type_of<int>(), mangle_args, target);
        halide_handle_cplusplus_type inner_type(halide_cplusplus_type_name(halide_cplusplus_type_name::Simple, "void"), {}, {},
                                                {halide_handle_cplusplus_type::Pointer, halide_handle_cplusplus_type::Pointer});
        Type void_star_star(Handle(1, &inner_type));
        names.argv_name = cplusplus_function_mangled_name(names.argv_name, namespaces, type_of<int>(), {ExternFuncArgument(make_zero(void_star_star))}, target);
        names.metadata_name = cplusplus_function_mangled_name(names.metadata_name, namespaces, type_of<const struct halide_filter_metadata_t *>(), {}, target);
    }
    return names;
}

MangledNames get_mangled_names(const LoweredFunc &f, const Target &target) {
    return get_mangled_names(f.name, f.linkage, f.name_mangling, f.args, target);
}

}  // namespace

llvm::FunctionType *CodeGen_LLVM::signature_to_type(const ExternSignature &signature) {
    internal_assert(void_t != nullptr && halide_buffer_t_type != nullptr);
    llvm::Type *ret_type =
        signature.is_void_return() ? void_t : llvm_type_of(upgrade_type_for_argument_passing(signature.ret_type()));
    std::vector<llvm::Type *> llvm_arg_types;
    for (const Type &t : signature.arg_types()) {
        if (t == type_of<struct halide_buffer_t *>()) {
            llvm_arg_types.push_back(halide_buffer_t_type->getPointerTo());
        } else {
            llvm_arg_types.push_back(llvm_type_of(upgrade_type_for_argument_passing(t)));
        }
    }

    return llvm::FunctionType::get(ret_type, llvm_arg_types, false);
}

/*static*/
std::unique_ptr<llvm::Module> CodeGen_LLVM::compile_trampolines(
    const Target &target,
    llvm::LLVMContext &context,
    const std::string &suffix,
    const std::vector<std::pair<std::string, ExternSignature>> &externs) {
    std::unique_ptr<CodeGen_LLVM> codegen(new_for_target(target, context));
    codegen->init_codegen("trampolines" + suffix);
    for (const std::pair<std::string, ExternSignature> &e : externs) {
        const std::string &callee_name = e.first;
        const std::string wrapper_name = callee_name + suffix;
        llvm::FunctionType *fn_type = codegen->signature_to_type(e.second);
        llvm::Function *callee = llvm::Function::Create(fn_type,
                                                        llvm::Function::ExternalLinkage, callee_name, codegen->module.get());
        codegen->add_argv_wrapper(callee, wrapper_name, /*result_in_argv*/ true);
    }
    return codegen->finish_codegen();
}

void CodeGen_LLVM::init_codegen(const std::string &name, bool any_strict_float) {
    init_module();

    internal_assert(module && context);

    debug(1) << "Target triple of initial module: " << module->getTargetTriple() << "\n";

    module->setModuleIdentifier(name);

    // Add some target specific info to the module as metadata.
    module->addModuleFlag(llvm::Module::Warning, "halide_use_soft_float_abi", use_soft_float_abi() ? 1 : 0);
    module->addModuleFlag(llvm::Module::Warning, "halide_mcpu", MDString::get(*context, mcpu()));
    module->addModuleFlag(llvm::Module::Warning, "halide_mattrs", MDString::get(*context, mattrs()));
    module->addModuleFlag(llvm::Module::Warning, "halide_use_pic", use_pic() ? 1 : 0);
    module->addModuleFlag(llvm::Module::Warning, "halide_per_instruction_fast_math_flags", any_strict_float);

    // Ensure some types we need are defined
    halide_buffer_t_type = module->getTypeByName("struct.halide_buffer_t");
    internal_assert(halide_buffer_t_type) << "Did not find halide_buffer_t in initial module";

    type_t_type = module->getTypeByName("struct.halide_type_t");
    internal_assert(type_t_type) << "Did not find halide_type_t in initial module";

    dimension_t_type = module->getTypeByName("struct.halide_dimension_t");
    internal_assert(dimension_t_type) << "Did not find halide_dimension_t in initial module";

    metadata_t_type = module->getTypeByName("struct.halide_filter_metadata_t");
    internal_assert(metadata_t_type) << "Did not find halide_filter_metadata_t in initial module";

    argument_t_type = module->getTypeByName("struct.halide_filter_argument_t");
    internal_assert(argument_t_type) << "Did not find halide_filter_argument_t in initial module";

    scalar_value_t_type = module->getTypeByName("struct.halide_scalar_value_t");
    internal_assert(scalar_value_t_type) << "Did not find halide_scalar_value_t in initial module";

    device_interface_t_type = module->getTypeByName("struct.halide_device_interface_t");
    internal_assert(device_interface_t_type) << "Did not find halide_device_interface_t in initial module";

    pseudostack_slot_t_type = module->getTypeByName("struct.halide_pseudostack_slot_t");
    internal_assert(pseudostack_slot_t_type) << "Did not find halide_pseudostack_slot_t in initial module";

    semaphore_t_type = module->getTypeByName("struct.halide_semaphore_t");
    internal_assert(semaphore_t_type) << "Did not find halide_semaphore_t in initial module";

    semaphore_acquire_t_type = module->getTypeByName("struct.halide_semaphore_acquire_t");
    internal_assert(semaphore_acquire_t_type) << "Did not find halide_semaphore_acquire_t in initial module";

    parallel_task_t_type = module->getTypeByName("struct.halide_parallel_task_t");
    internal_assert(parallel_task_t_type) << "Did not find halide_parallel_task_t in initial module";
}

std::unique_ptr<llvm::Module> CodeGen_LLVM::compile(const Module &input) {
    init_codegen(input.name(), input.any_strict_float());

    internal_assert(module && context && builder)
        << "The CodeGen_LLVM subclass should have made an initial module before calling CodeGen_LLVM::compile\n";

    add_external_code(input);

    // Generate the code for this module.
    debug(1) << "Generating llvm bitcode...\n";
    for (const auto &b : input.buffers()) {
        compile_buffer(b);
    }
    for (const auto &f : input.functions()) {
        const auto names = get_mangled_names(f, get_target());

        compile_func(f, names.simple_name, names.extern_name);

        // If the Func is externally visible, also create the argv wrapper and metadata.
        // (useful for calling from JIT and other machine interfaces).
        if (f.linkage == LinkageType::ExternalPlusMetadata) {
            llvm::Function *wrapper = add_argv_wrapper(function, names.argv_name);
            llvm::Function *metadata_getter = embed_metadata_getter(names.metadata_name,
                                                                    names.simple_name, f.args, input.get_metadata_name_map());

            if (target.has_feature(Target::Matlab)) {
                define_matlab_wrapper(module.get(), wrapper, metadata_getter);
            }
        }
    }

    debug(2) << module.get() << "\n";

    return finish_codegen();
}

std::unique_ptr<llvm::Module> CodeGen_LLVM::finish_codegen() {
    // Verify the module is ok
    internal_assert(!verifyModule(*module, &llvm::errs()));
    debug(2) << "Done generating llvm bitcode\n";

    // Optimize
    CodeGen_LLVM::optimize_module();

    if (target.has_feature(Target::EmbedBitcode)) {
        std::string halide_command = "halide target=" + target.to_string();
        embed_bitcode(module.get(), halide_command);
    }

    // Disown the module and return it.
    return std::move(module);
}

void CodeGen_LLVM::begin_func(LinkageType linkage, const std::string &name,
                              const std::string &extern_name, const std::vector<LoweredArgument> &args) {
    current_function_args = args;

    // Deduce the types of the arguments to our function
    vector<llvm::Type *> arg_types(args.size());
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer()) {
            arg_types[i] = halide_buffer_t_type->getPointerTo();
        } else {
            arg_types[i] = llvm_type_of(upgrade_type_for_argument_passing(args[i].type));
        }
    }
    FunctionType *func_t = FunctionType::get(i32_t, arg_types, false);

    // Make our function. There may already be a declaration of it.
    function = module->getFunction(extern_name);
    if (!function) {
        function = llvm::Function::Create(func_t, llvm_linkage(linkage), extern_name, module.get());
    } else {
        user_assert(function->isDeclaration())
            << "Another function with the name " << extern_name
            << " already exists in the same module\n";
        if (func_t != function->getFunctionType()) {
            std::cerr << "Desired function type for " << extern_name << ":\n";
            func_t->print(dbgs(), true);
            std::cerr << "Declared function type of " << extern_name << ":\n";
            function->getFunctionType()->print(dbgs(), true);
            user_error << "Cannot create a function with a declaration of mismatched type.\n";
        }
    }
    set_function_attributes_for_target(function, target);

    // Mark the buffer args as no alias
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer()) {
            function->addParamAttr(i, Attribute::NoAlias);
        }
    }

    debug(1) << "Generating llvm bitcode prolog for function " << name << "...\n";

    // Null out the destructor block.
    destructor_block = nullptr;

    // Make the initial basic block
    BasicBlock *block = BasicBlock::Create(*context, "entry", function);
    builder->SetInsertPoint(block);

    // Put the arguments in the symbol table
    {
        size_t i = 0;
        for (auto &arg : function->args()) {
            if (args[i].is_buffer()) {
                // Track this buffer name so that loads and stores from it
                // don't try to be too aligned.
                external_buffer.insert(args[i].name);
                sym_push(args[i].name + ".buffer", &arg);
            } else {
                Type passed_type = upgrade_type_for_argument_passing(args[i].type);
                if (args[i].type != passed_type) {
                    llvm::Value *a = builder->CreateBitCast(&arg, llvm_type_of(args[i].type));
                    sym_push(args[i].name, a);
                } else {
                    sym_push(args[i].name, &arg);
                }
            }

            i++;
        }
    }
}

void CodeGen_LLVM::end_func(const std::vector<LoweredArgument> &args) {
    return_with_error_code(ConstantInt::get(i32_t, 0));

    // Remove the arguments from the symbol table
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer()) {
            sym_pop(args[i].name + ".buffer");
        } else {
            sym_pop(args[i].name);
        }
    }

    internal_assert(!verifyFunction(*function, &llvm::errs()));

    current_function_args.clear();
}

void CodeGen_LLVM::compile_func(const LoweredFunc &f, const std::string &simple_name,
                                const std::string &extern_name) {
    // Generate the function declaration and argument unpacking code.
    begin_func(f.linkage, simple_name, extern_name, f.args);

    // If building with MSAN, ensure that calls to halide_msan_annotate_buffer_is_initialized()
    // happen for every output buffer if the function succeeds.
    if (f.linkage != LinkageType::Internal &&
        target.has_feature(Target::MSAN)) {
        llvm::Function *annotate_buffer_fn =
            module->getFunction("halide_msan_annotate_buffer_is_initialized_as_destructor");
        internal_assert(annotate_buffer_fn)
            << "Could not find halide_msan_annotate_buffer_is_initialized_as_destructor in module\n";
        annotate_buffer_fn->addParamAttr(0, Attribute::NoAlias);
        for (const auto &arg : f.args) {
            if (arg.kind == Argument::OutputBuffer) {
                register_destructor(annotate_buffer_fn, sym_get(arg.name + ".buffer"), OnSuccess);
            }
        }
    }

    // Generate the function body.
    debug(1) << "Generating llvm bitcode for function " << f.name << "...\n";
    f.body.accept(this);

    // Clean up and return.
    end_func(f.args);
}

// Given a range of iterators of constant ints, get a corresponding vector of llvm::Constant.
template<typename It>
std::vector<llvm::Constant *> get_constants(llvm::Type *t, It begin, It end) {
    std::vector<llvm::Constant *> ret;
    for (It i = begin; i != end; i++) {
        ret.push_back(ConstantInt::get(t, *i));
    }
    return ret;
}

BasicBlock *CodeGen_LLVM::get_destructor_block() {
    if (!destructor_block) {
        // Create it if it doesn't exist.
        IRBuilderBase::InsertPoint here = builder->saveIP();
        destructor_block = BasicBlock::Create(*context, "destructor_block", function);
        builder->SetInsertPoint(destructor_block);
        // The first instruction in the destructor block is a phi node
        // that collects the error code.
        PHINode *error_code = builder->CreatePHI(i32_t, 0);

        // Calls to destructors will get inserted here.

        // The last instruction is the return op that returns it.
        builder->CreateRet(error_code);

        // Jump back to where we were.
        builder->restoreIP(here);
    }
    internal_assert(destructor_block->getParent() == function);
    return destructor_block;
}

Value *CodeGen_LLVM::register_destructor(llvm::Function *destructor_fn, Value *obj, DestructorType when) {

    // Create a null-initialized stack slot to track this object
    llvm::Type *void_ptr = i8_t->getPointerTo();
    llvm::Value *stack_slot = create_alloca_at_entry(void_ptr, 1, true);

    // Cast the object to llvm's representation of void *
    obj = builder->CreatePointerCast(obj, void_ptr);

    // Put it in the stack slot
    builder->CreateStore(obj, stack_slot);

    // Passing the constant null as the object means the destructor
    // will never get called.
    {
        llvm::Constant *c = dyn_cast<llvm::Constant>(obj);
        if (c && c->isNullValue()) {
            internal_error << "Destructors must take a non-null object\n";
        }
    }

    // Switch to the destructor block, and add code that cleans up
    // this object if the contents of the stack slot is not nullptr.
    IRBuilderBase::InsertPoint here = builder->saveIP();
    BasicBlock *dtors = get_destructor_block();

    builder->SetInsertPoint(dtors->getFirstNonPHI());

    PHINode *error_code = dyn_cast<PHINode>(dtors->begin());
    internal_assert(error_code) << "The destructor block is supposed to start with a phi node\n";

    llvm::Value *should_call = nullptr;
    switch (when) {
    case Always:
        should_call = ConstantInt::get(i1_t, 1);
        break;
    case OnError:
        should_call = builder->CreateIsNotNull(error_code);
        break;
    case OnSuccess:
        should_call = builder->CreateIsNull(error_code);
        break;
    }
    llvm::Function *call_destructor = module->getFunction("call_destructor");
    internal_assert(call_destructor);
    internal_assert(destructor_fn);
    internal_assert(should_call);
    Value *args[] = {get_user_context(), destructor_fn, stack_slot, should_call};
    builder->CreateCall(call_destructor, args);

    // Switch back to the original location
    builder->restoreIP(here);

    // Return the stack slot so that it's possible to cleanup the object early.
    return stack_slot;
}

void CodeGen_LLVM::trigger_destructor(llvm::Function *destructor_fn, Value *stack_slot) {
    llvm::Function *call_destructor = module->getFunction("call_destructor");
    internal_assert(call_destructor);
    internal_assert(destructor_fn);
    stack_slot = builder->CreatePointerCast(stack_slot, i8_t->getPointerTo()->getPointerTo());
    Value *should_call = ConstantInt::get(i1_t, 1);
    Value *args[] = {get_user_context(), destructor_fn, stack_slot, should_call};
    builder->CreateCall(call_destructor, args);
}

void CodeGen_LLVM::compile_buffer(const Buffer<> &buf) {
    // Embed the buffer declaration as a global.
    internal_assert(buf.defined());

    user_assert(buf.data())
        << "Can't embed buffer " << buf.name() << " because it has a null host pointer.\n";
    user_assert(!buf.device_dirty())
        << "Can't embed Image \"" << buf.name() << "\""
        << " because it has a dirty device pointer\n";

    Constant *type_fields[] = {
        ConstantInt::get(i8_t, buf.type().code()),
        ConstantInt::get(i8_t, buf.type().bits()),
        ConstantInt::get(i16_t, buf.type().lanes())};

    Constant *shape = nullptr;
    if (buf.dimensions()) {
        size_t shape_size = buf.dimensions() * sizeof(halide_dimension_t);
        vector<char> shape_blob((char *)buf.raw_buffer()->dim, (char *)buf.raw_buffer()->dim + shape_size);
        shape = create_binary_blob(shape_blob, buf.name() + ".shape");
        shape = ConstantExpr::getPointerCast(shape, dimension_t_type->getPointerTo());
    } else {
        shape = ConstantPointerNull::get(dimension_t_type->getPointerTo());
    }

    // For now, we assume buffers that aren't scalar are constant,
    // while scalars can be mutated. This accommodates all our existing
    // use cases, which is that all buffers are constant, except those
    // used to store stateful module information in offloading runtimes.
    bool constant = buf.dimensions() != 0;

    vector<char> data_blob((const char *)buf.data(), (const char *)buf.data() + buf.size_in_bytes());

    Constant *fields[] = {
        ConstantInt::get(i64_t, 0),                                         // device
        ConstantPointerNull::get(device_interface_t_type->getPointerTo()),  // device_interface
        create_binary_blob(data_blob, buf.name() + ".data", constant),      // host
        ConstantInt::get(i64_t, halide_buffer_flag_host_dirty),             // flags
        ConstantStruct::get(type_t_type, type_fields),                      // type
        ConstantInt::get(i32_t, buf.dimensions()),                          // dimensions
        shape,                                                              // dim
        ConstantPointerNull::get(i8_t->getPointerTo()),                     // padding
    };
    Constant *buffer_struct = ConstantStruct::get(halide_buffer_t_type, fields);

    // Embed the halide_buffer_t and make it point to the data array.
    GlobalVariable *global = new GlobalVariable(*module, halide_buffer_t_type,
                                                false, GlobalValue::PrivateLinkage,
                                                0, buf.name() + ".buffer");
    global->setInitializer(buffer_struct);

    // Finally, dump it in the symbol table
    Constant *zero[] = {ConstantInt::get(i32_t, 0)};
    Constant *global_ptr = ConstantExpr::getInBoundsGetElementPtr(halide_buffer_t_type, global, zero);
    sym_push(buf.name() + ".buffer", global_ptr);
}

Constant *CodeGen_LLVM::embed_constant_scalar_value_t(const Expr &e) {
    if (!e.defined()) {
        return Constant::getNullValue(scalar_value_t_type->getPointerTo());
    }

    internal_assert(!e.type().is_handle()) << "Should never see Handle types here.";

    llvm::Value *val = codegen(e);
    llvm::Constant *constant = dyn_cast<llvm::Constant>(val);
    internal_assert(constant);

    // Verify that the size of the LLVM value is the size we expected.
    internal_assert((uint64_t)constant->getType()->getPrimitiveSizeInBits() == (uint64_t)e.type().bits());

    // It's important that we allocate a full scalar_value_t_type here,
    // even if the type of the value is smaller; downstream consumers should
    // be able to correctly load an entire scalar_value_t_type regardless of its
    // type, and if we emit just (say) a uint8 value here, the pointer may be
    // misaligned and/or the storage after may be unmapped. LLVM doesn't support
    // unions directly, so we'll fake it by making a constant array of the elements
    // we need, setting the first to the constant we want, and setting the rest
    // to all-zeros. (This happens to work because sizeof(halide_scalar_value_t) is evenly
    // divisible by sizeof(any-union-field.)

    const size_t value_size = e.type().bytes();
    internal_assert(value_size > 0 && value_size <= sizeof(halide_scalar_value_t));

    const size_t array_size = sizeof(halide_scalar_value_t) / value_size;
    internal_assert(array_size * value_size == sizeof(halide_scalar_value_t));

    vector<Constant *> array_entries(array_size, Constant::getNullValue(constant->getType()));
    array_entries[0] = constant;

    llvm::ArrayType *array_type = ArrayType::get(constant->getType(), array_size);
    GlobalVariable *storage = new GlobalVariable(
        *module,
        array_type,
        /*isConstant*/ true,
        GlobalValue::PrivateLinkage,
        ConstantArray::get(array_type, array_entries));

    // Ensure that the storage is aligned for halide_scalar_value_t
    storage->setAlignment(make_alignment((int)sizeof(halide_scalar_value_t)));

    Constant *zero[] = {ConstantInt::get(i32_t, 0)};
    return ConstantExpr::getBitCast(
        ConstantExpr::getInBoundsGetElementPtr(array_type, storage, zero),
        scalar_value_t_type->getPointerTo());
}

Constant *CodeGen_LLVM::embed_constant_expr(Expr e, llvm::Type *t) {
    internal_assert(t != scalar_value_t_type);

    if (!e.defined()) {
        return Constant::getNullValue(t->getPointerTo());
    }

    internal_assert(!e.type().is_handle()) << "Should never see Handle types here.";
    if (!is_const(e)) {
        e = simplify(e);
        internal_assert(is_const(e)) << "Should only see constant values for estimates.";
    }

    llvm::Value *val = codegen(e);
    llvm::Constant *constant = dyn_cast<llvm::Constant>(val);
    internal_assert(constant);

    GlobalVariable *storage = new GlobalVariable(
        *module,
        constant->getType(),
        /*isConstant*/ true,
        GlobalValue::PrivateLinkage,
        constant);

    Constant *zero[] = {ConstantInt::get(i32_t, 0)};
    return ConstantExpr::getBitCast(
        ConstantExpr::getInBoundsGetElementPtr(constant->getType(), storage, zero),
        t->getPointerTo());
}

// Make a wrapper to call the function with an array of pointer
// args. This is easier for the JIT to call than a function with an
// unknown (at compile time) argument list. If result_in_argv is false,
// the internal function result is returned as the wrapper function
// result; if result_in_argv is true, the internal function result
// is stored as the last item in the argv list (which must be one
// longer than the number of arguments), and the wrapper's actual
// return type is always 'void'.
llvm::Function *CodeGen_LLVM::add_argv_wrapper(llvm::Function *fn,
                                               const std::string &name,
                                               bool result_in_argv) {
    llvm::Type *wrapper_result_type = result_in_argv ? void_t : i32_t;
    llvm::Type *wrapper_args_t[] = {i8_t->getPointerTo()->getPointerTo()};
    llvm::FunctionType *wrapper_func_t = llvm::FunctionType::get(wrapper_result_type, wrapper_args_t, false);
    llvm::Function *wrapper_func = llvm::Function::Create(wrapper_func_t, llvm::GlobalValue::ExternalLinkage, name, module.get());
    llvm::BasicBlock *wrapper_block = llvm::BasicBlock::Create(module->getContext(), "entry", wrapper_func);
    builder->SetInsertPoint(wrapper_block);

    llvm::Value *arg_array = iterator_to_pointer(wrapper_func->arg_begin());
    std::vector<llvm::Value *> wrapper_args;
    for (llvm::Function::arg_iterator i = fn->arg_begin(); i != fn->arg_end(); i++) {
        // Get the address of the nth argument
        llvm::Value *ptr = builder->CreateConstGEP1_32(arg_array, wrapper_args.size());
        ptr = builder->CreateLoad(ptr);
        if (i->getType() == halide_buffer_t_type->getPointerTo()) {
            // Cast the argument to a halide_buffer_t *
            wrapper_args.push_back(builder->CreatePointerCast(ptr, halide_buffer_t_type->getPointerTo()));
        } else {
            // Cast to the appropriate type and load
            ptr = builder->CreatePointerCast(ptr, i->getType()->getPointerTo());
            wrapper_args.push_back(builder->CreateLoad(ptr));
        }
    }
    debug(4) << "Creating call from wrapper to actual function\n";
    llvm::CallInst *result = builder->CreateCall(fn, wrapper_args);
    // This call should never inline
    result->setIsNoInline();

    if (result_in_argv) {
        llvm::Value *result_in_argv_ptr = builder->CreateConstGEP1_32(arg_array, wrapper_args.size());
        if (fn->getReturnType() != void_t) {
            result_in_argv_ptr = builder->CreateLoad(result_in_argv_ptr);
            // Cast to the appropriate type and store
            result_in_argv_ptr = builder->CreatePointerCast(result_in_argv_ptr, fn->getReturnType()->getPointerTo());
            builder->CreateStore(result, result_in_argv_ptr);
        }
        builder->CreateRetVoid();
    } else {
        // We could probably support other types as return values,
        // but int32 results are all that have actually been tested.
        internal_assert(fn->getReturnType() == i32_t);
        builder->CreateRet(result);
    }
    internal_assert(!verifyFunction(*wrapper_func, &llvm::errs()));
    return wrapper_func;
}

llvm::Function *CodeGen_LLVM::embed_metadata_getter(const std::string &metadata_name,
                                                    const std::string &function_name, const std::vector<LoweredArgument> &args,
                                                    const std::map<std::string, std::string> &metadata_name_map) {
    Constant *zero = ConstantInt::get(i32_t, 0);

    const int num_args = (int)args.size();

    auto map_string = [&metadata_name_map](const std::string &from) -> std::string {
        auto it = metadata_name_map.find(from);
        return it == metadata_name_map.end() ? from : it->second;
    };

    vector<Constant *> arguments_array_entries;
    for (int arg = 0; arg < num_args; ++arg) {

        StructType *type_t_type = module->getTypeByName("struct.halide_type_t");
        internal_assert(type_t_type) << "Did not find halide_type_t in module.\n";

        Constant *type_fields[] = {
            ConstantInt::get(i8_t, args[arg].type.code()),
            ConstantInt::get(i8_t, args[arg].type.bits()),
            ConstantInt::get(i16_t, 1)};
        Constant *type = ConstantStruct::get(type_t_type, type_fields);

        auto argument_estimates = args[arg].argument_estimates;
        if (args[arg].type.is_handle()) {
            // Handle values are always emitted into metadata as "undefined", regardless of
            // what sort of Expr is provided.
            argument_estimates = ArgumentEstimates{};
        }

        Constant *buffer_estimates_array_ptr;
        if (args[arg].is_buffer() && !argument_estimates.buffer_estimates.empty()) {
            internal_assert((int)argument_estimates.buffer_estimates.size() == args[arg].dimensions);
            vector<Constant *> buffer_estimates_array_entries;
            for (const auto &be : argument_estimates.buffer_estimates) {
                Expr min = be.min;
                if (min.defined()) min = cast<int64_t>(min);
                Expr extent = be.extent;
                if (extent.defined()) extent = cast<int64_t>(extent);
                buffer_estimates_array_entries.push_back(embed_constant_expr(min, i64_t));
                buffer_estimates_array_entries.push_back(embed_constant_expr(extent, i64_t));
            }

            llvm::ArrayType *buffer_estimates_array = ArrayType::get(i64_t->getPointerTo(), buffer_estimates_array_entries.size());
            GlobalVariable *buffer_estimates_array_storage = new GlobalVariable(
                *module,
                buffer_estimates_array,
                /*isConstant*/ true,
                GlobalValue::PrivateLinkage,
                ConstantArray::get(buffer_estimates_array, buffer_estimates_array_entries));

            Value *zeros[] = {zero, zero};
            buffer_estimates_array_ptr = ConstantExpr::getInBoundsGetElementPtr(buffer_estimates_array, buffer_estimates_array_storage, zeros);
        } else {
            buffer_estimates_array_ptr = Constant::getNullValue(i64_t->getPointerTo()->getPointerTo());
        }

        Constant *argument_fields[] = {
            create_string_constant(map_string(args[arg].name)),
            ConstantInt::get(i32_t, args[arg].kind),
            ConstantInt::get(i32_t, args[arg].dimensions),
            type,
            embed_constant_scalar_value_t(argument_estimates.scalar_def),
            embed_constant_scalar_value_t(argument_estimates.scalar_min),
            embed_constant_scalar_value_t(argument_estimates.scalar_max),
            embed_constant_scalar_value_t(argument_estimates.scalar_estimate),
            buffer_estimates_array_ptr};
        arguments_array_entries.push_back(ConstantStruct::get(argument_t_type, argument_fields));
    }
    llvm::ArrayType *arguments_array = ArrayType::get(argument_t_type, num_args);
    GlobalVariable *arguments_array_storage = new GlobalVariable(
        *module,
        arguments_array,
        /*isConstant*/ true,
        GlobalValue::PrivateLinkage,
        ConstantArray::get(arguments_array, arguments_array_entries));

    Constant *version = ConstantInt::get(i32_t, halide_filter_metadata_t::VERSION);

    Value *zeros[] = {zero, zero};
    Constant *metadata_fields[] = {
        /* version */ version,
        /* num_arguments */ ConstantInt::get(i32_t, num_args),
        /* arguments */ ConstantExpr::getInBoundsGetElementPtr(arguments_array, arguments_array_storage, zeros),
        /* target */ create_string_constant(map_string(target.to_string())),
        /* name */ create_string_constant(map_string(function_name))};

    GlobalVariable *metadata_storage = new GlobalVariable(
        *module,
        metadata_t_type,
        /*isConstant*/ true,
        GlobalValue::PrivateLinkage,
        ConstantStruct::get(metadata_t_type, metadata_fields),
        metadata_name + "_storage");

    llvm::FunctionType *func_t = llvm::FunctionType::get(metadata_t_type->getPointerTo(), false);
    llvm::Function *metadata_getter = llvm::Function::Create(func_t, llvm::GlobalValue::ExternalLinkage, metadata_name, module.get());
    llvm::BasicBlock *block = llvm::BasicBlock::Create(module->getContext(), "entry", metadata_getter);
    builder->SetInsertPoint(block);
    builder->CreateRet(metadata_storage);
    internal_assert(!verifyFunction(*metadata_getter, &llvm::errs()));

    return metadata_getter;
}

llvm::Type *CodeGen_LLVM::llvm_type_of(const Type &t) const {
    return Internal::llvm_type_of(context, t);
}

void CodeGen_LLVM::optimize_module() {
    debug(3) << "Optimizing module\n";

    HALIDE_TIC;

    auto time_start = std::chrono::high_resolution_clock::now();

    if (debug::debug_level() >= 3) {
        module->print(dbgs(), nullptr, false, true);
    }

    std::unique_ptr<TargetMachine> tm = make_target_machine(*module);

    // At present, we default to *enabling* LLVM loop optimization,
    // unless DisableLLVMLoopOpt is set; we're going to flip this to defaulting
    // to *not* enabling these optimizations (and removing the DisableLLVMLoopOpt feature).
    // See https://github.com/halide/Halide/issues/4113 for more info.
    // (Note that setting EnableLLVMLoopOpt always enables loop opt, regardless
    // of the setting of DisableLLVMLoopOpt.)
    const bool do_loop_opt = !get_target().has_feature(Target::DisableLLVMLoopOpt) ||
                             get_target().has_feature(Target::EnableLLVMLoopOpt);

    PipelineTuningOptions pto;
    pto.LoopInterleaving = do_loop_opt;
    pto.LoopVectorization = do_loop_opt;
    pto.SLPVectorization = true;  // Note: SLP vectorization has no analogue in the Halide scheduling model
    pto.LoopUnrolling = do_loop_opt;
    // Clear ScEv info for all loops. Certain Halide applications spend a very
    // long time compiling in forgetLoop, and prefer to forget everything
    // and rebuild SCEV (aka "Scalar Evolution") from scratch.
    // Sample difference in compile time reduction at the time of this change was
    // 21.04 -> 14.78 using current ToT release build. (See also https://reviews.llvm.org/rL358304)
    pto.ForgetAllSCEVInLoopUnroll = true;

    llvm::PassBuilder pb(tm.get(), pto);

    bool debug_pass_manager = false;
    // These analysis managers have to be declared in this order.
    llvm::LoopAnalysisManager lam(debug_pass_manager);
    llvm::FunctionAnalysisManager fam(debug_pass_manager);
    llvm::CGSCCAnalysisManager cgam(debug_pass_manager);
    llvm::ModuleAnalysisManager mam(debug_pass_manager);

    llvm::AAManager aa = pb.buildDefaultAAPipeline();
    fam.registerPass([&] { return std::move(aa); });

    // Register all the basic analyses with the managers.
    pb.registerModuleAnalyses(mam);
    pb.registerCGSCCAnalyses(cgam);
    pb.registerFunctionAnalyses(fam);
    pb.registerLoopAnalyses(lam);
    pb.crossRegisterProxies(lam, fam, cgam, mam);
    ModulePassManager mpm(debug_pass_manager);

    PassBuilder::OptimizationLevel level = PassBuilder::OptimizationLevel::O3;

    if (get_target().has_feature(Target::ASAN)) {
        pb.registerPipelineStartEPCallback([&](ModulePassManager &mpm) {
            mpm.addPass(
                RequireAnalysisPass<ASanGlobalsMetadataAnalysis, llvm::Module>());
        });
        pb.registerOptimizerLastEPCallback(
            [](FunctionPassManager &fpm, PassBuilder::OptimizationLevel level) {
                constexpr bool compile_kernel = false;
                constexpr bool recover = false;
                constexpr bool use_after_scope = true;
                fpm.addPass(AddressSanitizerPass(
                    compile_kernel, recover, use_after_scope));
            });
        pb.registerPipelineStartEPCallback(
            [](ModulePassManager &mpm) {
                constexpr bool compile_kernel = false;
                constexpr bool recover = false;
                constexpr bool module_use_after_scope = false;
                constexpr bool use_odr_indicator = true;
                mpm.addPass(ModuleAddressSanitizerPass(
                    compile_kernel, recover, module_use_after_scope,
                    use_odr_indicator));
            });
    }

    if (get_target().has_feature(Target::TSAN)) {
        pb.registerOptimizerLastEPCallback(
            [](FunctionPassManager &fpm, PassBuilder::OptimizationLevel level) {
                fpm.addPass(ThreadSanitizerPass());
            });
    }

    for (llvm::Module::iterator i = module->begin(); i != module->end(); i++) {
        if (get_target().has_feature(Target::ASAN)) {
            i->addFnAttr(Attribute::SanitizeAddress);
        }
        if (get_target().has_feature(Target::TSAN)) {
            // Do not annotate any of Halide's low-level synchronization code as it has
            // tsan interface calls to mark its behavior and is much faster if
            // it is not analyzed instruction by instruction.
            if (!(i->getName().startswith("_ZN6Halide7Runtime8Internal15Synchronization") ||
                  // TODO: this is a benign data race that re-initializes the detected features;
                  // we should really fix it properly inside the implementation, rather than disabling
                  // it here as a band-aid.
                  i->getName().startswith("halide_default_can_use_target_features") ||
                  i->getName().startswith("halide_mutex_") ||
                  i->getName().startswith("halide_cond_"))) {
                i->addFnAttr(Attribute::SanitizeThread);
            }
        }
    }

    mpm = pb.buildPerModuleDefaultPipeline(level, debug_pass_manager);
    mpm.run(*module, mam);

    if (llvm::verifyModule(*module, &errs()))
        report_fatal_error("Transformation resulted in an invalid module\n");

    debug(3) << "After LLVM optimizations:\n";
    if (debug::debug_level() >= 2) {
        module->print(dbgs(), nullptr, false, true);
    }

    HALIDE_TOC;

    auto *logger = get_compiler_logger();
    if (logger) {
        auto time_end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = time_end - time_start;
        logger->record_compilation_time(CompilerLogger::Phase::LLVM, diff.count());
    }
}

void CodeGen_LLVM::sym_push(const string &name, llvm::Value *value) {
    if (!value->getType()->isVoidTy()) {
        value->setName(name);
    }
    symbol_table.push(name, value);
}

void CodeGen_LLVM::sym_pop(const string &name) {
    symbol_table.pop(name);
}

llvm::Value *CodeGen_LLVM::sym_get(const string &name, bool must_succeed) const {
    // look in the symbol table
    if (!symbol_table.contains(name)) {
        if (must_succeed) {
            std::ostringstream err;
            err << "Symbol not found: " << name << "\n";

            if (debug::debug_level() > 0) {
                err << "The following names are in scope:\n"
                    << symbol_table << "\n";
            }

            internal_error << err.str();
        } else {
            return nullptr;
        }
    }
    return symbol_table.get(name);
}

bool CodeGen_LLVM::sym_exists(const string &name) const {
    return symbol_table.contains(name);
}

Value *CodeGen_LLVM::codegen(const Expr &e) {
    internal_assert(e.defined());
    debug(4) << "Codegen: " << e.type() << ", " << e << "\n";
    value = nullptr;
    e.accept(this);
    internal_assert(value) << "Codegen of an expr did not produce an llvm value\n";
    // TODO: skip this correctness check for bool vectors,
    // as eliminate_bool_vectors() will cause a discrepancy for some backends
    // (eg OpenCL, HVX); for now we're just ignoring the assert, but
    // in the long run we should improve the smarts. See https://github.com/halide/Halide/issues/4194.
    const bool is_bool_vector = e.type().is_bool() && e.type().lanes() > 1;
    // TODO: skip this correctness check for prefetch, because the return type
    // of prefetch indicates the type being prefetched, which does not match the
    // implementation of prefetch.
    // See https://github.com/halide/Halide/issues/4211.
    const bool is_prefetch = e.as<Call>() && e.as<Call>()->is_intrinsic(Call::prefetch);
    internal_assert(is_bool_vector || is_prefetch ||
                    e.type().is_handle() ||
                    value->getType()->isVoidTy() ||
                    value->getType() == llvm_type_of(e.type()))
        << "Codegen of Expr " << e
        << " of type " << e.type()
        << " did not produce llvm IR of the corresponding llvm type.\n";
    return value;
}

void CodeGen_LLVM::codegen(const Stmt &s) {
    internal_assert(s.defined());
    debug(3) << "Codegen: " << s << "\n";
    value = nullptr;
    s.accept(this);
}

Type CodeGen_LLVM::upgrade_type_for_arithmetic(const Type &t) const {
    if (t.is_bfloat() || (t.is_float() && t.bits() < 32)) {
        return Float(32, t.lanes());
    } else {
        return t;
    }
}

Type CodeGen_LLVM::upgrade_type_for_argument_passing(const Type &t) const {
    if (t.is_bfloat() || (t.is_float() && t.bits() < 32)) {
        return t.with_code(halide_type_uint);
    } else {
        return t;
    }
}

Type CodeGen_LLVM::upgrade_type_for_storage(const Type &t) const {
    if (t.is_bfloat() || (t.is_float() && t.bits() < 32)) {
        return t.with_code(halide_type_uint);
    } else if (t.is_bool()) {
        return t.with_bits(8);
    } else if (t.is_handle()) {
        return UInt(64, t.lanes());
    } else {
        return t;
    }
}

void CodeGen_LLVM::visit(const IntImm *op) {
    value = ConstantInt::getSigned(llvm_type_of(op->type), op->value);
}

void CodeGen_LLVM::visit(const UIntImm *op) {
    value = ConstantInt::get(llvm_type_of(op->type), op->value);
}

void CodeGen_LLVM::visit(const FloatImm *op) {
    if (op->type.is_bfloat()) {
        codegen(reinterpret(BFloat(16), make_const(UInt(16), bfloat16_t(op->value).to_bits())));
    } else if (op->type.bits() == 16) {
        codegen(reinterpret(Float(16), make_const(UInt(16), float16_t(op->value).to_bits())));
    } else {
        value = ConstantFP::get(llvm_type_of(op->type), op->value);
    }
}

void CodeGen_LLVM::visit(const StringImm *op) {
    value = create_string_constant(op->value);
}

void CodeGen_LLVM::visit(const Cast *op) {
    Halide::Type src = op->value.type();
    Halide::Type dst = op->type;

    if (upgrade_type_for_arithmetic(src) != src ||
        upgrade_type_for_arithmetic(dst) != dst) {
        // Handle casts to and from types for which we don't have native support.
        debug(4) << "Emulating cast from " << src << " to " << dst << "\n";
        if ((src.is_float() && src.bits() < 32) ||
            (dst.is_float() && dst.bits() < 32)) {
            Expr equiv = lower_float16_cast(op);
            internal_assert(equiv.type() == op->type);
            codegen(equiv);
        } else {
            internal_error << "Cast from type: " << src
                           << " to " << dst
                           << " unimplemented\n";
        }
        return;
    }

    value = codegen(op->value);
    llvm::Type *llvm_dst = llvm_type_of(dst);

    if (dst.is_handle() && src.is_handle()) {
        value = builder->CreateBitCast(value, llvm_dst);
    } else if (dst.is_handle() || src.is_handle()) {
        internal_error << "Can't cast from " << src << " to " << dst << "\n";
    } else if (!src.is_float() && !dst.is_float()) {
        // Widening integer casts either zero extend or sign extend,
        // depending on the source type. Narrowing integer casts
        // always truncate.
        value = builder->CreateIntCast(value, llvm_dst, src.is_int());
    } else if (src.is_float() && dst.is_int()) {
        value = builder->CreateFPToSI(value, llvm_dst);
    } else if (src.is_float() && dst.is_uint()) {
        // fptoui has undefined behavior on overflow. Seems reasonable
        // to get an unspecified uint on overflow, but because uint1s
        // are stored in uint8s for float->uint1 casts this undefined
        // behavior manifests itself as uint1 values greater than 1,
        // which could in turn break our bounds inference
        // guarantees. So go via uint8 in this case.
        if (dst.bits() < 8) {
            value = builder->CreateFPToUI(value, llvm_type_of(dst.with_bits(8)));
            value = builder->CreateIntCast(value, llvm_dst, false);
        } else {
            value = builder->CreateFPToUI(value, llvm_dst);
        }
    } else if (src.is_int() && dst.is_float()) {
        value = builder->CreateSIToFP(value, llvm_dst);
    } else if (src.is_uint() && dst.is_float()) {
        value = builder->CreateUIToFP(value, llvm_dst);
    } else {
        internal_assert(src.is_float() && dst.is_float());
        // Float widening or narrowing
        value = builder->CreateFPCast(value, llvm_dst);
    }
}

void CodeGen_LLVM::visit(const Variable *op) {
    value = sym_get(op->name);
}

void CodeGen_LLVM::visit(const Add *op) {
    Type t = upgrade_type_for_arithmetic(op->type);
    if (t != op->type) {
        codegen(cast(op->type, Add::make(cast(t, op->a), cast(t, op->b))));
        return;
    }

    Value *a = codegen(op->a);
    Value *b = codegen(op->b);
    if (op->type.is_float()) {
        value = builder->CreateFAdd(a, b);
    } else if (op->type.is_int() && op->type.bits() >= 32) {
        // We tell llvm integers don't wrap, so that it generates good
        // code for loop indices.
        value = builder->CreateNSWAdd(a, b);
    } else {
        value = builder->CreateAdd(a, b);
    }
}

void CodeGen_LLVM::visit(const Sub *op) {
    Type t = upgrade_type_for_arithmetic(op->type);
    if (t != op->type) {
        codegen(cast(op->type, Sub::make(cast(t, op->a), cast(t, op->b))));
        return;
    }

    Value *a = codegen(op->a);
    Value *b = codegen(op->b);
    if (op->type.is_float()) {
        value = builder->CreateFSub(a, b);
    } else if (op->type.is_int() && op->type.bits() >= 32) {
        // We tell llvm integers don't wrap, so that it generates good
        // code for loop indices.
        value = builder->CreateNSWSub(a, b);
    } else {
        value = builder->CreateSub(a, b);
    }
}

void CodeGen_LLVM::visit(const Mul *op) {
    Type t = upgrade_type_for_arithmetic(op->type);
    if (t != op->type) {
        codegen(cast(op->type, Mul::make(cast(t, op->a), cast(t, op->b))));
        return;
    }

    Value *a = codegen(op->a);
    Value *b = codegen(op->b);
    if (op->type.is_float()) {
        value = builder->CreateFMul(a, b);
    } else if (op->type.is_int() && op->type.bits() >= 32) {
        // We tell llvm integers don't wrap, so that it generates good
        // code for loop indices.
        value = builder->CreateNSWMul(a, b);
    } else {
        value = builder->CreateMul(a, b);
    }
}

void CodeGen_LLVM::visit(const Div *op) {
    user_assert(!is_zero(op->b)) << "Division by constant zero in expression: " << Expr(op) << "\n";

    Type t = upgrade_type_for_arithmetic(op->type);
    if (t != op->type) {
        codegen(cast(op->type, Div::make(cast(t, op->a), cast(t, op->b))));
        return;
    }

    if (op->type.is_float()) {
        // Don't call codegen() multiple times within an argument list:
        // order-of-evaluation isn't guaranteed and can vary by compiler,
        // leading to different LLVM IR ordering, which makes comparing
        // output hard.
        Value *a = codegen(op->a);
        Value *b = codegen(op->b);
        value = builder->CreateFDiv(a, b);
    } else {
        value = codegen(lower_int_uint_div(op->a, op->b));
    }
}

void CodeGen_LLVM::visit(const Mod *op) {
    Type t = upgrade_type_for_arithmetic(op->type);
    if (t != op->type) {
        codegen(cast(op->type, Mod::make(cast(t, op->a), cast(t, op->b))));
        return;
    }

    if (op->type.is_float()) {
        value = codegen(simplify(op->a - op->b * floor(op->a / op->b)));
    } else {
        value = codegen(lower_int_uint_mod(op->a, op->b));
    }
}

void CodeGen_LLVM::visit(const Min *op) {
    Type t = upgrade_type_for_arithmetic(op->type);
    if (t != op->type) {
        codegen(cast(op->type, Min::make(cast(t, op->a), cast(t, op->b))));
        return;
    }

    string a_name = unique_name('a');
    string b_name = unique_name('b');
    Expr a = Variable::make(op->a.type(), a_name);
    Expr b = Variable::make(op->b.type(), b_name);
    value = codegen(Let::make(a_name, op->a,
                              Let::make(b_name, op->b,
                                        select(a < b, a, b))));
}

void CodeGen_LLVM::visit(const Max *op) {
    Type t = upgrade_type_for_arithmetic(op->type);
    if (t != op->type) {
        codegen(cast(op->type, Max::make(cast(t, op->a), cast(t, op->b))));
        return;
    }

    string a_name = unique_name('a');
    string b_name = unique_name('b');
    Expr a = Variable::make(op->a.type(), a_name);
    Expr b = Variable::make(op->b.type(), b_name);
    value = codegen(Let::make(a_name, op->a,
                              Let::make(b_name, op->b,
                                        select(a > b, a, b))));
}

void CodeGen_LLVM::visit(const EQ *op) {
    Type t = upgrade_type_for_arithmetic(op->a.type());
    if (t != op->a.type()) {
        codegen(EQ::make(cast(t, op->a), cast(t, op->b)));
        return;
    }

    Value *a = codegen(op->a);
    Value *b = codegen(op->b);
    if (t.is_float()) {
        value = builder->CreateFCmpOEQ(a, b);
    } else {
        value = builder->CreateICmpEQ(a, b);
    }
}

void CodeGen_LLVM::visit(const NE *op) {
    Type t = upgrade_type_for_arithmetic(op->a.type());
    if (t != op->a.type()) {
        codegen(NE::make(cast(t, op->a), cast(t, op->b)));
        return;
    }

    Value *a = codegen(op->a);
    Value *b = codegen(op->b);
    if (t.is_float()) {
        value = builder->CreateFCmpONE(a, b);
    } else {
        value = builder->CreateICmpNE(a, b);
    }
}

void CodeGen_LLVM::visit(const LT *op) {
    Type t = upgrade_type_for_arithmetic(op->a.type());
    if (t != op->a.type()) {
        codegen(LT::make(cast(t, op->a), cast(t, op->b)));
        return;
    }

    Value *a = codegen(op->a);
    Value *b = codegen(op->b);
    if (t.is_float()) {
        value = builder->CreateFCmpOLT(a, b);
    } else if (t.is_int()) {
        value = builder->CreateICmpSLT(a, b);
    } else {
        value = builder->CreateICmpULT(a, b);
    }
}

void CodeGen_LLVM::visit(const LE *op) {
    Type t = upgrade_type_for_arithmetic(op->a.type());
    if (t != op->a.type()) {
        codegen(LE::make(cast(t, op->a), cast(t, op->b)));
        return;
    }

    Value *a = codegen(op->a);
    Value *b = codegen(op->b);
    if (t.is_float()) {
        value = builder->CreateFCmpOLE(a, b);
    } else if (t.is_int()) {
        value = builder->CreateICmpSLE(a, b);
    } else {
        value = builder->CreateICmpULE(a, b);
    }
}

void CodeGen_LLVM::visit(const GT *op) {
    Type t = upgrade_type_for_arithmetic(op->a.type());
    if (t != op->a.type()) {
        codegen(GT::make(cast(t, op->a), cast(t, op->b)));
        return;
    }

    Value *a = codegen(op->a);
    Value *b = codegen(op->b);

    if (t.is_float()) {
        value = builder->CreateFCmpOGT(a, b);
    } else if (t.is_int()) {
        value = builder->CreateICmpSGT(a, b);
    } else {
        value = builder->CreateICmpUGT(a, b);
    }
}

void CodeGen_LLVM::visit(const GE *op) {
    Type t = upgrade_type_for_arithmetic(op->a.type());
    if (t != op->a.type()) {
        codegen(GE::make(cast(t, op->a), cast(t, op->b)));
        return;
    }

    Value *a = codegen(op->a);
    Value *b = codegen(op->b);
    if (t.is_float()) {
        value = builder->CreateFCmpOGE(a, b);
    } else if (t.is_int()) {
        value = builder->CreateICmpSGE(a, b);
    } else {
        value = builder->CreateICmpUGE(a, b);
    }
}

void CodeGen_LLVM::visit(const And *op) {
    Value *a = codegen(op->a);
    Value *b = codegen(op->b);
    value = builder->CreateAnd(a, b);
}

void CodeGen_LLVM::visit(const Or *op) {
    Value *a = codegen(op->a);
    Value *b = codegen(op->b);
    value = builder->CreateOr(a, b);
}

void CodeGen_LLVM::visit(const Not *op) {
    Value *a = codegen(op->a);
    value = builder->CreateNot(a);
}

void CodeGen_LLVM::visit(const Select *op) {
    Value *cmp = codegen(op->condition);
    Value *a = codegen(op->true_value);
    Value *b = codegen(op->false_value);
    value = builder->CreateSelect(cmp, a, b);
}

namespace {
Expr promote_64(const Expr &e) {
    if (const Add *a = e.as<Add>()) {
        return Add::make(promote_64(a->a), promote_64(a->b));
    } else if (const Sub *s = e.as<Sub>()) {
        return Sub::make(promote_64(s->a), promote_64(s->b));
    } else if (const Mul *m = e.as<Mul>()) {
        return Mul::make(promote_64(m->a), promote_64(m->b));
    } else if (const Min *m = e.as<Min>()) {
        return Min::make(promote_64(m->a), promote_64(m->b));
    } else if (const Max *m = e.as<Max>()) {
        return Max::make(promote_64(m->a), promote_64(m->b));
    } else {
        return cast(Int(64), e);
    }
}
}  // namespace

Value *CodeGen_LLVM::codegen_buffer_pointer(const string &buffer, Halide::Type type, Expr index) {
    // Find the base address from the symbol table
    Value *base_address = symbol_table.get(buffer);
    return codegen_buffer_pointer(base_address, type, std::move(index));
}

Value *CodeGen_LLVM::codegen_buffer_pointer(Value *base_address, Halide::Type type, Expr index) {
    // Promote index to 64-bit on targets that use 64-bit pointers.
    llvm::DataLayout d(module.get());
    if (promote_indices() && d.getPointerSize() == 8) {
        index = promote_64(index);
    }

    return codegen_buffer_pointer(base_address, type, codegen(index));
}

Value *CodeGen_LLVM::codegen_buffer_pointer(const string &buffer, Halide::Type type, Value *index) {
    // Find the base address from the symbol table
    Value *base_address = symbol_table.get(buffer);
    return codegen_buffer_pointer(base_address, type, index);
}

Value *CodeGen_LLVM::codegen_buffer_pointer(Value *base_address, Halide::Type type, Value *index) {
    llvm::Type *base_address_type = base_address->getType();
    unsigned address_space = base_address_type->getPointerAddressSpace();

    type = upgrade_type_for_storage(type);

    llvm::Type *load_type = llvm_type_of(type)->getPointerTo(address_space);

    // If the type doesn't match the expected type, we need to pointer cast
    if (load_type != base_address_type) {
        base_address = builder->CreatePointerCast(base_address, load_type);
    }

    llvm::Constant *constant_index = dyn_cast<llvm::Constant>(index);
    if (constant_index && constant_index->isZeroValue()) {
        return base_address;
    }

    // Promote index to 64-bit on targets that use 64-bit pointers.
    llvm::DataLayout d(module.get());
    if (d.getPointerSize() == 8) {
        index = builder->CreateIntCast(index, i64_t, true);
    }

    return builder->CreateInBoundsGEP(base_address, index);
}

namespace {
int next_power_of_two(int x) {
    for (int p2 = 1;; p2 *= 2) {
        if (p2 >= x) {
            return p2;
        }
    }
    // unreachable.
}
}  // namespace

void CodeGen_LLVM::add_tbaa_metadata(llvm::Instruction *inst, string buffer, const Expr &index) {

    // Get the unique name for the block of memory this allocate node
    // is using.
    buffer = get_allocation_name(buffer);

    // If the index is constant, we generate some TBAA info that helps
    // LLVM understand our loads/stores aren't aliased.
    bool constant_index = false;
    int64_t base = 0;
    int64_t width = 1;

    if (index.defined()) {
        if (const Ramp *ramp = index.as<Ramp>()) {
            const int64_t *pstride = as_const_int(ramp->stride);
            const int64_t *pbase = as_const_int(ramp->base);
            if (pstride && pbase) {
                // We want to find the smallest aligned width and offset
                // that contains this ramp.
                int64_t stride = *pstride;
                base = *pbase;
                internal_assert(base >= 0);
                width = next_power_of_two(ramp->lanes * stride);

                while (base % width) {
                    base -= base % width;
                    width *= 2;
                }
                constant_index = true;
            }
        } else {
            const int64_t *pbase = as_const_int(index);
            if (pbase) {
                base = *pbase;
                constant_index = true;
            }
        }
    }

    llvm::MDBuilder builder(*context);

    // Add type-based-alias-analysis metadata to the pointer, so that
    // loads and stores to different buffers can get reordered.
    MDNode *tbaa = builder.createTBAARoot("Halide buffer");

    tbaa = builder.createTBAAScalarTypeNode(buffer, tbaa);

    // We also add metadata for constant indices to allow loads and
    // stores to the same buffer to get reordered.
    if (constant_index) {
        for (int w = 1024; w >= width; w /= 2) {
            int64_t b = (base / w) * w;

            std::stringstream level;
            level << buffer << ".width" << w << ".base" << b;
            tbaa = builder.createTBAAScalarTypeNode(level.str(), tbaa);
        }
    }

    tbaa = builder.createTBAAStructTagNode(tbaa, tbaa, 0);

    inst->setMetadata("tbaa", tbaa);
}

void CodeGen_LLVM::visit(const Load *op) {
    // If the type should be stored as some other type, insert a reinterpret cast.
    Type storage_type = upgrade_type_for_storage(op->type);
    if (op->type != storage_type) {
        codegen(reinterpret(op->type, Load::make(storage_type, op->name,
                                                 op->index, op->image,
                                                 op->param, op->predicate, op->alignment)));
        return;
    }

    // Predicated load
    if (!is_one(op->predicate)) {
        codegen_predicated_vector_load(op);
        return;
    }

    // There are several cases. Different architectures may wish to override some.
    if (op->type.is_scalar()) {
        // Scalar loads
        Value *ptr = codegen_buffer_pointer(op->name, op->type, op->index);
        LoadInst *load = builder->CreateAlignedLoad(ptr, make_alignment(op->type.bytes()));
        add_tbaa_metadata(load, op->name, op->index);
        value = load;
    } else {
        const Ramp *ramp = op->index.as<Ramp>();
        const IntImm *stride = ramp ? ramp->stride.as<IntImm>() : nullptr;

        if (ramp && stride && stride->value == 1) {
            value = codegen_dense_vector_load(op);
        } else if (ramp && stride && stride->value == 2) {
            // Load two vectors worth and then shuffle
            Expr base_a = ramp->base, base_b = ramp->base + ramp->lanes;
            Expr stride_a = make_one(base_a.type());
            Expr stride_b = make_one(base_b.type());

            ModulusRemainder align_a = op->alignment;
            ModulusRemainder align_b = align_a + ramp->lanes;

            // False indicates we should take the even-numbered lanes
            // from the load, true indicates we should take the
            // odd-numbered-lanes.
            bool shifted_a = false, shifted_b = false;

            bool external = op->param.defined() || op->image.defined();

            // Don't read beyond the end of an external buffer.
            // (In ASAN mode, don't read beyond the end of internal buffers either,
            // as ASAN will complain even about harmless stack overreads.)
            if (external || target.has_feature(Target::ASAN)) {
                base_b -= 1;
                align_b = align_b - 1;
                shifted_b = true;
            } else {
                // If the base ends in an odd constant, then subtract one
                // and do a different shuffle. This helps expressions like
                // (f(2*x) + f(2*x+1) share loads
                const Add *add = ramp->base.as<Add>();
                const IntImm *offset = add ? add->b.as<IntImm>() : nullptr;
                if (offset && offset->value & 1) {
                    base_a -= 1;
                    align_a = align_a - 1;
                    shifted_a = true;
                    base_b -= 1;
                    align_b = align_b - 1;
                    shifted_b = true;
                }
            }

            // Do each load.
            Expr ramp_a = Ramp::make(base_a, stride_a, ramp->lanes);
            Expr ramp_b = Ramp::make(base_b, stride_b, ramp->lanes);
            Expr load_a = Load::make(op->type, op->name, ramp_a, op->image, op->param, op->predicate, align_a);
            Expr load_b = Load::make(op->type, op->name, ramp_b, op->image, op->param, op->predicate, align_b);
            Value *vec_a = codegen(load_a);
            Value *vec_b = codegen(load_b);

            // Shuffle together the results.
            vector<int> indices(ramp->lanes);
            for (int i = 0; i < (ramp->lanes + 1) / 2; i++) {
                indices[i] = i * 2 + (shifted_a ? 1 : 0);
            }
            for (int i = (ramp->lanes + 1) / 2; i < ramp->lanes; i++) {
                indices[i] = i * 2 + (shifted_b ? 1 : 0);
            }

            value = shuffle_vectors(vec_a, vec_b, indices);
        } else if (ramp && stride && stride->value == -1) {
            // Load the vector and then flip it in-place
            Expr flipped_base = ramp->base - ramp->lanes + 1;
            Expr flipped_stride = make_one(flipped_base.type());
            Expr flipped_index = Ramp::make(flipped_base, flipped_stride, ramp->lanes);
            ModulusRemainder align = op->alignment;
            // Switch to the alignment of the last lane
            align = align - (ramp->lanes - 1);
            Expr flipped_load = Load::make(op->type, op->name, flipped_index, op->image, op->param, op->predicate, align);

            Value *flipped = codegen(flipped_load);

            vector<int> indices(ramp->lanes);
            for (int i = 0; i < ramp->lanes; i++) {
                indices[i] = ramp->lanes - 1 - i;
            }

            value = shuffle_vectors(flipped, indices);
        } else if (ramp) {
            // Gather without generating the indices as a vector
            Value *ptr = codegen_buffer_pointer(op->name, op->type.element_of(), ramp->base);
            Value *stride = codegen(ramp->stride);
            value = UndefValue::get(llvm_type_of(op->type));
            for (int i = 0; i < ramp->lanes; i++) {
                Value *lane = ConstantInt::get(i32_t, i);
                LoadInst *val = builder->CreateLoad(ptr);
                add_tbaa_metadata(val, op->name, op->index);
                value = builder->CreateInsertElement(value, val, lane);
                ptr = builder->CreateInBoundsGEP(ptr, stride);
            }
        } else if ((false)) { /* should_scalarize(op->index) */
            // TODO: put something sensible in for
            // should_scalarize. Probably a good idea if there are no
            // loads in it, and it's all int32.

            // Compute the index as scalars, and then do a gather
            Value *vec = UndefValue::get(llvm_type_of(op->type));
            for (int i = 0; i < op->type.lanes(); i++) {
                Expr idx = extract_lane(op->index, i);
                Value *ptr = codegen_buffer_pointer(op->name, op->type.element_of(), idx);
                LoadInst *val = builder->CreateLoad(ptr);
                add_tbaa_metadata(val, op->name, op->index);
                vec = builder->CreateInsertElement(vec, val, ConstantInt::get(i32_t, i));
            }
            value = vec;
        } else {
            // General gathers
            Value *index = codegen(op->index);
            Value *vec = UndefValue::get(llvm_type_of(op->type));
            for (int i = 0; i < op->type.lanes(); i++) {
                Value *idx = builder->CreateExtractElement(index, ConstantInt::get(i32_t, i));
                Value *ptr = codegen_buffer_pointer(op->name, op->type.element_of(), idx);
                LoadInst *val = builder->CreateLoad(ptr);
                add_tbaa_metadata(val, op->name, op->index);
                vec = builder->CreateInsertElement(vec, val, ConstantInt::get(i32_t, i));
            }
            value = vec;
        }
    }
}

void CodeGen_LLVM::visit(const Ramp *op) {
    if (is_const(op->stride) && !is_const(op->base)) {
        // If the stride is const and the base is not (e.g. ramp(x, 1,
        // 4)), we can lift out the stride and broadcast the base so
        // we can do a single vector broadcast and add instead of
        // repeated insertion
        Expr broadcast = Broadcast::make(op->base, op->lanes);
        Expr ramp = Ramp::make(make_zero(op->base.type()), op->stride, op->lanes);
        value = codegen(broadcast + ramp);
    } else {
        // Otherwise we generate element by element by adding the stride to the base repeatedly

        Value *base = codegen(op->base);
        Value *stride = codegen(op->stride);

        value = UndefValue::get(llvm_type_of(op->type));
        for (int i = 0; i < op->type.lanes(); i++) {
            if (i > 0) {
                if (op->type.is_float()) {
                    base = builder->CreateFAdd(base, stride);
                } else if (op->type.is_int() && op->type.bits() >= 32) {
                    base = builder->CreateNSWAdd(base, stride);
                } else {
                    base = builder->CreateAdd(base, stride);
                }
            }
            value = builder->CreateInsertElement(value, base, ConstantInt::get(i32_t, i));
        }
    }
}

llvm::Value *CodeGen_LLVM::create_broadcast(llvm::Value *v, int lanes) {
    Constant *undef = UndefValue::get(VectorType::get(v->getType(), lanes));
    Constant *zero = ConstantInt::get(i32_t, 0);
    v = builder->CreateInsertElement(undef, v, zero);
#if LLVM_VERSION >= 110
    const llvm::ElementCount elem_count(lanes, /*scalable*/ false);
#else
    const int elem_count = lanes;
#endif
    Constant *zeros = ConstantVector::getSplat(elem_count, zero);
    return builder->CreateShuffleVector(v, undef, zeros);
}

void CodeGen_LLVM::visit(const Broadcast *op) {
    Value *v = codegen(op->value);
    value = create_broadcast(v, op->lanes);
}

Value *CodeGen_LLVM::interleave_vectors(const std::vector<Value *> &vecs) {
    internal_assert(!vecs.empty());
    for (size_t i = 1; i < vecs.size(); i++) {
        internal_assert(vecs[0]->getType() == vecs[i]->getType());
    }
    int vec_elements = get_vector_num_elements(vecs[0]->getType());

    if (vecs.size() == 1) {
        return vecs[0];
    } else if (vecs.size() == 2) {
        Value *a = vecs[0];
        Value *b = vecs[1];
        vector<int> indices(vec_elements * 2);
        for (int i = 0; i < vec_elements * 2; i++) {
            indices[i] = i % 2 == 0 ? i / 2 : i / 2 + vec_elements;
        }
        return shuffle_vectors(a, b, indices);
    } else {
        // Grab the even and odd elements of vecs.
        vector<Value *> even_vecs;
        vector<Value *> odd_vecs;
        for (size_t i = 0; i < vecs.size(); i++) {
            if (i % 2 == 0) {
                even_vecs.push_back(vecs[i]);
            } else {
                odd_vecs.push_back(vecs[i]);
            }
        }

        // If the number of vecs is odd, save the last one for later.
        Value *last = nullptr;
        if (even_vecs.size() > odd_vecs.size()) {
            last = even_vecs.back();
            even_vecs.pop_back();
        }
        internal_assert(even_vecs.size() == odd_vecs.size());

        // Interleave the even and odd parts.
        Value *even = interleave_vectors(even_vecs);
        Value *odd = interleave_vectors(odd_vecs);

        if (last) {
            int result_elements = vec_elements * vecs.size();

            // Interleave even and odd, leaving a space for the last element.
            vector<int> indices(result_elements, -1);
            for (int i = 0, idx = 0; i < result_elements; i++) {
                if (i % vecs.size() < vecs.size() - 1) {
                    indices[i] = idx % 2 == 0 ? idx / 2 : idx / 2 + vec_elements * even_vecs.size();
                    idx++;
                }
            }
            Value *even_odd = shuffle_vectors(even, odd, indices);

            // Interleave the last vector into the result.
            last = slice_vector(last, 0, result_elements);
            for (int i = 0; i < result_elements; i++) {
                if (i % vecs.size() < vecs.size() - 1) {
                    indices[i] = i;
                } else {
                    indices[i] = i / vecs.size() + result_elements;
                }
            }

            return shuffle_vectors(even_odd, last, indices);
        } else {
            return interleave_vectors({even, odd});
        }
    }
}

void CodeGen_LLVM::scalarize(const Expr &e) {
    llvm::Type *result_type = llvm_type_of(e.type());

    Value *result = UndefValue::get(result_type);

    for (int i = 0; i < e.type().lanes(); i++) {
        Value *v = codegen(extract_lane(e, i));
        result = builder->CreateInsertElement(result, v, ConstantInt::get(i32_t, i));
    }
    value = result;
}

void CodeGen_LLVM::codegen_predicated_vector_store(const Store *op) {
    const Ramp *ramp = op->index.as<Ramp>();
    if (ramp && is_one(ramp->stride)) {  // Dense vector store
        debug(4) << "Predicated dense vector store\n\t" << Stmt(op) << "\n";
        Value *vpred = codegen(op->predicate);
        Halide::Type value_type = op->value.type();
        Value *val = codegen(op->value);
        bool is_external = (external_buffer.find(op->name) != external_buffer.end());
        int alignment = value_type.bytes();
        int native_bits = native_vector_bits();
        int native_bytes = native_bits / 8;

        // Boost the alignment if possible, up to the native vector width.
        ModulusRemainder mod_rem = op->alignment;
        while ((mod_rem.remainder & 1) == 0 &&
               (mod_rem.modulus & 1) == 0 &&
               alignment < native_bytes) {
            mod_rem.modulus /= 2;
            mod_rem.remainder /= 2;
            alignment *= 2;
        }

        // If it is an external buffer, then we cannot assume that the host pointer
        // is aligned to at least the native vector width. However, we may be able to do
        // better than just assuming that it is unaligned.
        if (is_external && op->param.defined()) {
            int host_alignment = op->param.host_alignment();
            alignment = gcd(alignment, host_alignment);
        }

        // For dense vector stores wider than the native vector
        // width, bust them up into native vectors.
        int store_lanes = value_type.lanes();
        int native_lanes = native_bits / value_type.bits();

        for (int i = 0; i < store_lanes; i += native_lanes) {
            int slice_lanes = std::min(native_lanes, store_lanes - i);
            Expr slice_base = simplify(ramp->base + i);
            Expr slice_stride = make_one(slice_base.type());
            Expr slice_index = slice_lanes == 1 ? slice_base : Ramp::make(slice_base, slice_stride, slice_lanes);
            Value *slice_val = slice_vector(val, i, slice_lanes);
            Value *elt_ptr = codegen_buffer_pointer(op->name, value_type.element_of(), slice_base);
            Value *vec_ptr = builder->CreatePointerCast(elt_ptr, slice_val->getType()->getPointerTo());

            Value *slice_mask = slice_vector(vpred, i, slice_lanes);
#if LLVM_VERSION >= 110
            Instruction *store_inst =
                builder->CreateMaskedStore(slice_val, vec_ptr, make_alignment(alignment), slice_mask);
#else
            Instruction *store_inst =
                builder->CreateMaskedStore(slice_val, vec_ptr, alignment, slice_mask);
#endif
            add_tbaa_metadata(store_inst, op->name, slice_index);
        }
    } else {  // It's not dense vector store, we need to scalarize it
        debug(4) << "Scalarize predicated vector store\n";
        Type value_type = op->value.type().element_of();
        Value *vpred = codegen(op->predicate);
        Value *vval = codegen(op->value);
        Value *vindex = codegen(op->index);
        for (int i = 0; i < op->index.type().lanes(); i++) {
            Constant *lane = ConstantInt::get(i32_t, i);
            Value *p = builder->CreateExtractElement(vpred, lane);
            if (p->getType() != i1_t) {
                p = builder->CreateIsNotNull(p);
            }

            Value *v = builder->CreateExtractElement(vval, lane);
            Value *idx = builder->CreateExtractElement(vindex, lane);
            internal_assert(p && v && idx);

            BasicBlock *true_bb = BasicBlock::Create(*context, "true_bb", function);
            BasicBlock *after_bb = BasicBlock::Create(*context, "after_bb", function);
            builder->CreateCondBr(p, true_bb, after_bb);

            builder->SetInsertPoint(true_bb);

            // Scalar
            Value *ptr = codegen_buffer_pointer(op->name, value_type, idx);
            builder->CreateAlignedStore(v, ptr, make_alignment(value_type.bytes()));

            builder->CreateBr(after_bb);
            builder->SetInsertPoint(after_bb);
        }
    }
}

Value *CodeGen_LLVM::codegen_dense_vector_load(const Load *load, Value *vpred) {
    debug(4) << "Vectorize predicated dense vector load:\n\t" << Expr(load) << "\n";

    const Ramp *ramp = load->index.as<Ramp>();
    internal_assert(ramp && is_one(ramp->stride)) << "Should be dense vector load\n";

    bool is_external = (external_buffer.find(load->name) != external_buffer.end());
    int alignment = load->type.bytes();  // The size of a single element

    int native_bits = native_vector_bits();
    int native_bytes = native_bits / 8;

    // We assume halide_malloc for the platform returns buffers
    // aligned to at least the native vector width. So this is the
    // maximum alignment we can infer based on the index alone.

    // Boost the alignment if possible, up to the native vector width.
    ModulusRemainder mod_rem = load->alignment;
    while ((mod_rem.remainder & 1) == 0 &&
           (mod_rem.modulus & 1) == 0 &&
           alignment < native_bytes) {
        mod_rem.modulus /= 2;
        mod_rem.remainder /= 2;
        alignment *= 2;
    }

    // If it is an external buffer, then we cannot assume that the host pointer
    // is aligned to at least native vector width. However, we may be able to do
    // better than just assuming that it is unaligned.
    if (is_external) {
        if (load->param.defined()) {
            int host_alignment = load->param.host_alignment();
            alignment = gcd(alignment, host_alignment);
        } else if (get_target().has_feature(Target::JIT) && load->image.defined()) {
            // If we're JITting, use the actual pointer value to determine alignment for embedded buffers.
            alignment = gcd(alignment, (int)(((uintptr_t)load->image.data()) & std::numeric_limits<int>::max()));
        }
    }

    // For dense vector loads wider than the native vector
    // width, bust them up into native vectors
    int load_lanes = load->type.lanes();
    int native_lanes = native_bits / load->type.bits();
    vector<Value *> slices;
    for (int i = 0; i < load_lanes; i += native_lanes) {
        int slice_lanes = std::min(native_lanes, load_lanes - i);
        Expr slice_base = simplify(ramp->base + i);
        Expr slice_stride = make_one(slice_base.type());
        Expr slice_index = slice_lanes == 1 ? slice_base : Ramp::make(slice_base, slice_stride, slice_lanes);
        llvm::Type *slice_type = VectorType::get(llvm_type_of(load->type.element_of()), slice_lanes);
        Value *elt_ptr = codegen_buffer_pointer(load->name, load->type.element_of(), slice_base);
        Value *vec_ptr = builder->CreatePointerCast(elt_ptr, slice_type->getPointerTo());

        Instruction *load_inst;
        if (vpred != nullptr) {
            Value *slice_mask = slice_vector(vpred, i, slice_lanes);
#if LLVM_VERSION >= 110
            load_inst = builder->CreateMaskedLoad(vec_ptr, make_alignment(alignment), slice_mask);
#else
            load_inst = builder->CreateMaskedLoad(vec_ptr, alignment, slice_mask);
#endif
        } else {
            load_inst = builder->CreateAlignedLoad(vec_ptr, make_alignment(alignment));
        }
        add_tbaa_metadata(load_inst, load->name, slice_index);
        slices.push_back(load_inst);
    }
    value = concat_vectors(slices);
    return value;
}

void CodeGen_LLVM::codegen_predicated_vector_load(const Load *op) {
    const Ramp *ramp = op->index.as<Ramp>();
    const IntImm *stride = ramp ? ramp->stride.as<IntImm>() : nullptr;

    if (ramp && is_one(ramp->stride)) {  // Dense vector load
        Value *vpred = codegen(op->predicate);
        value = codegen_dense_vector_load(op, vpred);
    } else if (ramp && stride && stride->value == -1) {
        debug(4) << "Predicated dense vector load with stride -1\n\t" << Expr(op) << "\n";
        vector<int> indices(ramp->lanes);
        for (int i = 0; i < ramp->lanes; i++) {
            indices[i] = ramp->lanes - 1 - i;
        }

        // Flip the predicate
        Value *vpred = codegen(op->predicate);
        vpred = shuffle_vectors(vpred, indices);

        // Load the vector and then flip it in-place
        Expr flipped_base = ramp->base - ramp->lanes + 1;
        Expr flipped_stride = make_one(flipped_base.type());
        Expr flipped_index = Ramp::make(flipped_base, flipped_stride, ramp->lanes);
        ModulusRemainder align = op->alignment;
        align = align - (ramp->lanes - 1);

        Expr flipped_load = Load::make(op->type, op->name, flipped_index, op->image,
                                       op->param, const_true(op->type.lanes()), align);

        Value *flipped = codegen_dense_vector_load(flipped_load.as<Load>(), vpred);
        value = shuffle_vectors(flipped, indices);
    } else {  // It's not dense vector load, we need to scalarize it
        Expr load_expr = Load::make(op->type, op->name, op->index, op->image,
                                    op->param, const_true(op->type.lanes()), op->alignment);
        debug(4) << "Scalarize predicated vector load\n\t" << load_expr << "\n";
        Expr pred_load = Call::make(load_expr.type(),
                                    Call::if_then_else,
                                    {op->predicate, load_expr, make_zero(load_expr.type())},
                                    Internal::Call::Intrinsic);
        value = codegen(pred_load);
    }
}

void CodeGen_LLVM::codegen_atomic_store(const Store *op) {
    // TODO: predicated store (see https://github.com/halide/Halide/issues/4298).
    user_assert(is_one(op->predicate)) << "Atomic predicated store is not supported.\n";

    // Detect whether we can describe this as an atomic-read-modify-write,
    // otherwise fallback to a compare-and-swap loop.
    // Currently we only test for atomicAdd.
    Expr val_expr = op->value;
    Halide::Type value_type = op->value.type();
    // For atomicAdd, we check if op->value - store[index] is independent of store.
    // For llvm version < 9, the atomicRMW operations only support integers so we also check that.
    Expr equiv_load = Load::make(value_type, op->name,
                                 op->index,
                                 Buffer<>(),
                                 op->param,
                                 op->predicate,
                                 op->alignment);
    Expr delta = simplify(common_subexpression_elimination(op->value - equiv_load));
    bool is_atomic_add = supports_atomic_add(value_type) && !expr_uses_var(delta, op->name);
    if (is_atomic_add) {
        Value *val = codegen(delta);
        if (value_type.is_scalar()) {
            Value *ptr = codegen_buffer_pointer(op->name,
                                                op->value.type(),
                                                op->index);
            // llvm 9 has FAdd which can be used for atomic floats.
            if (value_type.is_float()) {
                builder->CreateAtomicRMW(AtomicRMWInst::FAdd, ptr, val, AtomicOrdering::Monotonic);
            } else {
                builder->CreateAtomicRMW(AtomicRMWInst::Add, ptr, val, AtomicOrdering::Monotonic);
            }
        } else {
            Value *index = codegen(op->index);
            // Scalarize vector store.
            for (int i = 0; i < value_type.lanes(); i++) {
                Value *lane = ConstantInt::get(i32_t, i);
                Value *idx = builder->CreateExtractElement(index, lane);
                Value *v = builder->CreateExtractElement(val, lane);
                Value *ptr = codegen_buffer_pointer(op->name, value_type.element_of(), idx);
                if (value_type.is_float()) {
                    builder->CreateAtomicRMW(AtomicRMWInst::FAdd, ptr, v, AtomicOrdering::Monotonic);
                } else {
                    builder->CreateAtomicRMW(AtomicRMWInst::Add, ptr, v, AtomicOrdering::Monotonic);
                }
            }
        }
    } else {
        // We want to create the following CAS loop:
        // entry:
        //   %orig = load atomic op->name[op->index]
        //   br label %casloop.start
        // casloop.start:
        //   %cmp = phi [%orig, %entry], [%value_loaded %casloop.start]
        //   %val = ...
        //   %val_success = cmpxchg %ptr, %cmp, %val, monotonic
        //   %val_loaded = extractvalue %val_success, 0
        //   %success = extractvalue %val_success, 1
        //   br %success, label %casloop.end, label %casloop.start
        // casloop.end:
        Value *vec_index = nullptr;
        if (!value_type.is_scalar()) {
            // Precompute index for vector store.
            vec_index = codegen(op->index);
        }
        // Scalarize vector store.
        for (int lane_id = 0; lane_id < value_type.lanes(); lane_id++) {
            LLVMContext &ctx = builder->getContext();
            BasicBlock *bb = builder->GetInsertBlock();
            llvm::Function *f = bb->getParent();
            BasicBlock *loop_bb =
                BasicBlock::Create(ctx, "casloop.start", f);
            // Load the old value for compare and swap test.
            Value *ptr = nullptr;
            if (value_type.is_scalar()) {
                ptr = codegen_buffer_pointer(op->name, value_type, op->index);
            } else {
                Value *idx = builder->CreateExtractElement(vec_index, ConstantInt::get(i32_t, lane_id));
                ptr = codegen_buffer_pointer(op->name, value_type.element_of(), idx);
            }
            LoadInst *orig = builder->CreateAlignedLoad(ptr, make_alignment(value_type.bytes()));
            orig->setOrdering(AtomicOrdering::Monotonic);
            add_tbaa_metadata(orig, op->name, op->index);
            // Explicit fall through from the current block to the cas loop body.
            builder->CreateBr(loop_bb);

            // CAS loop body:
            builder->SetInsertPoint(loop_bb);
            llvm::Type *ptr_type = ptr->getType();
            PHINode *cmp = builder->CreatePHI(ptr_type->getPointerElementType(), 2, "loaded");
            Value *cmp_val = cmp;
            cmp->addIncoming(orig, bb);
            Value *val = nullptr;
            if (value_type.is_scalar()) {
                val = codegen(op->value);
            } else {
                val = codegen(extract_lane(op->value, lane_id));
            }
            llvm::Type *val_type = val->getType();
            bool need_bit_cast = val_type->isFloatingPointTy();
            if (need_bit_cast) {
                IntegerType *int_type = builder->getIntNTy(val_type->getPrimitiveSizeInBits());
                unsigned int addr_space = ptr_type->getPointerAddressSpace();
                ptr = builder->CreateBitCast(ptr, int_type->getPointerTo(addr_space));
                val = builder->CreateBitCast(val, int_type);
                cmp_val = builder->CreateBitCast(cmp_val, int_type);
            }
            Value *cmpxchg_pair = builder->CreateAtomicCmpXchg(
                ptr, cmp_val, val, AtomicOrdering::Monotonic, AtomicOrdering::Monotonic);
            Value *val_loaded = builder->CreateExtractValue(cmpxchg_pair, 0, "val_loaded");
            Value *success = builder->CreateExtractValue(cmpxchg_pair, 1, "success");
            if (need_bit_cast) {
                val_loaded = builder->CreateBitCast(val_loaded, val_type);
            }
            cmp->addIncoming(val_loaded, loop_bb);
            BasicBlock *exit_bb =
                BasicBlock::Create(ctx, "casloop.end", f);
            builder->CreateCondBr(success, exit_bb, loop_bb);
            builder->SetInsertPoint(exit_bb);
        }
    }
}

void CodeGen_LLVM::visit(const Call *op) {
    internal_assert(op->is_extern() || op->is_intrinsic())
        << "Can only codegen extern calls and intrinsics\n";

    // Some call nodes are actually injected at various stages as a
    // cue for llvm to generate particular ops. In general these are
    // handled in the standard library, but ones with e.g. varying
    // types are handled here.
    if (op->is_intrinsic(Call::debug_to_file)) {
        internal_assert(op->args.size() == 3);
        const StringImm *filename = op->args[0].as<StringImm>();
        internal_assert(filename) << "Malformed debug_to_file node\n";
        // Grab the function from the initial module
        llvm::Function *debug_to_file = module->getFunction("halide_debug_to_file");
        internal_assert(debug_to_file) << "Could not find halide_debug_to_file function in initial module\n";

        // Make the filename a global string constant
        Value *user_context = get_user_context();
        Value *char_ptr = codegen(Expr(filename));
        vector<Value *> args = {user_context, char_ptr, codegen(op->args[1])};

        Value *buffer = codegen(op->args[2]);
        buffer = builder->CreatePointerCast(buffer, debug_to_file->getFunctionType()->getParamType(3));
        args.push_back(buffer);

        value = builder->CreateCall(debug_to_file, args);

    } else if (op->is_intrinsic(Call::bitwise_and)) {
        internal_assert(op->args.size() == 2);
        Value *a = codegen(op->args[0]);
        Value *b = codegen(op->args[1]);
        value = builder->CreateAnd(a, b);
    } else if (op->is_intrinsic(Call::bitwise_xor)) {
        internal_assert(op->args.size() == 2);
        Value *a = codegen(op->args[0]);
        Value *b = codegen(op->args[1]);
        value = builder->CreateXor(a, b);
    } else if (op->is_intrinsic(Call::bitwise_or)) {
        internal_assert(op->args.size() == 2);
        Value *a = codegen(op->args[0]);
        Value *b = codegen(op->args[1]);
        value = builder->CreateOr(a, b);
    } else if (op->is_intrinsic(Call::bitwise_not)) {
        internal_assert(op->args.size() == 1);
        Value *a = codegen(op->args[0]);
        value = builder->CreateNot(a);
    } else if (op->is_intrinsic(Call::reinterpret)) {
        internal_assert(op->args.size() == 1);
        Type dst = op->type;
        Type src = op->args[0].type();
        llvm::Type *llvm_dst = llvm_type_of(dst);
        value = codegen(op->args[0]);
        if (src.is_handle() && !dst.is_handle()) {
            internal_assert(dst.is_uint() && dst.bits() == 64);

            // Handle -> UInt64
            llvm::DataLayout d(module.get());
            if (d.getPointerSize() == 4) {
                llvm::Type *intermediate = llvm_type_of(UInt(32, dst.lanes()));
                value = builder->CreatePtrToInt(value, intermediate);
                value = builder->CreateZExt(value, llvm_dst);
            } else if (d.getPointerSize() == 8) {
                value = builder->CreatePtrToInt(value, llvm_dst);
            } else {
                internal_error << "Pointer size is neither 4 nor 8 bytes\n";
            }

        } else if (dst.is_handle() && !src.is_handle()) {
            internal_assert(src.is_uint() && src.bits() == 64);

            // UInt64 -> Handle
            llvm::DataLayout d(module.get());
            if (d.getPointerSize() == 4) {
                llvm::Type *intermediate = llvm_type_of(UInt(32, src.lanes()));
                value = builder->CreateTrunc(value, intermediate);
                value = builder->CreateIntToPtr(value, llvm_dst);
            } else if (d.getPointerSize() == 8) {
                value = builder->CreateIntToPtr(value, llvm_dst);
            } else {
                internal_error << "Pointer size is neither 4 nor 8 bytes\n";
            }

        } else {
            value = builder->CreateBitCast(value, llvm_dst);
        }
    } else if (op->is_intrinsic(Call::shift_left)) {
        internal_assert(op->args.size() == 2);
        Value *a = codegen(op->args[0]);
        Value *b = codegen(op->args[1]);
        if (op->args[1].type().is_uint()) {
            value = builder->CreateShl(a, b);
        } else {
            value = codegen(lower_signed_shift_left(op->args[0], op->args[1]));
        }
    } else if (op->is_intrinsic(Call::shift_right)) {
        internal_assert(op->args.size() == 2);
        Value *a = codegen(op->args[0]);
        Value *b = codegen(op->args[1]);
        if (op->args[1].type().is_uint()) {
            if (op->type.is_int()) {
                value = builder->CreateAShr(a, b);
            } else {
                value = builder->CreateLShr(a, b);
            }
        } else {
            value = codegen(lower_signed_shift_right(op->args[0], op->args[1]));
        }
    } else if (op->is_intrinsic(Call::abs)) {

        internal_assert(op->args.size() == 1);

        // Check if an appropriate vector abs for this type exists in the initial module
        Type t = op->args[0].type();
        string name = (t.is_float() ? "abs_f" : "abs_i") + std::to_string(t.bits());
        llvm::Function *builtin_abs =
            find_vector_runtime_function(name, op->type.lanes()).first;

        if (t.is_vector() && builtin_abs) {
            codegen(Call::make(op->type, name, op->args, Call::Extern));
        } else {
            // Generate select(x >= 0, x, -x) instead
            string x_name = unique_name('x');
            Expr x = Variable::make(op->args[0].type(), x_name);
            value = codegen(Let::make(x_name, op->args[0], select(x >= 0, x, -x)));
        }
    } else if (op->is_intrinsic(Call::absd)) {

        internal_assert(op->args.size() == 2);

        Expr a = op->args[0];
        Expr b = op->args[1];

        // Check if an appropriate vector abs for this type exists in the initial module
        Type t = a.type();
        string name;
        if (t.is_float()) {
            codegen(abs(a - b));
            return;
        } else if (t.is_int()) {
            name = "absd_i" + std::to_string(t.bits());
        } else {
            name = "absd_u" + std::to_string(t.bits());
        }

        llvm::Function *builtin_absd =
            find_vector_runtime_function(name, op->type.lanes()).first;

        if (t.is_vector() && builtin_absd) {
            codegen(Call::make(op->type, name, op->args, Call::Extern));
        } else {
            // Use a select instead
            string a_name = unique_name('a');
            string b_name = unique_name('b');
            Expr a_var = Variable::make(op->args[0].type(), a_name);
            Expr b_var = Variable::make(op->args[1].type(), b_name);
            codegen(Let::make(a_name, op->args[0],
                              Let::make(b_name, op->args[1],
                                        Select::make(a_var < b_var, b_var - a_var, a_var - b_var))));
        }
    } else if (op->is_intrinsic(Call::div_round_to_zero)) {
        internal_assert(op->args.size() == 2);
        Value *a = codegen(op->args[0]);
        Value *b = codegen(op->args[1]);
        if (op->type.is_int()) {
            value = builder->CreateSDiv(a, b);
        } else if (op->type.is_uint()) {
            value = builder->CreateUDiv(a, b);
        } else {
            internal_error << "div_round_to_zero of non-integer type.\n";
        }
    } else if (op->is_intrinsic(Call::mod_round_to_zero)) {
        internal_assert(op->args.size() == 2);
        Value *a = codegen(op->args[0]);
        Value *b = codegen(op->args[1]);
        if (op->type.is_int()) {
            value = builder->CreateSRem(a, b);
        } else if (op->type.is_uint()) {
            value = builder->CreateURem(a, b);
        } else {
            internal_error << "mod_round_to_zero of non-integer type.\n";
        }
    } else if (op->is_intrinsic(Call::mulhi_shr)) {
        internal_assert(op->args.size() == 3);

        Type ty = op->type;
        Type wide_ty = ty.with_bits(ty.bits() * 2);

        Expr p_wide = cast(wide_ty, op->args[0]) * cast(wide_ty, op->args[1]);
        const UIntImm *shift = op->args[2].as<UIntImm>();
        internal_assert(shift != nullptr) << "Third argument to mulhi_shr intrinsic must be an unsigned integer immediate.\n";
        value = codegen(cast(ty, p_wide >> (shift->value + ty.bits())));
    } else if (op->is_intrinsic(Call::sorted_avg)) {
        internal_assert(op->args.size() == 2);
        // b > a, so the following works without widening:
        // a + (b - a)/2
        value = codegen(op->args[0] + (op->args[1] - op->args[0]) / 2);
    } else if (op->is_intrinsic(Call::lerp)) {
        internal_assert(op->args.size() == 3);
        // If we need to upgrade the type, do the entire lerp in the
        // upgraded type for better precision.
        Type t = upgrade_type_for_arithmetic(op->type);
        Type wt = upgrade_type_for_arithmetic(op->args[2].type());
        Expr e = lower_lerp(cast(t, op->args[0]),
                            cast(t, op->args[1]),
                            cast(wt, op->args[2]));
        e = cast(op->type, e);
        codegen(e);
    } else if (op->is_intrinsic(Call::popcount)) {
        internal_assert(op->args.size() == 1);
        std::vector<llvm::Type *> arg_type(1);
        arg_type[0] = llvm_type_of(op->args[0].type());
        llvm::Function *fn = Intrinsic::getDeclaration(module.get(), Intrinsic::ctpop, arg_type);
        Value *a = codegen(op->args[0]);
        CallInst *call = builder->CreateCall(fn, a);
        value = call;
    } else if (op->is_intrinsic(Call::count_leading_zeros) ||
               op->is_intrinsic(Call::count_trailing_zeros)) {
        internal_assert(op->args.size() == 1);
        std::vector<llvm::Type *> arg_type(1);
        arg_type[0] = llvm_type_of(op->args[0].type());
        llvm::Function *fn = Intrinsic::getDeclaration(module.get(),
                                                       (op->is_intrinsic(Call::count_leading_zeros)) ? Intrinsic::ctlz : Intrinsic::cttz,
                                                       arg_type);
        llvm::Value *is_zero_undef = llvm::ConstantInt::getFalse(*context);
        llvm::Value *args[2] = {codegen(op->args[0]), is_zero_undef};
        CallInst *call = builder->CreateCall(fn, args);
        value = call;
    } else if (op->is_intrinsic(Call::return_second)) {
        internal_assert(op->args.size() == 2);
        codegen(op->args[0]);
        value = codegen(op->args[1]);
    } else if (op->is_intrinsic(Call::if_then_else)) {
        Expr cond = op->args[0];
        if (const Broadcast *b = cond.as<Broadcast>()) {
            cond = b->value;
        }
        if (cond.type().is_vector()) {
            scalarize(op);
        } else {

            internal_assert(op->args.size() == 3);

            BasicBlock *true_bb = BasicBlock::Create(*context, "true_bb", function);
            BasicBlock *false_bb = BasicBlock::Create(*context, "false_bb", function);
            BasicBlock *after_bb = BasicBlock::Create(*context, "after_bb", function);
            Value *c = codegen(cond);
            if (c->getType() != i1_t) {
                c = builder->CreateIsNotNull(c);
            }
            builder->CreateCondBr(c, true_bb, false_bb);
            builder->SetInsertPoint(true_bb);
            Value *true_value = codegen(op->args[1]);
            builder->CreateBr(after_bb);
            BasicBlock *true_pred = builder->GetInsertBlock();

            builder->SetInsertPoint(false_bb);
            Value *false_value = codegen(op->args[2]);
            builder->CreateBr(after_bb);
            BasicBlock *false_pred = builder->GetInsertBlock();

            builder->SetInsertPoint(after_bb);
            PHINode *phi = builder->CreatePHI(true_value->getType(), 2);
            phi->addIncoming(true_value, true_pred);
            phi->addIncoming(false_value, false_pred);

            value = phi;
        }
    } else if (op->is_intrinsic(Call::require)) {
        internal_assert(op->args.size() == 3);
        Expr cond = op->args[0];
        if (cond.type().is_vector()) {
            scalarize(op);
        } else {
            Value *c = codegen(cond);
            create_assertion(c, op->args[2]);
            value = codegen(op->args[1]);
        }
    } else if (op->is_intrinsic(Call::make_struct)) {
        if (op->type.is_vector()) {
            // Make a vector of pointers to distinct structs
            scalarize(op);
        } else if (op->args.empty()) {
            // Empty structs can be emitted for arrays of size zero
            // (e.g. the shape of a zero-dimensional buffer). We
            // generate a null in this situation. */
            value = ConstantPointerNull::get(dyn_cast<PointerType>(llvm_type_of(op->type)));
        } else {
            // Codegen each element.
            bool all_same_type = true;
            vector<llvm::Value *> args(op->args.size());
            vector<llvm::Type *> types(op->args.size());
            for (size_t i = 0; i < op->args.size(); i++) {
                args[i] = codegen(op->args[i]);
                types[i] = args[i]->getType();
                all_same_type &= (types[0] == types[i]);
            }

            // Use either a single scalar, a fixed-size array, or a
            // struct. The struct type would always be correct, but
            // the array or scalar type produce slightly simpler IR.
            if (args.size() == 1) {
                value = create_alloca_at_entry(types[0], 1);
                builder->CreateStore(args[0], value);
            } else {
                llvm::Type *aggregate_t = (all_same_type ? (llvm::Type *)ArrayType::get(types[0], types.size()) : (llvm::Type *)StructType::get(*context, types));

                value = create_alloca_at_entry(aggregate_t, 1);
                for (size_t i = 0; i < args.size(); i++) {
                    Value *elem_ptr = builder->CreateConstInBoundsGEP2_32(aggregate_t, value, 0, i);
                    builder->CreateStore(args[i], elem_ptr);
                }
            }
        }

    } else if (op->is_intrinsic(Call::stringify)) {
        internal_assert(!op->args.empty());

        if (op->type.is_vector()) {
            scalarize(op);
        } else {

            // Compute the maximum possible size of the message.
            int buf_size = 1;  // One for the terminating zero.
            for (size_t i = 0; i < op->args.size(); i++) {
                Type t = op->args[i].type();
                if (op->args[i].as<StringImm>()) {
                    buf_size += op->args[i].as<StringImm>()->value.size();
                } else if (t.is_int() || t.is_uint()) {
                    buf_size += 19;  // 2^64 = 18446744073709551616
                } else if (t.is_float()) {
                    if (t.bits() == 32) {
                        buf_size += 47;  // %f format of max negative float
                    } else {
                        buf_size += 14;  // Scientific notation with 6 decimal places.
                    }
                } else if (t == type_of<halide_buffer_t *>()) {
                    // Not a strict upper bound (there isn't one), but ought to be enough for most buffers.
                    buf_size += 512;
                } else {
                    internal_assert(t.is_handle());
                    buf_size += 18;  // 0x0123456789abcdef
                }
            }
            // Round up to a multiple of 16 bytes.
            buf_size = ((buf_size + 15) / 16) * 16;

            // Clamp to at most 8k.
            if (buf_size > 8 * 1024) buf_size = 8 * 1024;

            // Allocate a stack array to hold the message.
            llvm::Value *buf = create_alloca_at_entry(i8_t, buf_size);

            llvm::Value *dst = buf;
            llvm::Value *buf_end = builder->CreateConstGEP1_32(buf, buf_size);

            llvm::Function *append_string = module->getFunction("halide_string_to_string");
            llvm::Function *append_int64 = module->getFunction("halide_int64_to_string");
            llvm::Function *append_uint64 = module->getFunction("halide_uint64_to_string");
            llvm::Function *append_double = module->getFunction("halide_double_to_string");
            llvm::Function *append_pointer = module->getFunction("halide_pointer_to_string");
            llvm::Function *append_buffer = module->getFunction("halide_buffer_to_string");

            internal_assert(append_string);
            internal_assert(append_int64);
            internal_assert(append_uint64);
            internal_assert(append_double);
            internal_assert(append_pointer);
            internal_assert(append_buffer);

            for (size_t i = 0; i < op->args.size(); i++) {
                const StringImm *s = op->args[i].as<StringImm>();
                Type t = op->args[i].type();
                internal_assert(t.lanes() == 1);
                vector<Value *> call_args(2);
                call_args[0] = dst;
                call_args[1] = buf_end;

                if (s) {
                    call_args.push_back(codegen(op->args[i]));
                    dst = builder->CreateCall(append_string, call_args);
                } else if (t.is_bool()) {
                    Value *a = codegen(op->args[i]);
                    Value *t = codegen(StringImm::make("true"));
                    Value *f = codegen(StringImm::make("false"));
                    call_args.push_back(builder->CreateSelect(a, t, f));
                    dst = builder->CreateCall(append_string, call_args);
                } else if (t.is_int()) {
                    call_args.push_back(codegen(Cast::make(Int(64), op->args[i])));
                    call_args.push_back(ConstantInt::get(i32_t, 1));
                    dst = builder->CreateCall(append_int64, call_args);
                } else if (t.is_uint()) {
                    call_args.push_back(codegen(Cast::make(UInt(64), op->args[i])));
                    call_args.push_back(ConstantInt::get(i32_t, 1));
                    dst = builder->CreateCall(append_uint64, call_args);
                } else if (t.is_float()) {
                    call_args.push_back(codegen(Cast::make(Float(64), op->args[i])));
                    // Use scientific notation for doubles
                    call_args.push_back(ConstantInt::get(i32_t, t.bits() == 64 ? 1 : 0));
                    dst = builder->CreateCall(append_double, call_args);
                } else if (t == type_of<halide_buffer_t *>()) {
                    Value *buf = codegen(op->args[i]);
                    buf = builder->CreatePointerCast(buf, append_buffer->getFunctionType()->getParamType(2));
                    call_args.push_back(buf);
                    dst = builder->CreateCall(append_buffer, call_args);
                } else {
                    internal_assert(t.is_handle());
                    call_args.push_back(codegen(op->args[i]));
                    dst = builder->CreateCall(append_pointer, call_args);
                }
            }
            if (get_target().has_feature(Target::MSAN)) {
                // Note that we mark the entire buffer as initialized;
                // it would be more accurate to just mark (dst - buf)
                llvm::Function *annotate = module->getFunction("halide_msan_annotate_memory_is_initialized");
                vector<Value *> annotate_args(3);
                annotate_args[0] = get_user_context();
                annotate_args[1] = buf;
                annotate_args[2] = codegen(Cast::make(Int(64), buf_size));
                builder->CreateCall(annotate, annotate_args);
            }
            value = buf;
        }
    } else if (op->is_intrinsic(Call::memoize_expr)) {
        // Used as an annotation for caching, should be invisible to
        // codegen. Ignore arguments beyond the first as they are only
        // used in the cache key.
        internal_assert(!op->args.empty());
        value = codegen(op->args[0]);
    } else if (op->is_intrinsic(Call::alloca)) {
        // The argument is the number of bytes. For now it must be
        // const, or a call to size_of_halide_buffer_t.
        internal_assert(op->args.size() == 1);

        // We can generate slightly cleaner IR with fewer alignment
        // restrictions if we recognize the most common types we
        // expect to get alloca'd.
        const Call *call = op->args[0].as<Call>();
        if (op->type == type_of<struct halide_buffer_t *>() &&
            call && call->is_intrinsic(Call::size_of_halide_buffer_t)) {
            value = create_alloca_at_entry(halide_buffer_t_type, 1);
        } else {
            const int64_t *sz = as_const_int(op->args[0]);
            internal_assert(sz);
            if (op->type == type_of<struct halide_dimension_t *>()) {
                value = create_alloca_at_entry(dimension_t_type, *sz / sizeof(halide_dimension_t));
            } else {
                // Just use an i8* and make the users bitcast it.
                value = create_alloca_at_entry(i8_t, *sz);
            }
        }
    } else if (op->is_intrinsic(Call::register_destructor)) {
        internal_assert(op->args.size() == 2);
        const StringImm *fn = op->args[0].as<StringImm>();
        internal_assert(fn);
        llvm::Function *f = module->getFunction(fn->value);
        if (!f) {
            llvm::Type *arg_types[] = {i8_t->getPointerTo(), i8_t->getPointerTo()};
            FunctionType *func_t = FunctionType::get(void_t, arg_types, false);
            f = llvm::Function::Create(func_t, llvm::Function::ExternalLinkage, fn->value, module.get());
            f->setCallingConv(CallingConv::C);
        }
        internal_assert(op->args[1].type().is_handle());
        Value *arg = codegen(op->args[1]);
        value = register_destructor(f, arg, Always);
    } else if (op->is_intrinsic(Call::call_cached_indirect_function)) {
        // Arguments to call_cached_indirect_function are of the form
        //
        //    cond_1, "sub_function_name_1",
        //    cond_2, "sub_function_name_2",
        //    ...
        //    cond_N, "sub_function_name_N"
        //
        // This will generate code that corresponds (roughly) to
        //
        //    static FunctionPtr f = []{
        //      if (cond_1) return sub_function_name_1;
        //      if (cond_2) return sub_function_name_2;
        //      ...
        //      if (cond_N) return sub_function_name_N;
        //    }
        //    return f(args)
        //
        // i.e.: the conditions will be evaluated *in order*; the first one
        // evaluating to true will have its corresponding function cached,
        // which will be used to complete this (and all subsequent) calls.
        //
        // The final condition (cond_N) must evaluate to a constant TRUE
        // value (so that the final function will be selected if all others
        // fail); failure to do so will cause unpredictable results.
        //
        // There is currently no way to clear the cached function pointer.
        //
        // It is assumed/required that all of the conditions are "pure"; each
        // must evaluate to the same value (within a given runtime environment)
        // across multiple evaluations.
        //
        // It is assumed/required that all of the sub-functions have arguments
        // (and return values) that are identical to those of this->function.
        //
        // Note that we require >= 4 arguments: fewer would imply
        // only one condition+function pair, which is pointless to use
        // (the function should always be called directly).
        //
        internal_assert(op->args.size() >= 4);
        internal_assert(!(op->args.size() & 1));

        // Gather information we need about each function.
        struct SubFn {
            llvm::Function *fn;
            llvm::GlobalValue *fn_ptr;
            Expr cond;
        };
        vector<SubFn> sub_fns;
        for (size_t i = 0; i < op->args.size(); i += 2) {
            const string sub_fn_name = op->args[i + 1].as<StringImm>()->value;
            string extern_sub_fn_name = sub_fn_name;
            llvm::Function *sub_fn = module->getFunction(sub_fn_name);
            if (!sub_fn) {
                extern_sub_fn_name = get_mangled_names(sub_fn_name,
                                                       LinkageType::External,
                                                       NameMangling::Default,
                                                       current_function_args,
                                                       get_target())
                                         .extern_name;
                debug(1) << "Did not find function " << sub_fn_name
                         << ", assuming extern \"C\" " << extern_sub_fn_name << "\n";
                vector<llvm::Type *> arg_types;
                for (const auto &arg : function->args()) {
                    arg_types.push_back(arg.getType());
                }
                llvm::Type *result_type = llvm_type_of(upgrade_type_for_argument_passing(op->type));
                FunctionType *func_t = FunctionType::get(result_type, arg_types, false);
                sub_fn = llvm::Function::Create(func_t, llvm::Function::ExternalLinkage,
                                                extern_sub_fn_name, module.get());
                sub_fn->setCallingConv(CallingConv::C);
            }

            llvm::GlobalValue *sub_fn_ptr = module->getNamedValue(extern_sub_fn_name);
            if (!sub_fn_ptr) {
                debug(1) << "Did not find function ptr " << extern_sub_fn_name << ", assuming extern \"C\".\n";
                sub_fn_ptr = new GlobalVariable(*module, sub_fn->getType(),
                                                /*isConstant*/ true, GlobalValue::ExternalLinkage,
                                                /*initializer*/ nullptr, extern_sub_fn_name);
            }
            auto cond = op->args[i];
            sub_fns.push_back({sub_fn, sub_fn_ptr, cond});
        }

        // Create a null-initialized global to track this object.
        const auto base_fn = sub_fns.back().fn;
        const string global_name = unique_name(base_fn->getName().str() + "_indirect_fn_ptr");
        GlobalVariable *global = new GlobalVariable(
            *module,
            base_fn->getType(),
            /*isConstant*/ false,
            GlobalValue::PrivateLinkage,
            ConstantPointerNull::get(base_fn->getType()),
            global_name);
        LoadInst *loaded_value = builder->CreateLoad(global);

        BasicBlock *global_inited_bb = BasicBlock::Create(*context, "global_inited_bb", function);
        BasicBlock *global_not_inited_bb = BasicBlock::Create(*context, "global_not_inited_bb", function);
        BasicBlock *call_fn_bb = BasicBlock::Create(*context, "call_fn_bb", function);

        // Only init the global if not already inited.
        //
        // Note that we deliberately do not attempt to make this threadsafe via (e.g.) mutexes;
        // the requirements of the conditions above mean that multiple writes *should* only
        // be able to re-write the same value, which is harmless for our purposes, and
        // avoiding such code simplifies and speeds the resulting code.
        //
        // (Note that if we ever need to add a way to clear the cached function pointer,
        // we may need to reconsider this, to avoid amusingly horrible race conditions.)
        builder->CreateCondBr(builder->CreateIsNotNull(loaded_value),
                              global_inited_bb, global_not_inited_bb, very_likely_branch);

        // Build the not-already-inited case
        builder->SetInsertPoint(global_not_inited_bb);
        llvm::Value *selected_value = nullptr;
        for (int i = sub_fns.size() - 1; i >= 0; i--) {
            const auto sub_fn = sub_fns[i];
            if (!selected_value) {
                selected_value = sub_fn.fn_ptr;
            } else {
                Value *c = codegen(sub_fn.cond);
                selected_value = builder->CreateSelect(c, sub_fn.fn_ptr, selected_value);
            }
        }
        builder->CreateStore(selected_value, global);
        builder->CreateBr(call_fn_bb);

        // Just an incoming edge for the Phi node
        builder->SetInsertPoint(global_inited_bb);
        builder->CreateBr(call_fn_bb);

        builder->SetInsertPoint(call_fn_bb);
        PHINode *phi = builder->CreatePHI(selected_value->getType(), 2);
        phi->addIncoming(selected_value, global_not_inited_bb);
        phi->addIncoming(loaded_value, global_inited_bb);

        std::vector<llvm::Value *> call_args;
        for (auto &arg : function->args()) {
            call_args.push_back(&arg);
        }

        llvm::CallInst *call = builder->CreateCall(base_fn->getFunctionType(), phi, call_args);
        value = call;
    } else if (op->is_intrinsic(Call::prefetch)) {
        user_assert((op->args.size() == 4) && is_one(op->args[2]))
            << "Only prefetch of 1 cache line is supported.\n";

        llvm::Function *prefetch_fn = module->getFunction("_halide_prefetch");
        internal_assert(prefetch_fn);

        vector<llvm::Value *> args;
        args.push_back(codegen_buffer_pointer(codegen(op->args[0]), op->type, op->args[1]));
        // The first argument is a pointer, which has type i8*. We
        // need to cast the argument, which might be a pointer to a
        // different type.
        llvm::Type *ptr_type = prefetch_fn->getFunctionType()->params()[0];
        args[0] = builder->CreateBitCast(args[0], ptr_type);

        value = builder->CreateCall(prefetch_fn, args);

    } else if (op->is_intrinsic(Call::signed_integer_overflow)) {
        user_error << "Signed integer overflow occurred during constant-folding. Signed"
                      " integer overflow for int32 and int64 is undefined behavior in"
                      " Halide.\n";
    } else if (op->is_intrinsic(Call::undef)) {
        value = UndefValue::get(llvm_type_of(op->type));
    } else if (op->is_intrinsic(Call::size_of_halide_buffer_t)) {
        llvm::DataLayout d(module.get());
        value = ConstantInt::get(i32_t, (int)d.getTypeAllocSize(halide_buffer_t_type));
    } else if (op->is_intrinsic(Call::strict_float)) {
        IRBuilder<llvm::ConstantFolder, llvm::IRBuilderDefaultInserter>::FastMathFlagGuard guard(*builder);
        llvm::FastMathFlags safe_flags;
        safe_flags.clear();
        builder->setFastMathFlags(safe_flags);
        builder->setDefaultFPMathTag(strict_fp_math_md);
        value = codegen(op->args[0]);
    } else if (is_float16_transcendental(op)) {
        value = codegen(lower_float16_transcendental_to_float32_equivalent(op));
    } else if (op->is_intrinsic()) {
        internal_error << "Unknown intrinsic: " << op->name << "\n";
    } else if (op->call_type == Call::PureExtern && op->name == "pow_f32") {
        internal_assert(op->args.size() == 2);
        Expr x = op->args[0];
        Expr y = op->args[1];
        Halide::Expr abs_x_pow_y = Internal::halide_exp(Internal::halide_log(abs(x)) * y);
        Halide::Expr nan_expr = Call::make(x.type(), "nan_f32", {}, Call::PureExtern);
        Expr iy = floor(y);
        Expr one = make_one(x.type());
        Expr zero = make_zero(x.type());
        Expr e = select(x > 0, abs_x_pow_y,        // Strictly positive x
                        y == 0.0f, one,            // x^0 == 1
                        x == 0.0f, zero,           // 0^y == 0
                        y != iy, nan_expr,         // negative x to a non-integer power
                        iy % 2 == 0, abs_x_pow_y,  // negative x to an even power
                        -abs_x_pow_y);             // negative x to an odd power
        e = common_subexpression_elimination(e);
        e.accept(this);
    } else if (op->call_type == Call::PureExtern && op->name == "log_f32") {
        internal_assert(op->args.size() == 1);
        Expr e = Internal::halide_log(op->args[0]);
        e.accept(this);
    } else if (op->call_type == Call::PureExtern && op->name == "exp_f32") {
        internal_assert(op->args.size() == 1);
        Expr e = Internal::halide_exp(op->args[0]);
        e.accept(this);
    } else if (op->call_type == Call::PureExtern &&
               (op->name == "is_nan_f32" || op->name == "is_nan_f64")) {
        internal_assert(op->args.size() == 1);
        Value *a = codegen(op->args[0]);

        /* NaNs are not supposed to exist in "no NaNs" compilation
         * mode, but it appears llvm special cases the unordered
         * compare instruction when the global NoNaNsFPMath option is
         * set and still checks for a NaN. However if the nnan flag is
         * set on the instruction itself, llvm treats the comparison
         * as always false. Thus we always turn off the per-instruction
         * fast-math flags for this instruction. I.e. it is always
         * treated as strict. Note that compilation may still be in
         * fast-math mode due to global options, but that's ok due to
         * the aforementioned special casing. */
        IRBuilder<llvm::ConstantFolder, llvm::IRBuilderDefaultInserter>::FastMathFlagGuard guard(*builder);
        llvm::FastMathFlags safe_flags;
        safe_flags.clear();
        builder->setFastMathFlags(safe_flags);
        builder->setDefaultFPMathTag(strict_fp_math_md);

        value = builder->CreateFCmpUNO(a, a);
    } else if (op->call_type == Call::PureExtern &&
               (op->name == "is_inf_f32" || op->name == "is_inf_f64")) {
        internal_assert(op->args.size() == 1);

        IRBuilder<llvm::ConstantFolder, llvm::IRBuilderDefaultInserter>::FastMathFlagGuard guard(*builder);
        llvm::FastMathFlags safe_flags;
        safe_flags.clear();
        builder->setFastMathFlags(safe_flags);
        builder->setDefaultFPMathTag(strict_fp_math_md);

        // isinf(e) -> (fabs(e) == infinity)
        Expr e = op->args[0];
        internal_assert(e.type().is_float());
        Expr inf = e.type().max();
        codegen(abs(e) == inf);
    } else if (op->call_type == Call::PureExtern &&
               (op->name == "is_finite_f32" || op->name == "is_finite_f64")) {
        internal_assert(op->args.size() == 1);
        internal_assert(op->args[0].type().is_float());

        IRBuilder<llvm::ConstantFolder, llvm::IRBuilderDefaultInserter>::FastMathFlagGuard guard(*builder);
        llvm::FastMathFlags safe_flags;
        safe_flags.clear();
        builder->setFastMathFlags(safe_flags);
        builder->setDefaultFPMathTag(strict_fp_math_md);

        // isfinite(e) -> (fabs(e) != infinity && !isnan(e)) -> (fabs(e) != infinity && e == e)
        Expr e = op->args[0];
        internal_assert(e.type().is_float());
        Expr inf = e.type().max();
        codegen(abs(e) != inf && e == e);
    } else {
        // It's an extern call.

        std::string name;
        if (op->call_type == Call::ExternCPlusPlus) {
            user_assert(get_target().has_feature(Target::CPlusPlusMangling)) << "Target must specify C++ name mangling (\"c_plus_plus_name_mangling\") in order to call C++ externs. (" << op->name << ")\n";

            std::vector<std::string> namespaces;
            name = extract_namespaces(op->name, namespaces);
            std::vector<ExternFuncArgument> mangle_args;
            for (const auto &arg : op->args) {
                mangle_args.emplace_back(arg);
            }
            name = cplusplus_function_mangled_name(name, namespaces, op->type, mangle_args, get_target());
        } else {
            name = op->name;
        }

        // Codegen the args
        vector<Value *> args(op->args.size());
        for (size_t i = 0; i < op->args.size(); i++) {
            args[i] = codegen(op->args[i]);
        }

        llvm::Function *fn = module->getFunction(name);

        llvm::Type *result_type = llvm_type_of(upgrade_type_for_argument_passing(op->type));

        // Add a user context arg as needed. It's never a vector.
        bool takes_user_context = function_takes_user_context(op->name);
        if (takes_user_context) {
            internal_assert(fn) << "External function " << op->name << " is marked as taking user_context, but is not in the runtime module. Check if runtime_api.cpp needs to be rebuilt.\n";
            debug(4) << "Adding user_context to " << op->name << " args\n";
            args.insert(args.begin(), get_user_context());
        }

        // If we can't find it, declare it extern "C"
        if (!fn) {
            vector<llvm::Type *> arg_types(args.size());
            for (size_t i = 0; i < args.size(); i++) {
                arg_types[i] = args[i]->getType();
                if (arg_types[i]->isVectorTy()) {
                    VectorType *vt = dyn_cast<VectorType>(arg_types[i]);
                    arg_types[i] = vt->getElementType();
                }
            }

            llvm::Type *scalar_result_type = result_type;
            if (result_type->isVectorTy()) {
                VectorType *vt = dyn_cast<VectorType>(result_type);
                scalar_result_type = vt->getElementType();
            }

            FunctionType *func_t = FunctionType::get(scalar_result_type, arg_types, false);

            fn = llvm::Function::Create(func_t, llvm::Function::ExternalLinkage, name, module.get());
            fn->setCallingConv(CallingConv::C);
            debug(4) << "Did not find " << op->name << ". Declared it extern \"C\".\n";
        } else {
            debug(4) << "Found " << op->name << "\n";

            // TODO: Say something more accurate here as there is now
            // partial information in the handle_type field, but it is
            // not clear it can be matched to the LLVM types and it is
            // not always there.
            // Halide's type system doesn't preserve pointer types
            // correctly (they just get called "Handle()"), so we may
            // need to pointer cast to the appropriate type. Only look at
            // fixed params (not varags) in llvm function.
            FunctionType *func_t = fn->getFunctionType();
            for (size_t i = takes_user_context ? 1 : 0;
                 i < std::min(args.size(), (size_t)(func_t->getNumParams()));
                 i++) {
                Expr halide_arg = takes_user_context ? op->args[i - 1] : op->args[i];
                if (halide_arg.type().is_handle()) {
                    llvm::Type *t = func_t->getParamType(i);

                    // Widen to vector-width as needed. If the
                    // function doesn't actually take a vector,
                    // individual lanes will be extracted below.
                    if (halide_arg.type().is_vector() &&
                        !t->isVectorTy()) {
                        t = VectorType::get(t, halide_arg.type().lanes());
                    }

                    if (t != args[i]->getType()) {
                        debug(4) << "Pointer casting argument to extern call: "
                                 << halide_arg << "\n";
                        args[i] = builder->CreatePointerCast(args[i], t);
                    }
                }
            }
        }

        if (op->type.is_scalar()) {
            CallInst *call = builder->CreateCall(fn, args);
            if (op->is_pure()) {
                call->setDoesNotAccessMemory();
            }
            call->setDoesNotThrow();
            value = call;
        } else {

            // Check if a vector version of the function already
            // exists at some useful width.
            pair<llvm::Function *, int> vec =
                find_vector_runtime_function(name, op->type.lanes());
            llvm::Function *vec_fn = vec.first;
            int w = vec.second;

            if (vec_fn) {
                value = call_intrin(llvm_type_of(op->type), w,
                                    get_llvm_function_name(vec_fn), args);
            } else {

                // No vector version found. Scalarize. Extract each simd
                // lane in turn and do one scalar call to the function.
                value = UndefValue::get(result_type);
                for (int i = 0; i < op->type.lanes(); i++) {
                    Value *idx = ConstantInt::get(i32_t, i);
                    vector<Value *> arg_lane(args.size());
                    for (size_t j = 0; j < args.size(); j++) {
                        if (args[j]->getType()->isVectorTy()) {
                            arg_lane[j] = builder->CreateExtractElement(args[j], idx);
                        } else {
                            arg_lane[j] = args[j];
                        }
                    }
                    CallInst *call = builder->CreateCall(fn, arg_lane);
                    if (op->is_pure()) {
                        call->setDoesNotAccessMemory();
                    }
                    call->setDoesNotThrow();
                    if (!call->getType()->isVoidTy()) {
                        value = builder->CreateInsertElement(value, call, idx);
                    }  // otherwise leave it as undef.
                }
            }
        }
    }
}

void CodeGen_LLVM::visit(const Prefetch *op) {
    internal_error << "Prefetch encountered during codegen\n";
}

void CodeGen_LLVM::visit(const Let *op) {
    sym_push(op->name, codegen(op->value));
    value = codegen(op->body);
    sym_pop(op->name);
}

void CodeGen_LLVM::visit(const LetStmt *op) {
    sym_push(op->name, codegen(op->value));
    codegen(op->body);
    sym_pop(op->name);
}

void CodeGen_LLVM::visit(const AssertStmt *op) {
    create_assertion(codegen(op->condition), op->message);
}

Constant *CodeGen_LLVM::create_string_constant(const string &s) {
    map<string, Constant *>::iterator iter = string_constants.find(s);
    if (iter == string_constants.end()) {
        vector<char> data;
        data.reserve(s.size() + 1);
        data.insert(data.end(), s.begin(), s.end());
        data.push_back(0);
        Constant *val = create_binary_blob(data, "str");
        string_constants[s] = val;
        return val;
    } else {
        return iter->second;
    }
}

Constant *CodeGen_LLVM::create_binary_blob(const vector<char> &data, const string &name, bool constant) {
    internal_assert(!data.empty());
    llvm::Type *type = ArrayType::get(i8_t, data.size());
    GlobalVariable *global = new GlobalVariable(*module, type,
                                                constant, GlobalValue::PrivateLinkage,
                                                0, name);
    ArrayRef<unsigned char> data_array((const unsigned char *)&data[0], data.size());
    global->setInitializer(ConstantDataArray::get(*context, data_array));
    size_t alignment = 32;
    size_t native_vector_bytes = (size_t)(native_vector_bits() / 8);
    if (data.size() > alignment && native_vector_bytes > alignment) {
        alignment = native_vector_bytes;
    }
    global->setAlignment(make_alignment(alignment));

    Constant *zero = ConstantInt::get(i32_t, 0);
    Constant *zeros[] = {zero, zero};
    Constant *ptr = ConstantExpr::getInBoundsGetElementPtr(type, global, zeros);
    return ptr;
}

void CodeGen_LLVM::create_assertion(Value *cond, const Expr &message, llvm::Value *error_code) {

    internal_assert(!message.defined() || message.type() == Int(32))
        << "Assertion result is not an int: " << message;

    if (target.has_feature(Target::NoAsserts)) return;

    // If the condition is a vector, fold it down to a scalar
    VectorType *vt = dyn_cast<VectorType>(cond->getType());
    if (vt) {
        Value *scalar_cond = builder->CreateExtractElement(cond, ConstantInt::get(i32_t, 0));
        for (unsigned i = 1; i < vt->getNumElements(); i++) {
            Value *lane = builder->CreateExtractElement(cond, ConstantInt::get(i32_t, i));
            scalar_cond = builder->CreateAnd(scalar_cond, lane);
        }
        cond = scalar_cond;
    }

    // Make a new basic block for the assert
    BasicBlock *assert_fails_bb = BasicBlock::Create(*context, "assert failed", function);
    BasicBlock *assert_succeeds_bb = BasicBlock::Create(*context, "assert succeeded", function);

    // If the condition fails, enter the assert body, otherwise, enter the block after
    builder->CreateCondBr(cond, assert_succeeds_bb, assert_fails_bb, very_likely_branch);

    // Build the failure case
    builder->SetInsertPoint(assert_fails_bb);

    // Call the error handler
    if (!error_code) error_code = codegen(message);

    return_with_error_code(error_code);

    // Continue on using the success case
    builder->SetInsertPoint(assert_succeeds_bb);
}

void CodeGen_LLVM::return_with_error_code(llvm::Value *error_code) {
    // Branch to the destructor block, which cleans up and then bails out.
    BasicBlock *dtors = get_destructor_block();

    // Hook up our error code to the phi node that the destructor block starts with.
    PHINode *phi = dyn_cast<PHINode>(dtors->begin());
    internal_assert(phi) << "The destructor block is supposed to start with a phi node\n";
    phi->addIncoming(error_code, builder->GetInsertBlock());

    builder->CreateBr(get_destructor_block());
}

void CodeGen_LLVM::visit(const ProducerConsumer *op) {
    string name;
    if (op->is_producer) {
        name = std::string("produce ") + op->name;
    } else {
        name = std::string("consume ") + op->name;
    }
    BasicBlock *produce = BasicBlock::Create(*context, name, function);
    builder->CreateBr(produce);
    builder->SetInsertPoint(produce);
    codegen(op->body);
}

void CodeGen_LLVM::visit(const For *op) {
    Value *min = codegen(op->min);
    Value *extent = codegen(op->extent);
    const Acquire *acquire = op->body.as<Acquire>();

    if (op->for_type == ForType::Parallel ||
        (op->for_type == ForType::Serial &&
         acquire &&
         !expr_uses_var(acquire->count, op->name))) {
        do_as_parallel_task(op);
    } else if (op->for_type == ForType::Serial) {

        Value *max = builder->CreateNSWAdd(min, extent);

        BasicBlock *preheader_bb = builder->GetInsertBlock();

        // Make a new basic block for the loop
        BasicBlock *loop_bb = BasicBlock::Create(*context, std::string("for ") + op->name, function);
        // Create the block that comes after the loop
        BasicBlock *after_bb = BasicBlock::Create(*context, std::string("end for ") + op->name, function);

        // If min < max, fall through to the loop bb
        Value *enter_condition = builder->CreateICmpSLT(min, max);
        builder->CreateCondBr(enter_condition, loop_bb, after_bb, very_likely_branch);
        builder->SetInsertPoint(loop_bb);

        // Make our phi node.
        PHINode *phi = builder->CreatePHI(i32_t, 2);
        phi->addIncoming(min, preheader_bb);

        // Within the loop, the variable is equal to the phi value
        sym_push(op->name, phi);

        // Emit the loop body
        codegen(op->body);

        // Update the counter
        Value *next_var = builder->CreateNSWAdd(phi, ConstantInt::get(i32_t, 1));

        // Add the back-edge to the phi node
        phi->addIncoming(next_var, builder->GetInsertBlock());

        // Maybe exit the loop
        Value *end_condition = builder->CreateICmpNE(next_var, max);
        builder->CreateCondBr(end_condition, loop_bb, after_bb);

        builder->SetInsertPoint(after_bb);

        // Pop the loop variable from the scope
        sym_pop(op->name);
    } else {
        internal_error << "Unknown type of For node. Only Serial and Parallel For nodes should survive down to codegen.\n";
    }
}

void CodeGen_LLVM::do_parallel_tasks(const vector<ParallelTask> &tasks) {
    Closure closure;
    for (const auto &t : tasks) {
        Stmt s = t.body;
        if (!t.loop_var.empty()) {
            s = LetStmt::make(t.loop_var, 0, s);
        }
        s.accept(&closure);
    }

    // Allocate a closure
    StructType *closure_t = build_closure_type(closure, halide_buffer_t_type, context);
    Value *closure_ptr = create_alloca_at_entry(closure_t, 1);

    // Fill in the closure
    pack_closure(closure_t, closure_ptr, closure, symbol_table, halide_buffer_t_type, builder);

    closure_ptr = builder->CreatePointerCast(closure_ptr, i8_t->getPointerTo());

    int num_tasks = (int)tasks.size();

    // Make space on the stack for the tasks
    llvm::Value *task_stack_ptr = create_alloca_at_entry(parallel_task_t_type, num_tasks);

    llvm::Type *args_t[] = {i8_t->getPointerTo(), i32_t, i8_t->getPointerTo()};
    FunctionType *task_t = FunctionType::get(i32_t, args_t, false);
    llvm::Type *loop_args_t[] = {i8_t->getPointerTo(), i32_t, i32_t, i8_t->getPointerTo(), i8_t->getPointerTo()};
    FunctionType *loop_task_t = FunctionType::get(i32_t, loop_args_t, false);

    Value *result = nullptr;

    for (int i = 0; i < num_tasks; i++) {
        ParallelTask t = tasks[i];

        // Analyze the task body
        class MayBlock : public IRVisitor {
            using IRVisitor::visit;
            void visit(const Acquire *op) override {
                result = true;
            }

        public:
            bool result = false;
        };

        // TODO(zvookin|abadams): This makes multiple passes over the
        // IR to cover each node. (One tree walk produces the min
        // thread count for all nodes, but we redo each subtree when
        // compiling a given node.) Ideally we'd move to a lowering pass
        // that converts our parallelism constructs to Call nodes, or
        // direct hardware operations in some cases.
        // Also, this code has to exactly mirror the logic in get_parallel_tasks.
        // It would be better to do one pass on the tree and centralize the task
        // deduction logic in one place.
        class MinThreads : public IRVisitor {
            using IRVisitor::visit;

            std::pair<Stmt, int> skip_acquires(Stmt first) {
                int count = 0;
                while (first.defined()) {
                    const Acquire *acq = first.as<Acquire>();
                    if (acq == nullptr) {
                        break;
                    }
                    count++;
                    first = acq->body;
                }
                return {first, count};
            }

            void visit(const Fork *op) override {
                int total_threads = 0;
                int direct_acquires = 0;
                // Take the sum of min threads across all
                // cascaded Fork nodes.
                const Fork *node = op;
                while (node != NULL) {
                    result = 0;
                    auto after_acquires = skip_acquires(node->first);
                    direct_acquires += after_acquires.second;

                    after_acquires.first.accept(this);
                    total_threads += result;

                    const Fork *continued_branches = node->rest.as<Fork>();
                    if (continued_branches == NULL) {
                        result = 0;
                        after_acquires = skip_acquires(node->rest);
                        direct_acquires += after_acquires.second;
                        after_acquires.first.accept(this);
                        total_threads += result;
                    }
                    node = continued_branches;
                }
                if (direct_acquires == 0 && total_threads == 0) {
                    result = 0;
                } else {
                    result = total_threads + 1;
                }
            }

            void visit(const For *op) override {
                result = 0;

                if (op->for_type == ForType::Parallel) {
                    IRVisitor::visit(op);
                    if (result > 0) {
                        result += 1;
                    }
                } else if (op->for_type == ForType::Serial) {
                    auto after_acquires = skip_acquires(op->body);
                    if (after_acquires.second > 0 &&
                        !expr_uses_var(op->body.as<Acquire>()->count, op->name)) {
                        after_acquires.first.accept(this);
                        result++;
                    } else {
                        IRVisitor::visit(op);
                    }
                } else {
                    IRVisitor::visit(op);
                }
            }

            // This is a "standalone" Acquire and will result in its own task.
            // Treat it requiring one more thread than its body.
            void visit(const Acquire *op) override {
                result = 0;
                auto after_inner_acquires = skip_acquires(op);
                after_inner_acquires.first.accept(this);
                result = result + 1;
            }

            void visit(const Block *op) override {
                result = 0;
                op->first.accept(this);
                int result_first = result;
                result = 0;
                op->rest.accept(this);
                result = std::max(result, result_first);
            }

        public:
            int result = 0;
        };
        MinThreads min_threads;
        t.body.accept(&min_threads);

        // Decide if we're going to call do_par_for or
        // do_parallel_tasks. halide_do_par_for is simpler, but
        // assumes a bunch of things. Programs that don't use async
        // can also enter the task system via do_par_for.
        Value *task_parent = sym_get("__task_parent", false);
        bool use_do_par_for = (num_tasks == 1 &&
                               min_threads.result == 0 &&
                               t.semaphores.empty() &&
                               !task_parent);

        // Make the array of semaphore acquisitions this task needs to do before it runs.
        Value *semaphores;
        Value *num_semaphores = ConstantInt::get(i32_t, (int)t.semaphores.size());
        if (!t.semaphores.empty()) {
            semaphores = create_alloca_at_entry(semaphore_acquire_t_type, (int)t.semaphores.size());
            for (int i = 0; i < (int)t.semaphores.size(); i++) {
                Value *semaphore = codegen(t.semaphores[i].semaphore);
                semaphore = builder->CreatePointerCast(semaphore, semaphore_t_type->getPointerTo());
                Value *count = codegen(t.semaphores[i].count);
                Value *slot_ptr = builder->CreateConstGEP2_32(semaphore_acquire_t_type, semaphores, i, 0);
                builder->CreateStore(semaphore, slot_ptr);
                slot_ptr = builder->CreateConstGEP2_32(semaphore_acquire_t_type, semaphores, i, 1);
                builder->CreateStore(count, slot_ptr);
            }
        } else {
            semaphores = ConstantPointerNull::get(semaphore_acquire_t_type->getPointerTo());
        }

        FunctionType *fn_type = use_do_par_for ? task_t : loop_task_t;
        int closure_arg_idx = use_do_par_for ? 2 : 3;

        // Make a new function that does the body
        llvm::Function *containing_function = function;
        function = llvm::Function::Create(fn_type, llvm::Function::InternalLinkage,
                                          t.name, module.get());

        llvm::Value *task_ptr = builder->CreatePointerCast(function, fn_type->getPointerTo());

        function->addParamAttr(closure_arg_idx, Attribute::NoAlias);

        set_function_attributes_for_target(function, target);

        // Make the initial basic block and jump the builder into the new function
        IRBuilderBase::InsertPoint call_site = builder->saveIP();
        BasicBlock *block = BasicBlock::Create(*context, "entry", function);
        builder->SetInsertPoint(block);

        // Save the destructor block
        BasicBlock *parent_destructor_block = destructor_block;
        destructor_block = nullptr;

        // Make a new scope to use
        Scope<Value *> saved_symbol_table;
        symbol_table.swap(saved_symbol_table);

        // Get the function arguments

        // The user context is first argument of the function; it's
        // important that we override the name to be "__user_context",
        // since the LLVM function has a random auto-generated name for
        // this argument.
        llvm::Function::arg_iterator iter = function->arg_begin();
        sym_push("__user_context", iterator_to_pointer(iter));

        if (use_do_par_for) {
            // Next is the loop variable.
            ++iter;
            sym_push(t.loop_var, iterator_to_pointer(iter));
        } else if (!t.loop_var.empty()) {
            // We peeled off a loop. Wrap a new loop around the body
            // that just does the slice given by the arguments.
            string loop_min_name = unique_name('t');
            string loop_extent_name = unique_name('t');
            t.body = For::make(t.loop_var,
                               Variable::make(Int(32), loop_min_name),
                               Variable::make(Int(32), loop_extent_name),
                               ForType::Serial,
                               DeviceAPI::None,
                               t.body);
            ++iter;
            sym_push(loop_min_name, iterator_to_pointer(iter));
            ++iter;
            sym_push(loop_extent_name, iterator_to_pointer(iter));
        } else {
            // This task is not any kind of loop, so skip these args.
            ++iter;
            ++iter;
        }

        // The closure pointer is either the last (for halide_do_par_for) or
        // second to last argument (for halide_do_parallel_tasks).
        ++iter;
        iter->setName("closure");
        Value *closure_handle = builder->CreatePointerCast(iterator_to_pointer(iter),
                                                           closure_t->getPointerTo());

        // Load everything from the closure into the new scope
        unpack_closure(closure, symbol_table, closure_t, closure_handle, builder);

        if (!use_do_par_for) {
            // For halide_do_parallel_tasks the threading runtime task parent
            // is the last argument.
            ++iter;
            iter->setName("task_parent");
            sym_push("__task_parent", iterator_to_pointer(iter));
        }

        // Generate the new function body
        codegen(t.body);

        // Return success
        return_with_error_code(ConstantInt::get(i32_t, 0));

        // Move the builder back to the main function.
        builder->restoreIP(call_site);

        // Now restore the scope
        symbol_table.swap(saved_symbol_table);
        function = containing_function;

        // Restore the destructor block
        destructor_block = parent_destructor_block;

        Value *min = codegen(t.min);
        Value *extent = codegen(t.extent);
        Value *serial = codegen(cast(UInt(8), t.serial));

        if (use_do_par_for) {
            llvm::Function *do_par_for = module->getFunction("halide_do_par_for");
            internal_assert(do_par_for) << "Could not find halide_do_par_for in initial module\n";
            do_par_for->addParamAttr(4, Attribute::NoAlias);
            Value *args[] = {get_user_context(), task_ptr, min, extent, closure_ptr};
            debug(4) << "Creating call to do_par_for\n";
            result = builder->CreateCall(do_par_for, args);
        } else {
            // Populate the task struct
            Value *slot_ptr = builder->CreateConstGEP2_32(parallel_task_t_type, task_stack_ptr, i, 0);
            builder->CreateStore(task_ptr, slot_ptr);
            slot_ptr = builder->CreateConstGEP2_32(parallel_task_t_type, task_stack_ptr, i, 1);
            builder->CreateStore(closure_ptr, slot_ptr);
            slot_ptr = builder->CreateConstGEP2_32(parallel_task_t_type, task_stack_ptr, i, 2);
            builder->CreateStore(create_string_constant(t.name), slot_ptr);
            slot_ptr = builder->CreateConstGEP2_32(parallel_task_t_type, task_stack_ptr, i, 3);
            builder->CreateStore(semaphores, slot_ptr);
            slot_ptr = builder->CreateConstGEP2_32(parallel_task_t_type, task_stack_ptr, i, 4);
            builder->CreateStore(num_semaphores, slot_ptr);
            slot_ptr = builder->CreateConstGEP2_32(parallel_task_t_type, task_stack_ptr, i, 5);
            builder->CreateStore(min, slot_ptr);
            slot_ptr = builder->CreateConstGEP2_32(parallel_task_t_type, task_stack_ptr, i, 6);
            builder->CreateStore(extent, slot_ptr);
            slot_ptr = builder->CreateConstGEP2_32(parallel_task_t_type, task_stack_ptr, i, 7);
            builder->CreateStore(ConstantInt::get(i32_t, min_threads.result), slot_ptr);
            slot_ptr = builder->CreateConstGEP2_32(parallel_task_t_type, task_stack_ptr, i, 8);
            builder->CreateStore(serial, slot_ptr);
        }
    }

    if (!result) {
        llvm::Function *do_parallel_tasks = module->getFunction("halide_do_parallel_tasks");
        internal_assert(do_parallel_tasks) << "Could not find halide_do_parallel_tasks in initial module\n";
        do_parallel_tasks->addParamAttr(2, Attribute::NoAlias);
        Value *task_parent = sym_get("__task_parent", false);
        if (!task_parent) {
            task_parent = ConstantPointerNull::get(i8_t->getPointerTo());  // void*
        }
        Value *args[] = {get_user_context(),
                         ConstantInt::get(i32_t, num_tasks),
                         task_stack_ptr,
                         task_parent};
        result = builder->CreateCall(do_parallel_tasks, args);
    }

    // Check for success
    Value *did_succeed = builder->CreateICmpEQ(result, ConstantInt::get(i32_t, 0));
    create_assertion(did_succeed, Expr(), result);
}

namespace {

string task_debug_name(const std::pair<string, int> &prefix) {
    if (prefix.second <= 1) {
        return prefix.first;
    } else {
        return prefix.first + "_" + std::to_string(prefix.second - 1);
    }
}

void add_fork(std::pair<string, int> &prefix) {
    if (prefix.second == 0) {
        prefix.first += ".fork";
    }
    prefix.second++;
}

void add_suffix(std::pair<string, int> &prefix, const string &suffix) {
    if (prefix.second > 1) {
        prefix.first += "_" + std::to_string(prefix.second - 1);
        prefix.second = 0;
    }
    prefix.first += suffix;
}

}  // namespace

void CodeGen_LLVM::get_parallel_tasks(const Stmt &s, vector<ParallelTask> &result, std::pair<string, int> prefix) {
    const For *loop = s.as<For>();
    const Acquire *acquire = loop ? loop->body.as<Acquire>() : s.as<Acquire>();
    if (const Fork *f = s.as<Fork>()) {
        add_fork(prefix);
        get_parallel_tasks(f->first, result, prefix);
        get_parallel_tasks(f->rest, result, prefix);
    } else if (!loop && acquire) {
        const Variable *v = acquire->semaphore.as<Variable>();
        internal_assert(v);
        add_suffix(prefix, "." + v->name);
        ParallelTask t{s, {}, "", 0, 1, const_false(), task_debug_name(prefix)};
        while (acquire) {
            t.semaphores.push_back({acquire->semaphore, acquire->count});
            t.body = acquire->body;
            acquire = t.body.as<Acquire>();
        }
        result.push_back(t);
    } else if (loop && loop->for_type == ForType::Parallel) {
        add_suffix(prefix, ".par_for." + loop->name);
        result.push_back(ParallelTask{loop->body, {}, loop->name, loop->min, loop->extent, const_false(), task_debug_name(prefix)});
    } else if (loop &&
               loop->for_type == ForType::Serial &&
               acquire &&
               !expr_uses_var(acquire->count, loop->name)) {
        const Variable *v = acquire->semaphore.as<Variable>();
        internal_assert(v);
        add_suffix(prefix, ".for." + v->name);
        ParallelTask t{loop->body, {}, loop->name, loop->min, loop->extent, const_true(), task_debug_name(prefix)};
        while (acquire) {
            t.semaphores.push_back({acquire->semaphore, acquire->count});
            t.body = acquire->body;
            acquire = t.body.as<Acquire>();
        }
        result.push_back(t);
    } else {
        add_suffix(prefix, "." + std::to_string(result.size()));
        result.push_back(ParallelTask{s, {}, "", 0, 1, const_false(), task_debug_name(prefix)});
    }
}

void CodeGen_LLVM::do_as_parallel_task(const Stmt &s) {
    vector<ParallelTask> tasks;
    get_parallel_tasks(s, tasks, {function->getName().str(), 0});
    do_parallel_tasks(tasks);
}

void CodeGen_LLVM::visit(const Acquire *op) {
    do_as_parallel_task(op);
}

void CodeGen_LLVM::visit(const Fork *op) {
    do_as_parallel_task(op);
}

void CodeGen_LLVM::visit(const Store *op) {
    Halide::Type value_type = op->value.type();
    Halide::Type storage_type = upgrade_type_for_storage(value_type);
    if (value_type != storage_type) {
        Expr v = reinterpret(storage_type, op->value);
        codegen(Store::make(op->name, v, op->index, op->param, op->predicate, op->alignment));
        return;
    }

    if (inside_atomic_mutex_node) {
        user_assert(value_type.is_scalar())
            << "The vectorized atomic operation for the store " << op->name
            << " is lowered into a mutex lock, which does not support vectorization.\n";
    }

    // Issue atomic store if we are inside an atomic node.
    if (emit_atomic_stores) {
        codegen_atomic_store(op);
        return;
    }

    // Predicated store.
    if (!is_one(op->predicate)) {
        codegen_predicated_vector_store(op);
        return;
    }

    Value *val = codegen(op->value);
    bool is_external = (external_buffer.find(op->name) != external_buffer.end());
    // Scalar
    if (value_type.is_scalar()) {
        Value *ptr = codegen_buffer_pointer(op->name, value_type, op->index);
        StoreInst *store = builder->CreateAlignedStore(val, ptr, make_alignment(value_type.bytes()));
        add_tbaa_metadata(store, op->name, op->index);
    } else if (const Let *let = op->index.as<Let>()) {
        Stmt s = Store::make(op->name, op->value, let->body, op->param, op->predicate, op->alignment);
        codegen(LetStmt::make(let->name, let->value, s));
    } else {
        int alignment = value_type.bytes();
        const Ramp *ramp = op->index.as<Ramp>();
        if (ramp && is_one(ramp->stride)) {

            int native_bits = native_vector_bits();
            int native_bytes = native_bits / 8;

            // Boost the alignment if possible, up to the native vector width.
            ModulusRemainder mod_rem = op->alignment;
            while ((mod_rem.remainder & 1) == 0 &&
                   (mod_rem.modulus & 1) == 0 &&
                   alignment < native_bytes) {
                mod_rem.modulus /= 2;
                mod_rem.remainder /= 2;
                alignment *= 2;
            }

            // If it is an external buffer, then we cannot assume that the host pointer
            // is aligned to at least the native vector width. However, we may be able to do
            // better than just assuming that it is unaligned.
            if (is_external && op->param.defined()) {
                int host_alignment = op->param.host_alignment();
                alignment = gcd(alignment, host_alignment);
            }

            // For dense vector stores wider than the native vector
            // width, bust them up into native vectors.
            int store_lanes = value_type.lanes();
            int native_lanes = native_bits / value_type.bits();

            for (int i = 0; i < store_lanes; i += native_lanes) {
                int slice_lanes = std::min(native_lanes, store_lanes - i);
                Expr slice_base = simplify(ramp->base + i);
                Expr slice_stride = make_one(slice_base.type());
                Expr slice_index = slice_lanes == 1 ? slice_base : Ramp::make(slice_base, slice_stride, slice_lanes);
                Value *slice_val = slice_vector(val, i, slice_lanes);
                Value *elt_ptr = codegen_buffer_pointer(op->name, value_type.element_of(), slice_base);
                Value *vec_ptr = builder->CreatePointerCast(elt_ptr, slice_val->getType()->getPointerTo());
                StoreInst *store = builder->CreateAlignedStore(slice_val, vec_ptr, make_alignment(alignment));
                add_tbaa_metadata(store, op->name, slice_index);
            }
        } else if (ramp) {
            Type ptr_type = value_type.element_of();
            Value *ptr = codegen_buffer_pointer(op->name, ptr_type, ramp->base);
            const IntImm *const_stride = ramp->stride.as<IntImm>();
            Value *stride = codegen(ramp->stride);
            // Scatter without generating the indices as a vector
            for (int i = 0; i < ramp->lanes; i++) {
                Constant *lane = ConstantInt::get(i32_t, i);
                Value *v = builder->CreateExtractElement(val, lane);
                if (const_stride) {
                    // Use a constant offset from the base pointer
                    Value *p =
                        builder->CreateConstInBoundsGEP1_32(
                            llvm_type_of(ptr_type),
                            ptr,
                            const_stride->value * i);
                    StoreInst *store = builder->CreateStore(v, p);
                    add_tbaa_metadata(store, op->name, op->index);
                } else {
                    // Increment the pointer by the stride for each element
                    StoreInst *store = builder->CreateStore(v, ptr);
                    add_tbaa_metadata(store, op->name, op->index);
                    ptr = builder->CreateInBoundsGEP(ptr, stride);
                }
            }
        } else {
            // Scatter
            Value *index = codegen(op->index);
            for (int i = 0; i < value_type.lanes(); i++) {
                Value *lane = ConstantInt::get(i32_t, i);
                Value *idx = builder->CreateExtractElement(index, lane);
                Value *v = builder->CreateExtractElement(val, lane);
                Value *ptr = codegen_buffer_pointer(op->name, value_type.element_of(), idx);
                StoreInst *store = builder->CreateStore(v, ptr);
                add_tbaa_metadata(store, op->name, op->index);
            }
        }
    }
}

void CodeGen_LLVM::codegen_asserts(const vector<const AssertStmt *> &asserts) {
    if (asserts.size() < 4) {
        for (const auto *a : asserts) {
            codegen(Stmt(a));
        }
        return;
    }

    internal_assert(asserts.size() <= 63);

    // Mix all the conditions together into a bitmask

    Expr bitmask = cast<uint64_t>(1) << 63;
    for (size_t i = 0; i < asserts.size(); i++) {
        bitmask = bitmask | (cast<uint64_t>(!asserts[i]->condition) << i);
    }

    Expr switch_case = count_trailing_zeros(bitmask);

    BasicBlock *no_errors_bb = BasicBlock::Create(*context, "no_errors_bb", function);

    // Now switch on the bitmask to the correct failure
    Expr case_idx = cast<int32_t>(count_trailing_zeros(bitmask));
    llvm::SmallVector<uint32_t, 64> weights;
    weights.push_back(1 << 30);
    for (int i = 0; i < (int)asserts.size(); i++) {
        weights.push_back(0);
    }
    llvm::MDBuilder md_builder(*context);
    llvm::MDNode *switch_very_likely_branch = md_builder.createBranchWeights(weights);
    auto *switch_inst = builder->CreateSwitch(codegen(case_idx), no_errors_bb, asserts.size(), switch_very_likely_branch);
    for (int i = 0; i < (int)asserts.size(); i++) {
        BasicBlock *fail_bb = BasicBlock::Create(*context, "assert_failed", function);
        switch_inst->addCase(ConstantInt::get(IntegerType::get(*context, 32), i), fail_bb);
        builder->SetInsertPoint(fail_bb);
        Value *v = codegen(asserts[i]->message);
        builder->CreateRet(v);
    }
    builder->SetInsertPoint(no_errors_bb);
}

void CodeGen_LLVM::visit(const Block *op) {
    // Peel blocks of assertions with pure conditions
    const AssertStmt *a = op->first.as<AssertStmt>();
    if (a && is_pure(a->condition)) {
        vector<const AssertStmt *> asserts;
        asserts.push_back(a);
        Stmt s = op->rest;
        while ((op = s.as<Block>()) && (a = op->first.as<AssertStmt>()) && is_pure(a->condition) && asserts.size() < 63) {
            asserts.push_back(a);
            s = op->rest;
        }
        codegen_asserts(asserts);
        codegen(s);
    } else {
        codegen(op->first);
        codegen(op->rest);
    }
}

void CodeGen_LLVM::visit(const Realize *op) {
    internal_error << "Realize encountered during codegen\n";
}

void CodeGen_LLVM::visit(const Provide *op) {
    internal_error << "Provide encountered during codegen\n";
}

void CodeGen_LLVM::visit(const IfThenElse *op) {
    BasicBlock *true_bb = BasicBlock::Create(*context, "true_bb", function);
    BasicBlock *false_bb = BasicBlock::Create(*context, "false_bb", function);
    BasicBlock *after_bb = BasicBlock::Create(*context, "after_bb", function);
    builder->CreateCondBr(codegen(op->condition), true_bb, false_bb);

    builder->SetInsertPoint(true_bb);
    codegen(op->then_case);
    builder->CreateBr(after_bb);

    builder->SetInsertPoint(false_bb);
    if (op->else_case.defined()) {
        codegen(op->else_case);
    }
    builder->CreateBr(after_bb);

    builder->SetInsertPoint(after_bb);
}

void CodeGen_LLVM::visit(const Evaluate *op) {
    codegen(op->value);

    // Discard result
    value = nullptr;
}

void CodeGen_LLVM::visit(const Shuffle *op) {
    if (op->is_interleave()) {
        vector<Value *> vecs;
        for (Expr i : op->vectors) {
            vecs.push_back(codegen(i));
        }
        value = interleave_vectors(vecs);
    } else {
        vector<Value *> vecs;
        for (Expr i : op->vectors) {
            vecs.push_back(codegen(i));
        }
        value = concat_vectors(vecs);
        if (op->is_concat()) {
            // If this is just a concat, we're done.
        } else if (op->is_slice() && op->slice_stride() == 1) {
            value = slice_vector(value, op->indices[0], op->indices.size());
        } else {
            value = shuffle_vectors(value, op->indices);
        }
    }

    if (op->type.is_scalar()) {
        value = builder->CreateExtractElement(value, ConstantInt::get(i32_t, 0));
    }
}

void CodeGen_LLVM::visit(const Atomic *op) {
    if (op->mutex_name != "") {
        internal_assert(!inside_atomic_mutex_node)
            << "Nested atomic mutex locks detected. This might causes a deadlock.\n";
        ScopedValue<bool> old_inside_atomic_mutex_node(inside_atomic_mutex_node, true);
        // Mutex locking & unlocking are handled by function calls generated by previous lowering passes.
        codegen(op->body);
    } else {
        // Issue atomic stores.
        ScopedValue<bool> old_emit_atomic_stores(emit_atomic_stores, true);
        codegen(op->body);
    }
}

Value *CodeGen_LLVM::create_alloca_at_entry(llvm::Type *t, int n, bool zero_initialize, const string &name) {
    IRBuilderBase::InsertPoint here = builder->saveIP();
    BasicBlock *entry = &builder->GetInsertBlock()->getParent()->getEntryBlock();
    if (entry->empty()) {
        builder->SetInsertPoint(entry);
    } else {
        builder->SetInsertPoint(entry, entry->getFirstInsertionPt());
    }
    Value *size = ConstantInt::get(i32_t, n);
    AllocaInst *ptr = builder->CreateAlloca(t, size, name);
    int align = native_vector_bits() / 8;
    llvm::DataLayout d(module.get());
    int allocated_size = n * (int)d.getTypeAllocSize(t);
    if (t->isVectorTy() || n > 1) {
        ptr->setAlignment(make_alignment(align));
    }
    requested_alloca_total += allocated_size;

    if (zero_initialize) {
        if (n == 1) {
            builder->CreateStore(Constant::getNullValue(t), ptr);
        } else {
            builder->CreateMemSet(ptr, Constant::getNullValue(t), n, make_alignment(align));
        }
    }
    builder->restoreIP(here);
    return ptr;
}

Value *CodeGen_LLVM::get_user_context() const {
    Value *ctx = sym_get("__user_context", false);
    if (!ctx) {
        ctx = ConstantPointerNull::get(i8_t->getPointerTo());  // void*
    }
    return ctx;
}

Value *CodeGen_LLVM::call_intrin(const Type &result_type, int intrin_lanes,
                                 const string &name, vector<Expr> args) {
    vector<Value *> arg_values(args.size());
    for (size_t i = 0; i < args.size(); i++) {
        arg_values[i] = codegen(args[i]);
    }

    return call_intrin(llvm_type_of(result_type),
                       intrin_lanes,
                       name, arg_values);
}

Value *CodeGen_LLVM::call_intrin(llvm::Type *result_type, int intrin_lanes,
                                 const string &name, vector<Value *> arg_values) {
    internal_assert(result_type->isVectorTy()) << "call_intrin is for vector intrinsics only\n";

    int arg_lanes = get_vector_num_elements(result_type);

    if (intrin_lanes != arg_lanes) {
        // Cut up each arg into appropriately-sized pieces, call the
        // intrinsic on each, then splice together the results.
        vector<Value *> results;
        for (int start = 0; start < arg_lanes; start += intrin_lanes) {
            vector<Value *> args;
            for (size_t i = 0; i < arg_values.size(); i++) {
                if (arg_values[i]->getType()->isVectorTy()) {
                    int arg_i_lanes = get_vector_num_elements(arg_values[i]->getType());
                    internal_assert(arg_i_lanes >= arg_lanes);
                    // Horizontally reducing intrinsics may have
                    // arguments that have more lanes than the
                    // result. Assume that the horizontally reduce
                    // neighboring elements...
                    int reduce = arg_i_lanes / arg_lanes;
                    args.push_back(slice_vector(arg_values[i], start * reduce, intrin_lanes * reduce));
                } else {
                    args.push_back(arg_values[i]);
                }
            }

            llvm::Type *result_slice_type =
                llvm::VectorType::get(result_type->getScalarType(), intrin_lanes);

            results.push_back(call_intrin(result_slice_type, intrin_lanes, name, args));
        }
        Value *result = concat_vectors(results);
        return slice_vector(result, 0, arg_lanes);
    }

    vector<llvm::Type *> arg_types(arg_values.size());
    for (size_t i = 0; i < arg_values.size(); i++) {
        arg_types[i] = arg_values[i]->getType();
    }

    llvm::Function *fn = module->getFunction(name);

    if (!fn) {
        llvm::Type *intrinsic_result_type = VectorType::get(result_type->getScalarType(), intrin_lanes);
        FunctionType *func_t = FunctionType::get(intrinsic_result_type, arg_types, false);
        fn = llvm::Function::Create(func_t, llvm::Function::ExternalLinkage, name, module.get());
        fn->setCallingConv(CallingConv::C);
    }

    CallInst *call = builder->CreateCall(fn, arg_values);

    call->setDoesNotAccessMemory();
    call->setDoesNotThrow();

    return call;
}

Value *CodeGen_LLVM::slice_vector(Value *vec, int start, int size) {
    int vec_lanes = get_vector_num_elements(vec->getType());

    if (start == 0 && size == vec_lanes) {
        return vec;
    }

    vector<int> indices(size);
    for (int i = 0; i < size; i++) {
        int idx = start + i;
        if (idx >= 0 && idx < vec_lanes) {
            indices[i] = idx;
        } else {
            indices[i] = -1;
        }
    }
    return shuffle_vectors(vec, indices);
}

Value *CodeGen_LLVM::concat_vectors(const vector<Value *> &v) {
    if (v.size() == 1) return v[0];

    internal_assert(!v.empty());

    vector<Value *> vecs = v;

    // Force them all to be actual vectors
    for (Value *&val : vecs) {
        if (!val->getType()->isVectorTy()) {
            val = create_broadcast(val, 1);
        }
    }

    while (vecs.size() > 1) {
        vector<Value *> new_vecs;

        for (size_t i = 0; i < vecs.size() - 1; i += 2) {
            Value *v1 = vecs[i];
            Value *v2 = vecs[i + 1];

            int w1 = get_vector_num_elements(v1->getType());
            int w2 = get_vector_num_elements(v2->getType());

            // Possibly pad one of the vectors to match widths.
            if (w1 < w2) {
                v1 = slice_vector(v1, 0, w2);
            } else if (w2 < w1) {
                v2 = slice_vector(v2, 0, w1);
            }
            int w_matched = std::max(w1, w2);

            internal_assert(v1->getType() == v2->getType());

            vector<int> indices(w1 + w2);
            for (int i = 0; i < w1; i++) {
                indices[i] = i;
            }
            for (int i = 0; i < w2; i++) {
                indices[w1 + i] = w_matched + i;
            }

            Value *merged = shuffle_vectors(v1, v2, indices);

            new_vecs.push_back(merged);
        }

        // If there were an odd number of them, we need to also push
        // the one that didn't get merged.
        if (vecs.size() & 1) {
            new_vecs.push_back(vecs.back());
        }

        vecs.swap(new_vecs);
    }

    return vecs[0];
}

Value *CodeGen_LLVM::shuffle_vectors(Value *a, Value *b,
                                     const std::vector<int> &indices) {
    internal_assert(a->getType() == b->getType());
    vector<Constant *> llvm_indices(indices.size());
    for (size_t i = 0; i < llvm_indices.size(); i++) {
        if (indices[i] >= 0) {
            internal_assert(indices[i] < get_vector_num_elements(a->getType()) * 2);
            llvm_indices[i] = ConstantInt::get(i32_t, indices[i]);
        } else {
            // Only let -1 be undef.
            internal_assert(indices[i] == -1);
            llvm_indices[i] = UndefValue::get(i32_t);
        }
    }

    return builder->CreateShuffleVector(a, b, ConstantVector::get(llvm_indices));
}

Value *CodeGen_LLVM::shuffle_vectors(Value *a, const std::vector<int> &indices) {
    Value *b = UndefValue::get(a->getType());
    return shuffle_vectors(a, b, indices);
}

std::pair<llvm::Function *, int> CodeGen_LLVM::find_vector_runtime_function(const std::string &name, int lanes) {
    // Check if a vector version of the function already
    // exists at some useful width. We use the naming
    // convention that a N-wide version of a function foo is
    // called fooxN. All of our intrinsics are power-of-two
    // sized, so starting at the first power of two >= the
    // vector width, we'll try all powers of two in decreasing
    // order.
    vector<int> sizes_to_try;
    int l = 1;
    while (l < lanes)
        l *= 2;
    for (int i = l; i > 1; i /= 2) {
        sizes_to_try.push_back(i);
    }

    // If none of those match, we'll also try doubling
    // the lanes up to the next power of two (this is to catch
    // cases where we're a 64-bit vector and have a 128-bit
    // vector implementation).
    sizes_to_try.push_back(l * 2);

    for (size_t i = 0; i < sizes_to_try.size(); i++) {
        int l = sizes_to_try[i];
        llvm::Function *vec_fn = module->getFunction(name + "x" + std::to_string(l));
        if (vec_fn) {
            return {vec_fn, l};
        }
    }

    return {nullptr, 0};
}

bool CodeGen_LLVM::supports_atomic_add(const Type &t) const {
    return t.is_int_or_uint();
}

bool CodeGen_LLVM::use_pic() const {
    return true;
}

}  // namespace Internal
}  // namespace Halide
