#include "AsmX64Compiler.h"

#include <algorithm>

static inline int32_t truncate(int64_t v) {
    int32_t t = int32_t(v & 0xFFFFFFFF);
    Assert(t == v, "Truncated 64-bit 0x%llx to 32-bit 0x%x", v, t);
    return t;
}

void AsmX64Compiler::compilePrologue() {
    // Align the stack to a 16-byte boundary - it always comes in
    // offset by 8 bytes because it contains the 64-bit return
    // address.
    a.sub(a.rsp, 8);
    
    // Save all the registers that the 64-bit C abi tells us we're
    // supposed to. This maintains stack alignment.
    a.pushNonVolatiles();
    
    // Find any variables that are fused over the definitions
    // TODO
}

void AsmX64Compiler::compileEpilogue() {
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
}

// TODO: refactor this into base Compiler::compileDefinition and more detailed compileBody per-backend?
// TODO: only pushes compileBody in concrete subclasses? And loop management code?
// Compile the evaluation of a single FImage
void AsmX64Compiler::preCompileDefinition(FImage *im, int definition) {

    time_t t1 = timeGetTime();

    // Transform code, build vars and roots lists, vectorWidth and unroll, etc.
    Compiler::preCompileDefinition(im, definition);

    // TODO: reorganize comments for new structure
    
    // -----------------------------------------------------------
    // Everything below here can be ripped out and pushed to llvm
    // -----------------------------------------------------------

    // Register assignment and evaluation ordering. This returns a
    // vector of vector of IRNode - one to be computed at each loop
    // level. We're assuming the loop structure looks like this:

    // compute constants (order[0])
    // for var level 1:
    //   compute things that depend on var level 1 (order[1])
    //   for var level 2:
    //     compute things that depend on var level 2 (order[2])
    //     for var level 3:
    //       compute things that depend on var level 3 (order[3])
    //       for ...
    //          ...
    
    // TODO: lift this out, push into just compileBody prologue by tracking and clearing single "registers allocated?" bit at start of compileDefinition, and allocating if not already?
    printf("Register assignment...\n");
    assignRegisters();
    printf("Done\n");

    // which register is the output pointer in?
    //AsmX64::Reg outPtr(roots[roots.size()-1]->reg);

    // Set up label strings
    for (int i = 0; i < 10; i++) {
        snprintf(labels[i], 20, "l%d.%d", definition, i);
    }

    time_t t2 = timeGetTime();
    printf("Pre-compilation took %ld ms\n", t2-t1);
}

void AsmX64Compiler::compileLoopHeader(size_t i) {
    a.mov(varRegs[i], vars[i]->interval.min());
    a.label(labels[i]);
}

void AsmX64Compiler::compileLoopTail(size_t i) {
    // Iterate loops at tail
    // TODO: factor out - express in IRNodes? Compile Loop Tail?
    if (varData(i)->order == Decreasing) {
        // should these 
        a.sub(varRegs[i], vectorWidth[i]*unroll[i]);
        a.cmp(varRegs[i], truncate(vars[i]->interval.min()));
        a.jge(labels[i]);
    } else {
        // At this point, parallel is treated as increasing
        a.add(varRegs[i], vectorWidth[i]*unroll[i]);
        a.cmp(varRegs[i], truncate(vars[i]->interval.max()+1));
        a.jl(labels[i]);
    }
}

// Generate machine code for a vector of IRNodes. Registers must have already been assigned.
void AsmX64Compiler::compileBody(vector<IRNode::Ptr> code) {
        
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
                printf("%ld %ld %ld %ld\n", i.min(), i.max(), i.remainder(), i.modulus());
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
}

