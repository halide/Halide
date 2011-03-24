#include "LLVMCompiler.h"
#include "base.h"

#include <llvm/DerivedTypes.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/LLVMContext.h>
#include <llvm/Module.h>
#include <llvm/PassManager.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/Analysis/Passes.h>
#include <llvm/Target/TargetData.h>
#include <llvm/Target/TargetSelect.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Support/IRBuilder.h>

#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Target/TargetRegistry.h>
#include <llvm/Target/TargetData.h>
#include <llvm/Support/Host.h>
//#include <llvm/Support/Debug.h>

#include <iostream>

using namespace llvm;

static inline int32_t truncate(int64_t v) {
    int32_t t = int32_t(v & 0xFFFFFFFF);
    Assert(t == v, "Truncated 64-bit 0x%llx to 32-bit 0x%x", v, t);
    return t;
}

LLVMCompiler::LLVMCompiler() : ctx(getGlobalContext()) {
    InitializeNativeTarget();

    builder = new IRBuilder<>(ctx);

    // Make the module, which holds all the code.
    module = new llvm::Module("FImage JIT", ctx);
    
    // Create the JIT.  This takes ownership of the module.
    std::string errStr;
    ee = EngineBuilder(module).setErrorStr(&errStr).create();
    if (!ee) {
        fprintf(stderr, "Could not create ExecutionEngine: %s\n", errStr.c_str());
        exit(1);
    }

    // Set up the pass manager
    passMgr = new FunctionPassManager(module);
    passMgr->add(new TargetData(*ee->getTargetData()));
    // AliasAnalysis support for GVN
    passMgr->add(createBasicAliasAnalysisPass());
    // Peephole, bit-twiddling optimizations
    passMgr->add(createInstructionCombiningPass());
    // Reassociate expressions
    passMgr->add(createReassociatePass());
    // Eliminate common sub-expressions
    passMgr->add(createGVNPass());
    // Simplify CFG (delete unreachable blocks, etc.)
    passMgr->add(createCFGSimplificationPass());
    
    passMgr->doInitialization();
}

void LLVMCompiler::run() {
    void (*func)(void) = (void (*)(void))ee->getPointerToFunction(mainFunc);
    func();
    //ee->runFunction(mainFunc, args);
}

void LLVMCompiler::compilePrologue() {
    // Set up the main function
    std::vector<const Type*> voidArgs(0);
    mainFunc = Function::Create(FunctionType::get(Type::getVoidTy(ctx), voidArgs, false), Function::ExternalLinkage, "__fimage", module);
    
    BasicBlock* bb = BasicBlock::Create(ctx, "entry", mainFunc);
    builder->SetInsertPoint(bb);
    
    // Generate dummy code in the `entry` block to emit `printf("hi!\n")`;
    Value* s = builder->CreateGlobalString("hi!\n");
    std::vector<const Type*> cstrArgs(1, PointerType::get(Type::getInt8Ty(ctx), 0));
    Function* printfFunc = Function::Create(FunctionType::get(Type::getInt32Ty(ctx), cstrArgs, true), Function::ExternalLinkage, "printf", module);
    s->print(errs()); errs() << "\n";
    s->getType()->print(errs()); errs() << "\n";
    printfFunc->getFunctionType()->getParamType(0)->print(errs()); errs() << "\n";
    builder->CreateCall(printfFunc, builder->CreateConstGEP2_32(s, 0, 0));
}

