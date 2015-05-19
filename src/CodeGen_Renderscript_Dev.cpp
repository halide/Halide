#if LLVM_VERSION < 36 || WITH_NATIVE_CLIENT
#include "BitWriter_3_2.35/ReaderWriter_3_2.h"
#else
#include "BitWriter_3_2/ReaderWriter_3_2.h"
#endif
#include "CodeGen_Renderscript_Dev.h"
#include "CodeGen_Internal.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Debug.h"
#include "Target.h"
#include "LLVM_Headers.h"
#include "LLVM_Runtime_Linker.h"

namespace Halide {
namespace Internal {

using std::vector;
using std::string;

using namespace llvm;

// Sniff the contents of a kernel to extracts the bounds of all the
// thread indices (so we know how many threads to launch), and the
// amount of shared memory to allocate.
class ExtractBoundsNames : public IRVisitor {
public:
    string names[4];

    ExtractBoundsNames() {
        for (int i = 0; i < 4; i++) {
            names[i] = "";
        }
    }

private:
    using IRVisitor::visit;

    void visit(const For *op) {
        if (CodeGen_GPU_Dev::is_gpu_var(op->name)) {
            internal_assert(is_zero(op->min));
        }

        if (ends_with(op->name, ".__block_id_x")) {
            names[0] = op->name;
        } else if (ends_with(op->name, ".__block_id_y")) {
            names[1] = op->name;
        } else if (ends_with(op->name, ".__block_id_z")) {
            names[2] = op->name;
        } else if (ends_with(op->name, ".__block_id_w")) {
            names[3] = op->name;
        }
        op->body.accept(this);
    }
};

CodeGen_Renderscript_Dev::CodeGen_Renderscript_Dev(Target host) : CodeGen_LLVM(host) {
    debug(2) << "Created CodeGen_Renderscript_Dev for target " << host.to_string().c_str()
             << "\n";
#if !(WITH_RENDERSCRIPT)
    user_error << "rs not enabled for this build of Halide.\n";
#endif
    context = new llvm::LLVMContext();
}

CodeGen_Renderscript_Dev::~CodeGen_Renderscript_Dev() { delete context; }

void CodeGen_Renderscript_Dev::add_kernel(Stmt stmt, const std::string &kernel_name,
                                const std::vector<GPU_Argument> &args) {
    internal_assert(module != NULL);

    // Use [kernel_name] as the function name.
    debug(2) << "In CodeGen_Renderscript_Dev::add_kernel name=" << kernel_name << "\n";

    ExtractBoundsNames bounds_names;
    stmt.accept(&bounds_names);

    // Type Definitions
    StructType *StructTy_struct_rs_allocation = module->getTypeByName("struct.rs_allocation");

    // Put the arguments in the symbol table
    vector<std::tuple<string, Value *>> globals_sym_names;

    // Constant Definitions
    ConstantAggregateZero *const_empty_allocation_struct = ConstantAggregateZero::get(StructTy_struct_rs_allocation);
    ConstantInt *const_0 = ConstantInt::get(*context, APInt(32, StringRef("0"), 10));

    NamedMDNode *rs_export_var = module->getOrInsertNamedMetadata("#rs_export_var");
    NamedMDNode *rs_objects_slots = module->getOrInsertNamedMetadata("#rs_object_slots");

    // Now deduce the types of the arguments to our function
    vector<llvm::Type *> arg_types_1(args.size());
    llvm::Type *argument_type = NULL;
    for (size_t i = 0; i < args.size(); i++) {
        string arg_name = args[i].name;
        debug(1) << "CodeGen_Renderscript_Dev arg[" << i << "].name=" << arg_name << "\n";
        if (args[i].is_buffer) {
            // Remember actual type of buffer argument - will use it for kernel input buffer type.
            argument_type = llvm_type_of(args[i].type);
        }

        // Reuse global variables between kernels.
        std::map<std::string, GlobalVariable*>::const_iterator f = rs_global_vars.find(arg_name);
        if (f != rs_global_vars.end()) {
            globals_sym_names.push_back(std::make_tuple(arg_name, f->second));
        } else {
            enum RS_ARGUMENT_TYPE { RS_INT = 6, RS_BUFFER = 20 } rs_argument_type;
            GlobalVariable *gvar;
            if (!args[i].is_buffer) {
                gvar = new GlobalVariable(*module, llvm::Type::getInt32Ty(*context),
                                          false,  // isConstant
                                          GlobalValue::CommonLinkage,
                                          0,  // has initializer, specified below
                                         arg_name);
                gvar->setInitializer(const_0);
                globals_sym_names.push_back(std::make_tuple(arg_name, gvar));

                rs_argument_type = RS_INT;
            } else {
                gvar = new GlobalVariable(*module, StructTy_struct_rs_allocation,
                                          false,  // isConstant
                                          GlobalValue::CommonLinkage,
                                          0,  // has initializer, specified below
                                          arg_name);
                gvar->setInitializer(const_empty_allocation_struct);

                rs_argument_type = RS_BUFFER;
                rs_objects_slots->addOperand(
                    MDNode::get(*context,
                                vec<LLVMMDNodeArgumentType>(MDString::get(
                                    *context,
                                    std::to_string(rs_export_var->getNumOperands())))));
            }
            gvar->setAlignment(4);
            globals_sym_names.push_back(std::make_tuple(arg_name, gvar));
            rs_global_vars.insert(std::pair<std::string,GlobalVariable*>(arg_name, gvar));

            rs_export_var->addOperand(MDNode::get(
                *context,
                vec<LLVMMDNodeArgumentType>(MDString::get(*context, arg_name),
                                            MDString::get(*context, std::to_string(rs_argument_type)))));

            debug(2) << "args[" << i << "] = {"
                     << "name=" << args[i].name
                     << " is_buffer=" << args[i].is_buffer
                     << " dimensions=" << (+args[i].dimensions)
                     << " type=" << args[i].type << "}\n";
        }
    }

    // Make our function with arguments for kernel defined per Renderscript
    // convention: (in_type in, i32 x, i32 y)
    vector<llvm::Type *> arg_types;
    arg_types.push_back(argument_type);  // "in"
    for (int i = 0; i < 4; i++) {
        debug(2) << "  adding argument type at " << i << ": "
                 << bounds_names.names[i] << "\n";
        if (!bounds_names.names[i].empty()) {
            arg_types.push_back(i32);
        }
    }

    FunctionType *func_t = FunctionType::get(void_t, arg_types, false);
    function = llvm::Function::Create(func_t, llvm::Function::ExternalLinkage,
                                      kernel_name, module);

    vector<string> arg_sym_names;

    // Add kernel function's parameters to the scope.
    llvm::Function::arg_iterator input_arg = function->arg_begin();
    input_arg->setName("in");
    input_arg++;  // skip "in" buffer
    for (int i = 0; i < 4; i++, input_arg++) {
        string bounds_name = bounds_names.names[i];
        if (!bounds_name.empty()) {
            input_arg->setName(bounds_name);
            sym_push(bounds_name, input_arg);
            debug(2) << "  adding kernel function parameter " << bounds_name
                     << " with type ";
            if (debug::debug_level >= 2) {
                input_arg->getType()->dump();
            }
            arg_sym_names.push_back(bounds_name);
        }
    }

    // Make the initial basic block.
    entry_block = BasicBlock::Create(*context, "entry", function);
    builder->SetInsertPoint(entry_block);

    // We won't end the entry block yet, because we'll want to add
    // some allocas to it later if there are local allocations. Start
    // a new block to put all the code.
    BasicBlock *body_block = BasicBlock::Create(*context, "body", function);
    builder->SetInsertPoint(body_block);

    // Global symbols are pointers to the values, so
    // we need to dereference and load actual values.
    for (auto name_and_value : globals_sym_names) {
        string name = std::get<0>(name_and_value);
        debug(2) << "Pushing global symbol " << name << " into sym table\n";

        // Buffer symbols are bit-casted to rs_allocation*.
        Value *value = std::get<1>(name_and_value);
        llvm::PointerType *p = llvm::cast<llvm::PointerType>(value->getType());
        if (p != NULL) {
            if (p->getElementType() == StructTy_struct_rs_allocation) {
                value = builder->CreateBitCast(
                    value,
                    PointerType::get(
                        ArrayType::get(IntegerType::get(*context, 32), 1), 0));
            }
        }

        value = builder->CreateAlignedLoad(value, 4 /*alignment*/);
        sym_push(name, value);
        arg_sym_names.push_back(name);
    }

    debug(1) << "Generating llvm bitcode for kernel...\n";
    // Ok, we have a module, function, context, and a builder
    // pointing at a brand new basic block. We're good to go.
    stmt.accept(this);

    builder->CreateRetVoid();
    // Make the entry block point to the body block
    builder->SetInsertPoint(entry_block);
    builder->CreateBr(body_block);

    // Generated kernels have to be added to the list kept in module's metadata.
    rs_export_foreach_name->addOperand(MDNode::get(
        *context,
        vec<LLVMMDNodeArgumentType>(MDString::get(*context, kernel_name))));

    const char* kernel_signature = "57";
    rs_export_foreach->addOperand(MDNode::get(
        *context, vec<LLVMMDNodeArgumentType>(MDString::get(
                      *context, kernel_signature))));

    // Now verify the function is ok
    verifyFunction(*function);

    // Finally, verify the module is ok
    verifyModule(*module);

    debug(2) << "Done generating llvm bitcode for RS\n";

    // Clear the symbol table
    for (size_t i = 0; i < arg_sym_names.size(); i++) {
        sym_pop(arg_sym_names[i]);
    }
}

void CodeGen_Renderscript_Dev::init_module() {
    debug(2) << "CodeGen_Renderscript_Dev::init_module\n";
    init_context();
#ifdef WITH_RENDERSCRIPT
    delete module;
    module = get_initial_module_for_renderscript_device(target, context);

    // Add Renderscript standard set of metadata.
    NamedMDNode *meta_llvm_module_flags = module->getOrInsertNamedMetadata("llvm.module.flags");
    meta_llvm_module_flags->addOperand(MDNode::get(
        *context, vec<LLVMMDNodeArgumentType>(
                      value_as_metadata_type(ConstantInt::get(i32, 1)),
                      MDString::get(*context, "wchar_size"),
                      value_as_metadata_type(ConstantInt::get(i32, 4)))));
    meta_llvm_module_flags->addOperand(MDNode::get(
        *context, vec<LLVMMDNodeArgumentType>(
                      value_as_metadata_type(ConstantInt::get(i32, 1)),
                      MDString::get(*context, "min_enum_size"),
                      value_as_metadata_type(ConstantInt::get(i32, 4)))));

    module->getOrInsertNamedMetadata("llvm.ident") ->addOperand(
            MDNode::get(*context, vec<LLVMMDNodeArgumentType>(MDString::get(
                                      *context, "clang version 3.6 "))));

    NamedMDNode *meta_pragma = module->getOrInsertNamedMetadata("#pragma");
    meta_pragma->addOperand(MDNode::get(
        *context,
        vec<LLVMMDNodeArgumentType>(MDString::get(*context, "version"),
                                    MDString::get(*context, "1"))));
    meta_pragma->addOperand(MDNode::get(
        *context,
        vec<LLVMMDNodeArgumentType>(MDString::get(*context, "rs_fp_relaxed"),
                                    MDString::get(*context, ""))));

    rs_export_foreach_name = module->getOrInsertNamedMetadata("#rs_export_foreach_name");
    rs_export_foreach_name->addOperand(MDNode::get(
        *context,
        vec<LLVMMDNodeArgumentType>(MDString::get(*context, "root"))));

    rs_export_foreach = module->getOrInsertNamedMetadata("#rs_export_foreach");
    rs_export_foreach->addOperand(MDNode::get(
        *context, vec<LLVMMDNodeArgumentType>(MDString::get(*context, "0"))));
#endif
}

llvm::Triple CodeGen_LLVM::get_target_triple() const {
    return Triple(Triple::normalize("armv7-none-linux-gnueabi"));
}

llvm::DataLayout CodeGen_Renderscript_Dev::get_data_layout() const {
    return llvm::DataLayout("e-m:e-p:32:32-i64:64-v128:64:128-n32-S64");
}

//
// Loops become kernels. There should be no explicit loops in
// generated RenderScript code.
//
void CodeGen_Renderscript_Dev::visit(const For *loop) {
    debug(2) << "RS: Visiting for loop, loop->name is " << loop->name
             << " is_gpu_var? " << is_gpu_var(loop->name) << "\n";
    if (is_gpu_var(loop->name)) {
        // Whether it's thread-parallelization loop or loop over
        // coordinate variables, collapse them going straight to the body
        // because Renderscript takes care of setting up the loops.
        // We just need to produce a kernel code.
        loop->body.accept(this);
    } else {
        user_assert(loop->for_type != ForType::Parallel)
            << "Cannot use loops inside RS kernel\n";
        CodeGen_LLVM::visit(loop);
    }
}

void CodeGen_Renderscript_Dev::visit(const Allocate *alloc) {
    debug(2) << "RS: Allocate " << alloc->name << " on device\n";
    codegen(alloc->body);
}

void CodeGen_Renderscript_Dev::visit(const Free *f) {
    // TODO(aam): Implement this.
    debug(2) << "RS: Free on device\n";
}

llvm::Function *CodeGen_Renderscript_Dev::fetch_GetElement_func(int width) {
    // Following symbols correspond to public Android API functions.
    // The symbols will be resolved once the code compiles on the target
    // Android device.
    std::string func_name = width == 1
                                ? "_Z20rsGetElementAt_uchar13rs_allocationjjj"
                                : "_Z21rsGetElementAt_uchar413rs_allocationjj";
    llvm::Function *func = module->getFunction(func_name);
    internal_assert(func) << "Cant' find " << func_name << "function";
    return func;
}

llvm::Function *CodeGen_Renderscript_Dev::fetch_SetElement_func(int width) {
    // Following symbols correspond to public Android API functions.
    // The symbols will be resolved once the code compiles on the target
    // Android device.
    std::string func_name =
        width == 1 ? "_Z20rsSetElementAt_uchar13rs_allocationhjjj"
                   : "_Z21rsSetElementAt_uchar413rs_allocationDv4_hjj";
    llvm::Function *func = module->getFunction(func_name);
    internal_assert(func) << "Cant' find " << func_name << "function";
    return func;
}

vector<Value *> CodeGen_Renderscript_Dev::add_x_y_c_args(Expr name, Expr x, Expr y,
                                               Expr c) {
    vector<Value *> args;
    const Broadcast *b_name = name.as<Broadcast>();
    const Broadcast *b_x = x.as<Broadcast>();
    const Broadcast *b_y = y.as<Broadcast>();
    const Ramp *ramp_c = c.as<Ramp>();
    if (b_name != NULL && b_x != NULL && b_y != NULL && ramp_c != NULL) {
        // vectorized over c, use x and y to retrieve 4-byte RGBA chunk.
        const IntImm *stride = ramp_c->stride.IRHandle::as<IntImm>();
        user_assert(stride->value == 1 && ramp_c->width == 4)
            << "Only vectorized RGBA format is supported at present.\n";
        user_assert(b_x->value.type().width == 1)
            << "image_load/store x coordinate is not scalar.\n";
        user_assert(b_y->value.type().width == 1)
            << "image_load/store y coordinate is not scalar.\n";
        args.push_back(sym_get(b_name->value.as<StringImm>()->value));
        args.push_back(codegen(b_x->value));
        args.push_back(codegen(b_y->value));
    } else {
        // Use all three coordinates to retrieve single byte.
        user_assert(b_name == NULL && b_x == NULL && b_y == NULL && ramp_c == NULL);
        args.push_back(sym_get(name.as<StringImm>()->value));
        args.push_back(codegen(x));
        args.push_back(codegen(y));
        args.push_back(codegen(c));
    }
    return args;
}

void CodeGen_Renderscript_Dev::visit(const Call *op) {
    if (op->call_type == Call::Intrinsic) {
        if (op->name == Call::image_load || op->name == Call::image_store) {
            //
            // image_load(<image name>, <buffer>, <x>, <x-extent>, <y>,
            // <y-extent>, <c>, <c-extent>)
            // or
            // image_store(<image name>, <buffer>, <x>, <y>, <c>, <value>)
            //
            const int index_name = 0;
            const int index_x = op->name == Call::image_load ? 2 : 2;
            const int index_y = op->name == Call::image_load ? 4 : 3;
            const int index_c = op->name == Call::image_load ? 6 : 4;
            vector<Value *> args =
                add_x_y_c_args(op->args[index_name], op->args[index_x],
                               op->args[index_y], op->args[index_c]);

            if (op->name == Call::image_store) {
                args.insert(args.begin() + 1, codegen(op->args[5]));
            }

            debug(2) << "Generating " << op->type.width
                     << "byte-wide call with " << args.size() << " args:\n";
            if (debug::debug_level >= 2) {
                int i = 1;
                for (Value *arg : args) {
                    debug(2) << " #" << i++ << ":";
                    arg->getType()->dump();
                    arg->dump();
                }
            }

            llvm::Function *func = op->name == Call::image_load
                                       ? fetch_GetElement_func(op->type.width)
                                       : fetch_SetElement_func(op->type.width);
            value = builder->CreateCall(func, args);
            return;
        }
    }
    CodeGen_LLVM::visit(op);
}

string CodeGen_Renderscript_Dev::march() const { return "armv7"; }

string CodeGen_Renderscript_Dev::mcpu() const { return "none"; }

string CodeGen_Renderscript_Dev::mattrs() const { return "linux-gnueabi"; }

bool CodeGen_Renderscript_Dev::use_soft_float_abi() const {
    // Taken from CodeGen_ARM::use_soft_float_abit.
    return target.bits == 32;
}

llvm::Triple CodeGen_Renderscript_Dev::get_target_triple() const {
    return Triple(Triple::normalize(march() + "-" + mcpu() + "-" + mattrs()));
}

// Data structures below as well as writeAndroidBitcodeWrapper function are
// taken from https://android.googlesource.com/platform/frameworks/compile/libbcc/+/master/include/bcinfo/BitcodeWrapper.h
struct AndroidBitcodeWrapper {
    uint32_t Magic;
    uint32_t Version;
    uint32_t BitcodeOffset;
    uint32_t BitcodeSize;
    uint32_t HeaderVersion;
    uint32_t TargetAPI;
    uint32_t PNaClVersion;
    uint16_t CompilerVersionTag;
    uint16_t CompilerVersionLen;
    uint32_t CompilerVersion;
    uint16_t OptimizationLevelTag;
    uint16_t OptimizationLevelLen;
    uint32_t OptimizationLevel;
};

class BCHeaderField {
public:
    typedef enum {
        kInvalid = 0,
        kBitcodeHash = 1,
        kAndroidCompilerVersion = 0x4001,
        kAndroidOptimizationLevel = 0x4002
    } Tag;
};

/**
 * Helper function to emit just the bitcode wrapper returning the number of
 * bytes that were written.
 *
 * \param wrapper - where to write header information into.
 * \param bitcodeSize - size of bitcode in bytes.
 * \param targetAPI - target API version for this bitcode.
 * \param compilerVersion - compiler version that generated this bitcode.
 * \param optimizationLevel - compiler optimization level for this bitcode.
 *
 * \return number of wrapper bytes written into the \p buffer.
 */
static inline size_t writeAndroidBitcodeWrapper(AndroidBitcodeWrapper *wrapper,
                                                size_t bitcodeSize,
                                                uint32_t targetAPI,
                                                uint32_t compilerVersion,
                                                uint32_t optimizationLevel) {
    if (!wrapper) {
        return 0;
    }

    wrapper->Magic = 0x0B17C0DE;
    wrapper->Version = 0;
    wrapper->BitcodeOffset = sizeof(*wrapper);  // 0x2c
    wrapper->BitcodeSize = bitcodeSize;
    wrapper->HeaderVersion = 0;
    wrapper->TargetAPI = targetAPI;  // 0x00000015
    wrapper->PNaClVersion = 0;
    wrapper->CompilerVersionTag =
        BCHeaderField::kAndroidCompilerVersion;  // 0x40001
    wrapper->CompilerVersionLen = 4;
    wrapper->CompilerVersion = compilerVersion;  // 0x000076d
    wrapper->OptimizationLevelTag = BCHeaderField::kAndroidOptimizationLevel;
    wrapper->OptimizationLevelLen = 4;
    wrapper->OptimizationLevel = optimizationLevel;  // 3

    return sizeof(*wrapper);
}

vector<char> CodeGen_Renderscript_Dev::compile_to_src() {
    llvm::Triple triple = get_target_triple();
    llvm::DataLayout dl = get_data_layout();
    module->setTargetTriple(triple.str());

    #if LLVM_VERSION > 36
    module->setDataLayout(dl);
    #else
    module->setDataLayout(&dl);
    #endif

    // Generic llvm optimizations on the module.
    optimize_module();

    debug(2) << "CodeGen_Renderscript_Dev::compile_to_src resultant module:\n";
    if (debug::debug_level >= 2) {
        module->dump();
    }

    std::string str;
    llvm::raw_string_ostream OS(str);
    llvm_3_2::WriteBitcodeToFile(module, OS);
    OS.flush();

    //
    // Values below are to accomodate Android Renderscript bitcode reader.
    //
    // The minimum version which does not require translation (i.e. is already
    // compatible with LLVM's default bitcode reader).
    //
    const unsigned int kMinimumUntranslatedVersion = 21;

    AndroidBitcodeWrapper wrapper;
    size_t actualWrapperLen = writeAndroidBitcodeWrapper(
        &wrapper, str.size(), kMinimumUntranslatedVersion,
        0x000076d /*BCWrapper.getCompilerVersion()*/,
        3 /*BCWrapper.getOptimizationLevel()*/);

    internal_assert(actualWrapperLen > 0)
        << "Couldn't produce bitcode wrapper.\n";

    size_t mTranslatedBitcodeSize = actualWrapperLen + str.size();
    char *c = new char[mTranslatedBitcodeSize];
    memcpy(c, &wrapper, actualWrapperLen);
    memcpy(c + actualWrapperLen, str.c_str(), str.size());

    debug(1) << "RS kernel:\n" << str.size() << " bytes\n";
    vector<char> buffer(c, c + mTranslatedBitcodeSize);
    delete[] c;
    return buffer;
}

int CodeGen_Renderscript_Dev::native_vector_bits() const {
    // as per CodeGen_ARM.
    return 128;
}

string CodeGen_Renderscript_Dev::get_current_kernel_name() {
    // Renderscript function to launch RS kernel needs number(slot index) as a kernel identifier.
    return int_to_string(rs_export_foreach_name->getNumOperands() - 1);
}

void CodeGen_Renderscript_Dev::dump() { module->dump(); }

std::string CodeGen_Renderscript_Dev::print_gpu_name(const std::string &name) {
    return name;
}
}
}