// This function assigns registers and generates an evaluation
// order for an array of expressions (roots). It takes as input:
//
// roots: the vector of expressions to be assigned registers
//
// reserved: Registers corresponding to bits marked high in this mask
// may not be used. Bit 31 (which corresponds to register xmm15) must
// not be marked high, because the code generator uses that as
// scratch.
// 
// As output it returns primarily order: a vector of vectors of
// IRNodes, one to be computed at each loop level. For a 2D image, the
// loop structure would look like this: We're assuming the loop
// structure looks like this:
//
// compute constants (order[0])
// for c:
//   compute things that depend on c (order[1])
//   for y:
//     compute things that depend on y (order[2])
//     for x:
//       compute things that depens on x (order[3])
// 
// Also, clobberedRegs will contain masks of which registers get
// clobbered at each level, and outputRegs will indicate which
// registers contain output from a level (i.e. registers either
// used by roots, or used by a higher level).
// 
void AsmX64Compiler::assignRegisters() {

    // Assign the variables some registers
    varRegs = vector<AsmX64::Reg>(vars.size());
    if (varRegs.size() > 0) varRegs[0] = a.rax;
    if (varRegs.size() > 1) varRegs[1] = a.rcx;
    if (varRegs.size() > 2) varRegs[2] = a.rdx;
    if (varRegs.size() > 3) varRegs[3] = a.rbx;
    if (varRegs.size() > 4) varRegs[4] = a.rbp;
    if (varRegs.size() > 5) varRegs[5] = a.rsi;
    if (varRegs.size() > 6) varRegs[6] = a.rdi;
    if (varRegs.size() > 7) panic("Can't handle more than 7 loop indices for now\n");
    
    AsmX64::Reg tmp = a.r15;
    
    // Mark these registers as unclobberable for the register allocation
    uint32_t reserved = ((1 << tmp.num) | 
                         (1 << a.rsp.num));
    
    // Force the indices into the intended registers and mark them as reserved
    for (size_t i = 0; i < varRegs.size(); i++) {
        reserved |= (1 << varRegs[i].num);
        vars[i]->reg = varRegs[i].num;
    }
    
    // Clear any previous register assignment, and make the
    // descendents of the roots for evaluation (sets tag to 1)
    for (size_t i = 0; i < roots.size(); i++) {
        regClear(roots[i]);
    }

    // Who's currently occupying which register? First the 16 gprs, then the 16 sse registers.
    vector<IRNode::Ptr > regs(32);

    // Reserve xmm15 for the code generator to use as scratch
    Assert(!(reserved & (1<<31)),
           "Register xmm15 is reserved for the code generator\n");
    reserved |= (1<<31);
    
    // Now assign a register to each node, in the order of evaluation
    for (size_t l = 0; l < order.size(); l++) {
        for (size_t i = 0; i < order[l].size(); i++) {
            // Assign registers to this expression (sets tag to 3)
            regAssign(order[l][i], reserved, regs, order);

            // If we just evaluated a root, don't let it get clobbered            
            if (find(roots.begin(), roots.end(), order[l][i]) != roots.end())
                reserved |= (1 << order[l][i]->reg);
        }
    }

    // Detect what registers get clobbered
    vector<uint32_t> clobberedRegs(order.size(), 0);
    for (size_t i = 0; i < order.size(); i++) {
        clobberedRegs[i] = (1<<31);
        for (size_t j = 0; j < order[i].size(); j++) {
            IRNode::Ptr node = order[i][j];
            clobberedRegs[i] |= (1 << node->reg);
        }
    }


    // Detect what registers are used for inter-level communication
    vector<uint32_t> outputRegs(order.size(), 0);
    
    if (outputRegs.size()) outputRegs[0] = 0;
    for (size_t i = 1; i < order.size(); i++) {
        outputRegs[i] = 0;
        for (size_t j = 0; j < order[i].size(); j++) {
            IRNode::Ptr node = order[i][j];
            for (size_t k = 0; k < node->inputs.size(); k++) {
                IRNode::Ptr input = node->inputs[k];
                if (input->level != node->level) {
                    outputRegs[input->level] |= (1 << input->reg);
                }
            }
        }
    }    


    // Detect what registers are used as the final outputs
    for (size_t i = 0; i < roots.size(); i++) {
        outputRegs[outputRegs.size()-1] |= (1 << roots[i]->reg);
    }



}