void LLVMCompiler::compileEpilogue() {
    // Insert return from the generated function
    builder->CreateRetVoid();

    // Verify the module
    std::cerr << "verifying...";
    if (verifyModule(*module)) {
        std::cerr << ": Error constructing function!\n";
        exit(1);
    }
    std::cerr << "OK\n";

    // print the LLVM IR to stderr
    // Different from cerr << *module?
    module->dump();
    
    // Emit a binary object for later inspection.
    // Based on llvm/tools/llc/llc.cpp
    std::string errStr;
    
    const Target* target = TargetRegistry::lookupTarget(sys::getHostTriple(), errStr);
    std::string featureStr;
    TargetMachine* machine = target->createTargetMachine(sys::getHostTriple(), featureStr);
    
    tool_output_file *outfile = new tool_output_file("generated.o", errStr, raw_fd_ostream::F_Binary);
    if (!errStr.empty()) {
        std::cerr << errStr << "\n";
        exit(1);
    }
    formatted_raw_ostream fos(outfile->os());

#if 0
    // Save an object file that you can use dumpbin on to inspect
    printf("Saving object file\n");
    
    // Clone the JIT pass manager -- TODO: is this safe?
    PassManager pm;
    pm.add(new TargetData(*(machine->getTargetData())));
    machine->setAsmVerbosityDefault(true);
    // TODO: Use non-binary file + TargetMachine::CGFT_AssemblyFile?
    if (machine->addPassesToEmitFile(pm, fos, TargetMachine::CGFT_ObjectFile, CodeGenOpt::None))
    {
        errs() << "Target " << target->getName() << " does not support generation of requested object file type.\n";
        exit(1);
    }
    
    // The PassManager performs and optimization and dumps out generated.o
    pm.run(*module);
#endif //0
}

// TODO: refactor this into base Compiler::compileDefinition and more detailed compileBody per-backend?
// TODO: only pushes compileBody in concrete subclasses? And loop management code?
// Compile the evaluation of a single FImage
void LLVMCompiler::preCompileDefinition(FImage *im, int definition) {

    time_t t1 = timeGetTime();

    // Transform code, build vars and roots lists, vectorWidth and unroll, etc.
    Compiler::preCompileDefinition(im, definition);

    levelBlocks.clear();
    levelBlocks.resize(vars.size());

    // Initialize the varValues storage area    
    varValues.clear();
    varValues.resize(vars.size());
    // TODO: replace this with emission-ordered varValues[i] = new expression assigning to the var; simplify away loadIfPointer...?
    for (size_t i = 0; i < vars.size(); i++) {
        varValues[i] = builder->CreateAlloca(Type::getInt64Ty(ctx));
        nodeValues[vars[i]] = varValues[i];
    }
    
    time_t t2 = timeGetTime();
    printf("Pre-compilation took %ld ms\n", t2-t1);
}

void LLVMCompiler::compileLoopHeader(size_t i) {
#if 0
    a.mov(varRegs[i], vars[i]->interval.min());
    a.label(labels[i]);
#endif
    char label[20];
    snprintf(label, 20, "level%d", (int)i);
    
    Assert(varData(i)->order != Decreasing, "Don't currently seem to initialize for decreasing loops.");
    // Store the initial loop induction value
    Value* loopMin = ConstantInt::get(Type::getInt64Ty(ctx), vars[i]->interval.min());
    builder->CreateStore(loopMin, varValues[i]);
    //varValues[i] = loopMin;
    //nodeValues[vars[i]] = varValues[i];
    errs() << "varValues." << i << " = "; varValues[i]->print(errs()); errs() << "\n";
    
    BasicBlock* bb = BasicBlock::Create(ctx, label, mainFunc);
    builder->CreateBr(bb);
    builder->SetInsertPoint(bb);
    levelBlocks[i] = bb;
}

