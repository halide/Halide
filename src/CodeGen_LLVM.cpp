#include <limits>
#include <memory>
#include <sstream>

#include "CPlusPlusMangle.h"
#include "CSE.h"
#include "CodeGen_Internal.h"
#include "CodeGen_LLVM.h"
#include "CodeGen_Posix.h"
#include "CodeGen_Targets.h"
#include "CompilerLogger.h"
#include "Debug.h"
#include "Deinterleave.h"
#include "EmulateFloat16Math.h"
#include "ExprUsesVar.h"
#include "FindIntrinsics.h"
#include "IREquality.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "IntegerDivisionTable.h"
#include "JITModule.h"
#include "LLVM_Headers.h"
#include "LLVM_Runtime_Linker.h"
#include "Lerp.h"
#include "LowerParallelTasks.h"
#include "Pipeline.h"
#include "Simplify.h"
#include "Util.h"

// MSVC won't set __cplusplus correctly unless certain compiler flags are set
// (and CMake doesn't set those flags for you even if you specify C++17),
// so we need to check against _MSVC_LANG as well, for completeness.
#if !(__cplusplus >= 201703L || _MSVC_LANG >= 201703L)
#error "Halide requires C++17 or later; please upgrade your compiler."
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
    LLVMInitialize##target##TargetMC();

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

#ifdef WITH_NVPTX
#define InitializeNVPTXTarget() InitializeTarget(NVPTX)
// #define InitializeNVPTXAsmParser() InitializeAsmParser(NVPTX) // there is no ASM parser for NVPTX
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

llvm::Value *CreateConstGEP1_32(IRBuilderBase *builder, llvm::Type *gep_type,
                                Value *ptr, unsigned index) {
    return builder->CreateConstGEP1_32(gep_type, ptr, index);
}

llvm::Value *CreateInBoundsGEP(IRBuilderBase *builder, llvm::Type *gep_type,
                               Value *ptr, ArrayRef<Value *> index_list) {
    return builder->CreateInBoundsGEP(gep_type, ptr, index_list);
}

// Get the LLVM linkage corresponding to a Halide linkage type.
llvm::GlobalValue::LinkageTypes llvm_linkage(LinkageType t) {
    // TODO(dsharlet): For some reason, marking internal functions as
    // private linkage on OSX is causing some of the static tests to
    // fail. Figure out why so we can remove this.
    return llvm::GlobalValue::ExternalLinkage;

    // switch (t) {
    // case LinkageType::ExternalPlusArgv:
    // case LinkageType::ExternalPlusMetadata:
    // case LinkageType::External:
    //     return llvm::GlobalValue::ExternalLinkage;
    // default:
    //     return llvm::GlobalValue::PrivateLinkage;
    // }
}

}  // namespace

CodeGen_LLVM::CodeGen_LLVM(const Target &t)
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

      wild_u1_(Variable::make(UInt(1), "*")),
      wild_i8_(Variable::make(Int(8), "*")),
      wild_u8_(Variable::make(UInt(8), "*")),
      wild_i16_(Variable::make(Int(16), "*")),
      wild_u16_(Variable::make(UInt(16), "*")),
      wild_i32_(Variable::make(Int(32), "*")),
      wild_u32_(Variable::make(UInt(32), "*")),
      wild_i64_(Variable::make(Int(64), "*")),
      wild_u64_(Variable::make(UInt(64), "*")),
      wild_f32_(Variable::make(Float(32), "*")),
      wild_f64_(Variable::make(Float(64), "*")),

      inside_atomic_mutex_node(false),
      emit_atomic_stores(false),
      use_llvm_vp_intrinsics(false),

      destructor_block(nullptr),
      strict_float(t.has_feature(Target::StrictFloat)),
      llvm_large_code_model(t.has_feature(Target::LLVMLargeCodeModel)),
      effective_vscale(0) {
    initialize_llvm();
}

void CodeGen_LLVM::set_context(llvm::LLVMContext &context) {
    this->context = &context;
    effective_vscale = target_vscale();
}

std::unique_ptr<CodeGen_LLVM> CodeGen_LLVM::new_for_target(const Target &target, llvm::LLVMContext &context) {
    std::unique_ptr<CodeGen_LLVM> result;
    if (target.arch == Target::X86) {
        result = new_CodeGen_X86(target);
    } else if (target.arch == Target::ARM) {
        result = new_CodeGen_ARM(target);
    } else if (target.arch == Target::POWERPC) {
        result = new_CodeGen_PowerPC(target);
    } else if (target.arch == Target::Hexagon) {
        result = new_CodeGen_Hexagon(target);
    } else if (target.arch == Target::WebAssembly) {
        result = new_CodeGen_WebAssembly(target);
    } else if (target.arch == Target::RISCV) {
        result = new_CodeGen_RISCV(target);
    }
    user_assert(result) << "Unknown target architecture: " << target.to_string() << "\n";
    result->set_context(context);
    return result;
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
            // TODO: Remove after opaque pointers become the default in LLVM.
            // This is here to document how to turn on opaque pointers, for testing, in LLVM 15
            //            c_arg_vec.push_back("-opaque-pointers");
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
#undef LLVM_ASM_PRINTER
    });
}

void CodeGen_LLVM::init_context() {
    // Ensure our IRBuilder is using the current context.
    builder = std::make_unique<IRBuilder<>>(*context);

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

    // Ensure no Value pointers carry over from previous context.
    struct_type_recovery.clear();
}

void CodeGen_LLVM::init_module() {
    init_context();

    // Start with a module containing the initial module for this target.
    module = get_initial_module_for_target(target, context);
}

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
        // callee might already be present for builtins, e.g. halide_print
        llvm::Function *callee = codegen->module->getFunction(callee_name);
        if (!callee) {
            callee = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, callee_name, codegen->module.get());
        }

        std::vector<bool> buffer_args(e.second.arg_types().size());
        size_t index = 0;
        for (const Type &t : e.second.arg_types()) {
            buffer_args[index++] = (t == type_of<struct halide_buffer_t *>());
        }
        codegen->add_argv_wrapper(callee, wrapper_name, /*result_in_argv*/ true, buffer_args);
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
    module->addModuleFlag(llvm::Module::Warning, "halide_mcpu_target", MDString::get(*context, mcpu_target()));
    module->addModuleFlag(llvm::Module::Warning, "halide_mcpu_tune", MDString::get(*context, mcpu_tune()));
    module->addModuleFlag(llvm::Module::Warning, "halide_mattrs", MDString::get(*context, mattrs()));
    module->addModuleFlag(llvm::Module::Warning, "halide_mabi", MDString::get(*context, mabi()));
    module->addModuleFlag(llvm::Module::Warning, "halide_use_pic", use_pic() ? 1 : 0);
    module->addModuleFlag(llvm::Module::Warning, "halide_use_large_code_model", llvm_large_code_model ? 1 : 0);
    module->addModuleFlag(llvm::Module::Warning, "halide_per_instruction_fast_math_flags", any_strict_float);
    if (effective_vscale != 0) {
        module->addModuleFlag(llvm::Module::Warning, "halide_vscale_range",
                              MDString::get(*context, std::to_string(effective_vscale) + ", " + std::to_string(effective_vscale)));
    }

    // Ensure some types we need are defined
    halide_buffer_t_type = get_llvm_struct_type_by_name(module.get(), "struct.halide_buffer_t");
    internal_assert(halide_buffer_t_type) << "Did not find halide_buffer_t in initial module";

    type_t_type = get_llvm_struct_type_by_name(module.get(), "struct.halide_type_t");
    internal_assert(type_t_type) << "Did not find halide_type_t in initial module";

    dimension_t_type = get_llvm_struct_type_by_name(module.get(), "struct.halide_dimension_t");
    internal_assert(dimension_t_type) << "Did not find halide_dimension_t in initial module";

    metadata_t_type = get_llvm_struct_type_by_name(module.get(), "struct.halide_filter_metadata_t");
    internal_assert(metadata_t_type) << "Did not find halide_filter_metadata_t in initial module";

    argument_t_type = get_llvm_struct_type_by_name(module.get(), "struct.halide_filter_argument_t");
    internal_assert(argument_t_type) << "Did not find halide_filter_argument_t in initial module";

    scalar_value_t_type = get_llvm_struct_type_by_name(module.get(), "struct.halide_scalar_value_t");
    internal_assert(scalar_value_t_type) << "Did not find halide_scalar_value_t in initial module";

    device_interface_t_type = get_llvm_struct_type_by_name(module.get(), "struct.halide_device_interface_t");
    internal_assert(device_interface_t_type) << "Did not find halide_device_interface_t in initial module";

    pseudostack_slot_t_type = get_llvm_struct_type_by_name(module.get(), "struct.halide_pseudostack_slot_t");
    internal_assert(pseudostack_slot_t_type) << "Did not find halide_pseudostack_slot_t in initial module";

    semaphore_t_type = get_llvm_struct_type_by_name(module.get(), "struct.halide_semaphore_t");
    internal_assert(semaphore_t_type) << "Did not find halide_semaphore_t in initial module";
}

std::unique_ptr<llvm::Module> CodeGen_LLVM::compile(const Module &input) {
    init_codegen(input.name(), input.any_strict_float());

    internal_assert(module && context && builder)
        << "The CodeGen_LLVM subclass should have made an initial module before calling CodeGen_LLVM::compile\n";

    // Generate the code for this module.
    debug(1) << "Generating llvm bitcode...\n";
    for (const auto &b : input.buffers()) {
        compile_buffer(b);
    }

    vector<MangledNames> function_names;

    // Declare all functions
    for (const auto &f : input.functions()) {
        const auto names = get_mangled_names(f, get_target());
        function_names.push_back(names);

        // Deduce the types of the arguments to our function
        vector<llvm::Type *> arg_types(f.args.size());
        for (size_t i = 0; i < f.args.size(); i++) {
            if (f.args[i].is_buffer()) {
                arg_types[i] = halide_buffer_t_type->getPointerTo();
            } else {
                arg_types[i] = llvm_type_of(upgrade_type_for_argument_passing(f.args[i].type));
            }
        }
        FunctionType *func_t = FunctionType::get(i32_t, arg_types, false);
        function = llvm::Function::Create(func_t, llvm_linkage(f.linkage), names.extern_name, module.get());
        set_function_attributes_from_halide_target_options(*function);

        // Mark the buffer args as no alias and save indication for add_argv_wrapper if needed
        std::vector<bool> buffer_args(f.args.size());
        for (size_t i = 0; i < f.args.size(); i++) {
            bool is_buffer = f.args[i].is_buffer();
            buffer_args[i] = is_buffer;
            if (is_buffer) {
                function->addParamAttr(i, Attribute::NoAlias);
            }
        }

        // sym_push helpfully calls setName, which we don't want
        symbol_table.push("::" + f.name, function);

        // If the Func is externally visible, also create the argv wrapper and metadata.
        // (useful for calling from JIT and other machine interfaces).
        if (f.linkage == LinkageType::ExternalPlusArgv || f.linkage == LinkageType::ExternalPlusMetadata) {
            add_argv_wrapper(function, names.argv_name, false, buffer_args);
            if (f.linkage == LinkageType::ExternalPlusMetadata) {
                embed_metadata_getter(names.metadata_name,
                                      names.simple_name, f.args, input.get_metadata_name_map());
            }
        }

        // Workaround for https://github.com/halide/Halide/issues/635:
        // For historical reasons, Halide-generated AOT code
        // defines user_context as `void const*`, but expects all
        // define_extern code with user_context usage to use `void *`. This
        // usually isn't an issue, but if both the caller and callee of the
        // pass a user_context, *and* c_plus_plus_name_mangling is enabled,
        // we get link errors because of this dichotomy. Fixing this
        // "correctly" (ie so that everything always uses identical types for
        // user_context in all cases) will require a *lot* of downstream
        // churn (see https://github.com/halide/Halide/issues/7298),
        // so this is a workaround: Add a wrapper with `void*`
        // ucon -> `void const*` ucon. In most cases this will be ignored
        // (and probably dead-stripped), but in these cases it's critical.
        //
        // (Note that we don't check to see if c_plus_plus_name_mangling is
        // enabled, since that would have to be done on the caller side, and
        // this is purely a callee-side fix.)
        if (f.linkage != LinkageType::Internal &&
            target.has_feature(Target::CPlusPlusMangling) &&
            target.has_feature(Target::UserContext)) {

            int wrapper_ucon_index = -1;
            auto wrapper_args = f.args;               // make a copy
            auto wrapper_llvm_arg_types = arg_types;  // make a copy
            for (int i = 0; i < (int)wrapper_args.size(); i++) {
                if (wrapper_args[i].name == "__user_context" && wrapper_args[i].type == type_of<void const *>()) {
                    // Update the type of the user_context argument to be void* rather than void const*
                    wrapper_args[i].type = type_of<void *>();
                    wrapper_llvm_arg_types[i] = llvm_type_of(upgrade_type_for_argument_passing(wrapper_args[i].type));
                    wrapper_ucon_index = i;
                }
            }
            if (wrapper_ucon_index >= 0) {
                const auto wrapper_names = get_mangled_names(f.name, f.linkage, f.name_mangling, wrapper_args, target);

                FunctionType *wrapper_func_t = FunctionType::get(i32_t, wrapper_llvm_arg_types, false);
                llvm::Function *wrapper_func = llvm::Function::Create(wrapper_func_t,
                                                                      llvm::GlobalValue::ExternalLinkage,
                                                                      wrapper_names.extern_name,
                                                                      module.get());
                set_function_attributes_from_halide_target_options(*wrapper_func);
                llvm::BasicBlock *wrapper_block = llvm::BasicBlock::Create(module->getContext(), "entry", wrapper_func);
                builder->SetInsertPoint(wrapper_block);

                std::vector<llvm::Value *> wrapper_call_args;
                for (auto &arg : wrapper_func->args()) {
                    wrapper_call_args.push_back(&arg);
                }
                wrapper_call_args[wrapper_ucon_index] = builder->CreatePointerCast(wrapper_call_args[wrapper_ucon_index],
                                                                                   llvm_type_of(type_of<void const *>()));

                llvm::CallInst *wrapper_result = builder->CreateCall(function, wrapper_call_args);
                // This call should never inline
                wrapper_result->setIsNoInline();
                builder->CreateRet(wrapper_result);
                internal_assert(!verifyFunction(*wrapper_func, &llvm::errs()));
            }
        }
    }
    // Define all functions
    int idx = 0;
    for (const auto &f : input.functions()) {
        const auto names = function_names[idx++];

        run_with_large_stack([&]() {
            compile_func(f, names.simple_name, names.extern_name);
        });
    }

    debug(2) << "llvm::Module pointer: " << module.get() << "\n";

    return finish_codegen();
}

