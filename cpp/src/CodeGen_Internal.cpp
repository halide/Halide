#include "CodeGen_Internal.h"
#include "Log.h"

// Temporary affordance to compile with both llvm 3.2 and 3.3.
// Protected as at least one installation of llvm elides version macros.
#if defined(LLVM_VERSION_MINOR) && LLVM_VERSION_MINOR < 3
#include <llvm/Value.h>
#include <llvm/Module.h>
#include <llvm/Function.h>
#include <llvm/IRBuilder.h>
// They renamed this type in 3.3
typedef llvm::Attributes Attribute;
typedef llvm::Attributes::AttrVal AttrKind;
#else
#include <llvm/IR/Value.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
typedef llvm::Attribute::AttrKind AttrKind;
#endif

using std::string;
using std::map;
using std::vector;
using namespace llvm;

namespace Halide {
namespace Internal {

void CodeGen::Closure::visit(const Let *op) {
    op->value.accept(this);
    ignore.push(op->name, 0);
    op->body.accept(this);
    ignore.pop(op->name);
}

void CodeGen::Closure::visit(const LetStmt *op) {
    op->value.accept(this);
    ignore.push(op->name, 0);
    op->body.accept(this);
    ignore.pop(op->name);
}

void CodeGen::Closure::visit(const For *op) {
    ignore.push(op->name, 0);
    op->min.accept(this);
    op->extent.accept(this);
    op->body.accept(this);
    ignore.pop(op->name);
}

void CodeGen::Closure::visit(const Load *op) {
    op->index.accept(this);
    if (!ignore.contains(op->name)) {
        log(3) << "Adding " << op->name << " to closure\n";
        // result[op->name + ".host"] = gen->llvm_type_of(op->type)->getPointerTo();
        reads[op->name] = op->type;
    } else {
        log(3) << "Not adding " << op->name << " to closure\n";
    }
}

void CodeGen::Closure::visit(const Store *op) {
    op->index.accept(this);
    op->value.accept(this);
    if (!ignore.contains(op->name)) {
        log(3) << "Adding " << op->name << " to closure\n";
        // result[op->name + ".host"] = gen->llvm_type_of(op->value.type())->getPointerTo();
        writes[op->name] = op->value.type();
    } else {
        log(3) << "Not adding " << op->name << " to closure\n";
    }
}

void CodeGen::Closure::visit(const Allocate *op) {
    ignore.push(op->name, 0);
    op->size.accept(this);
    op->body.accept(this);
    ignore.pop(op->name);
}

void CodeGen::Closure::visit(const Variable *op) {            
    if (ignore.contains(op->name)) {
        log(3) << "Not adding " << op->name << " to closure\n";
    } else {
        log(3) << "Adding " << op->name << " to closure\n";
        vars[op->name] = op->type;
    }
}

CodeGen::Closure::Closure(Stmt s, const string &loop_variable) {
    ignore.push(loop_variable, 0);
    s.accept(this);
}

vector<llvm::Type*> CodeGen::Closure::llvm_types(CodeGen *gen) {
    vector<llvm::Type *> res;
    map<string, Type>::const_iterator iter;
    for (iter = vars.begin(); iter != vars.end(); ++iter) {
        res.push_back(gen->llvm_type_of(iter->second));
    }
    for (iter = reads.begin(); iter != reads.end(); ++iter) {
        res.push_back(gen->llvm_type_of(iter->second)->getPointerTo());
    }
    for (iter = writes.begin(); iter != writes.end(); ++iter) {
        res.push_back(gen->llvm_type_of(iter->second)->getPointerTo());
    }
    return res;
}

vector<string> CodeGen::Closure::names() {
    vector<string> res;
    map<string, Type>::const_iterator iter;
    for (iter = vars.begin(); iter != vars.end(); ++iter) {
        log(2) << "vars:  " << iter->first << "\n";
        res.push_back(iter->first);
    }
    for (iter = reads.begin(); iter != reads.end(); ++iter) {
        log(2) << "reads: " << iter->first << "\n";
        res.push_back(iter->first + ".host");
    }
    for (iter = writes.begin(); iter != writes.end(); ++iter) {
        log(2) << "writes: " << iter->first << "\n";
        res.push_back(iter->first + ".host");
    }
    return res;
}

StructType *CodeGen::Closure::build_type(CodeGen *gen) {
    StructType *struct_t = StructType::create(*gen->context, "closure_t");
    struct_t->setBody(llvm_types(gen), false);
    return struct_t;
}

void CodeGen::Closure::pack_struct(CodeGen *gen, Value *dst, const Scope<Value *> &src, IRBuilder<> *builder) {
    // dst should be a pointer to a struct of the type returned by build_type
    int idx = 0;
    vector<string> nm = names();
    vector<llvm::Type*> ty = llvm_types(gen);
    for (size_t i = 0; i < nm.size(); i++) {
        Value *val = src.get(nm[i]);
        Value *ptr = builder->CreateConstInBoundsGEP2_32(dst, 0, idx++);
        if (val->getType() != ty[i]) {
            val = builder->CreateBitCast(val, ty[i]);
        }            
        builder->CreateStore(val, ptr);
    }
}

void CodeGen::Closure::unpack_struct(CodeGen *gen, Scope<Value *> &dst, Value *src, IRBuilder<> *builder, Module *module, LLVMContext &context) {
    // src should be a pointer to a struct of the type returned by build_type
    int idx = 0;
    vector<string> nm = names();
    for (size_t i = 0; i < nm.size(); i++) {
        Value *ptr = builder->CreateConstInBoundsGEP2_32(src, 0, idx++);
        LoadInst *load = builder->CreateLoad(ptr);
        Value *val = load;
        if (load->getType()->isPointerTy()) {
            // Give it a unique type so that tbaa tells llvm that this can't alias anything
            load->setMetadata("tbaa", MDNode::get(context, vec<Value *>(MDString::get(context, nm[i]))));
            
            llvm::Function *fn = module->getFunction("force_no_alias");
            assert(fn && "Did not find force_no_alias in initial module");
            Value *arg = builder->CreatePointerCast(load, llvm::Type::getInt8Ty(context)->getPointerTo());
            CallInst *call = builder->CreateCall(fn, vec(arg));
            mark_call_return_no_alias(call, context);
            val = builder->CreatePointerCast(call, val->getType());
            
        }
        dst.push(nm[i], val);
        val->setName(nm[i]);
    }
}

JITModuleHolder::JITModuleHolder(llvm::Module *module, class CodeGen *cg) : context(&module->getContext()) {
    log(2) << "Creating new execution engine\n";
    string error_string;

    TargetOptions options;
    options.LessPreciseFPMADOption = true;
    options.NoFramePointerElim = false;
    options.NoFramePointerElimNonLeaf = false;
    options.AllowFPOpFusion = FPOpFusion::Fast;
    options.UnsafeFPMath = true;
    options.NoInfsFPMath = true;
    options.NoNaNsFPMath = true;
    options.HonorSignDependentRoundingFPMathOption = false;
    options.UseSoftFloat = false;
    options.FloatABIType = 
        cg->use_soft_float_abi() ? FloatABI::Soft : FloatABI::Hard;
    options.NoZerosInBSS = false;
    options.GuaranteedTailCallOpt = false;
    options.DisableTailCalls = false;
    options.StackAlignmentOverride = 0;
    options.RealignStack = true;
    options.TrapFuncName = "";
    options.PositionIndependentExecutable = true;
    options.EnableSegmentedStacks = false;
    options.UseInitArray = false;
    options.SSPBufferSize = 0;
    
    EngineBuilder engine_builder(module);
    engine_builder.setTargetOptions(options);
    engine_builder.setErrorStr(&error_string);
    engine_builder.setEngineKind(EngineKind::JIT);
    #ifdef USE_MCJIT
    engine_builder.setUseMCJIT(true);        
    #if defined(LLVM_VERSION_MINOR) && LLVM_VERSION_MINOR < 3
    engine_builder.setJITMemoryManager(JITMemoryManager::CreateDefaultMemManager());
    #else
    engine_builder.setJITMemoryManager(new SectionMemoryManager());
    #endif
    #else
    engine_builder.setUseMCJIT(false);
    #endif
    engine_builder.setOptLevel(CodeGenOpt::Aggressive);
    engine_builder.setMCPU(cg->mcpu());
    engine_builder.setMAttrs(vec<string>(cg->mattrs()));
    execution_engine = engine_builder.create();
    if (!execution_engine) std::cout << error_string << std::endl;
    assert(execution_engine && "Couldn't create execution engine");        
}

JITModuleHolder::~JITModuleHolder() {
    shutdown_thread_pool();
    delete execution_engine;
    delete context;
}

template<>
EXPORT RefCount &ref_count<JITModuleHolder>(const JITModuleHolder *f) {return f->ref_count;}

template<>
EXPORT void destroy<JITModuleHolder>(const JITModuleHolder *f) {delete f;}

}}