void LLVMCompiler::compileLoopTail(size_t i) {
    // Iterate loops at tail
    // TODO: factor out - express in IRNodes? Compile Loop Tail?
    Value* step = ConstantInt::get(Type::getInt64Ty(ctx), vectorWidth[i]*unroll[i]);
    BasicBlock* nextBlock = BasicBlock::Create(ctx, "", mainFunc);
    Value *cond;
    if (varData(i)->order == Decreasing) {
        #if 0
        // should these 
        a.sub(varRegs[i], vectorWidth[i]*unroll[i]);
        a.cmp(varRegs[i], truncate(vars[i]->interval.min()));
        a.jge(labels[i]);
        #endif
        /*
        // TODO: unify varValues[i] as accessor method: ref to nodeValues[vars[i]] for simplicity/safety?
        varValues[i] = builder->CreateSub(varValues[i], step);
        nodeValues[vars[i]] = varValues[i];
         */
        Value* newVar = builder->CreateSub(loadIfPointer(varValues[i]), step);
        builder->CreateStore(newVar, varValues[i]);
        
        // TODO: truncate interval.min, like AsmX64? Seemingly unnecessary/unsafe...
        Value* bound = ConstantInt::get(Type::getInt64Ty(ctx), vars[i]->interval.min());
        cond = builder->CreateICmpSGE(newVar, bound);
        builder->CreateCondBr(cond, levelBlocks[i], nextBlock);
    } else {
        #if 0
        // At this point, parallel is treated as increasing
        a.add(varRegs[i], vectorWidth[i]*unroll[i]);
        a.cmp(varRegs[i], truncate(vars[i]->interval.max()+1));
        a.jl(labels[i]);
        #endif
        /*
        errs() << "varValues." << i << " = "; varValues[i]->print(errs()); errs() << "\n";
        varValues[i] = builder->CreateAdd(varValues[i], step);
        nodeValues[vars[i]] = varValues[i];
        errs() << "varValues." << i << " = "; varValues[i]->print(errs()); errs() << "\n";
         */
        Value* newVar = builder->CreateAdd(loadIfPointer(varValues[i]), step);
        builder->CreateStore(newVar, varValues[i]);

        // TODO: truncate interval.min, like AsmX64? Seemingly unnecessary/unsafe...
        Value* bound = ConstantInt::get(Type::getInt64Ty(ctx), vars[i]->interval.max()+1);
        cond = builder->CreateICmpSLT(newVar, bound);
    }
    builder->CreateCondBr(cond, levelBlocks[i], nextBlock);
    builder->SetInsertPoint(nextBlock);
}

Value* LLVMCompiler::loadIfPointer(Value* v) {
    if (v->getType()->isPointerTy()) {
        errs() << "loading "; v->getType()->print(errs());
        return builder->CreateLoad(v);
    } else {
        errs() << "not loading "; v->getType()->print(errs());
        return v;
    }
}

