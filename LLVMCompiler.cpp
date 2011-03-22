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
#include "llvm/Support/IRBuilder.h"

using namespace llvm;

static inline int32_t truncate(int64_t v) {
    int32_t t = int32_t(v & 0xFFFFFFFF);
    Assert(t == v, "Truncated 64-bit 0x%llx to 32-bit 0x%x", v, t);
    return t;
}

LLVMCompiler::LLVMCompiler() {
    InitializeNativeTarget();

    LLVMContext &context = getGlobalContext();
    IRBuilder<> *builder = new IRBuilder<>(context);
    

    // Make the module, which holds all the code.
    llvm::Module *module = new llvm::Module("FImage JIT", context);
    

    // Create the JIT.  This takes ownership of the module.
    std::string errStr;
    ExecutionEngine *executionEngine = EngineBuilder(module).setErrorStr(&errStr).create();
    if (!executionEngine) {
        fprintf(stderr, "Could not create ExecutionEngine: %s\n", errStr.c_str());
        exit(1);
    }

    FunctionPassManager *passmgr = new FunctionPassManager(module);
}

void LLVMCompiler::run() {
#if 0
#endif
}

void LLVMCompiler::compilePrologue() {
#if 0
    // Align the stack to a 16-byte boundary - it always comes in
    // offset by 8 bytes because it contains the 64-bit return
    // address.
    a.sub(a.rsp, 8);
    
    // Save all the registers that the 64-bit C abi tells us we're
    // supposed to. This maintains stack alignment.
    a.pushNonVolatiles();
    
    // Find any variables that are fused over the definitions
    // TODO
#endif
}

void LLVMCompiler::compileEpilogue() {
#if 0
    // Exit any loops that were fused over definitions
    // TODO
    
    // Pop the stack and return
    a.popNonVolatiles();
    a.add(a.rsp, 8);
    a.ret();        
    
    printf("Saving object file\n");
    
    // Save an object file that you can use dumpbin on to inspect
    a.saveCOFF("generated.obj");        
    a.saveELF("generated.o");
#endif
}

// TODO: refactor this into base Compiler::compileDefinition and more detailed compileBody per-backend?
// TODO: only pushes compileBody in concrete subclasses? And loop management code?
// Compile the evaluation of a single FImage
void LLVMCompiler::preCompileDefinition(FImage *im, int definition) {

    time_t t1 = timeGetTime();

    // Transform code, build vars and roots lists, vectorWidth and unroll, etc.
    Compiler::preCompileDefinition(im, definition);

    // Set up label strings
#if 0
    for (int i = 0; i < 10; i++) {
        snprintf(labels[i], 20, "l%d.%d", definition, i);
    }
#endif

    time_t t2 = timeGetTime();
    printf("Pre-compilation took %ld ms\n", t2-t1);
}

void LLVMCompiler::compileLoopHeader(size_t i) {
#if 0
    a.mov(varRegs[i], vars[i]->interval.min());
    a.label(labels[i]);
#endif
}

void LLVMCompiler::compileLoopTail(size_t i) {
    // Iterate loops at tail
    // TODO: factor out - express in IRNodes? Compile Loop Tail?
    if (varData(i)->order == Decreasing) {
        #if 0
        // should these 
        a.sub(varRegs[i], vectorWidth[i]*unroll[i]);
        a.cmp(varRegs[i], truncate(vars[i]->interval.min()));
        a.jge(labels[i]);
        #endif
    } else {
        #if 0
        // At this point, parallel is treated as increasing
        a.add(varRegs[i], vectorWidth[i]*unroll[i]);
        a.cmp(varRegs[i], truncate(vars[i]->interval.max()+1));
        a.jl(labels[i]);
        #endif
    }
}