// Remove all assigned registers
void AsmX64Compiler::regClear(IRNode::Ptr node) {
    // We don't clobber the registers assigned to external loop vars
    if (node->op == Variable) return;

    node->reg = -1;

    node->tag = 1;
    for (size_t i = 0; i < node->inputs.size(); i++) {
        regClear(node->inputs[i]);
    }        

    // Some ops don't need one
    if (node->op == Store || node->op == StoreVector || node->op == NoOp)
        node->reg = 33;
}
     
// Recursively assign registers to sub-expressions
void AsmX64Compiler::regAssign(IRNode::Ptr node,
                         uint32_t reserved,
                         vector<IRNode::Ptr > &regs, 
                         vector<vector<IRNode::Ptr > > &order) {

    // Check we're at a known loop level
    Assert(node->level || node->constant, "Cannot assign registers to a node that depends on a variable with a loop order not yet assigned.\n");

    // Check order is larger enough
    Assert(node->level < (int)order.size(), "The order vector should have more levels that it does!\n");

    // If I already have a register bail out. This may occur
    // because I was manually assigned a register outside
    // doRegisterAssignment, or if common subexpression
    // elimination has resulted in two parts of the expression
    // pointing to the same IRNode.
    if (node->reg >= 0) {
        return;
    }    

    // Check all the inputs already have registers
    for (size_t i = 0; i < node->inputs.size(); i++) {
        Assert(node->inputs[i]->reg >= 0, "Cannot assign register to a node whose inputs don't have registers\n");
        //regAssign(node->inputs[i], reserved, regs, order);
    }

    // Figure out if we're going into a GPR or an SSE
    // register. All vectors go in SSE. Scalar floats also go in
    // SSE. Masks resulting from comparisons also go in SSE
    bool gpr = (node->width == 1) && (node->type == IRNode::Int);

    // If there are inputs, see if we can use the register of the
    // one of the inputs as output - the first is optimal, as this
    // makes x64 codegen easier. To reuse the register of the
    // input it has to be at the same level as us (otherwise it
    // will have been computed once and stored outside the for
    // loop this node lives in), and have no other outputs that
    // haven't already been evaluated.
    if (node->inputs.size()) {

        IRNode::Ptr input1 = node->inputs[0];
        bool okToClobber = true;

        // Check it's not reserved.
        if (reserved & (1 << input1->reg)) okToClobber = false;

        // Check it's the same type of register.
        if ((gpr && (input1->reg >= 16)) ||
            (!gpr && (input1->reg < 16))) okToClobber = false;

        // Must be at the same loop level.
        if (node->level != input1->level) okToClobber = false;

        // Every parent must be this node, or at the same level and
        // already evaluated, or not a descendent of our root node we
        // started register assignment from (tag == 0). Note that a parent can't
        // possible be at a higher loop level.
        for (size_t i = 0; i < input1->outputs.size() && okToClobber; i++) {
            IRNode::Ptr out = input1->outputs[i].lock();
            if (!out) continue;
            if (out == node) continue;
            if (out->tag == 0) continue;
            if (out->level == node->level && out->reg >= 0) continue;
            okToClobber = false;
        }

        if (okToClobber) {
            node->reg = input1->reg;
            regs[input1->reg] = node;
            return;
        }
    }
        
    // Some binary ops are easy to flip, so we should try to
    // clobber the second input next for those.

    if (node->op == And ||
        node->op == Or ||
        node->op == Plus ||
        node->op == Times ||
        node->op == LT ||
        node->op == GT ||
        node->op == LTE ||
        node->op == GTE ||
        node->op == EQ ||
        node->op == NEQ) {

        IRNode::Ptr input2 = node->inputs[1];
        bool okToClobber = true;

        // Check it's not reserved.
        if (reserved & (1 << input2->reg)) okToClobber = false;

        // Check it's the same type of register.
        if ((gpr && (input2->reg >= 16)) ||
            (!gpr && (input2->reg < 16))) okToClobber = false;

        // Must be the same level.
        if (node->level != input2->level) okToClobber = false;

        // Every parent must be this, or at the same level and already evaluated.
        for (size_t i = 0; i < input2->outputs.size() && okToClobber; i++) {
            IRNode::Ptr out = input2->outputs[i].lock();
            if (!out) continue;
            if (out == node) continue;
            if (out->tag == 0) continue;
            if (out->level == node->level && out->reg >= 0) continue;
            okToClobber = false;
        }
        if (okToClobber) {
            node->reg = input2->reg;
            regs[input2->reg] = node;
            return;
        }
    }


    // Next, try to find a previously used register that is safe
    // to clobber - meaning it's at the same or higher level and all
    // its outputs will have already been evaluated and are at the
    // same or higher level.
    for (size_t i = 0; i < regs.size(); i++) {            
        // Don't consider unused registers yet
        if (!regs[i]) continue;

        // Check it's not reserved
        if (reserved & (1 << i)) continue;

        // Check it's the right type of register
        if (gpr && (i >= 16)) break;
        if (!gpr && (i < 16)) continue;

        // Don't clobber registers from a higher level
        if (regs[i]->level < node->level) continue;

        // Only clobber registers whose outputs will have been
        // fully evaluated - if they've already been assigned
        // registers that means they're ahead of us in the
        // evaluation order and will have already been
        // evaluated.
        bool okToClobber = true;            
        for (size_t j = 0; j < regs[i]->outputs.size(); j++) {
            IRNode::Ptr out = regs[i]->outputs[j].lock();
            if (!out) continue;
            if (out == node) continue; 
            if (out->tag == 0) continue;
            if (out->level == node->level && out->reg >= 0) continue;
            okToClobber = false;
        }

        if (okToClobber) {
            node->reg = (unsigned char)i;
            regs[i] = node;
            return;
        }
    }


    // Find a completely unused register and use that. 
    for (size_t i = 0; i < regs.size(); i++) {
        // Don't consider used registers
        if (regs[i]) continue;
            
        // Don't consider the wrong type of register
        if (gpr && (i >= 16)) break;
        if (!gpr && (i < 16)) continue;

        // Don't consider reserved registers
        if (reserved & (1 << i)) continue;

        node->reg = (unsigned char)i;
        regs[i] = node;
        return;
    }

    // Finally, clobber a non-primary input. This sometimes
    // requires two inserted movs, so it's the least favored
    // option.
    for (size_t i = 1; i < node->inputs.size(); i++) {
        IRNode::Ptr input = node->inputs[i];

        bool okToClobber = true;

        // Check it's not reserved
        if (reserved & (1 << input->reg)) okToClobber = false;

        // Check it's the same type of register
        if ((gpr && (input->reg >= 16)) ||
            (!gpr && (input->reg < 16))) okToClobber = false;

        // Must be the same level
        if (node->level != input->level) okToClobber = false;

        // Every parent must be this node, or at the same level and already evaluated
        for (size_t i = 0; i < input->outputs.size() && okToClobber; i++) {
            IRNode::Ptr out = input->outputs[i].lock();
            if (!out) continue;
            if (out == node) continue;
            if (out->tag == 0) continue;
            if (out->level == node->level && out->reg >= 0) continue;
            okToClobber = false;
        }
        if (okToClobber) {
            node->reg = input->reg;
            regs[input->reg] = node;
            return;
        }
    }
        
    // Freak out - we're out of registers and we don't know
    // how to spill to the stack yet. 
    printf("Register assignments:\n");
    for (int i = 0; i < (int)regs.size(); i++) {
        if (regs[i]) {
            printf("%d: %s ", i, opname(regs[i]->op));
            regs[i]->printExp();
            printf("\n");
        } else if (reserved & (1<<i)) 
            printf("%d: (reserved)\n", i);
        else
            printf("%d: (empty)\n", i);
    }
    printf("Out of registers compiling:\n");
    node->printExp();
    printf("\n");
    printf("Cannot clobber inputs because...\n");
    for (size_t i = 0; i < node->inputs.size(); i++) {
        printf("Child %d has %d outputs\n", (int)i, (int)node->inputs[i]->outputs.size());
    }
    panic("Out of registers!\n");
    
}