// Generate machine code for a vector of IRNodes. Registers must have already been assigned.
void LLVMCompiler::compileBody(vector<IRNode::Ptr> code) {
    for (size_t i = 0; i < code.size(); i++) {
        // Extract the node, its register, and any inputs and their registers
        IRNode::Ptr node = code[i];
        IRNode::Ptr c1 = (node->inputs.size() >= 1) ? node->inputs[0] : NULL_IRNODE_PTR;
        IRNode::Ptr c2 = (node->inputs.size() >= 2) ? node->inputs[1] : NULL_IRNODE_PTR;
        IRNode::Ptr c3 = (node->inputs.size() >= 3) ? node->inputs[2] : NULL_IRNODE_PTR;
        IRNode::Ptr c4 = (node->inputs.size() >= 4) ? node->inputs[3] : NULL_IRNODE_PTR;

        Value* v1 = (node->inputs.size() >= 1) ? nodeValues[c1] : NULL;
        Value* v2 = (node->inputs.size() >= 2) ? nodeValues[c2] : NULL;
        Value* v3 = (node->inputs.size() >= 3) ? nodeValues[c3] : NULL;
        Value* v4 = (node->inputs.size() >= 4) ? nodeValues[c4] : NULL;

        switch(node->op) {
        case Const:
            errs() << "Const\n";
            if (node->type == IRNode::Float) {
                nodeValues[node] = ConstantFP::get(Type::getFloatTy(ctx), node->fval);
            } else if (node->type == IRNode::Bool) {
                nodeValues[node] = ConstantInt::get(Type::getInt1Ty(ctx), node->ival);
            } else {
                nodeValues[node] = ConstantInt::get(Type::getInt64Ty(ctx), node->ival);
            }
            break;
        case Variable:
            errs() << "Variable\n";
            // TODO: remove extraneous validation
            Assert(nodeValues.count(node), "Variables should be recorded at init-time\n");
            Assert(std::find(vars.begin(), vars.end(), node) != vars.end(),
                   "Variable node should have been in vars vector\n");
            // These are placed in GPRs externally
            //Assert(gpr, "Variables must be manually placed in gprs\n");
            break;
        
        // TODO: vector ops unsupported so far
        //case StoreVector:
        //    errs() << "Vector ";
        case Store:
            errs() << "Store\n";
            // TODO REFACTOR: address computation logic is reundant with Load
            // add base+imm
            v1 = builder->CreateAdd(v1, ConstantInt::get(Type::getInt64Ty(ctx), node->ival));
            // Cast to pointer
            if (node->type == IRNode::Float) {
                v1 = builder->CreateCast(Instruction::IntToPtr, v1, Type::getFloatPtrTy(ctx));
            } else {
                assert(node->type == IRNode::Int);
                v1 = builder->CreateCast(Instruction::IntToPtr, v1, Type::getInt64PtrTy(ctx));
            }
            // Store it
            nodeValues[node] = builder->CreateStore(loadIfPointer(v2), v1);
            break;
        
        // TODO: vector ops unsupported so far
        //case LoadVector:
        //    errs() << "Vector ";
        case Load:
            errs() << "Load\n";
            // add base+imm
            v1 = builder->CreateAdd(v1, ConstantInt::get(Type::getInt64Ty(ctx), node->ival));
            // Cast to pointer
            if (node->type == IRNode::Float) {
                v1 = builder->CreateCast(Instruction::IntToPtr, v1, Type::getFloatPtrTy(ctx));
            } else {
                assert(node->type == IRNode::Int);
                v1 = builder->CreateCast(Instruction::IntToPtr, v1, Type::getInt64PtrTy(ctx));
            }
            // Load it
            nodeValues[node] = builder->CreateLoad(v1);
            break;
            
        case NoOp:
            break;

        default:
            panic("Not implemented: %s\n", opname(node->op));
            break;

        case PlusImm:
            errs() << "Imm ";
            // TODO REFACTOR: redundant imm-const-loading code among *Imm ops
            if (node->type == IRNode::Float) {
                v2 = ConstantInt::get(Type::getFloatTy(ctx), node->fval);
            } else {
                assert(node->type == IRNode::Int);
                v2 = ConstantInt::get(Type::getInt64Ty(ctx), node->ival);
            }
        case Plus:
            errs() << "Plus\n";
            if (node->type == IRNode::Float) {
                nodeValues[node] = builder->CreateFAdd(loadIfPointer(v1), loadIfPointer(v2));
            } else {
                assert(node->type == IRNode::Int);
                nodeValues[node] = builder->CreateAdd(loadIfPointer(v1), loadIfPointer(v2));
            }
            break;
        case Minus:
            errs() << "Minus\n";
            if (node->type == IRNode::Float) {
                nodeValues[node] = builder->CreateFSub(loadIfPointer(v1), loadIfPointer(v2));
            } else {
                assert(node->type == IRNode::Int);
                nodeValues[node] = builder->CreateSub(loadIfPointer(v1), loadIfPointer(v2));
            }
            break;
        case TimesImm:
            errs() << "Imm ";
            // TODO REFACTOR: redundant imm-const-loading code among *Imm ops
            if (node->type == IRNode::Float) {
                v2 = ConstantInt::get(Type::getFloatTy(ctx), node->fval);
            } else {
                assert(node->type == IRNode::Int);
                v2 = ConstantInt::get(Type::getInt64Ty(ctx), node->ival);
            }
        case Times:
            errs() << "Times\n";
            if (node->type == IRNode::Float) {
                nodeValues[node] = builder->CreateFMul(loadIfPointer(v1), loadIfPointer(v2));
            } else {
                assert(c1->type == IRNode::Int);
                nodeValues[node] = builder->CreateMul(loadIfPointer(v1), loadIfPointer(v2));
            }
            break;
        case Divide:
            errs() << "Divide\n";
            if (node->type == IRNode::Float) {
                nodeValues[node] = builder->CreateFDiv(loadIfPointer(v1), loadIfPointer(v2));
            } else {
                assert(c1->type == IRNode::Int);
                nodeValues[node] = //builder->CreateDiv(loadIfPointer(v1), loadIfPointer(v2));
                builder->CreateSDiv(loadIfPointer(v1), loadIfPointer(v2));
            }
            break;
#if 0
        case And:
            Assert(!gpr && !gpr1 && !gpr2, "Can only and in sse regs for now\n");
            if (dst == src1) 
                a.bandps(dst, src2);
            else if (dst == src2)
                a.bandps(dst, src1);
            else {
                a.movaps(dst, src1);
                a.bandps(dst, src2);
            }
            break;
        case Nand:
            Assert(!gpr && !gpr1 && !gpr2, "Can only nand in sse regs for now\n");
            if (dst == src1) {
                a.bandnps(dst, src2);
            } else if (dst == src2) {
                a.movaps(tmp, src2);
                a.movaps(src2, src1);
                a.bandnps(src2, tmp); 
            } else {
                a.movaps(dst, src1);
                a.bandnps(dst, src2);
            }
            break;
        case Or:               
            Assert(!gpr && !gpr1 && !gpr2, "Can only or in sse regs for now\n");
            if (dst == src1) 
                a.borps(dst, src2);
            else if (dst == src2)
                a.borps(dst, src1);
            else {
                a.movaps(dst, src1);
                a.borps(dst, src2);
            }
            break;
        case NEQ:                               
            Assert(!gpr && !gpr1 && !gpr2, "Can only neq in sse regs for now\n");
            if (dst == src1) 
                a.cmpneqps(dst, src2);
            else if (dst == src2)
                a.cmpneqps(dst, src1);
            else {
                a.movaps(dst, src1);
                a.cmpneqps(dst, src2);
            }
            break;
        case EQ:
            Assert(!gpr && !gpr1 && !gpr2, "Can only eq in sse regs for now\n");
            if (dst == src1) 
                a.cmpeqps(dst, src2);
            else if (dst == src2)
                a.cmpeqps(dst, src1);
            else {
                a.movaps(dst, src1);
                a.cmpeqps(dst, src2);
            }
            break;
        case LT:
            Assert(!gpr && !gpr1 && !gpr2, "Can only lt in sse regs for now\n");
            if (dst == src1) 
                a.cmpltps(dst, src2);
            else if (dst == src2)
                a.cmpnleps(dst, src1);
            else {
                a.movaps(dst, src1);
                a.cmpltps(dst, src2);
            }
            break;
        case GT:
            Assert(!gpr && !gpr1 && !gpr2, "Can only gt in sse regs for now\n");
            if (dst == src1) 
                a.cmpnleps(dst, src2);
            else if (dst == src2)
                a.cmpltps(dst, src1);
            else {
                a.movaps(dst, src1);
                a.cmpnleps(dst, src2);
            }
            break;
        case LTE:
            Assert(!gpr && !gpr1 && !gpr2, "Can only lte in sse regs for now\n");
            if (dst == src1) 
                a.cmpleps(dst, src2);
            else if (dst == src2)
                a.cmpnltps(dst, src1);
            else {
                a.movaps(dst, src1);
                a.cmpleps(dst, src2);
            }
            break;
        case GTE:
            Assert(!gpr && !gpr1 && !gpr2, "Can only gte in sse regs for now\n");
            if (dst == src1) 
                a.cmpnltps(dst, src2);
            else if (dst == src2)
                a.cmpleps(dst, src1);
            else {
                a.movaps(dst, src1);
                a.cmpnltps(dst, src2);
            }
            break;
        case ATan2:
        case Mod:
        case Power:
        case Sin:
        case Cos:
        case Tan:
        case ASin:
        case ACos:
        case ATan:
        case Exp:
        case Log:
        case Floor:
        case Ceil:
        case Round:
        case Abs:               
        case FloatToInt:
            panic("Not implemented: %s\n", opname(node->op));                
            break;
        case IntToFloat:
            if (gpr1 && !gpr) {
                // TODO: this truncates to 32-bits currently
                a.cvtsi2ss(dst, gsrc1);
            } else {
                panic("IntToFloat can only go from gpr to sse\n");
            }
            break;
        case ExtractVector:
            Assert(!gpr && !gpr1 && !gpr2, "Can only select vector in sse regs\n");
            if (node->ival == 1) {
                if (dst == src1) {                                        
                    a.movaps(tmp, src1);
                    a.shufps(tmp, src2, 3, 3, 0, 0);
                    a.shufps(dst, tmp, 1, 2, 0, 2);
                } else if (dst == src2) {
                    a.movaps(tmp, src2);
                    a.shufps(tmp, src1, 0, 0, 3, 3);
                    a.movaps(dst, src1);
                    a.shufps(dst, tmp, 1, 2, 2, 0);                    
                } else {
                    a.movaps(tmp, src1);
                    a.shufps(tmp, src2, 3, 3, 0, 0);                    
                    a.movaps(dst, src1);
                    a.shufps(dst, tmp, 1, 2, 0, 2);
                }
            } else if (node->ival == 2) {
                if (dst == src1) {
                    a.shufps(dst, src2, 2, 3, 0, 1);
                } else if (dst == src2) {
                    a.movaps(tmp, src2);
                    a.movaps(dst, src1);
                    a.shufps(dst, tmp, 2, 3, 0, 1);                    
                } else {
                    a.movaps(dst, src1);
                    a.shufps(dst, src2, 2, 3, 0, 1);
                }
            } else if (node->ival == 3) {
                if (dst == src1) {
                    a.shufps(dst, src2, 3, 3, 0, 0);
                    a.shufps(dst, src2, 0, 2, 1, 2);
                } else if (dst == src2) {
                    a.movaps(tmp, src1);
                    a.shufps(tmp, src2, 3, 3, 0, 0);
                    a.shufps(tmp, src2, 0, 2, 1, 2);
                    a.movaps(dst, tmp);
                } else {
                    a.movaps(dst, src1);
                    a.shufps(dst, src2, 3, 3, 0, 0);
                    a.shufps(dst, src2, 0, 2, 1, 2);
                }
            } else {
                panic("Can't deal with ExtractVector with argument other than 1, 2, or 3\n");
            }
            break;
        case ExtractScalar:
            Assert(!gpr && !gpr1, "Can only extract scalar from sse regs into sse regs\n");
            if (!(dst == src1)) a.movaps(dst, src1);
            Assert(node->ival >= 0 && node->ival < 4, 
                   "Integer argument to ExtractScalar must be 0, 1, 2, or 3\n");
            a.shufps(dst, src1, 
                      (uint8_t)node->ival, (uint8_t)node->ival,
                      (uint8_t)node->ival, (uint8_t)node->ival);
            break;
        case Vector:
            Assert(!gpr, "Can't put vectors in gprs");

            // Can we use shufps?
            if (src1 == src2 && src3 == src4) {
                if (src1 == dst) {
                    a.shufps(dst, src3, 0, 0, 0, 0);
                } else if (src3 == dst) {
                    a.movaps(tmp, src1);
                    a.shufps(tmp, src3, 0, 0, 0, 0);
                    a.movaps(src3, tmp);
                } else {
                    a.movaps(dst, src1);
                    a.shufps(dst, src3, 0, 0, 0, 0);
                }
            } else if (dst == src1) {
                a.punpckldq(dst, src2);
                a.movaps(tmp, src3);
                a.punpckldq(tmp, src4);
                a.punpcklqdq(dst, tmp);
            } else {
                // Most general case: We're allowed to clobber the
                // high floats in the sources, because they're scalar

                a.movaps(tmp, src1);
                a.punpckldq(tmp, src2);
                a.punpckldq(src3, src4); // clobber the high words in src3
                a.punpcklqdq(tmp, src3);
                a.movaps(dst, tmp);

                // No clobber version:
                /*
                a.movaps(tmp, src1);
                a.punpckldq(tmp, src2);
                a.movaps(tmp2, src3);
                a.punpckldq(tmp2, src4);
                a.punpcklqdq(tmp, tmp2);
                a.movaps(dst, tmp);
                */
            }
            break;
#endif //0
        }
    }
}