// Generate machine code for a vector of IRNodes. Registers must have already been assigned.
void LLVMCompiler::compileBody(vector<IRNode::Ptr> code) {
#if 0
    AsmX64::SSEReg tmp = a.xmm15;
    AsmX64::Reg gtmp = a.r15;

    for (size_t i = 0; i < code.size(); i++) {
        // Extract the node, its register, and any inputs and their registers
        IRNode::Ptr node = code[i];
        IRNode::Ptr c1 = (node->inputs.size() >= 1) ? node->inputs[0] : NULL_IRNODE_PTR;
        IRNode::Ptr c2 = (node->inputs.size() >= 2) ? node->inputs[1] : NULL_IRNODE_PTR;
        IRNode::Ptr c3 = (node->inputs.size() >= 3) ? node->inputs[2] : NULL_IRNODE_PTR;
        IRNode::Ptr c4 = (node->inputs.size() >= 4) ? node->inputs[3] : NULL_IRNODE_PTR;

        // SSE source and destination registers
        AsmX64::SSEReg dst(node->reg-16);
        AsmX64::SSEReg src1(c1 ? c1->reg-16 : 0);
        AsmX64::SSEReg src2(c2 ? c2->reg-16 : 0);
        AsmX64::SSEReg src3(c3 ? c3->reg-16 : 0);
        AsmX64::SSEReg src4(c4 ? c4->reg-16 : 0);

        // Is the destination a GPR?
        bool gpr = node->reg < 16;

        // Which sources are GPRs?
        bool gpr1 = c1 && (c1->reg < 16);
        bool gpr2 = c2 && (c2->reg < 16);
        //bool gpr3 = c3 && (c3->reg < 16);
        //bool gpr4 = c4 && (c4->reg < 16);

        // GPR source and destination registers
        AsmX64::Reg gdst(node->reg);
        AsmX64::Reg gsrc1(c1 ? c1->reg : 0);
        AsmX64::Reg gsrc2(c2 ? c2->reg : 0);
        AsmX64::Reg gsrc3(c3 ? c3->reg : 0);
        AsmX64::Reg gsrc4(c4 ? c4->reg : 0);

        switch(node->op) {
        case Const: 
            if (node->type == IRNode::Float) {
                if (node->fval == 0.0f) {
                    a.bxorps(dst, dst);
                } else {
                    a.mov(gtmp, a.addData(node->fval));
                    a.movss(dst, AsmX64::Mem(gtmp));
                    //a.shufps(dst, dst, 0, 0, 0, 0);
                }
            } else if (node->type == IRNode::Bool) {
                if (gpr) {
                    if (node->ival) 
                        a.mov(gdst, -1);
                    else
                        a.mov(gdst, 0);
                } else {
                    if (node->ival) {
                        a.cmpeqps(dst, dst);
                    } else {
                        a.bxorps(dst, dst);                    
                    }
                }
            } else {
                if (gpr) {
                    a.mov(gdst, node->ival);
                } else {
                    a.mov(a.r15, node->ival);
                    // ints are 32-bit for now, so this works
                    a.cvtsi2ss(dst, a.r15);
                    //a.shufps(dst, dst, 0, 0, 0, 0);                        
                }
            }
            break;
        case Variable:
            // These are placed in GPRs externally
            Assert(gpr, "Variables must be manually placed in gprs\n");
            break;
        case Plus:
            if (gpr && gpr1 && gpr2) {
                if (gdst == gsrc1)
                    a.add(gdst, gsrc2);
                else if (gdst == gsrc2) 
                    a.add(gdst, gsrc1);
                else {
                    a.mov(gdst, gsrc1);
                    a.add(gdst, gsrc2);
                }
            } else if (!gpr && !gpr1 && !gpr2) {
                if (dst == src1)
                    a.addps(dst, src2);
                else if (dst == src2) 
                    a.addps(dst, src1);
                else {
                    a.movaps(dst, src1);
                    a.addps(dst, src2);
                }
            } else {
                panic("Can't add between gpr/sse\n");
            }
            break;
        case Minus:
            if (gpr && gpr1 && gpr2) {
                if (gdst == gsrc1) {
                    a.sub(gdst, gsrc2);
                } else if (gdst == gsrc2) {
                    a.mov(gtmp, gsrc2);
                    a.mov(gsrc2, gsrc1);
                    a.sub(gsrc2, gtmp);
                } else {                         
                    a.mov(gdst, gsrc1);
                    a.sub(gdst, gsrc2);
                }
            } else if (!gpr && !gpr1 && !gpr2) {
                if (dst == src1) {
                    a.subps(dst, src2);
                } else if (dst == src2) {
                    a.movaps(tmp, src2);
                    a.movaps(src2, src1);
                    a.subps(src2, tmp);
                } else { 
                    a.movaps(dst, src1);
                    a.subps(dst, src2);
                }
            } else {
                panic("Can't sub between gpr/sse\n");
            }
            break;
        case Times: 
            if (gpr && gpr1 && gpr2) {
                if (gdst == gsrc1)
                    a.imul(gdst, gsrc2);
                else if (gdst == gsrc2) 
                    a.imul(gdst, gsrc1);
                else {
                    a.mov(gdst, gsrc1);
                    a.imul(gdst, gsrc2);
                }
            } else if (!gpr && !gpr1 && !gpr2) {
                if (dst == src1)
                    a.mulps(dst, src2);
                else if (dst == src2) 
                    a.mulps(dst, src1);
                else {
                    a.movaps(dst, src1);
                    a.mulps(dst, src2);
                }
            } else {
                panic("Can't sub between gpr/sse\n");
            }
            break;
        case TimesImm:
            Assert(fits32(node->ival), 
                   "TimesImm may only use a 32-bit signed constant\n");
            if (gdst == gsrc1) {
                a.imul(gdst, (int32_t)node->ival);
            } else {
                a.mov(gdst, (int32_t)node->ival);
                a.imul(gdst, gsrc1);
            }
            break;
        case PlusImm:
            Assert(fits32(node->ival),
                   "PlusImm may only use a 32-bit signed constant\n");
            if (gdst == gsrc1) {
                a.add(gdst, (int32_t)node->ival);
            } else {
                a.mov(gdst, (int32_t)node->ival);
                a.add(gdst, gsrc1);
            }
            break;
        case Divide: 
            Assert(!gpr && !gpr1 && !gpr2, "Can only divide in sse regs for now\n");
            if (dst == src1) {
                a.divps(dst, src2);
            } else if (dst == src2) {
                a.movaps(tmp, src2);
                a.movaps(src2, src1);
                a.divps(src2, tmp); 
            } else {
                a.movaps(dst, src1);
                a.divps(dst, src2);
            }
            break;
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
        case StoreVector:
        case Store:
            Assert(gpr1, "Can only store using addresses in gprs\n");
            Assert(!gpr2, "Can only store values in sse registers\n");
            Assert(fits32(node->ival),
                   "Store may only use a 32-bit signed constant - 0x%llx overflows\n", node->ival);
            if (node->width == 1) {
                a.movss(AsmX64::Mem(gsrc1, (int32_t)node->ival), src2);
            } else {
                SteppedInterval i = node->inputs[0]->interval + node->ival;
                printf("%lld %lld %lld %lld\n", i.min(), i.max(), i.remainder(), i.modulus());
                if ((i.modulus() & 0xf) == 0 &&
                    (i.remainder() & 0xf) == 0) {
                    a.movaps(AsmX64::Mem(gsrc1, (int32_t)node->ival), src2);
                } else {
                    printf("Unaligned store!\n");
                    a.movups(AsmX64::Mem(gsrc1, (int32_t)node->ival), src2);
                }
            }
            break;
        case LoadVector:
        case Load:
            Assert(gpr1, "Can only load using addresses in gprs\n");
            Assert(!gpr, "Can only load into sse regs\n");
            Assert(fits32(node->ival),
                   "Load may only use a 32-bit signed constant\n");
            if (node->width == 1) {
                a.movss(dst, AsmX64::Mem(gsrc1, (int32_t)node->ival));
            } else {
                SteppedInterval i = node->inputs[0]->interval + node->ival;
                if ((i.modulus() & 0xf) == 0 &&
                    (i.remainder() & 0xf) == 0) {
                    a.movaps(dst, AsmX64::Mem(gsrc1, (int32_t)node->ival));
                } else {
                    printf("Unaligned load!\n");
                    a.movups(dst, AsmX64::Mem(gsrc1, (int32_t)node->ival));
                }
            }
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
        case NoOp:
            break;
        }
    }
#endif
}