std::unique_ptr<llvm::Module> CodeGen_LLVM::finish_codegen() {
    llvm::for_each(*module, set_function_attributes_from_halide_target_options);

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
    function = module->getFunction(extern_name);
    if (!function) {
        internal_assert(function) << "Could not find a function of name " << extern_name << " in module\n";
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
    for (const auto &arg : args) {
        if (arg.is_buffer()) {
            sym_pop(arg.name + ".buffer");
        } else {
            sym_pop(arg.name);
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

    // Show one time warning and clear it.
    for (auto it = onetime_warnings.begin(); it != onetime_warnings.end(); it = onetime_warnings.erase(it)) {
        user_warning << "In function " << f.name << ", " << it->second;
    }

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
                                                nullptr, buf.name() + ".buffer");
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
    storage->setAlignment(llvm::Align((int)sizeof(halide_scalar_value_t)));

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
                                               bool result_in_argv,
                                               std::vector<bool> &arg_is_buffer) {
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
        llvm::Value *ptr = CreateConstGEP1_32(builder.get(), i8_t->getPointerTo(),
                                              arg_array, wrapper_args.size());
        ptr = builder->CreateLoad(i8_t->getPointerTo(), ptr);
        if (arg_is_buffer[i->getArgNo()]) {
            // Cast the argument to a halide_buffer_t *
            wrapper_args.push_back(builder->CreatePointerCast(ptr, halide_buffer_t_type->getPointerTo()));
        } else {
            // Cast to the appropriate type and load
            ptr = builder->CreatePointerCast(ptr, i->getType()->getPointerTo());
            wrapper_args.push_back(builder->CreateLoad(i->getType(), ptr));
        }
    }
    debug(4) << "Creating call from wrapper to actual function\n";
    llvm::CallInst *result = builder->CreateCall(fn, wrapper_args);
    // This call should never inline
    result->setIsNoInline();

    if (result_in_argv) {
        llvm::Value *result_in_argv_ptr = CreateConstGEP1_32(builder.get(), i8_t->getPointerTo(),
                                                             arg_array, wrapper_args.size());
        if (fn->getReturnType() != void_t) {
            result_in_argv_ptr = builder->CreateLoad(i8_t->getPointerTo(), result_in_argv_ptr);
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
                                                    const MetadataNameMap &metadata_name_map) {
    Constant *zero = ConstantInt::get(i32_t, 0);

    const int num_args = (int)args.size();

    auto map_string = [&metadata_name_map](const std::string &from) -> std::string {
        auto it = metadata_name_map.find(from);
        return it == metadata_name_map.end() ? from : it->second;
    };

    vector<Constant *> arguments_array_entries;
    for (int arg = 0; arg < num_args; ++arg) {

        llvm::StructType *type_t_type = get_llvm_struct_type_by_name(module.get(), "struct.halide_type_t");
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
                if (min.defined()) {
                    min = cast<int64_t>(min);
                }
                Expr extent = be.extent;
                if (extent.defined()) {
                    extent = cast<int64_t>(extent);
                }
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
        /* target */ create_string_constant(target.to_string()),
        /* name */ create_string_constant(function_name)};

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
    return llvm_type_of(context, t, effective_vscale);
}

void CodeGen_LLVM::optimize_module() {
    debug(3) << "Optimizing module\n";

    auto time_start = std::chrono::high_resolution_clock::now();

    if (debug::debug_level() >= 3) {
        module->print(dbgs(), nullptr, false, true);
    }

    std::unique_ptr<TargetMachine> tm = make_target_machine(*module);

    const bool do_loop_opt = get_target().has_feature(Target::EnableLLVMLoopOpt);

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
    llvm::LoopAnalysisManager lam;
    llvm::FunctionAnalysisManager fam;
    llvm::CGSCCAnalysisManager cgam;
    llvm::ModuleAnalysisManager mam;

    // Register all the basic analyses with the managers.
    pb.registerModuleAnalyses(mam);
    pb.registerCGSCCAnalyses(cgam);
    pb.registerFunctionAnalyses(fam);
    pb.registerLoopAnalyses(lam);
    pb.crossRegisterProxies(lam, fam, cgam, mam);
    ModulePassManager mpm;

    using OptimizationLevel = llvm::OptimizationLevel;
    OptimizationLevel level = OptimizationLevel::O3;

    if (get_target().has_feature(Target::SanitizerCoverage)) {
        pb.registerOptimizerLastEPCallback(
            [&](ModulePassManager &mpm, OptimizationLevel level) {
                SanitizerCoverageOptions sanitizercoverage_options;
                // Mirror what -fsanitize=fuzzer-no-link would enable.
                // See https://github.com/halide/Halide/issues/6528
                sanitizercoverage_options.CoverageType = SanitizerCoverageOptions::SCK_Edge;
                sanitizercoverage_options.IndirectCalls = true;
                sanitizercoverage_options.TraceCmp = true;
                sanitizercoverage_options.Inline8bitCounters = true;
                sanitizercoverage_options.PCTable = true;
                // Due to TLS differences, stack depth tracking is only enabled on Linux
                if (get_target().os == Target::OS::Linux) {
                    sanitizercoverage_options.StackDepth = true;
                }
#if LLVM_VERSION >= 160
                mpm.addPass(SanitizerCoveragePass(sanitizercoverage_options));
#else
                mpm.addPass(ModuleSanitizerCoveragePass(sanitizercoverage_options));
#endif
            });
    }

    if (get_target().has_feature(Target::ASAN)) {
#if LLVM_VERSION >= 150
        // Nothing, ASanGlobalsMetadataAnalysis no longer exists
#else
        pb.registerPipelineStartEPCallback([&](ModulePassManager &mpm, OptimizationLevel) {
            mpm.addPass(RequireAnalysisPass<ASanGlobalsMetadataAnalysis, llvm::Module>());
        });
#endif
        pb.registerPipelineStartEPCallback([](ModulePassManager &mpm, OptimizationLevel) {
            AddressSanitizerOptions asan_options;  // default values are good...
            asan_options.UseAfterScope = true;     // ...except this one
            constexpr bool use_global_gc = false;
            constexpr bool use_odr_indicator = true;
            constexpr auto destructor_kind = AsanDtorKind::Global;
#if LLVM_VERSION >= 160
            mpm.addPass(AddressSanitizerPass(
                asan_options, use_global_gc, use_odr_indicator, destructor_kind));
#else
            mpm.addPass(ModuleAddressSanitizerPass(
                asan_options, use_global_gc, use_odr_indicator, destructor_kind));
#endif
        });
    }

    // Target::MSAN handling is sprinkled throughout the codebase,
    // there is no need to run MemorySanitizerPass here.

    if (get_target().has_feature(Target::TSAN)) {
        pb.registerOptimizerLastEPCallback(
            [](ModulePassManager &mpm, OptimizationLevel level) {
                mpm.addPass(
                    createModuleToFunctionPassAdaptor(ThreadSanitizerPass()));
            });
    }

    for (auto &function : *module) {
        if (get_target().has_feature(Target::ASAN)) {
            function.addFnAttr(Attribute::SanitizeAddress);
        }
        if (get_target().has_feature(Target::MSAN)) {
            function.addFnAttr(Attribute::SanitizeMemory);
        }
        if (get_target().has_feature(Target::TSAN)) {
            // Do not annotate any of Halide's low-level synchronization code as it has
            // tsan interface calls to mark its behavior and is much faster if
            // it is not analyzed instruction by instruction.
            if (!(function.getName().startswith("_ZN6Halide7Runtime8Internal15Synchronization") ||
                  // TODO: this is a benign data race that re-initializes the detected features;
                  // we should really fix it properly inside the implementation, rather than disabling
                  // it here as a band-aid.
                  function.getName().startswith("halide_default_can_use_target_features") ||
                  function.getName().startswith("halide_mutex_") ||
                  function.getName().startswith("halide_cond_"))) {
                function.addFnAttr(Attribute::SanitizeThread);
            }
        }
    }

    if (tm) {
        tm->registerPassBuilderCallbacks(pb);
    }

    mpm = pb.buildPerModuleDefaultPipeline(level, debug_pass_manager);
    mpm.run(*module, mam);

    if (llvm::verifyModule(*module, &errs())) {
        report_fatal_error("Transformation resulted in an invalid module\n");
    }

    debug(3) << "After LLVM optimizations:\n";
    if (debug::debug_level() >= 2) {
        module->print(dbgs(), nullptr, false, true);
    }

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
    internal_assert(value) << "Codegen of an expr did not produce an llvm value\n"
                           << e;

    // Halide's type system doesn't distinguish between scalars and
    // vectors of size 1, so if a codegen method returned a vector of
    // size one, just extract it out as a scalar.
    if (e.type().is_scalar() &&
        value->getType()->isVectorTy()) {
        internal_assert(get_vector_num_elements(value->getType()) == 1);
        value = builder->CreateExtractElement(value, ConstantInt::get(i32_t, 0));
    }

    // Make sure fixed/vscale property of vector types match what is exepected.
    value = normalize_fixed_scalable_vector_type(llvm_type_of(e.type()), value);

    // TODO: skip this correctness check for bool vectors,
    // as eliminate_bool_vectors() will cause a discrepancy for some backends
    // (eg OpenCL, HVX, WASM); for now we're just ignoring the assert, but
    // in the long run we should improve the smarts. See https://github.com/halide/Halide/issues/4194.
    const bool is_bool_vector = e.type().is_bool() && e.type().lanes() > 1;
    // TODO: skip this correctness check for prefetch, because the return type
    // of prefetch indicates the type being prefetched, which does not match the
    // implementation of prefetch.
    // See https://github.com/halide/Halide/issues/4211.
    const bool is_prefetch = Call::as_intrinsic(e, {Call::prefetch});
    bool types_match = is_bool_vector || is_prefetch ||
                       e.type().is_handle() ||
                       value->getType()->isVoidTy() ||
                       value->getType() == llvm_type_of(e.type());
    if (!types_match && debug::debug_level() > 0) {
        debug(1) << "Unexpected LLVM type for generated expression. Expected (llvm_type_of(e.type())): ";
        llvm_type_of(e.type())->print(dbgs(), true);
        debug(1) << " got (value->getType()): ";
        value->print(dbgs(), true);
        debug(1) << "\n";
    }
    internal_assert(types_match)
        << "Codegen of Expr " << e
        << " of type " << e.type()
        << " did not produce llvm IR of the corresponding llvm type.\n";
    return value;
}

void CodeGen_LLVM::codegen(const Stmt &s) {
    internal_assert(s.defined());
    debug(4) << "Codegen: " << s << "\n";
    value = nullptr;
    s.accept(this);
}
namespace {

bool is_power_of_two(int x) {
    return (x & (x - 1)) == 0;
}

int next_power_of_two(int x) {
    return static_cast<int>(1) << static_cast<int>(std::ceil(std::log2(x)));
}

}  // namespace

Type CodeGen_LLVM::upgrade_type_for_arithmetic(const Type &t) const {
    if (t.is_bfloat() || (t.is_float() && t.bits() < 32)) {
        return Float(32, t.lanes());
    } else if (t.is_int_or_uint() && !is_power_of_two(t.bits())) {
        return t.with_bits(next_power_of_two(t.bits()));
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
    } else if (t.is_int_or_uint() && !is_power_of_two(t.bits())) {
        return t.with_bits(next_power_of_two(t.bits()));
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
            string warn_msg =
                "(b)float16 type operation is emulated, which is likely to slow down the performance. "
                "If your target supports native (b)float16 operations, "
                "it could be improved by adding Target feature to enable it.\n";
            onetime_warnings.try_emplace(WarningKind::EmulatedFloat16, warn_msg);
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

    if (const Call *c = Call::as_intrinsic(op->value, {Call::lerp})) {
        // We want to codegen a cast of a lerp as a single thing, because it can
        // be done more intelligently than a lerp followed by a cast.
        Type t = upgrade_type_for_arithmetic(c->type);
        Type wt = upgrade_type_for_arithmetic(c->args[2].type());
        Expr e = lower_lerp(op->type,
                            cast(t, c->args[0]),
                            cast(t, c->args[1]),
                            cast(wt, c->args[2]),
                            target);
        codegen(e);
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

void CodeGen_LLVM::visit(const Reinterpret *op) {
    Type dst = op->type;
    llvm::Type *llvm_dst = llvm_type_of(dst);
    value = codegen(op->value);
    // Our `Reinterpret` expr directly maps to LLVM IR bitcast/ptrtoint/inttoptr
    // instructions with no additional handling required:
    // * bitcast between vectors and scalars is well-formed.
    // * ptrtoint/inttoptr implicitly truncates/zero-extends the integer
    //   to match the pointer size.
    value = builder->CreateBitOrPointerCast(value, llvm_dst);
}

void CodeGen_LLVM::visit(const Variable *op) {
    value = sym_get(op->name);
}

template<typename Op>
bool CodeGen_LLVM::try_to_fold_vector_reduce(const Expr &a, Expr b) {
    const VectorReduce *red = a.as<VectorReduce>();
    if (!red) {
        red = b.as<VectorReduce>();
        b = a;
    }
    if (red &&
        ((std::is_same<Op, Add>::value && red->op == VectorReduce::Add) ||
         (std::is_same<Op, Min>::value && red->op == VectorReduce::Min) ||
         (std::is_same<Op, Max>::value && red->op == VectorReduce::Max) ||
         (std::is_same<Op, Mul>::value && red->op == VectorReduce::Mul) ||
         (std::is_same<Op, And>::value && red->op == VectorReduce::And) ||
         (std::is_same<Op, Or>::value && red->op == VectorReduce::Or) ||
         (std::is_same<Op, Call>::value && red->op == VectorReduce::SaturatingAdd))) {
        codegen_vector_reduce(red, b);
        return true;
    }
    return false;
}

void CodeGen_LLVM::visit(const Add *op) {
    Type t = upgrade_type_for_arithmetic(op->type);
    if (t != op->type) {
        codegen(cast(op->type, Add::make(cast(t, op->a), cast(t, op->b))));
        return;
    }

    // Some backends can fold the add into a vector reduce
    if (try_to_fold_vector_reduce<Add>(op->a, op->b)) {
        return;
    }

    Value *a = codegen(op->a);
    Value *b = codegen(op->b);
    if (op->type.is_float()) {
        if (!try_vector_predication_intrinsic("llvm.vp.fadd", llvm_type_of(t), t.lanes(), AllEnabledMask(),
                                              {VPArg(a, 0), VPArg(b)})) {
            value = builder->CreateFAdd(a, b);
        }
    } else if (op->type.is_int() && op->type.bits() >= 32) {
        // We tell llvm integers don't wrap, so that it generates good
        // code for loop indices.
        // TODO(zvookin): This needs vector predication, but I can't
        // see a way to do it. May go away in introducing correct
        // index type instead of using int32_t.
        value = builder->CreateNSWAdd(a, b);
    } else {
        if (!try_vector_predication_intrinsic("llvm.vp.add", llvm_type_of(t), t.lanes(), AllEnabledMask(),
                                              {VPArg(a, 0), VPArg(b)})) {
            value = builder->CreateAdd(a, b);
        }
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
        if (!try_vector_predication_intrinsic("llvm.vp.fsub", llvm_type_of(t), t.lanes(), AllEnabledMask(),
                                              {VPArg(a, 0), VPArg(b)})) {
            value = builder->CreateFSub(a, b);
        }
    } else if (op->type.is_int() && op->type.bits() >= 32) {
        // We tell llvm integers don't wrap, so that it generates good
        // code for loop indices.
        // TODO(zvookin): This needs vector predication, but I can't
        // see a way to do it. May go away in introducing correct
        // index type instead of using int32_t.
        value = builder->CreateNSWSub(a, b);
    } else {
        if (!try_vector_predication_intrinsic("llvm.vp.sub", llvm_type_of(t), t.lanes(), AllEnabledMask(),
                                              {VPArg(a, 0), VPArg(b)})) {
            value = builder->CreateSub(a, b);
        }
    }
}

void CodeGen_LLVM::visit(const Mul *op) {
    Type t = upgrade_type_for_arithmetic(op->type);
    if (t != op->type) {
        codegen(cast(op->type, Mul::make(cast(t, op->a), cast(t, op->b))));
        return;
    }

    if (try_to_fold_vector_reduce<Mul>(op->a, op->b)) {
        return;
    }

    Value *a = codegen(op->a);
    Value *b = codegen(op->b);
    if (op->type.is_float()) {
        if (!try_vector_predication_intrinsic("llvm.vp.fmul", llvm_type_of(t), t.lanes(), AllEnabledMask(),
                                              {VPArg(a, 0), VPArg(b)})) {
            value = builder->CreateFMul(a, b);
        }
    } else if (op->type.is_int() && op->type.bits() >= 32) {
        // We tell llvm integers don't wrap, so that it generates good
        // code for loop indices.
        // TODO(zvookin): This needs vector predication, but I can't
        // see a way to do it. May go away in introducing correct
        // index type instead of using int32_t.
        value = builder->CreateNSWMul(a, b);
    } else {
        if (!try_vector_predication_intrinsic("llvm.vp.mul", llvm_type_of(t), t.lanes(), AllEnabledMask(),
                                              {VPArg(a, 0), VPArg(b)})) {
            value = builder->CreateMul(a, b);
        }
    }
}

void CodeGen_LLVM::visit(const Div *op) {
    user_assert(!is_const_zero(op->b)) << "Division by constant zero in expression: " << Expr(op) << "\n";

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
        if (!try_vector_predication_intrinsic("llvm.vp.fdiv", llvm_type_of(t), t.lanes(), AllEnabledMask(),
                                              {VPArg(a, 0), VPArg(b)})) {
            value = builder->CreateFDiv(a, b);
        }
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

    if (try_to_fold_vector_reduce<Min>(op->a, op->b)) {
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

    if (try_to_fold_vector_reduce<Max>(op->a, op->b)) {
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
        if (!try_vector_predication_comparison("llvm.vp.fcmp", op->type, AllEnabledMask(), a, b, "oeq")) {
            value = builder->CreateFCmpOEQ(a, b);
        }
    } else {
        if (!try_vector_predication_comparison("llvm.vp.icmp", op->type, AllEnabledMask(), a, b, "eq")) {
            value = builder->CreateICmpEQ(a, b);
        }
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
        if (!try_vector_predication_comparison("llvm.vp.fcmp", op->type, AllEnabledMask(), a, b, "one")) {
            value = builder->CreateFCmpONE(a, b);
        }
    } else {
        if (!try_vector_predication_comparison("llvm.vp.icmp", op->type, AllEnabledMask(), a, b, "ne")) {
            value = builder->CreateICmpNE(a, b);
        }
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
        if (!try_vector_predication_comparison("llvm.vp.fcmp", op->type, AllEnabledMask(), a, b, "olt")) {
            value = builder->CreateFCmpOLT(a, b);
        }
    } else if (t.is_int()) {
        if (!try_vector_predication_comparison("llvm.vp.icmp", op->type, AllEnabledMask(), a, b, "slt")) {
            value = builder->CreateICmpSLT(a, b);
        }
    } else {
        if (!try_vector_predication_comparison("llvm.vp.icmp", op->type, AllEnabledMask(), a, b, "ult")) {
            value = builder->CreateICmpULT(a, b);
        }
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
        if (!try_vector_predication_comparison("llvm.vp.fcmp", op->type, AllEnabledMask(), a, b, "ole")) {
            value = builder->CreateFCmpOLE(a, b);
        }
    } else if (t.is_int()) {
        if (!try_vector_predication_comparison("llvm.vp.icmp", op->type, AllEnabledMask(), a, b, "sle")) {
            value = builder->CreateICmpSLE(a, b);
        }
    } else {
        if (!try_vector_predication_comparison("llvm.vp.icmp", op->type, AllEnabledMask(), a, b, "ule")) {
            value = builder->CreateICmpULE(a, b);
        }
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
        if (!try_vector_predication_comparison("llvm.vp.fcmp", op->type, AllEnabledMask(), a, b, "ogt")) {
            value = builder->CreateFCmpOGT(a, b);
        }
    } else if (t.is_int()) {
        if (!try_vector_predication_comparison("llvm.vp.icmp", op->type, AllEnabledMask(), a, b, "sgt")) {
            value = builder->CreateICmpSGT(a, b);
        }
    } else {
        if (!try_vector_predication_comparison("llvm.vp.icmp", op->type, AllEnabledMask(), a, b, "ugt")) {
            value = builder->CreateICmpUGT(a, b);
        }
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
        if (!try_vector_predication_comparison("llvm.vp.fcmp", op->type, AllEnabledMask(), a, b, "oge")) {
            value = builder->CreateFCmpOGE(a, b);
        }
    } else if (t.is_int()) {
        if (!try_vector_predication_comparison("llvm.vp.icmp", op->type, AllEnabledMask(), a, b, "sge")) {
            value = builder->CreateICmpSGE(a, b);
        }
    } else {
        if (!try_vector_predication_comparison("llvm.vp.icmp", op->type, AllEnabledMask(), a, b, "uge")) {
            value = builder->CreateICmpUGE(a, b);
        }
    }
}

void CodeGen_LLVM::visit(const And *op) {
    if (try_to_fold_vector_reduce<And>(op->a, op->b)) {
        return;
    }

    Value *a = codegen(op->a);
    Value *b = codegen(op->b);
    if (!try_vector_predication_intrinsic("llvm.vp.and", llvm_type_of(op->type), op->type.lanes(),
                                          AllEnabledMask(), {VPArg(a, 0), VPArg(b)})) {
        value = builder->CreateAnd(a, b);
    }
}

void CodeGen_LLVM::visit(const Or *op) {
    if (try_to_fold_vector_reduce<Or>(op->a, op->b)) {
        return;
    }

    Value *a = codegen(op->a);
    Value *b = codegen(op->b);
    if (!try_vector_predication_intrinsic("llvm.vp.or", llvm_type_of(op->type), op->type.lanes(),
                                          AllEnabledMask(), {VPArg(a, 0), VPArg(b)})) {
        value = builder->CreateOr(a, b);
    }
}

void CodeGen_LLVM::visit(const Not *op) {
    Value *a = codegen(op->a);
    if (!try_vector_predication_intrinsic("llvm.vp.not", llvm_type_of(op->type), op->type.lanes(),
                                          AllEnabledMask(), {VPArg(a, 0)})) {
        value = builder->CreateNot(a);
    }
}

void CodeGen_LLVM::visit(const Select *op) {
    Value *cmp = codegen(op->condition);
    if (use_llvm_vp_intrinsics &&
        op->type.is_vector() &&
        op->condition.type().is_scalar()) {
        cmp = create_broadcast(cmp, op->type.lanes());
    }

    Value *a = codegen(op->true_value);
    Value *b = codegen(op->false_value);
    if (!try_vector_predication_intrinsic("llvm.vp.select", llvm_type_of(op->type), op->type.lanes(),
                                          NoMask(), {VPArg(cmp), VPArg(a, 0), VPArg(b)})) {
        value = builder->CreateSelect(cmp, a, b);
    }
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

    // Peel off a constant offset as a second GEP. This helps LLVM's
    // aliasing analysis, especially for backends that do address
    // computation in 32 bits but use 64-bit pointers.
    if (const Add *add = index.as<Add>()) {
        if (const int64_t *offset = as_const_int(add->b)) {
            Value *base = codegen_buffer_pointer(base_address, type, add->a);
            Value *off = codegen(make_const(Int(8 * d.getPointerSize()), *offset));
            return CreateInBoundsGEP(builder.get(), llvm_type_of(type), base, off);
        }
    }

    return codegen_buffer_pointer(base_address, type, codegen(index));
}

Value *CodeGen_LLVM::codegen_buffer_pointer(const string &buffer, Halide::Type type, Value *index) {
    // Find the base address from the symbol table
    Value *base_address = symbol_table.get(buffer);
    return codegen_buffer_pointer(base_address, type, index);
}

Value *CodeGen_LLVM::codegen_buffer_pointer(Value *base_address, Halide::Type type, Value *index) {
    type = upgrade_type_for_storage(type);
    llvm::Type *load_type = llvm_type_of(type);
    unsigned address_space = base_address->getType()->getPointerAddressSpace();
    llvm::Type *pointer_load_type = load_type->getPointerTo(address_space);

    // TODO: This can likely be removed once opaque pointers are default
    // in all supported LLVM versions.
    base_address = builder->CreatePointerCast(base_address, pointer_load_type);

    llvm::Constant *constant_index = dyn_cast<llvm::Constant>(index);
    if (constant_index && constant_index->isZeroValue()) {
        return base_address;
    }

    // Promote index to 64-bit on targets that use 64-bit pointers.
    llvm::DataLayout d(module.get());
    if (d.getPointerSize() == 8) {
        llvm::Type *index_type = index->getType();
        llvm::Type *desired_index_type = i64_t;
        if (isa<VectorType>(index_type)) {
            desired_index_type = VectorType::get(desired_index_type,
                                                 dyn_cast<VectorType>(index_type)->getElementCount());
        }
        index = builder->CreateIntCast(index, desired_index_type, true);
    }

    return CreateInBoundsGEP(builder.get(), load_type, base_address, index);
}

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

void CodeGen_LLVM::function_does_not_access_memory(llvm::Function *fn) {
#if LLVM_VERSION >= 160
    fn->addFnAttr("memory(none)");
#else
    fn->addFnAttr(llvm::Attribute::ReadNone);
#endif
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
    if (!is_const_one(op->predicate)) {
        codegen_predicated_load(op);
        return;
    }

    // There are several cases. Different architectures may wish to override some.
    if (op->type.is_scalar()) {
        // Scalar loads
        Value *ptr = codegen_buffer_pointer(op->name, op->type, op->index);
        LoadInst *load = builder->CreateAlignedLoad(llvm_type_of(op->type), ptr, llvm::Align(op->type.bytes()));
        add_tbaa_metadata(load, op->name, op->index);
        value = load;
    } else {
        const Ramp *ramp = op->index.as<Ramp>();
        const IntImm *stride = ramp ? ramp->stride.as<IntImm>() : nullptr;

        llvm::Type *load_type = llvm_type_of(op->type.element_of());
        if (ramp && stride && stride->value == 1) {
            value = codegen_dense_vector_load(op);
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
            value = PoisonValue::get(llvm_type_of(op->type));
            for (int i = 0; i < ramp->lanes; i++) {
                Value *lane = ConstantInt::get(i32_t, i);
                LoadInst *val = builder->CreateLoad(load_type, ptr);
                add_tbaa_metadata(val, op->name, op->index);
                value = builder->CreateInsertElement(value, val, lane);
                ptr = CreateInBoundsGEP(builder.get(), load_type, ptr, stride);
            }
        } else if ((false)) { /* should_scalarize(op->index) */
            // TODO: put something sensible in for
            // should_scalarize. Probably a good idea if there are no
            // loads in it, and it's all int32.

            // Compute the index as scalars, and then do a gather
            Value *vec = PoisonValue::get(llvm_type_of(op->type));
            for (int i = 0; i < op->type.lanes(); i++) {
                Expr idx = extract_lane(op->index, i);
                Value *ptr = codegen_buffer_pointer(op->name, op->type.element_of(), idx);
                LoadInst *val = builder->CreateLoad(load_type, ptr);
                add_tbaa_metadata(val, op->name, op->index);
                vec = builder->CreateInsertElement(vec, val, ConstantInt::get(i32_t, i));
            }
            value = vec;
        } else {
            // General gathers
            Value *index = codegen(op->index);
            Value *vec = PoisonValue::get(llvm_type_of(op->type));
            for (int i = 0; i < op->type.lanes(); i++) {
                Value *idx = builder->CreateExtractElement(index, ConstantInt::get(i32_t, i));
                Value *ptr = codegen_buffer_pointer(op->name, op->type.element_of(), idx);
                LoadInst *val = builder->CreateLoad(load_type, ptr);
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
    } else if (!is_const(op->stride)) {
        Expr broadcast_base = Broadcast::make(op->base, op->lanes);
        Expr broadcast_stride = Broadcast::make(op->stride, op->lanes);
        Expr ramp = Ramp::make(make_zero(op->base.type()), make_one(op->base.type()), op->lanes);
        value = codegen(broadcast_base + broadcast_stride * ramp);
    } else {
        internal_assert(is_const(op->base) && is_const(op->stride));
        // At this point base and stride should be constant. Generate
        // an insert element sequence. The code will be lifted to a
        // constant vector stored in .rodata or similar.
        Value *base = codegen(op->base);
        Value *stride = codegen(op->stride);

        value = PoisonValue::get(llvm_type_of(op->type));
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
    Constant *poison = PoisonValue::get(get_vector_type(v->getType(), lanes));
    Constant *zero = ConstantInt::get(i32_t, 0);
    v = builder->CreateInsertElement(poison, v, zero);
    Constant *zeros = get_splat(lanes, zero);
    return builder->CreateShuffleVector(v, poison, zeros);
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

    Value *result = PoisonValue::get(result_type);

    for (int i = 0; i < e.type().lanes(); i++) {
        Value *v = codegen(extract_lane(e, i));
        result = builder->CreateInsertElement(result, v, ConstantInt::get(i32_t, i));
    }
    value = result;
}

void CodeGen_LLVM::codegen_predicated_store(const Store *op) {
    const Ramp *ramp = op->index.as<Ramp>();
    if (ramp && is_const_one(ramp->stride) && !emit_atomic_stores) {  // Dense vector store
        debug(4) << "Predicated dense vector store\n\t" << Stmt(op) << "\n";
        Value *vpred = codegen(op->predicate);
        Halide::Type value_type = op->value.type();
        Value *val = codegen(op->value);
        int alignment = value_type.bytes();
        int native_bytes = native_vector_bits() / 8;

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
        if (op->param.defined()) {
            int host_alignment = op->param.host_alignment();
            alignment = gcd(alignment, host_alignment);
        }

        // For dense vector stores wider than the native vector
        // width, bust them up into native vectors.
        int store_lanes = value_type.lanes();
        int native_lanes = maximum_vector_bits() / value_type.bits();

        for (int i = 0; i < store_lanes; i += native_lanes) {
            int slice_lanes = std::min(native_lanes, store_lanes - i);
            Expr slice_base = simplify(ramp->base + i);
            Expr slice_stride = make_one(slice_base.type());
            Expr slice_index = slice_lanes == 1 ? slice_base : Ramp::make(slice_base, slice_stride, slice_lanes);
            Value *slice_val = slice_vector(val, i, slice_lanes);
            Value *elt_ptr = codegen_buffer_pointer(op->name, value_type.element_of(), slice_base);
            Value *vec_ptr = builder->CreatePointerCast(elt_ptr, slice_val->getType()->getPointerTo());

            Value *slice_mask = slice_vector(vpred, i, slice_lanes);
            Instruction *store;
            if (try_vector_predication_intrinsic("llvm.vp.store", void_t, slice_lanes, slice_mask,
                                                 {VPArg(slice_val, 0), VPArg(vec_ptr, 1, alignment)})) {
                store = dyn_cast<Instruction>(value);
            } else {
                store = builder->CreateMaskedStore(slice_val, vec_ptr, llvm::Align(alignment), slice_mask);
            }
            add_tbaa_metadata(store, op->name, slice_index);
        }
    } else {  // It's not dense vector store, we need to scalarize it
        debug(4) << "Scalarize predicated vector store\n";
        Type value_type = op->value.type().element_of();
        Value *vpred = codegen(op->predicate);
        Value *vval = codegen(op->value);
        Value *vindex = codegen(op->index);
        for (int i = 0; i < op->index.type().lanes(); i++) {
            Constant *lane = ConstantInt::get(i32_t, i);
            Value *p = vpred;
            Value *v = vval;
            Value *idx = vindex;
            if (op->index.type().lanes() > 1) {
                p = builder->CreateExtractElement(p, lane);
                v = builder->CreateExtractElement(v, lane);
                idx = builder->CreateExtractElement(idx, lane);
            }
            internal_assert(p && v && idx);

            if (p->getType() != i1_t) {
                p = builder->CreateIsNotNull(p);
            }

            BasicBlock *true_bb = BasicBlock::Create(*context, "true_bb", function);
            BasicBlock *after_bb = BasicBlock::Create(*context, "after_bb", function);
            builder->CreateCondBr(p, true_bb, after_bb);

            builder->SetInsertPoint(true_bb);

            // Scalar
            Value *ptr = codegen_buffer_pointer(op->name, value_type, idx);
            StoreInst *store = builder->CreateAlignedStore(v, ptr, llvm::Align(value_type.bytes()));
            if (emit_atomic_stores) {
                store->setAtomic(AtomicOrdering::Monotonic);
            }

            builder->CreateBr(after_bb);
            builder->SetInsertPoint(after_bb);
        }
    }
}

llvm::Value *CodeGen_LLVM::codegen_vector_load(const Type &type, const std::string &name, const Expr &base,
                                               const Buffer<> &image, const Parameter &param, const ModulusRemainder &alignment,
                                               llvm::Value *vpred, bool slice_to_native, llvm::Value *stride) {
    debug(4) << "Vectorize predicated dense vector load:\n\t"
             << "(" << type << ")" << name << "[ramp(base, 1, " << type.lanes() << ")]\n";

    int align_bytes = type.bytes();  // The size of a single element

    int native_bits = native_vector_bits();
    int native_bytes = native_bits / 8;

    // We assume halide_malloc for the platform returns buffers
    // aligned to at least the native vector width. So this is the
    // maximum alignment we can infer based on the index alone.

    // Boost the alignment if possible, up to the native vector width.
    ModulusRemainder mod_rem = alignment;
    while ((mod_rem.remainder & 1) == 0 &&
           (mod_rem.modulus & 1) == 0 &&
           align_bytes < native_bytes) {
        mod_rem.modulus /= 2;
        mod_rem.remainder /= 2;
        align_bytes *= 2;
    }

    // If it is an external buffer, then we cannot assume that the host pointer
    // is aligned to at least native vector width. However, we may be able to do
    // better than just assuming that it is unaligned.
    if (param.defined()) {
        int host_alignment = param.host_alignment();
        align_bytes = gcd(align_bytes, host_alignment);
    } else if (get_target().has_feature(Target::JIT) && image.defined()) {
        // If we're JITting, use the actual pointer value to determine alignment for embedded buffers.
        align_bytes = gcd(align_bytes, (int)(((uintptr_t)image.data()) & std::numeric_limits<int>::max()));
    }

    // For dense vector loads wider than the native vector
    // width, bust them up into native vectors
    int load_lanes = type.lanes();
    int native_lanes = slice_to_native ? std::max(1, maximum_vector_bits() / type.bits()) : load_lanes;
    vector<Value *> slices;
    for (int i = 0; i < load_lanes; i += native_lanes) {
        int slice_lanes = std::min(native_lanes, load_lanes - i);
        Expr slice_base = simplify(base + i);
        Expr slice_stride = make_one(slice_base.type());
        Expr slice_index = slice_lanes == 1 ? slice_base : Ramp::make(slice_base, slice_stride, slice_lanes);
        llvm::Type *slice_type = get_vector_type(llvm_type_of(type.element_of()), slice_lanes);
        Value *elt_ptr = codegen_buffer_pointer(name, type.element_of(), slice_base);
        Value *vec_ptr = builder->CreatePointerCast(elt_ptr, slice_type->getPointerTo());

        Value *slice_mask = (vpred != nullptr) ? slice_vector(vpred, i, slice_lanes) : nullptr;
        MaskVariant vp_slice_mask = slice_mask ? MaskVariant(slice_mask) : AllEnabledMask();

        Instruction *load_inst = nullptr;
        // In this path, strided predicated loads are only handled if vector
        // predication is enabled. Otherwise this would be scalarized at a higher
        // level. Assume that if stride is passed, this is not dense, though
        // LLVM should codegen the same thing for a constant 1 strided load as
        // for a non-strided load.
        if (stride) {
            if (get_target().bits == 64 && !stride->getType()->isIntegerTy(64)) {
                stride = builder->CreateIntCast(stride, i64_t, true);
            }
            if (try_vector_predication_intrinsic("llvm.experimental.vp.strided.load", VPResultType(slice_type, 0),
                                                 slice_lanes, vp_slice_mask,
                                                 {VPArg(vec_ptr, 1, align_bytes), VPArg(stride, 1)})) {
                load_inst = dyn_cast<Instruction>(value);
            } else {
                internal_error << "Vector predicated strided load should not be requested if not supported.\n";
            }
        } else {
            if (try_vector_predication_intrinsic("llvm.vp.load", VPResultType(slice_type, 0), slice_lanes, vp_slice_mask,
                                                 {VPArg(vec_ptr, 1, align_bytes)})) {
                load_inst = dyn_cast<Instruction>(value);
            } else {
                if (slice_mask != nullptr) {
                    load_inst = builder->CreateMaskedLoad(slice_type, vec_ptr, llvm::Align(align_bytes), slice_mask);
                } else {
                    load_inst = builder->CreateAlignedLoad(slice_type, vec_ptr, llvm::Align(align_bytes));
                }
            }
        }
        add_tbaa_metadata(load_inst, name, slice_index);
        slices.push_back(load_inst);
    }
    value = concat_vectors(slices);
    return value;
}

Value *CodeGen_LLVM::codegen_dense_vector_load(const Load *load, Value *vpred, bool slice_to_native) {
    const Ramp *ramp = load->index.as<Ramp>();
    internal_assert(ramp && is_const_one(ramp->stride)) << "Should be dense vector load\n";

    return codegen_vector_load(load->type, load->name, ramp->base, load->image, load->param,
                               load->alignment, vpred, slice_to_native, nullptr);
}

void CodeGen_LLVM::codegen_predicated_load(const Load *op) {
    const Ramp *ramp = op->index.as<Ramp>();
    const IntImm *stride = ramp ? ramp->stride.as<IntImm>() : nullptr;

    if (ramp && is_const_one(ramp->stride)) {  // Dense vector load
        Value *vpred = codegen(op->predicate);
        value = codegen_dense_vector_load(op, vpred);
    } else if (use_llvm_vp_intrinsics && stride) {  // Case only handled by vector predication, otherwise must scalarize.
        Value *vpred = codegen(op->predicate);
        Value *llvm_stride = codegen(stride);  // Not 1 (dense) as that was caught above.
        value = codegen_vector_load(op->type, op->name, ramp->base, op->image, op->param,
                                    op->alignment, vpred, true, llvm_stride);
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
                                    {op->predicate, load_expr},
                                    Internal::Call::PureIntrinsic);
        value = codegen(pred_load);
    }
}

void CodeGen_LLVM::codegen_atomic_rmw(const Store *op) {
    // TODO: predicated store (see https://github.com/halide/Halide/issues/4298).
    user_assert(is_const_one(op->predicate)) << "Atomic predicated store is not supported.\n";

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
            if (value_type.is_float()) {
                builder->CreateAtomicRMW(AtomicRMWInst::FAdd, ptr, val, llvm::MaybeAlign(), AtomicOrdering::Monotonic);
            } else {
                builder->CreateAtomicRMW(AtomicRMWInst::Add, ptr, val, llvm::MaybeAlign(), AtomicOrdering::Monotonic);
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
                    builder->CreateAtomicRMW(AtomicRMWInst::FAdd, ptr, v, llvm::MaybeAlign(), AtomicOrdering::Monotonic);
                } else {
                    builder->CreateAtomicRMW(AtomicRMWInst::Add, ptr, v, llvm::MaybeAlign(), AtomicOrdering::Monotonic);
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
            llvm::Type *load_type = llvm_type_of(value_type.element_of());
            LoadInst *orig = builder->CreateAlignedLoad(load_type, ptr, llvm::Align(value_type.bytes()));
            orig->setOrdering(AtomicOrdering::Monotonic);
            add_tbaa_metadata(orig, op->name, op->index);
            // Explicit fall through from the current block to the cas loop body.
            builder->CreateBr(loop_bb);

            // CAS loop body:
            builder->SetInsertPoint(loop_bb);
            PHINode *cmp = builder->CreatePHI(load_type, 2, "loaded");
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
                unsigned int addr_space = ptr->getType()->getPointerAddressSpace();
                ptr = builder->CreateBitCast(ptr, int_type->getPointerTo(addr_space));
                val = builder->CreateBitCast(val, int_type);
                cmp_val = builder->CreateBitCast(cmp_val, int_type);
            }
            Value *cmpxchg_pair = builder->CreateAtomicCmpXchg(
                ptr, cmp_val, val, llvm::MaybeAlign(), AtomicOrdering::Monotonic, AtomicOrdering::Monotonic);
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

    value = call_overloaded_intrin(op->type, op->name, op->args);
    if (value) {
        return;
    }

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
        if (!try_vector_predication_intrinsic("llvm.vp.and", llvm_type_of(op->type), op->type.lanes(),
                                              AllEnabledMask(), {VPArg(a, 0), VPArg(b)})) {
            value = builder->CreateAnd(a, b);
        }
    } else if (op->is_intrinsic(Call::bitwise_xor)) {
        internal_assert(op->args.size() == 2);
        Value *a = codegen(op->args[0]);
        Value *b = codegen(op->args[1]);
        if (!try_vector_predication_intrinsic("llvm.vp.xor", llvm_type_of(op->type), op->type.lanes(),
                                              AllEnabledMask(), {VPArg(a, 0), VPArg(b)})) {
            value = builder->CreateXor(a, b);
        }
    } else if (op->is_intrinsic(Call::bitwise_or)) {
        internal_assert(op->args.size() == 2);
        Value *a = codegen(op->args[0]);
        Value *b = codegen(op->args[1]);
        if (!try_vector_predication_intrinsic("llvm.vp.or", llvm_type_of(op->type), op->type.lanes(),
                                              AllEnabledMask(), {VPArg(a, 0), VPArg(b)})) {
            value = builder->CreateOr(a, b);
        }
    } else if (op->is_intrinsic(Call::bitwise_not)) {
        internal_assert(op->args.size() == 1);
        Value *a = codegen(op->args[0]);
        if (!try_vector_predication_intrinsic("llvm.vp.not", llvm_type_of(op->type), op->type.lanes(),
                                              AllEnabledMask(), {VPArg(a, 0)})) {
            value = builder->CreateNot(a);
        }
    } else if (op->is_intrinsic(Call::shift_left)) {
        internal_assert(op->args.size() == 2);
        if (op->args[1].type().is_uint()) {
            Value *a = codegen(op->args[0]);
            Value *b = codegen(op->args[1]);
            if (!try_vector_predication_intrinsic("llvm.vp.shl", llvm_type_of(op->type), op->type.lanes(),
                                                  AllEnabledMask(), {VPArg(a, 0), VPArg(b)})) {
                value = builder->CreateShl(a, b);
            }
        } else {
            value = codegen(lower_signed_shift_left(op->args[0], op->args[1]));
        }
    } else if (op->is_intrinsic(Call::shift_right)) {
        internal_assert(op->args.size() == 2);
        if (op->args[1].type().is_uint()) {
            Value *a = codegen(op->args[0]);
            Value *b = codegen(op->args[1]);
            if (op->type.is_int()) {
                if (!try_vector_predication_intrinsic("llvm.vp.ashr", llvm_type_of(op->type), op->type.lanes(),
                                                      AllEnabledMask(), {VPArg(a, 0), VPArg(b)})) {
                    value = builder->CreateAShr(a, b);
                }
            } else {
                if (!try_vector_predication_intrinsic("llvm.vp.lshr", llvm_type_of(op->type), op->type.lanes(),
                                                      AllEnabledMask(), {VPArg(a, 0), VPArg(b)})) {
                    value = builder->CreateLShr(a, b);
                }
            }
        } else {
            value = codegen(lower_signed_shift_right(op->args[0], op->args[1]));
        }
    } else if (op->is_intrinsic(Call::abs)) {
        internal_assert(op->args.size() == 1);
        // Generate select(x >= 0, x, -x) instead
        string x_name = unique_name('x');
        Expr x = Variable::make(op->args[0].type(), x_name);
        value = codegen(Let::make(x_name, op->args[0], select(x >= 0, x, -x)));
    } else if (op->is_intrinsic(Call::absd)) {
        internal_assert(op->args.size() == 2);
        Expr a = op->args[0];
        Expr b = op->args[1];
        string a_name = unique_name('a');
        string b_name = unique_name('b');
        Expr a_var = Variable::make(op->args[0].type(), a_name);
        Expr b_var = Variable::make(op->args[1].type(), b_name);
        Expr cond = a_var < b_var;
        // Cast to unsigned because we want wrapping semantics on the subtract
        // in the signed case.
        a_var = cast(op->type, a_var);
        b_var = cast(op->type, b_var);
        codegen(Let::make(a_name, op->args[0],
                          Let::make(b_name, op->args[1],
                                    Select::make(cond, b_var - a_var, a_var - b_var))));
    } else if (op->is_intrinsic(Call::div_round_to_zero)) {
        // See if we can rewrite it to something faster (e.g. a shift)
        Expr e = lower_int_uint_div(op->args[0], op->args[1], /** round to zero */ true);
        if (!e.as<Call>()) {
            codegen(e);
            return;
        }
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
    } else if (op->is_intrinsic(Call::lerp)) {
        internal_assert(op->args.size() == 3);
        // If we need to upgrade the type, do the entire lerp in the
        // upgraded type for better precision.
        // TODO: This might be surprising behavior?
        Type t = upgrade_type_for_arithmetic(op->type);
        Type wt = upgrade_type_for_arithmetic(op->args[2].type());
        Expr e = lower_lerp(op->type,
                            cast(t, op->args[0]),
                            cast(t, op->args[1]),
                            cast(wt, op->args[2]),
                            target);
        codegen(e);
    } else if (op->is_intrinsic(Call::popcount)) {
        internal_assert(op->args.size() == 1);
        std::vector<llvm::Type *> arg_type(1);
        arg_type[0] = llvm_type_of(op->args[0].type());
        llvm::Function *fn = llvm::Intrinsic::getDeclaration(module.get(), llvm::Intrinsic::ctpop, arg_type);
        Value *a = codegen(op->args[0]);
        CallInst *call = builder->CreateCall(fn, a);
        value = call;
    } else if (op->is_intrinsic(Call::count_leading_zeros) ||
               op->is_intrinsic(Call::count_trailing_zeros)) {
        internal_assert(op->args.size() == 1);
        std::vector<llvm::Type *> arg_type(1);
        arg_type[0] = llvm_type_of(op->args[0].type());
        llvm::Function *fn = llvm::Intrinsic::getDeclaration(module.get(),
                                                             (op->is_intrinsic(Call::count_leading_zeros)) ? llvm::Intrinsic::ctlz : llvm::Intrinsic::cttz,
                                                             arg_type);
        llvm::Value *is_const_zero_poison = llvm::ConstantInt::getFalse(*context);
        llvm::Value *args[2] = {codegen(op->args[0]), is_const_zero_poison};
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

            internal_assert(op->args.size() == 2 || op->args.size() == 3);

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
            Value *false_value = codegen(op->args.size() == 3 ? op->args[2] : make_zero(op->type));
            builder->CreateBr(after_bb);
            BasicBlock *false_pred = builder->GetInsertBlock();

            builder->SetInsertPoint(after_bb);
            PHINode *phi = builder->CreatePHI(true_value->getType(), 2);
            phi->addIncoming(true_value, true_pred);
            phi->addIncoming(false_value, false_pred);

            value = phi;
        }
    } else if (op->is_intrinsic(Call::round)) {
        value = codegen(lower_round_to_nearest_ties_to_even(op->args[0]));
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
                llvm::Type *aggregate_t = (all_same_type ? (llvm::Type *)ArrayType::get(types[0], types.size()) : (llvm::Type *)llvm::StructType::get(*context, types));

                value = create_alloca_at_entry(aggregate_t, 1);
                struct_type_recovery[value] = aggregate_t;
                for (size_t i = 0; i < args.size(); i++) {
                    Value *elem_ptr = builder->CreateConstInBoundsGEP2_32(aggregate_t, value, 0, i);
                    builder->CreateStore(args[i], elem_ptr);
                }
            }
        }
    } else if (op->is_intrinsic(Call::load_typed_struct_member)) {
        // Given a void * instance of a typed struct, an in-scope prototype
        // struct of the same type, and the index of a slot, load the value of
        // that slot.
        //
        // It is assumed that the slot index is valid for the given typed struct.
        //
        // TODO: this comment is replicated in CodeGen_LLVM and should be updated there too.
        // TODO: https://github.com/halide/Halide/issues/6468
        internal_assert(op->args.size() == 3);
        llvm::Value *struct_instance = codegen(op->args[0]);
        llvm::Value *struct_prototype = codegen(op->args[1]);
        llvm::Value *typed_struct_instance = builder->CreatePointerCast(struct_instance, struct_prototype->getType());
        const int64_t *index = as_const_int(op->args[2]);

        // make_struct can use a fixed-size struct, an array type, or a scalar
        llvm::Type *pointee_type;
        auto iter = struct_type_recovery.find(struct_prototype);
        if (iter != struct_type_recovery.end()) {
            pointee_type = iter->second;
        } else {
            pointee_type = llvm_type_of(op->type);
        }
        llvm::StructType *struct_type = llvm::dyn_cast<llvm::StructType>(pointee_type);
        llvm::Type *array_type = llvm::dyn_cast<llvm::ArrayType>(pointee_type);
        if (struct_type || array_type) {
            internal_assert(index != nullptr);
            llvm::Value *gep = CreateInBoundsGEP(builder.get(), pointee_type, typed_struct_instance,
                                                 {ConstantInt::get(i32_t, 0),
                                                  ConstantInt::get(i32_t, (int)*index)});
            llvm::Type *result_type = struct_type ? struct_type->getElementType(*index) : array_type->getArrayElementType();
            value = builder->CreateLoad(result_type, gep);
        } else {
            // The struct is actually just a scalar
            internal_assert(index == nullptr || *index == 0);
            value = builder->CreateLoad(pointee_type, typed_struct_instance);
        }
    } else if (op->is_intrinsic(Call::get_user_context)) {
        internal_assert(op->args.empty());
        value = get_user_context();
    } else if (op->is_intrinsic(Call::saturating_add) || op->is_intrinsic(Call::saturating_sub)) {
        internal_assert(op->args.size() == 2);

        // Try to fold the vector reduce for a call to saturating_add
        const bool folded = op->is_intrinsic(Call::saturating_add) && try_to_fold_vector_reduce<Call>(op->args[0], op->args[1]);

        if (!folded) {
            std::string intrin;
            if (op->type.is_int()) {
                intrin = "llvm.s";
            } else {
                internal_assert(op->type.is_uint());
                intrin = "llvm.u";
            }
            if (op->is_intrinsic(Call::saturating_add)) {
                intrin += "add.sat.";
            } else {
                internal_assert(op->is_intrinsic(Call::saturating_sub));
                intrin += "sub.sat.";
            }
            if (op->type.lanes() > 1) {
                int lanes = op->type.lanes();
                llvm::Type *llvm_type = llvm_type_of(op->type);
                if (isa<ScalableVectorType>(llvm_type)) {
                    internal_assert((effective_vscale != 0) && ((lanes % effective_vscale) == 0));
                    intrin += "nx";
                    lanes /= effective_vscale;
                }
                intrin += "v" + std::to_string(lanes);
            }
            intrin += "i" + std::to_string(op->type.bits());
            value = call_intrin(op->type, op->type.lanes(), intrin, op->args);
        }
    } else if (op->is_intrinsic(Call::stringify)) {
        internal_assert(!op->args.empty());

        if (op->type.is_vector()) {
            scalarize(op);
        } else {

            // Compute the maximum possible size of the message.
            int buf_size = 1;  // One for the terminating zero.
            for (const auto &arg : op->args) {
                Type t = arg.type();
                if (arg.as<StringImm>()) {
                    buf_size += arg.as<StringImm>()->value.size();
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
            buf_size = std::min(8 * 1024, buf_size);

            // Allocate a stack array to hold the message.
            llvm::Value *buf = create_alloca_at_entry(i8_t, buf_size);

            llvm::Value *dst = buf;
            llvm::Value *buf_end = CreateConstGEP1_32(builder.get(), i8_t, buf, buf_size);

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

            for (const auto &arg : op->args) {
                const StringImm *s = arg.as<StringImm>();
                Type t = arg.type();
                internal_assert(t.lanes() == 1);
                vector<Value *> call_args(2);
                call_args[0] = dst;
                call_args[1] = buf_end;

                if (s) {
                    call_args.push_back(codegen(arg));
                    dst = builder->CreateCall(append_string, call_args);
                } else if (t.is_bool()) {
                    Value *a = codegen(arg);
                    Value *t = codegen(StringImm::make("true"));
                    Value *f = codegen(StringImm::make("false"));
                    call_args.push_back(builder->CreateSelect(a, t, f));
                    dst = builder->CreateCall(append_string, call_args);
                } else if (t.is_int()) {
                    call_args.push_back(codegen(Cast::make(Int(64), arg)));
                    call_args.push_back(ConstantInt::get(i32_t, 1));
                    dst = builder->CreateCall(append_int64, call_args);
                } else if (t.is_uint()) {
                    call_args.push_back(codegen(Cast::make(UInt(64), arg)));
                    call_args.push_back(ConstantInt::get(i32_t, 1));
                    dst = builder->CreateCall(append_uint64, call_args);
                } else if (t.is_float()) {
                    call_args.push_back(codegen(Cast::make(Float(64), arg)));
                    // Use scientific notation for doubles
                    call_args.push_back(ConstantInt::get(i32_t, t.bits() == 64 ? 1 : 0));
                    dst = builder->CreateCall(append_double, call_args);
                } else if (t == type_of<halide_buffer_t *>()) {
                    Value *buf = codegen(arg);
                    buf = builder->CreatePointerCast(buf, append_buffer->getFunctionType()->getParamType(2));
                    call_args.push_back(buf);
                    dst = builder->CreateCall(append_buffer, call_args);
                } else {
                    internal_assert(t.is_handle());
                    Value *ptr = codegen(arg);
                    ptr = builder->CreatePointerCast(ptr, i8_t->getPointerTo());
                    call_args.push_back(ptr);
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
        const int64_t *sz = as_const_int(op->args[0]);
        if (op->type == type_of<struct halide_buffer_t *>() &&
            call && call->is_intrinsic(Call::size_of_halide_buffer_t)) {
            value = create_alloca_at_entry(halide_buffer_t_type, 1);
        } else if (op->type == type_of<struct halide_semaphore_t *>() &&
                   semaphore_t_type != nullptr &&
                   sz && *sz == 16) {
            value = create_alloca_at_entry(semaphore_t_type, 1);
        } else {
            internal_assert(sz != nullptr);
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
        auto *const base_fn = sub_fns.back().fn;
        const string global_name = unique_name(base_fn->getName().str() + "_indirect_fn_ptr");
        GlobalVariable *global = new GlobalVariable(
            *module,
            base_fn->getType(),
            /*isConstant*/ false,
            GlobalValue::PrivateLinkage,
            ConstantPointerNull::get(base_fn->getType()),
            global_name);
        LoadInst *loaded_value = builder->CreateLoad(base_fn->getType(), global);

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
        user_assert((op->args.size() == 4) && is_const_one(op->args[2]))
            << "Only prefetch of 1 cache line is supported.\n";

        const Expr &base_address = op->args[0];
        const Expr &base_offset = op->args[1];
        // const Expr &extent0 = op->args[2];  // unused
        // const Expr &stride0 = op->args[3];  // unused

        llvm::Function *prefetch_fn = module->getFunction("_halide_prefetch");
        internal_assert(prefetch_fn);

        vector<llvm::Value *> args;
        args.push_back(codegen_buffer_pointer(codegen(base_address), op->type, base_offset));
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
        user_error << "undef not eliminated before code generation. Please report this as a Halide bug.\n";
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
    } else if (is_float16_transcendental(op) && !supports_call_as_float16(op)) {
        value = codegen(lower_float16_transcendental_to_float32_equivalent(op));
    } else if (op->is_intrinsic(Call::mux)) {
        value = codegen(lower_mux(op));
    } else if (op->is_intrinsic(Call::extract_bits)) {
        value = codegen(lower_extract_bits(op));
    } else if (op->is_intrinsic(Call::concat_bits)) {
        value = codegen(lower_concat_bits(op));
    } else if (op->is_intrinsic()) {
        Expr lowered = lower_intrinsic(op);
        if (!lowered.defined()) {
            internal_error << "Unknown intrinsic " << op->name;
        }
        value = codegen(lowered);
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
               (op->name == "is_nan_f32" || op->name == "is_nan_f64" || op->name == "is_nan_f16")) {
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
               (op->name == "is_inf_f32" || op->name == "is_inf_f64" || op->name == "is_inf_f16")) {
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
               (op->name == "is_finite_f32" || op->name == "is_finite_f64" || op->name == "is_finite_f16")) {
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
                        t = get_vector_type(t, halide_arg.type().lanes());
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
                value = PoisonValue::get(result_type);
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
                                                nullptr, name);
    ArrayRef<unsigned char> data_array((const unsigned char *)&data[0], data.size());
    global->setInitializer(ConstantDataArray::get(*context, data_array));
    size_t alignment = 32;
    size_t native_vector_bytes = (size_t)(native_vector_bits() / 8);
    if (data.size() > alignment && native_vector_bytes > alignment) {
        alignment = native_vector_bytes;
    }
    global->setAlignment(llvm::Align(alignment));

    Constant *zero = ConstantInt::get(i32_t, 0);
    Constant *zeros[] = {zero, zero};
    Constant *ptr = ConstantExpr::getInBoundsGetElementPtr(type, global, zeros);
    return ptr;
}

void CodeGen_LLVM::create_assertion(Value *cond, const Expr &message, llvm::Value *error_code) {

    internal_assert(!message.defined() || message.type() == Int(32))
        << "Assertion result is not an int: " << message;

    if (target.has_feature(Target::NoAsserts)) {
        return;
    }

    // If the condition is a vector, fold it down to a scalar
    VectorType *vt = dyn_cast<VectorType>(cond->getType());
    if (vt) {
        Value *scalar_cond = builder->CreateExtractElement(cond, ConstantInt::get(i32_t, 0));
        for (int i = 1; i < get_vector_num_elements(vt); i++) {
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
    if (!error_code) {
        error_code = codegen(message);
    }

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

    // TODO(zvookin): remove this after validating it doesn't happen
    internal_assert(!(op->for_type == ForType::Parallel ||
                      (op->for_type == ForType::Serial &&
                       acquire &&
                       !expr_uses_var(acquire->count, op->name))));

    if (op->for_type == ForType::Serial) {

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

void CodeGen_LLVM::visit(const Store *op) {
    if (!emit_atomic_stores) {
        // Peel lets off the index to make us more likely to pattern
        // match a ramp.
        if (const Let *let = op->index.as<Let>()) {
            Stmt s = Store::make(op->name, op->value, let->body, op->param, op->predicate, op->alignment);
            codegen(LetStmt::make(let->name, let->value, s));
            return;
        }
    }

    // Fix up the type
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

    bool recursive = (expr_uses_var(op->index, op->name) ||
                      expr_uses_var(op->value, op->name));
    // Issue atomic store if we are inside an atomic node.
    if (emit_atomic_stores && recursive) {
        codegen_atomic_rmw(op);
        return;
    }

    // Predicated store.
    if (!is_const_one(op->predicate)) {
        codegen_predicated_store(op);
        return;
    }

    auto annotate_store = [&](StoreInst *store, const Expr &index) {
        add_tbaa_metadata(store, op->name, index);
        if (emit_atomic_stores) {
            store->setAtomic(AtomicOrdering::Monotonic);
        }
    };

    Value *val = codegen(op->value);

    if (value_type.is_scalar()) {
        // Scalar
        Value *ptr = codegen_buffer_pointer(op->name, value_type, op->index);
        StoreInst *store = builder->CreateAlignedStore(val, ptr, llvm::Align(value_type.bytes()));
        annotate_store(store, op->index);
    } else if (const Let *let = op->index.as<Let>()) {
        Stmt s = Store::make(op->name, op->value, let->body, op->param, op->predicate, op->alignment);
        codegen(LetStmt::make(let->name, let->value, s));
    } else {
        int alignment = value_type.bytes();
        const Ramp *ramp = op->index.as<Ramp>();
        // TODO(zvookin): consider splitting out vector predication path. Current
        // code shows how vector predication would simplify things as the
        // following scalarization cases would go away.
        bool is_dense = ramp && is_const_one(ramp->stride);
        if (use_llvm_vp_intrinsics || is_dense) {

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
            if (op->param.defined()) {
                int host_alignment = op->param.host_alignment();
                alignment = gcd(alignment, host_alignment);
            }

            // For dense vector stores wider than the native vector
            // width, bust them up into native vectors.
            int store_lanes = value_type.lanes();
            int native_lanes = maximum_vector_bits() / value_type.bits();

            Expr base = (ramp != nullptr) ? ramp->base : 0;
            Expr stride = (ramp != nullptr) ? ramp->stride : 0;
            Value *stride_val = (!is_dense && ramp != nullptr) ? codegen(stride) : nullptr;
            Value *index = (ramp == nullptr) ? codegen(op->index) : nullptr;

            for (int i = 0; i < store_lanes; i += native_lanes) {
                int slice_lanes = std::min(native_lanes, store_lanes - i);
                Expr slice_base = simplify(base + i * stride);
                Expr slice_stride = make_one(slice_base.type());
                Expr slice_index = slice_lanes == 1 ? slice_base : Ramp::make(slice_base, slice_stride, slice_lanes);
                Value *slice_val = slice_vector(val, i, slice_lanes);
                Value *elt_ptr = codegen_buffer_pointer(op->name, value_type.element_of(), slice_base);
                Value *vec_ptr = builder->CreatePointerCast(elt_ptr, slice_val->getType()->getPointerTo());
                if (is_dense || slice_lanes == 1) {
                    if (try_vector_predication_intrinsic("llvm.vp.store", void_t, slice_lanes, AllEnabledMask(),
                                                         {VPArg(slice_val, 0), VPArg(vec_ptr, 1, alignment)})) {
                        add_tbaa_metadata(dyn_cast<Instruction>(value), op->name, slice_index);
                    } else {
                        StoreInst *store = builder->CreateAlignedStore(slice_val, vec_ptr, llvm::Align(alignment));
                        annotate_store(store, slice_index);
                    }
                } else if (ramp != nullptr) {
                    if (get_target().bits == 64 && !stride_val->getType()->isIntegerTy(64)) {
                        stride_val = builder->CreateIntCast(stride_val, i64_t, true);
                    }
                    bool generated = try_vector_predication_intrinsic("llvm.experimental.vp.strided.store", void_t, slice_lanes, AllEnabledMask(),
                                                                      {VPArg(slice_val, 0), VPArg(vec_ptr, 1, alignment), VPArg(stride_val, 2)});
                    internal_assert(generated) << "Using vector predicated intrinsics, but code generation was not successful for strided store.\n";
                    add_tbaa_metadata(dyn_cast<Instruction>(value), op->name, slice_index);
                } else {
                    Value *slice_index = slice_vector(index, i, slice_lanes);
                    Value *vec_ptrs = codegen_buffer_pointer(op->name, value_type, slice_index);
                    bool generated = try_vector_predication_intrinsic("llvm.vp.scatter", void_t, slice_lanes, AllEnabledMask(),
                                                                      {VPArg(slice_val, 0), VPArg(vec_ptrs, 1, alignment)});
                    internal_assert(generated) << "Using vector predicated intrinsics, but code generation was not successful for gathering store.\n";
                }
            }
        } else if (ramp) {
            Type ptr_type = value_type.element_of();
            Value *ptr = codegen_buffer_pointer(op->name, ptr_type, ramp->base);
            const IntImm *const_stride = ramp->stride.as<IntImm>();
            Value *stride = codegen(ramp->stride);
            llvm::Type *load_type = llvm_type_of(ptr_type);
            // Scatter without generating the indices as a vector
            for (int i = 0; i < ramp->lanes; i++) {
                Constant *lane = ConstantInt::get(i32_t, i);
                Value *v = builder->CreateExtractElement(val, lane);
                if (const_stride) {
                    // Use a constant offset from the base pointer
                    Value *p =
                        builder->CreateConstInBoundsGEP1_32(
                            load_type, ptr,
                            const_stride->value * i);
                    StoreInst *store = builder->CreateStore(v, p);
                    annotate_store(store, op->index);
                } else {
                    // Increment the pointer by the stride for each element
                    StoreInst *store = builder->CreateStore(v, ptr);
                    annotate_store(store, op->index);
                    ptr = CreateInBoundsGEP(builder.get(), load_type, ptr, stride);
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
                annotate_store(store, op->index);
            }
        }
    }
}

void CodeGen_LLVM::codegen_asserts(const vector<const AssertStmt *> &asserts) {
    if (target.has_feature(Target::NoAsserts)) {
        return;
    }

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

    // Gather the conditions and values in an if-else chain
    vector<pair<Expr, Stmt>> blocks;
    Stmt final_else;
    const IfThenElse *next_if = op;
    do {
        blocks.emplace_back(next_if->condition,
                            next_if->then_case);
        final_else = next_if->else_case;
        next_if = final_else.defined() ? final_else.as<IfThenElse>() : nullptr;
    } while (next_if);

    // Check if we should use a switch statement or an if-else tree
    Expr lhs;
    bool use_switch = blocks.size() > 1;
    vector<int> rhs;
    for (auto &block : blocks) {
        const EQ *eq = block.first.as<EQ>();
        const int64_t *r = eq ? as_const_int(eq->b) : nullptr;
        if (eq &&
            r &&
            Int(32).can_represent(*r) &&
            is_pure(eq->a) &&
            is_const(eq->b) &&
            (!lhs.defined() || equal(lhs, eq->a))) {
            lhs = eq->a;
            rhs.push_back((int)*r);
        } else {
            use_switch = false;
        }
    }

    if (use_switch) {
        // Conditions are all of the form expr == constant for a
        // consistent expr and different constants. Use a switch
        // statement.

        BasicBlock *after_bb = BasicBlock::Create(*context, "after_bb", function);
        BasicBlock *default_bb = BasicBlock::Create(*context, "default_bb", function);

        auto *switch_inst = builder->CreateSwitch(codegen(lhs), default_bb, blocks.size());
        for (int i = 0; i < (int)blocks.size(); i++) {
            string name = "case_" + std::to_string(rhs[i]) + "_bb";
            BasicBlock *case_bb = BasicBlock::Create(*context, name, function);
            switch_inst->addCase(ConstantInt::get(IntegerType::get(*context, 32), rhs[i]), case_bb);
            builder->SetInsertPoint(case_bb);
            codegen(blocks[i].second);
            builder->CreateBr(after_bb);
        }

        builder->SetInsertPoint(default_bb);
        if (final_else.defined()) {
            codegen(final_else);
        }
        builder->CreateBr(after_bb);

        builder->SetInsertPoint(after_bb);
    } else {
        // Codegen an regular if-else chain using branches.

        BasicBlock *after_bb = BasicBlock::Create(*context, "after_bb", function);

        for (const auto &p : blocks) {
            BasicBlock *then_bb = BasicBlock::Create(*context, "then_bb", function);
            BasicBlock *next_bb = BasicBlock::Create(*context, "next_bb", function);
            builder->CreateCondBr(codegen(p.first), then_bb, next_bb);
            builder->SetInsertPoint(then_bb);
            codegen(p.second);
            builder->CreateBr(after_bb);
            builder->SetInsertPoint(next_bb);
        }

        if (final_else.defined()) {
            codegen(final_else);
        }
        builder->CreateBr(after_bb);

        builder->SetInsertPoint(after_bb);
    }
}

void CodeGen_LLVM::visit(const Evaluate *op) {
    codegen(op->value);

    // Discard result
    value = nullptr;
}

void CodeGen_LLVM::visit(const Shuffle *op) {
    vector<Value *> vecs;
    for (const Expr &e : op->vectors) {
        vecs.push_back(codegen(e));
    }

    if (op->is_interleave()) {
        value = interleave_vectors(vecs);
    } else if (op->is_concat()) {
        value = concat_vectors(vecs);
    } else {
        // If the even-numbered indices equal the odd-numbered
        // indices, only generate one and then do a self-interleave.
        for (int f : {4, 3, 2}) {
            bool self_interleave = (op->indices.size() % f) == 0;
            for (size_t i = 0; i < op->indices.size(); i++) {
                self_interleave &= (op->indices[i] == op->indices[i - (i % f)]);
            }
            if (self_interleave) {
                vector<int> sub_indices;
                for (size_t i = 0; i < op->indices.size(); i += f) {
                    sub_indices.push_back(op->indices[i]);
                }
                Expr equiv = Shuffle::make(op->vectors, sub_indices);
                value = codegen(equiv);
                value = interleave_vectors(std::vector<Value *>(f, value));
                return;
            }

            // Check for an interleave of slices (i.e. an in-vector transpose)
            bool interleave_of_slices = op->vectors.size() == 1 && (op->indices.size() % f) == 0;
            int step = op->type.lanes() / f;
            for (int i = 0; i < step; i++) {
                for (int j = 0; j < f; j++) {
                    interleave_of_slices &= (op->indices[i * f + j] == j * step + i);
                }
            }
            if (interleave_of_slices) {
                value = codegen(op->vectors[0]);
                vector<Value *> slices;
                for (int i = 0; i < f; i++) {
                    slices.push_back(slice_vector(value, i * step, step));
                }
                value = interleave_vectors(slices);
            }
        }
        // If the indices form contiguous aligned runs, do the shuffle
        // on entire sub-vectors by reinterpreting them as a wider
        // type.
        for (int f : {8, 4, 2}) {
            if (op->type.lanes() % f != 0) {
                continue;
            }

            if (op->type.bits() * f > 64) {
                continue;
            }
            bool contiguous = true;
            for (const Expr &vec : op->vectors) {
                contiguous &= ((vec.type().lanes() % f) == 0);
            }
            for (size_t i = 0; i < op->indices.size(); i += f) {
                contiguous &= (op->indices[i] % f) == 0;
                for (int j = 0; j < f; j++) {
                    contiguous &= (op->indices[i + j] == op->indices[i] + j);
                }
            }
            if (contiguous) {
                vector<Expr> equiv_args;
                for (const Expr &vec : op->vectors) {
                    Type t = UInt(vec.type().bits() * f, vec.type().lanes() / f);
                    equiv_args.push_back(reinterpret(t, vec));
                }
                vector<int> equiv_indices;
                for (size_t i = 0; i < op->indices.size(); i += f) {
                    equiv_indices.push_back(op->indices[i] / f);
                }
                Expr equiv = Shuffle::make(equiv_args, equiv_indices);
                equiv = reinterpret(op->type, equiv);
                codegen(equiv);
                return;
            }
        }

        // Do a concat and then a single shuffle
        value = concat_vectors(vecs);
        if (op->is_slice() && op->slice_stride() == 1) {
            value = slice_vector(value, op->indices[0], op->indices.size());
        } else {
            value = shuffle_vectors(value, op->indices);
        }
    }

    if (op->type.is_scalar() && value->getType()->isVectorTy()) {
        value = builder->CreateExtractElement(value, ConstantInt::get(i32_t, 0));
    }
}

void CodeGen_LLVM::visit(const VectorReduce *op) {
    codegen_vector_reduce(op, Expr());
}

void CodeGen_LLVM::codegen_vector_reduce(const VectorReduce *op, const Expr &init) {
    Expr val = op->value;
    const int output_lanes = op->type.lanes();
    const int native_lanes = maximum_vector_bits() / op->type.bits();
    const int factor = val.type().lanes() / output_lanes;
    Type elt = op->type.element_of();

    Expr (*binop)(Expr, Expr) = nullptr;
    switch (op->op) {
    case VectorReduce::Add:
        binop = Add::make;
        break;
    case VectorReduce::Mul:
        binop = Mul::make;
        break;
    case VectorReduce::Min:
        binop = Min::make;
        break;
    case VectorReduce::Max:
        binop = Max::make;
        break;
    case VectorReduce::And:
        binop = And::make;
        break;
    case VectorReduce::Or:
        binop = Or::make;
        break;
    case VectorReduce::SaturatingAdd:
        binop = Halide::saturating_add;
        break;
    }

    if (op->type.is_bool() && op->op == VectorReduce::Or) {
        // Cast to u8, use max, cast back to bool.
        Expr equiv = cast(op->value.type().with_bits(8), op->value);
        equiv = VectorReduce::make(VectorReduce::Max, equiv, op->type.lanes());
        if (init.defined()) {
            equiv = max(equiv, init);
        }
        equiv = cast(op->type, equiv);
        equiv.accept(this);
        return;
    }

    if (op->type.is_bool() && op->op == VectorReduce::And) {
        // Cast to u8, use min, cast back to bool.
        Expr equiv = cast(op->value.type().with_bits(8), op->value);
        equiv = VectorReduce::make(VectorReduce::Min, equiv, op->type.lanes());
        equiv = cast(op->type, equiv);
        if (init.defined()) {
            equiv = min(equiv, init);
        }
        equiv.accept(this);
        return;
    }

    if (elt == Float(16) && upgrade_type_for_arithmetic(elt) != elt) {
        Expr equiv = cast(op->value.type().with_bits(32), op->value);
        equiv = VectorReduce::make(op->op, equiv, op->type.lanes());
        if (init.defined()) {
            equiv = binop(equiv, init);
        }
        equiv = cast(op->type, equiv);
        equiv.accept(this);
        return;
    }

    if (output_lanes == 1) {
        const int input_lanes = val.type().lanes();
        const int input_bytes = input_lanes * val.type().bytes();
        const bool llvm_has_intrinsic =
            // Must be one of these ops
            ((op->op == VectorReduce::Add ||
              op->op == VectorReduce::Mul ||
              op->op == VectorReduce::Min ||
              op->op == VectorReduce::Max) &&
             (use_llvm_vp_intrinsics ||
              // Must be a power of two lanes
              ((input_lanes >= 2) &&
               ((input_lanes & (input_lanes - 1)) == 0) &&
               // int versions exist up to 1024 bits
               ((!op->type.is_float() && input_bytes <= 1024) ||
                // float versions exist up to 16 lanes
                input_lanes <= 16) &&
               // As of the release of llvm 10, the 64-bit experimental total
               // reductions don't seem to be done yet on arm.
               (val.type().bits() != 64 ||
                target.arch != Target::ARM))));

        if (llvm_has_intrinsic) {
            const char *name = "<err>";
            const int bits = op->type.bits();
            bool takes_initial_value = use_llvm_vp_intrinsics;
            Expr initial_value = init;
            if (op->type.is_float()) {
                switch (op->op) {
                case VectorReduce::Add:
                    name = "fadd";
                    takes_initial_value = true;
                    if (!initial_value.defined()) {
                        initial_value = make_zero(op->type);
                    }
                    break;
                case VectorReduce::Mul:
                    name = "fmul";
                    takes_initial_value = true;
                    if (!initial_value.defined()) {
                        initial_value = make_one(op->type);
                    }
                    break;
                case VectorReduce::Min:
                    name = "fmin";
                    // TODO(zvookin): Not correct for stricT_float. See: https://github.com/halide/Halide/issues/7118
                    if (takes_initial_value && !initial_value.defined()) {
                        initial_value = op->type.max();
                    }
                    break;
                case VectorReduce::Max:
                    name = "fmax";
                    // TODO(zvookin): Not correct for stricT_float. See: https://github.com/halide/Halide/issues/7118
                    if (takes_initial_value && !initial_value.defined()) {
                        initial_value = op->type.min();
                    }
                    break;
                default:
                    break;
                }
            } else if (op->type.is_int() || op->type.is_uint()) {
                switch (op->op) {
                case VectorReduce::Add:
                    name = "add";
                    if (takes_initial_value && !initial_value.defined()) {
                        initial_value = make_zero(op->type);
                    }
                    break;
                case VectorReduce::Mul:
                    name = "mul";
                    if (takes_initial_value && !initial_value.defined()) {
                        initial_value = make_one(op->type);
                    }
                    break;
                case VectorReduce::Min:
                    name = op->type.is_int() ? "smin" : "umin";
                    if (takes_initial_value && !initial_value.defined()) {
                        initial_value = op->type.max();
                    }
                    break;
                case VectorReduce::Max:
                    name = op->type.is_int() ? "smax" : "umax";
                    if (takes_initial_value && !initial_value.defined()) {
                        initial_value = op->type.min();
                    }
                    break;
                default:
                    break;
                }
            }

            if (use_llvm_vp_intrinsics) {
                string vp_name = "llvm.vp.reduce." + std::string(name);
                codegen(initial_value);
                llvm::Value *init = value;
                codegen(op->value);
                llvm::Value *val = value;
                bool generated = try_vector_predication_intrinsic(vp_name, llvm_type_of(op->type), op->value.type().lanes(),
                                                                  AllEnabledMask(), {VPArg(init), VPArg(val, 0)});
                internal_assert(generated) << "Vector predication intrinsic generation failed for vector reduction " << name << "\n";
            } else {
                std::stringstream build_name;
                build_name << "llvm.vector.reduce.";
                build_name << name;
                build_name << ".v" << val.type().lanes() << (op->type.is_float() ? 'f' : 'i') << bits;

                string intrin_name = build_name.str();

                vector<Expr> args;
                if (takes_initial_value) {
                    args.push_back(initial_value);
                    initial_value = Expr();
                }
                args.push_back(op->value);

                // Make sure the declaration exists, or the codegen for
                // call will assume that the args should scalarize.
                if (!module->getFunction(intrin_name)) {
                    vector<llvm::Type *> arg_types;
                    for (const Expr &e : args) {
                        arg_types.push_back(llvm_type_of(e.type()));
                    }
                    FunctionType *func_t = FunctionType::get(llvm_type_of(op->type), arg_types, false);
                    llvm::Function::Create(func_t, llvm::Function::ExternalLinkage, intrin_name, module.get());
                }

                Expr equiv = Call::make(op->type, intrin_name, args, Call::PureExtern);
                if (initial_value.defined()) {
                    equiv = binop(initial_value, equiv);
                }
                equiv.accept(this);
            }
            return;
        }
    }

    if (output_lanes == 1 &&
        factor > native_lanes &&
        (use_llvm_vp_intrinsics || (factor % native_lanes == 0))) {
        // It's a total reduction of multiple native
        // vectors. Start by adding the vectors together.
        Expr equiv;
        for (int i = 0; i < factor / native_lanes; i++) {
            Expr next = Shuffle::make_slice(val, i * native_lanes, 1, native_lanes);
            if (equiv.defined()) {
                equiv = binop(equiv, next);
            } else {
                equiv = next;
            }
        }
        equiv = VectorReduce::make(op->op, equiv, 1);
        if (init.defined()) {
            equiv = binop(equiv, init);
        }
        equiv = common_subexpression_elimination(equiv);
        equiv.accept(this);
        return;
    }

    if (factor > 2 && ((factor & 1) == 0)) {
        // Factor the reduce into multiple stages. If we're going to
        // be widening the type by 4x or more we should also factor the
        // widening into multiple stages.
        Type intermediate_type = op->value.type().with_lanes(op->value.type().lanes() / 2);
        Expr equiv = VectorReduce::make(op->op, op->value, intermediate_type.lanes());
        if (op->op == VectorReduce::Add &&
            (op->type.is_int() || op->type.is_uint()) &&
            op->type.bits() >= 32) {
            Type narrower_type = op->value.type().narrow().narrow();
            Expr narrower = lossless_cast(narrower_type, op->value);
            if (!narrower.defined() && narrower_type.is_int()) {
                // Maybe we can narrow to an unsigned int instead.
                narrower_type = narrower_type.with_code(Type::UInt);
                narrower = lossless_cast(narrower_type, op->value);
            }
            if (narrower.defined()) {
                // Widen it by 2x before the horizontal add
                narrower = cast(narrower.type().widen(), narrower);
                equiv = VectorReduce::make(op->op, narrower, intermediate_type.lanes());
                // Then widen it by 2x again afterwards
                equiv = cast(intermediate_type, equiv);
            }
        }
        equiv = VectorReduce::make(op->op, equiv, op->type.lanes());
        if (init.defined()) {
            equiv = binop(equiv, init);
        }
        equiv = common_subexpression_elimination(equiv);
        codegen(equiv);
        return;
    }

    // Extract each slice and combine
    Expr equiv = init;
    for (int i = 0; i < factor; i++) {
        Expr next = Shuffle::make_slice(val, i, factor, val.type().lanes() / factor);
        if (equiv.defined()) {
            equiv = binop(equiv, next);
        } else {
            equiv = next;
        }
    }
    equiv = common_subexpression_elimination(equiv);
    codegen(equiv);
}  // namespace Internal

void CodeGen_LLVM::visit(const Atomic *op) {
    if (!op->mutex_name.empty()) {
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
        ptr->setAlignment(llvm::Align(align));
    }
    requested_alloca_total += allocated_size;

    if (zero_initialize) {
        if (n == 1) {
            builder->CreateStore(Constant::getNullValue(t), ptr);
        } else {
            builder->CreateMemSet(ptr, Constant::getNullValue(t), n, llvm::Align(align));
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

llvm::Function *CodeGen_LLVM::get_llvm_intrin(llvm::Type *ret_type, const std::string &name, const std::vector<llvm::Type *> &arg_types) {
    llvm::Function *intrin = module->getFunction(name);
    if (!intrin) {
        FunctionType *func_t = FunctionType::get(ret_type, arg_types, false);
        intrin = llvm::Function::Create(func_t, llvm::Function::ExternalLinkage, name, module.get());
        intrin->setCallingConv(CallingConv::C);
    }
    return intrin;
}

llvm::Function *CodeGen_LLVM::get_llvm_intrin(const Type &ret_type, const std::string &name, const std::vector<Type> &arg_types, bool scalars_are_vectors) {
    llvm::Function *intrin = module->getFunction(name);
    if (intrin) {
        return intrin;
    }

    vector<llvm::Type *> llvm_arg_types(arg_types.size());
    for (size_t i = 0; i < arg_types.size(); i++) {
        llvm_arg_types[i] = llvm_type_of(arg_types[i]);
        if (arg_types[i].is_scalar() && scalars_are_vectors) {
            llvm_arg_types[i] = get_vector_type(llvm_arg_types[i], 1);
        }
    }

    llvm::Type *llvm_ret_type = llvm_type_of(ret_type);
    if (ret_type.is_scalar() && scalars_are_vectors) {
        llvm_ret_type = get_vector_type(llvm_ret_type, 1);
    }
    return get_llvm_intrin(llvm_ret_type, name, llvm_arg_types);
}

llvm::Function *CodeGen_LLVM::declare_intrin_overload(const std::string &name, const Type &ret_type, const std::string &impl_name, std::vector<Type> arg_types, bool scalars_are_vectors) {
    llvm::Function *intrin = get_llvm_intrin(ret_type, impl_name, arg_types, scalars_are_vectors);
    internal_assert(intrin);
    intrinsics[name].emplace_back(ret_type, std::move(arg_types), intrin);
    return intrin;
}

void CodeGen_LLVM::declare_intrin_overload(const std::string &name, const Type &ret_type, llvm::Function *impl, std::vector<Type> arg_types) {
    internal_assert(impl);
    intrinsics[name].emplace_back(ret_type, std::move(arg_types), impl);
}

Value *CodeGen_LLVM::call_overloaded_intrin(const Type &result_type, const std::string &name, const std::vector<Expr> &args) {
    constexpr int debug_level = 4;

    debug(debug_level) << "call_overloaded_intrin: " << result_type << " " << name << "(";
    const char *comma = "";
    for (const Expr &i : args) {
        debug(debug_level) << comma << i;
        comma = ", ";
    }
    debug(debug_level) << ")\n";

    auto impls_i = intrinsics.find(name);
    if (impls_i == intrinsics.end()) {
        debug(debug_level) << "No intrinsic " << name << "\n";
        return nullptr;
    }

    const Intrinsic *resolved = nullptr;
    for (const Intrinsic &overload : impls_i->second) {
        debug(debug_level) << "Considering candidate " << overload.result_type << "(";
        const char *comma = "";
        for (const auto &i : overload.arg_types) {
            debug(debug_level) << comma << i;
            comma = ", ";
        }
        debug(debug_level) << ")\n";
        if (overload.arg_types.size() != args.size()) {
            debug(debug_level) << "Wrong number of arguments\n";
            continue;
        }

        if (overload.result_type.element_of() != result_type.element_of()) {
            debug(debug_level) << "Wrong result type\n";
            continue;
        }

        bool match = true;
        for (int i = 0; i < (int)overload.arg_types.size(); i++) {
            if (args[i].type().is_scalar()) {
                // Allow lossless casting for scalar arguments, and
                // allow broadcasting to vector arguments.
                if (!lossless_cast(overload.arg_types[i].element_of(), args[i]).defined()) {
                    match = false;
                    debug(debug_level) << "Cannot promote scalar argument " << i << "\n";
                    break;
                }
            } else {
                int required_lanes = result_type.lanes() * overload.arg_types[i].lanes() / overload.result_type.lanes();
                if (required_lanes != args[i].type().lanes()) {
                    match = false;
                    debug(debug_level) << "Need " << required_lanes << " lanes for argument " << i << "\n";
                    break;
                }

                // Vector arguments must be exact.
                if (overload.arg_types[i].element_of() != args[i].type().element_of()) {
                    match = false;
                    debug(debug_level) << "Vector types not equal " << i << "\n";
                    break;
                }
            }
        }
        if (!match) {
            continue;
        }

        if (!resolved) {
            debug(debug_level) << "Resolved!\n";
            resolved = &overload;
        } else {
            if (resolved->result_type.lanes() < result_type.lanes()) {
                // The current match is smaller than the result type. Take the bigger intrinsic.
                if (overload.result_type.lanes() > resolved->result_type.lanes()) {
                    debug(debug_level) << "Replaced with bigger intrinsic\n";
                    resolved = &overload;
                }
            } else {
                // The current match is bigger than the result type. If the current candidate is also bigger,
                // but smaller than the current match, take it instead.
                if (overload.result_type.lanes() >= result_type.lanes() && overload.result_type.lanes() < resolved->result_type.lanes()) {
                    debug(debug_level) << "Replaced with smaller intrinsic\n";
                    resolved = &overload;
                }
            }
        }
    }

    if (resolved) {
        std::vector<Expr> promoted_args;
        promoted_args.reserve(args.size());
        for (size_t i = 0; i < args.size(); i++) {
            Expr promoted_arg = args[i];
            if (args[i].type().is_scalar()) {
                promoted_arg = lossless_cast(resolved->arg_types[i].element_of(), promoted_arg);
            }
            if (resolved->arg_types[i].is_vector() && args[i].type().is_scalar() && result_type.lanes() > 1) {
                // We're passing a scalar to a vector argument, broadcast it.
                promoted_args.emplace_back(Broadcast::make(promoted_arg, result_type.lanes()));
            } else {
                promoted_args.emplace_back(promoted_arg);
            }
            internal_assert(promoted_args.back().defined());
        }
        return call_intrin(result_type, resolved->result_type.lanes(), resolved->impl, promoted_args);
    } else {
        debug(debug_level) << "Unresolved intrinsic " << name << "\n";
    }
    return nullptr;
}

Value *CodeGen_LLVM::call_intrin(const Type &result_type, int intrin_lanes,
                                 const string &name, vector<Expr> args) {
    vector<Value *> arg_values(args.size());
    for (size_t i = 0; i < args.size(); i++) {
        arg_values[i] = codegen(args[i]);
    }

    llvm::Type *t = llvm_type_of(result_type);

    return call_intrin(t,
                       intrin_lanes,
                       name, arg_values, isa<llvm::ScalableVectorType>(t));
}

Value *CodeGen_LLVM::call_intrin(const Type &result_type, int intrin_lanes,
                                 llvm::Function *intrin, vector<Expr> args) {
    vector<Value *> arg_values(args.size());
    for (size_t i = 0; i < args.size(); i++) {
        arg_values[i] = codegen(args[i]);
    }

    llvm::Type *t = llvm_type_of(result_type);

    return call_intrin(t,
                       intrin_lanes,
                       intrin, arg_values);
}

Value *CodeGen_LLVM::call_intrin(const llvm::Type *result_type, int intrin_lanes,
                                 const string &name, vector<Value *> arg_values,
                                 bool scalable_vector_result, bool is_reduction) {
    llvm::Function *fn = module->getFunction(name);
    if (!fn) {
        vector<llvm::Type *> arg_types(arg_values.size());
        for (size_t i = 0; i < arg_values.size(); i++) {
            arg_types[i] = arg_values[i]->getType();
        }

        llvm::Type *intrinsic_result_type = result_type->getScalarType();
        if (intrin_lanes > 1 && !is_reduction) {
            if (scalable_vector_result && effective_vscale != 0) {
                intrinsic_result_type = get_vector_type(result_type->getScalarType(),
                                                        intrin_lanes / effective_vscale, VectorTypeConstraint::VScale);
            } else {
                intrinsic_result_type = get_vector_type(result_type->getScalarType(),
                                                        intrin_lanes, VectorTypeConstraint::Fixed);
            }
        }
        FunctionType *func_t = FunctionType::get(intrinsic_result_type, arg_types, false);
        fn = llvm::Function::Create(func_t, llvm::Function::ExternalLinkage, name, module.get());
        fn->setCallingConv(CallingConv::C);
    }

    return call_intrin(result_type, intrin_lanes, fn, arg_values, is_reduction);
}

Value *CodeGen_LLVM::call_intrin(const llvm::Type *result_type, int intrin_lanes,
                                 llvm::Function *intrin, vector<Value *> arg_values,
                                 bool is_reduction) {
    internal_assert(intrin);
    int arg_lanes = 1;
    if (result_type->isVoidTy()) {
        arg_lanes = intrin_lanes;
    } else if (result_type->isVectorTy()) {
        arg_lanes = get_vector_num_elements(result_type);
    }

    if (!is_reduction && intrin_lanes != arg_lanes) {
        // Cut up each arg into appropriately-sized pieces, call the
        // intrinsic on each, then splice together the results.
        vector<Value *> results;
        for (int start = 0; start < arg_lanes; start += intrin_lanes) {
            vector<Value *> args;
            for (size_t i = 0; i < arg_values.size(); i++) {
                int arg_i_lanes = 1;
                if (arg_values[i]->getType()->isVectorTy()) {
                    arg_i_lanes = get_vector_num_elements(arg_values[i]->getType());
                }

                if (arg_i_lanes >= arg_lanes) {
                    // Horizontally reducing intrinsics may have
                    // arguments that have more lanes than the
                    // result. Assume that the horizontally reduce
                    // neighboring elements...
                    int reduce = arg_i_lanes / arg_lanes;
                    args.push_back(slice_vector(arg_values[i], start * reduce, intrin_lanes * reduce));
                } else if (arg_i_lanes == 1) {
                    if (intrin->getFunctionType()->getParamType(i)->isVectorTy()) {
                        // It's a scalar argument to a vector parameter. Broadcast it.
                        // Overwriting the parameter means this only happens once.
                        arg_values[i] = create_broadcast(arg_values[i], intrin_lanes);
                    } else {
                        // It's a scalar arg to an intrinsic that returns
                        // a vector. Replicate it over the slices.
                    }
                    args.push_back(arg_values[i]);
                } else {
                    internal_error << "Argument in call_intrin has " << arg_i_lanes
                                   << " with result type having " << arg_lanes << "\n";
                }
            }

            llvm::Type *result_slice_type =
                get_vector_type(result_type->getScalarType(), intrin_lanes);

            results.push_back(call_intrin(result_slice_type, intrin_lanes, intrin, args));
        }
        Value *result = concat_vectors(results);
        return slice_vector(result, 0, arg_lanes);
    }

    llvm::FunctionType *intrin_type = intrin->getFunctionType();
    for (int i = 0; i < (int)arg_values.size(); i++) {
        if (arg_values[i]->getType() != intrin_type->getParamType(i)) {
            arg_values[i] = normalize_fixed_scalable_vector_type(intrin_type->getParamType(i), arg_values[i]);
        }
        if (arg_values[i]->getType() != intrin_type->getParamType(i)) {
            // There can be some mismatches in types, such as when passing scalar Halide type T
            // to LLVM vector type <1 x T>.
            arg_values[i] = builder->CreateBitCast(arg_values[i], intrin_type->getParamType(i));
        }
    }

    CallInst *call = builder->CreateCall(intrin, arg_values);
    return call;
}

Value *CodeGen_LLVM::slice_vector(Value *vec, int start, int size) {
    // Force the arg to be an actual vector
    if (!vec->getType()->isVectorTy()) {
        vec = create_broadcast(vec, 1);
    }

    int vec_lanes = get_vector_num_elements(vec->getType());

    if (start == 0 && size == vec_lanes) {
        return vec;
    }

    if (size == 1) {
        return builder->CreateExtractElement(vec, (uint64_t)start);
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
    if (v.size() == 1) {
        return v[0];
    }

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
    if (!a->getType()->isVectorTy()) {
        a = create_broadcast(a, 1);
        b = create_broadcast(b, 1);
    }
    vector<Constant *> llvm_indices(indices.size());
    for (size_t i = 0; i < llvm_indices.size(); i++) {
        if (indices[i] >= 0) {
            internal_assert(indices[i] < get_vector_num_elements(a->getType()) * 2);
            llvm_indices[i] = ConstantInt::get(i32_t, indices[i]);
        } else {
            // Only let -1 be undef.
            internal_assert(indices[i] == -1);
            llvm_indices[i] = PoisonValue::get(i32_t);
        }
    }
    if (isa<llvm::ScalableVectorType>(a->getType())) {
        a = scalable_to_fixed_vector_type(a);
    }
    if (isa<llvm::ScalableVectorType>(b->getType())) {
        b = scalable_to_fixed_vector_type(b);
    }
    return builder->CreateShuffleVector(a, b, ConstantVector::get(llvm_indices));
}

Value *CodeGen_LLVM::shuffle_vectors(Value *a, const std::vector<int> &indices) {
    Value *b = PoisonValue::get(a->getType());
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
    while (l < lanes) {
        l *= 2;
    }
    for (int i = l; i > 1; i /= 2) {
        sizes_to_try.push_back(i);
    }

    // If none of those match, we'll also try doubling
    // the lanes up to the next power of two (this is to catch
    // cases where we're a 64-bit vector and have a 128-bit
    // vector implementation).
    sizes_to_try.push_back(l * 2);

    for (int l : sizes_to_try) {
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

std::string CodeGen_LLVM::mabi() const {
    return "";
}

bool CodeGen_LLVM::supports_call_as_float16(const Call *op) const {
    return false;
}

llvm::Value *CodeGen_LLVM::normalize_fixed_scalable_vector_type(llvm::Type *desired_type, llvm::Value *result) {
    llvm::Type *actual_type = result->getType();

    if (isa<llvm::FixedVectorType>(actual_type) &&
        isa<llvm::ScalableVectorType>(desired_type)) {
        const llvm::FixedVectorType *fixed = cast<llvm::FixedVectorType>(actual_type);
        const llvm::ScalableVectorType *scalable = cast<llvm::ScalableVectorType>(desired_type);
        if (fixed->getElementType() == scalable->getElementType()) {
            return fixed_to_scalable_vector_type(result);
        }
    } else if (isa<llvm::FixedVectorType>(desired_type) &&
               isa<llvm::ScalableVectorType>(actual_type)) {
        const llvm::ScalableVectorType *scalable = cast<llvm::ScalableVectorType>(actual_type);
        const llvm::FixedVectorType *fixed = cast<llvm::FixedVectorType>(desired_type);
        if (fixed->getElementType() == scalable->getElementType()) {
            return scalable_to_fixed_vector_type(result);
        }
    }

    return result;
}

llvm::Value *CodeGen_LLVM::fixed_to_scalable_vector_type(llvm::Value *fixed_arg) {
    internal_assert(effective_vscale != 0);
    internal_assert(isa<llvm::FixedVectorType>(fixed_arg->getType()));
    const llvm::FixedVectorType *fixed = cast<llvm::FixedVectorType>(fixed_arg->getType());
    internal_assert(fixed != nullptr);
    auto lanes = fixed->getNumElements();

    const llvm::ScalableVectorType *scalable = cast<llvm::ScalableVectorType>(get_vector_type(fixed->getElementType(),
                                                                                              lanes / effective_vscale, VectorTypeConstraint::VScale));
    internal_assert(fixed != nullptr);

    internal_assert(fixed->getElementType() == scalable->getElementType());
    internal_assert(lanes == (scalable->getMinNumElements() * effective_vscale));

    // E.g. <vscale x 2 x i64> llvm.vector.insert.nxv2i64.v4i64(<vscale x 2 x i64>, <4 x i64>, i64)
    const char *type_designator;
    if (fixed->getElementType()->isIntegerTy()) {
        type_designator = "i";
    } else {
        type_designator = "f";
    }
    std::string intrin = "llvm.vector.insert.nxv" + std::to_string(scalable->getMinNumElements());
    intrin += type_designator;
    std::string bits_designator = std::to_string(fixed->getScalarSizeInBits());
    intrin += bits_designator;
    intrin += ".v" + std::to_string(lanes) + type_designator + bits_designator;
    Constant *poison = PoisonValue::get(scalable->getElementType());
    llvm::Value *result_vec = ConstantVector::getSplat(scalable->getElementCount(), poison);

    std::vector<llvm::Value *> args;
    args.push_back(result_vec);
    args.push_back(value);
    args.push_back(ConstantInt::get(i64_t, 0));
    return call_intrin(scalable, lanes, intrin, args, true);
}

llvm::Value *CodeGen_LLVM::scalable_to_fixed_vector_type(llvm::Value *scalable_arg) {
    internal_assert(effective_vscale != 0);
    internal_assert(isa<llvm::ScalableVectorType>(scalable_arg->getType()));
    const llvm::ScalableVectorType *scalable = cast<llvm::ScalableVectorType>(scalable_arg->getType());
    internal_assert(scalable != nullptr);

    const llvm::FixedVectorType *fixed = cast<llvm::FixedVectorType>(get_vector_type(scalable->getElementType(),
                                                                                     scalable->getMinNumElements() * effective_vscale, VectorTypeConstraint::Fixed));
    internal_assert(fixed != nullptr);

    internal_assert(fixed->getElementType() == scalable->getElementType());
    internal_assert(fixed->getNumElements() == (scalable->getMinNumElements() * effective_vscale));

    // E.g. <64 x i8> @llvm.vector.extract.v64i8.nxv8i8(<vscale x 8 x i8> %vresult, i64 0)
    const char *type_designator;
    if (scalable->getElementType()->isIntegerTy()) {
        type_designator = "i";
    } else {
        type_designator = "f";
    }
    std::string bits_designator = std::to_string(fixed->getScalarSizeInBits());
    std::string intrin = "llvm.vector.extract.v" + std::to_string(fixed->getNumElements()) + type_designator + bits_designator;
    intrin += ".nxv" + std::to_string(scalable->getMinNumElements()) + type_designator + bits_designator;
    std::vector<llvm::Value *> args;
    args.push_back(scalable_arg);
    args.push_back(ConstantInt::get(i64_t, 0));

    return call_intrin(fixed, fixed->getNumElements(), intrin, args, false);
}

int CodeGen_LLVM::get_vector_num_elements(const llvm::Type *t) {
    if (isa<llvm::FixedVectorType>(t)) {
        const auto *vt = cast<llvm::FixedVectorType>(t);
        return vt->getNumElements();
    } else if (isa<llvm::ScalableVectorType>(t)) {
        internal_assert(effective_vscale != 0) << "Scalable vector type enountered without vector_bits being set.\n";
        const auto *vt = cast<llvm::ScalableVectorType>(t);
        return vt->getMinNumElements() * effective_vscale;
    } else {
        return 1;
    }
}

llvm::Type *CodeGen_LLVM::llvm_type_of(LLVMContext *c, Halide::Type t,
                                       int effective_vscale) const {
    if (t.lanes() == 1) {
        if (t.is_float() && !t.is_bfloat()) {
            switch (t.bits()) {
            case 16:
                return llvm::Type::getHalfTy(*c);
            case 32:
                return llvm::Type::getFloatTy(*c);
            case 64:
                return llvm::Type::getDoubleTy(*c);
            default:
                internal_error << "There is no llvm type matching this floating-point bit width: " << t << "\n";
                return nullptr;
            }
        } else if (t.is_handle()) {
            return llvm::Type::getInt8PtrTy(*c);
        } else {
            return llvm::Type::getIntNTy(*c, t.bits());
        }
    } else {
        llvm::Type *element_type = llvm_type_of(c, t.element_of(), 0);
        bool scalable = false;
        int lanes = t.lanes();
        if (effective_vscale != 0) {
            int total_bits = t.bits() * t.lanes();
            scalable = ((total_bits % effective_vscale) == 0);
            if (scalable) {
                lanes /= effective_vscale;
            } else {
                // TODO(zvookin): This error indicates that the requested number of vector lanes
                // is not expressible exactly via vscale. This will be fairly unusual unless
                // non-power of two, or very short, vector sizes are used in a schedule.
                // It is made an error, instead of passing the fixed non-vscale vector type to LLVM,
                // to catch the case early while developing vscale backends.
                // We may need to change this to allow the case so if one hits this error in situation
                // where it should pass through a fixed width vector type, please discuss.
                internal_error << "Failed to make vscale vector type with bits " << t.bits() << " lanes " << t.lanes()
                               << " effective_vscale " << effective_vscale << " total_bits " << total_bits << "\n";
            }
        }
        return get_vector_type(element_type, lanes,
                               scalable ? VectorTypeConstraint::VScale : VectorTypeConstraint::Fixed);
    }
}

llvm::Type *CodeGen_LLVM::get_vector_type(llvm::Type *t, int n,
                                          VectorTypeConstraint type_constraint) const {
    if (t->isVoidTy()) {
        return t;
    }

    bool scalable = false;
    switch (type_constraint) {
    case VectorTypeConstraint::None:
        scalable = effective_vscale != 0 &&
                   ((n % effective_vscale) == 0);
        if (scalable) {
            n = n / effective_vscale;
        }
        break;
    case VectorTypeConstraint::Fixed:
        scalable = false;
        break;
    case VectorTypeConstraint::VScale:
        scalable = true;
        break;
    default:
        internal_error << "Impossible";
        break;
    }

    return VectorType::get(t, n, scalable);
}

llvm::Constant *CodeGen_LLVM::get_splat(int lanes, llvm::Constant *value,
                                        VectorTypeConstraint type_constraint) const {
    bool scalable = false;
    switch (type_constraint) {
    case VectorTypeConstraint::None:
        scalable = effective_vscale != 0 &&
                   ((lanes % effective_vscale) == 0);
        if (scalable) {
            lanes = lanes / effective_vscale;
        }
        break;
    case VectorTypeConstraint::Fixed:
        scalable = false;
        break;
    case VectorTypeConstraint::VScale:
        scalable = true;
        break;
    }

    llvm::ElementCount ec = scalable ? llvm::ElementCount::getScalable(lanes) :
                                       llvm::ElementCount::getFixed(lanes);
    return ConstantVector::getSplat(ec, value);
}

std::string CodeGen_LLVM::mangle_llvm_type(llvm::Type *type) {
    std::string type_string = ".";
    llvm::ElementCount llvm_vector_ec;
    if (isa<PointerType>(type)) {
        const auto *vt = cast<llvm::PointerType>(type);
        type_string = ".p" + std::to_string(vt->getAddressSpace());
    } else if (isa<llvm::ScalableVectorType>(type)) {
        const auto *vt = cast<llvm::ScalableVectorType>(type);
        const char *type_designator = vt->getElementType()->isIntegerTy() ? "i" : "f";
        std::string bits_designator = std::to_string(vt->getScalarSizeInBits());
        llvm_vector_ec = vt->getElementCount();
        type_string = ".nxv" + std::to_string(vt->getMinNumElements()) + type_designator + bits_designator;
    } else if (isa<llvm::FixedVectorType>(type)) {
        const auto *vt = cast<llvm::FixedVectorType>(type);
        const char *type_designator = vt->getElementType()->isIntegerTy() ? "i" : "f";
        std::string bits_designator = std::to_string(vt->getScalarSizeInBits());
        llvm_vector_ec = vt->getElementCount();
        type_string = ".v" + std::to_string(vt->getNumElements()) + type_designator + bits_designator;
    } else if (type->isIntegerTy()) {
        type_string = ".i" + std::to_string(type->getScalarSizeInBits());
    } else if (type->isFloatTy()) {
        type_string = ".f" + std::to_string(type->getScalarSizeInBits());
    } else {
        std::string type_name;
        llvm::raw_string_ostream type_name_stream(type_name);
        type->print(type_name_stream, true);
        internal_error << "Attempt to mangle unknown LLVM type " << type_name << "\n";
    }
    return type_string;
}

bool CodeGen_LLVM::try_vector_predication_intrinsic(const std::string &name, VPResultType result_type,
                                                    int32_t length, MaskVariant mask, std::vector<VPArg> vp_args) {
    if (!use_llvm_vp_intrinsics) {
        return false;
    }

    llvm::Type *llvm_result_type = result_type.type;
    bool any_scalable = isa<llvm::ScalableVectorType>(llvm_result_type);
    bool any_fixed = isa<llvm::FixedVectorType>(llvm_result_type);
    bool result_is_vector_type = any_scalable || any_fixed;
    bool is_reduction = !any_scalable && !any_fixed;
    llvm::Type *base_vector_type = nullptr;
    for (const VPArg &arg : vp_args) {
        llvm::Type *arg_type = arg.value->getType();
        bool scalable = isa<llvm::ScalableVectorType>(arg_type);
        bool fixed = isa<llvm::FixedVectorType>(arg_type);
        if (base_vector_type == nullptr && (fixed || scalable)) {
            base_vector_type = arg_type;
        }
        any_scalable |= scalable;
        any_fixed |= fixed;
    }
    if (!any_fixed && !any_scalable) {
        return false;
    }
    internal_assert(!(any_scalable && any_fixed)) << "Cannot combine fixed and scalable vectors to vector predication intrinsic.\n";
    if (base_vector_type == nullptr && result_is_vector_type) {
        base_vector_type = llvm_result_type;
    }
    bool is_scalable = any_scalable;

    std::vector<llvm::Value *> args;
    args.reserve(2 + vp_args.size());
    std::vector<string> mangled_types(vp_args.size() + 1);

    for (const VPArg &arg : vp_args) {
        args.push_back(arg.value);
        if (arg.mangle_index) {
            llvm::Type *llvm_type = arg.value->getType();
            mangled_types[arg.mangle_index.value()] = mangle_llvm_type(llvm_type);
        }
    }
    if (result_type.mangle_index) {
        mangled_types[result_type.mangle_index.value()] = mangle_llvm_type(llvm_result_type);
    }

    std::string full_name = name;
    for (const std::string &mangle : mangled_types) {
        full_name += mangle;
    }

    if (!std::holds_alternative<NoMask>(mask)) {
        if (std::holds_alternative<AllEnabledMask>(mask)) {
            internal_assert(base_vector_type != nullptr) << "Requested all enabled mask without any vector type to use for type/length.\n";
            llvm::ElementCount llvm_vector_ec;
            if (is_scalable) {
                const auto *vt = cast<llvm::ScalableVectorType>(base_vector_type);
                llvm_vector_ec = vt->getElementCount();
            } else {
                const auto *vt = cast<llvm::FixedVectorType>(base_vector_type);
                llvm_vector_ec = vt->getElementCount();
            }
            args.push_back(ConstantVector::getSplat(llvm_vector_ec, ConstantInt::get(i1_t, 1)));
        } else {
            args.push_back(std::get<llvm::Value *>(mask));
        }
    }
    args.push_back(ConstantInt::get(i32_t, length));

    value = call_intrin(llvm_result_type, length, full_name, args, is_scalable, is_reduction);
    llvm::CallInst *call = dyn_cast<llvm::CallInst>(value);
    for (size_t i = 0; i < vp_args.size(); i++) {
        if (vp_args[i].alignment != 0) {
            call->addParamAttr(i, Attribute::getWithAlignment(*context, llvm::Align(vp_args[i].alignment)));
        }
    }
    return true;
}

bool CodeGen_LLVM::try_vector_predication_comparison(const std::string &name, const Type &result_type,
                                                     MaskVariant mask, llvm::Value *a, llvm::Value *b,
                                                     const char *cmp_op) {
    // Early out to prevent creating useless metadata.
    if (!use_llvm_vp_intrinsics ||
        result_type.is_scalar()) {
        return false;
    }

    internal_assert(result_type.is_bool()) << "Vector predicated comparisons must return bool type.\n";

    llvm::MDBuilder md_builder(*context);
    llvm::Value *md_val = llvm::MetadataAsValue::get(*context, md_builder.createString(cmp_op));
    return try_vector_predication_intrinsic(name, llvm_type_of(result_type), result_type.lanes(), mask,
                                            {VPArg(a, 0), VPArg(b), VPArg(md_val)});
}

}  // namespace Internal
}  // namespace Halide
