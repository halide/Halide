#include "Compiler.h"

#include <algorithm>


void Compiler::collectInputs(IRNode::Ptr node, OpCode op, IRNode::PtrSet &nodes) {
    if (node->op == op) nodes.insert(node);
    for (int i = 0; i < node->inputs.size(); i++) {
        collectInputs(node->inputs[i], op, nodes);
    }
}

// Compile the evaluation of a single FImage
void Compiler::compile(AsmX64 *a, FImage *im) {
    printf("Image has %d definitions.\n", im->definitions.size()); fflush(stdout);
    // Only consider the first definition for now
    IRNode::Ptr def = im->definitions[0].node;

    // It should be a store or storeVector node.
    assert(def->op == Store || def->op == StoreVector, "Definitions of images should be Store nodes\n");

    IRNode::Ptr lhs = def->inputs[0];
    IRNode::Ptr rhs = def->inputs[1];

    int t1 = timeGetTime();

    // Find the variables we need to iterate over by digging into the lhs and rhs
    printf("Collecting free variables\n");
    IRNode::PtrSet varSet;
    collectInputs(def, Var, varSet);

    assert(varSet.size() < 256, "FImage can't cope with more than 255 variables\n");

    // put them in a vector
    vector<IRNode::Ptr> vars(varSet.size());
    int i = 0;
    for (IRNode::PtrSet::iterator iter = varSet.begin();
         iter != varSet.end(); iter++) {
        vars[i++] = *iter;
    }

    // Differentiate the store address w.r.t each var. If one of them
    // has a derivative of 4, we should vectorize across it and put it
    // in the inner loop
    vector<int64_t> storeDelta(vars.size());
    for (size_t i = 0; i < vars.size(); i++) {
        IRNode::Ptr next = IRNode::make(PlusImm, vars[i], 1);
        IRNode::Ptr nextLhs = lhs->substitute(vars[i], next);
        if (nextLhs == lhs) {
            storeDelta[i] = 0;
        } else {
            IRNode::Ptr delta = IRNode::make(Minus, nextLhs, lhs)->optimize();
            printf("delta = "); delta->printExp(); printf("\n");
            if (delta->op == Const && delta->type == IRNode::Int) {
                storeDelta[i] = delta->ival;
            } else {
                // Unknown store delta
                storeDelta[i] = 0x7fffffffffffffff;
            }
        }
    }

    // Stable sort loop levels by the store delta and assign loop levels
    for (size_t i = 0; i < vars.size(); i++) {
        for (size_t j = i+1; j < vars.size(); j++) {
            if (abs(storeDelta[i]) < abs(storeDelta[j])) {
                IRNode::Ptr v = vars[i];
                vars[i] = vars[j];
                vars[j] = v;
                int64_t d = storeDelta[i];
                storeDelta[i] = storeDelta[j];
                storeDelta[j] = d;
            }
        }
        vars[i]->assignLevel((unsigned char)(i+1));
    }


    // Check all the vars have sane bounds.
    for (size_t i = 0; i < vars.size(); i++) {
        printf("Var %d : [%d %d]\n", i, vars[i]->min, vars[i]->max);
        assert(vars[i]->max >= vars[i]->min, "Variable %d has undefined bounds\n");
    }

    // Should we vectorize across the loop vars? Right now the answer
    // is always yes, unless we're accumulating onto a scalar. In the
    // future we'll have to figure out how to horizontally reduce at
    // the end in this case.
    bool vectorize = storeDelta[0] != 0;

    // Vectorize across the smallest non-zero store delta
    int vectorDim = 0;
    for (int i = 0; i < (int)vars.size(); i++) {
        if (storeDelta[i] == 0) break;
        vectorDim = i;
    }

    vector<int> vectorWidth(vars.size(), 1);
    if (vectorize) vectorWidth[vectorDim] = 4;

    // Let the compiler know that the innermost var will be a multiple
    // of four because we're vectorizing across it
    vars[vectorDim]->modulus = 4;
    vars[vectorDim]->remainder = 0;


    printf("Compiling: ");
    def->printExp();
    printf("\n");

    // Do a final optimization pass now that levels are assigned.
    IRNode::saveDot("before.dot");
    def = def->optimize();

    if (vectorize) {
        // vectorize across some variable
        // TODO: we're assuming its bounds are suitable for this - a multiple of four
        def = IRNode::make(Vector, def, 
                           def->substitute(vars[vectorDim], 
                                           IRNode::make(PlusImm, vars[vectorDim], 1)),
                           def->substitute(vars[vectorDim], 
                                           IRNode::make(PlusImm, vars[vectorDim], 2)),
                           def->substitute(vars[vectorDim], 
                                           IRNode::make(PlusImm, vars[vectorDim], 3)));
    }

    // Unroll across some vars
    vector<int> unroll(vars.size(), 1);

    // This should be smarter, right now we just unroll by 2 in the
    // two innermost vars. Assumes they have even bounds.
    //unroll[unroll.size()-1] = 2;
    //unroll[unroll.size()-2] = 2;

    vector<IRNode::Ptr> roots(unroll[0]*unroll[1]*unroll[2]);
    roots[0] = def;
    for (int i = 0; i < unroll[0]; i++) {
        if (i > 0) {
            roots[i*unroll[1]*unroll[2]] = 
                def->substitute(vars[0], IRNode::make(PlusImm, vars[0], i*vectorWidth[0]));
        }
        for (int j = 0; j < unroll[1]; j++) {
            if (j > 0) {
                roots[(i*unroll[1] + j)*unroll[2]] = 
                    roots[i*unroll[1]*unroll[2]]->substitute(vars[1], IRNode::make(PlusImm, vars[1], j*vectorWidth[1]));      
            }
            for (int k = 0; k < unroll[2]; k++) {
                if (k > 0) {
                    roots[(i*unroll[1] + j)*unroll[2] + k] = 
                        roots[(i*unroll[1] + j)*unroll[2]]->substitute(vars[2], IRNode::make(PlusImm, vars[2], k*vectorWidth[2]));   
                }
            }
        }
    }

    IRNode::saveDot("after.dot");

    printf("Done optimizing\n");

    // Look for loads that are possibly aliased with the store and
    // increase their loop level to the same as the store
    IRNode::PtrSet loadSet, storeSet;
    for (size_t i = 0; i < roots.size(); i++) {
        collectInputs(roots[i], Load, loadSet);
        collectInputs(roots[i], LoadVector, loadSet);
        collectInputs(roots[i], Store, storeSet);
        collectInputs(roots[i], StoreVector, storeSet);
    }

    
    for (IRNode::PtrSet::iterator storeIter = storeSet.begin();
         storeIter != storeSet.end(); storeIter++) {
        IRNode::Ptr store = *storeIter;
        int64_t storeMin = lhs->min + store->ival;
        int64_t storeMax = lhs->max + store->ival;
        printf("Store address bounds: %d %d\n", storeMin, storeMax);
        for (IRNode::PtrSet::iterator loadIter = loadSet.begin();
             loadIter != loadSet.end(); loadIter++) {
            IRNode::Ptr load = *loadIter;
            IRNode::Ptr loadAddr = load->inputs[0];
            int64_t loadMin = loadAddr->min + load->ival;
            int64_t loadMax = loadAddr->max + load->ival;
            printf("Load address bounds: %d : %d\n", loadMin, loadMax);
            
            if (loadMin >= storeMin && loadMin <= storeMax ||
                loadMax >= storeMin && loadMax <= storeMin) {
                printf("Possible aliasing detected\n");
                printf("Promoting load at loop level %d to loop level %d\n", load->level, store->level);
                load->assignLevel(def->level);
            }
        }
    }   

    // Assign the variables some registers
    vector<AsmX64::Reg> varRegs(vars.size());
    if (varRegs.size() > 0) varRegs[0] = a->rax;
    if (varRegs.size() > 1) varRegs[1] = a->rcx;
    if (varRegs.size() > 2) varRegs[2] = a->rdx;
    if (varRegs.size() > 3) varRegs[3] = a->rbx;
    if (varRegs.size() > 4) varRegs[4] = a->rbp;
    if (varRegs.size() > 5) varRegs[5] = a->rsi;
    if (varRegs.size() > 6) varRegs[6] = a->rdi;
    if (varRegs.size() > 7) panic("Can't handle more than 7 loop indices for now\n");
    
    AsmX64::Reg tmp = a->r15;

    // Mark these registers as unclobberable for the register allocation
    uint32_t reserved = ((1 << tmp.num) | 
                         (1 << a->rsp.num));

    // Force the indices into the intended registers and mark them as reserved
    for (size_t i = 0; i < varRegs.size(); i++) {
        reserved |= (1 << varRegs[i].num);
        vars[i]->reg = varRegs[i].num;
    }

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
    printf("Register assignment...\n");
    vector<uint32_t> clobbered, outputs;
    vector<vector<IRNode::Ptr > > order;        
    doRegisterAssignment(roots, reserved, order, clobbered, outputs);   
    printf("Done\n");
    int t2 = timeGetTime();

    // which register is the output pointer in?
    AsmX64::Reg outPtr(roots[roots.size()-1]->reg);

    printf("Compilation took %d ms\n", t2-t1);

    // print out the proposed ordering and register assignment for inspection
    
    for (size_t l = 0; l < order.size(); l++) {
        if (l) {
            for (size_t k = 1; k < l; k++) putchar(' ');
            printf("for:\n");
        }
        for (size_t i = 0; i < order[l].size(); i++) {
            IRNode::Ptr next = order[l][i];
            for (size_t k = 0; k < l; k++) putchar(' ');
            next->print();
        }
    }
    

    // Align the stack to a 16-byte boundary - it always comes in
    // offset by 8 bytes because it contains the 64-bit return
    // address.
    a->sub(a->rsp, 8);
        
    // Save all the registers that the 64-bit C abi tells us we're
    // supposed to. This maintains stack alignment.
    a->pushNonVolatiles();
        
    compileBody(a, order[0]);

    char *labels[] = {"l0", "l1", "l2", "l3", "l4", "l5", "l6", "l7", "l8"};

    for (size_t i = 0; i < vars.size(); i++) {
        printf("Starting loop %d\n", i);
        a->mov(varRegs[i], vars[i]->min);
        a->label(labels[i]); 
    
        compileBody(a, order[i+1]);
    }

    for (int i = (int)vars.size()-1; i >= 0; i--) {
        a->add(varRegs[i], vectorWidth[i]*unroll[i]);
        a->cmp(varRegs[i], vars[i]->max+1);
        a->jl(labels[i]);
    }

    // Pop the stack and return
    a->popNonVolatiles();
    a->add(a->rsp, 8);
    a->ret();        

    printf("Saving object file\n");

    // Save an object file that you can use dumpbin on to inspect
    a->saveCOFF("generated.obj");        
}
    
// Generate machine code for a vector of IRNodes. Registers must have already been assigned.
void Compiler::compileBody(AsmX64 *a, vector<IRNode::Ptr> code) {
        
    AsmX64::SSEReg tmp = a->xmm15;
    AsmX64::Reg gtmp = a->r15;

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
        bool gpr1 = c1 && c1->reg < 16;
        bool gpr2 = c2 && c2->reg < 16;
        bool gpr3 = c3 && c3->reg < 16;
        bool gpr4 = c4 && c4->reg < 16;

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
                    a->bxorps(dst, dst);
                } else {
                    a->mov(gtmp, a->addData(node->fval));
                    a->movss(dst, AsmX64::Mem(gtmp));
                    //a->shufps(dst, dst, 0, 0, 0, 0);
                }
            } else if (node->type == IRNode::Bool) {
                if (gpr) {
                    if (node->ival) 
                        a->mov(gdst, -1);
                    else
                        a->mov(gdst, 0);
                } else {
                    if (node->ival) {
                        a->cmpeqps(dst, dst);
                    } else {
                        a->bxorps(dst, dst);                    
                    }
                }
            } else {
                if (gpr) {
                    a->mov(gdst, node->ival);
                } else {
                    a->mov(a->r15, node->ival);
                    // ints are 32-bit for now, so this works
                    a->cvtsi2ss(dst, a->r15);
                    //a->shufps(dst, dst, 0, 0, 0, 0);                        
                }
            }
            break;
        case Var:
            // These are placed in GPRs externally
            assert(gpr, "Vars must be manually placed in gprs\n");
            break;
        case Plus:
            if (gpr && gpr1 && gpr2) {
                if (gdst == gsrc1)
                    a->add(gdst, gsrc2);
                else if (gdst == gsrc2) 
                    a->add(gdst, gsrc1);
                else {
                    a->mov(gdst, gsrc1);
                    a->add(gdst, gsrc2);
                }
            } else if (!gpr && !gpr1 && !gpr2) {
                if (dst == src1)
                    a->addps(dst, src2);
                else if (dst == src2) 
                    a->addps(dst, src1);
                else {
                    a->movaps(dst, src1);
                    a->addps(dst, src2);
                }
            } else {
                panic("Can't add between gpr/sse\n");
            }
            break;
        case Minus:
            if (gpr && gpr1 && gpr2) {
                if (gdst == gsrc1) {
                    a->sub(gdst, gsrc2);
                } else if (gdst == gsrc2) {
                    a->mov(gtmp, gsrc2);
                    a->mov(gsrc2, gsrc1);
                    a->sub(gsrc2, gtmp);
                } else {                         
                    a->mov(gdst, gsrc1);
                    a->sub(gdst, gsrc2);
                }
            } else if (!gpr && !gpr1 && !gpr2) {
                if (dst == src1) {
                    a->subps(dst, src2);
                } else if (dst == src2) {
                    a->movaps(tmp, src2);
                    a->movaps(src2, src1);
                    a->subps(src2, tmp);
                } else { 
                    a->movaps(dst, src1);
                    a->subps(dst, src2);
                }
            } else {
                panic("Can't sub between gpr/sse\n");
            }
            break;
        case Times: 
            if (gpr && gpr1 && gpr2) {
                if (gdst == gsrc1)
                    a->imul(gdst, gsrc2);
                else if (gdst == gsrc2) 
                    a->imul(gdst, gsrc1);
                else {
                    a->mov(gdst, gsrc1);
                    a->imul(gdst, gsrc2);
                }
            } else if (!gpr && !gpr1 && !gpr2) {
                if (dst == src1)
                    a->mulps(dst, src2);
                else if (dst == src2) 
                    a->mulps(dst, src1);
                else {
                    a->movaps(dst, src1);
                    a->mulps(dst, src2);
                }
            } else {
                panic("Can't sub between gpr/sse\n");
            }
            break;
        case TimesImm:
            assert(abs(node->ival) >> 32 == 0, 
                   "TimesImm may only use a 32-bit signed constant\n");
            if (gdst == gsrc1) {
                a->imul(gdst, (int32_t)node->ival);
            } else {
                a->mov(gdst, (int32_t)node->ival);
                a->imul(gdst, gsrc1);
            }
            break;
        case PlusImm:
            assert(abs(node->ival) >> 32 == 0,
                   "PlusImm may only use a 32-bit signed constant\n");
            if (gdst == gsrc1) {
                a->add(gdst, (int32_t)node->ival);
            } else {
                a->mov(gdst, (int32_t)node->ival);
                a->add(gdst, gsrc1);
            }
            break;
        case Divide: 
            assert(!gpr && !gpr1 && !gpr2, "Can only divide in sse regs for now\n");
            if (dst == src1) {
                a->divps(dst, src2);
            } else if (dst == src2) {
                a->movaps(tmp, src2);
                a->movaps(src2, src1);
                a->divps(src2, tmp); 
            } else {
                a->movaps(dst, src1);
                a->divps(dst, src2);
            }
            break;
        case And:
            assert(!gpr && !gpr1 && !gpr2, "Can only and in sse regs for now\n");
            if (dst == src1) 
                a->bandps(dst, src2);
            else if (dst == src2)
                a->bandps(dst, src1);
            else {
                a->movaps(dst, src1);
                a->bandps(dst, src2);
            }
            break;
        case Nand:
            assert(!gpr && !gpr1 && !gpr2, "Can only nand in sse regs for now\n");
            if (dst == src1) {
                a->bandnps(dst, src2);
            } else if (dst == src2) {
                a->movaps(tmp, src2);
                a->movaps(src2, src1);
                a->bandnps(src2, tmp); 
            } else {
                a->movaps(dst, src1);
                a->bandnps(dst, src2);
            }
            break;
        case Or:               
            assert(!gpr && !gpr1 && !gpr2, "Can only or in sse regs for now\n");
            if (dst == src1) 
                a->borps(dst, src2);
            else if (dst == src2)
                a->borps(dst, src1);
            else {
                a->movaps(dst, src1);
                a->borps(dst, src2);
            }
            break;
        case NEQ:                               
            assert(!gpr && !gpr1 && !gpr2, "Can only neq in sse regs for now\n");
            if (dst == src1) 
                a->cmpneqps(dst, src2);
            else if (dst == src2)
                a->cmpneqps(dst, src1);
            else {
                a->movaps(dst, src1);
                a->cmpneqps(dst, src2);
            }
            break;
        case EQ:
            assert(!gpr && !gpr1 && !gpr2, "Can only eq in sse regs for now\n");
            if (dst == src1) 
                a->cmpeqps(dst, src2);
            else if (dst == src2)
                a->cmpeqps(dst, src1);
            else {
                a->movaps(dst, src1);
                a->cmpeqps(dst, src2);
            }
            break;
        case LT:
            assert(!gpr && !gpr1 && !gpr2, "Can only lt in sse regs for now\n");
            if (dst == src1) 
                a->cmpltps(dst, src2);
            else if (dst == src2)
                a->cmpnleps(dst, src1);
            else {
                a->movaps(dst, src1);
                a->cmpltps(dst, src2);
            }
            break;
        case GT:
            assert(!gpr && !gpr1 && !gpr2, "Can only gt in sse regs for now\n");
            if (dst == src1) 
                a->cmpnleps(dst, src2);
            else if (dst == src2)
                a->cmpltps(dst, src1);
            else {
                a->movaps(dst, src1);
                a->cmpnleps(dst, src2);
            }
            break;
        case LTE:
            assert(!gpr && !gpr1 && !gpr2, "Can only lte in sse regs for now\n");
            if (dst == src1) 
                a->cmpleps(dst, src2);
            else if (dst == src2)
                a->cmpnltps(dst, src1);
            else {
                a->movaps(dst, src1);
                a->cmpleps(dst, src2);
            }
            break;
        case GTE:
            assert(!gpr && !gpr1 && !gpr2, "Can only gte in sse regs for now\n");
            if (dst == src1) 
                a->cmpnltps(dst, src2);
            else if (dst == src2)
                a->cmpleps(dst, src1);
            else {
                a->movaps(dst, src1);
                a->cmpnltps(dst, src2);
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
            panic("Not implemented: %s\n", opname[node->op]);                
            break;
        case IntToFloat:
            if (gpr1 && !gpr) {
                // TODO: this truncates to 32-bits currently
                a->cvtsi2ss(dst, gsrc1);
            } else {
                panic("IntToFloat can only go from gpr to sse\n");
            }
            break;
        case SelectVector:
            assert(!gpr && !gpr1 && !gpr2, "Can only select vector in sse regs\n");
            if (node->ival == 1) {
                if (dst == src1) {                                        
                    a->movaps(tmp, src1);
                    a->shufps(tmp, src2, 3, 3, 0, 0);
                    a->shufps(dst, tmp, 1, 2, 0, 2);
                } else if (dst == src2) {
                    a->movaps(tmp, src2);
                    a->shufps(tmp, src1, 0, 0, 3, 3);
                    a->movaps(dst, src1);
                    a->shufps(dst, tmp, 1, 2, 2, 0);                    
                } else {
                    a->movaps(tmp, src1);
                    a->shufps(tmp, src2, 3, 3, 0, 0);                    
                    a->movaps(dst, src1);
                    a->shufps(dst, tmp, 1, 2, 0, 2);
                }
            } else if (node->ival == 2) {
                if (dst == src1) {
                    a->shufps(dst, src2, 2, 3, 0, 1);
                } else if (dst == src2) {
                    a->movaps(tmp, src2);
                    a->movaps(dst, src1);
                    a->shufps(dst, tmp, 2, 3, 0, 1);                    
                } else {
                    a->movaps(dst, src1);
                    a->shufps(dst, src2, 2, 3, 0, 1);
                }
            } else if (node->ival == 3) {
                if (dst == src1) {
                    a->shufps(dst, src2, 3, 3, 0, 0);
                    a->shufps(dst, src2, 0, 2, 1, 2);
                } else if (dst == src2) {
                    a->movaps(tmp, src1);
                    a->shufps(tmp, src2, 3, 3, 0, 0);
                    a->shufps(tmp, src2, 0, 2, 1, 2);
                    a->movaps(dst, tmp);
                } else {
                    a->movaps(dst, src1);
                    a->shufps(dst, src2, 3, 3, 0, 0);
                    a->shufps(dst, src2, 0, 2, 1, 2);
                }
            } else {
                panic("Can't deal with SelectVector with argument other than 1, 2, or 3\n");
            }
            break;
        case ExtractScalar:
            assert(!gpr && !gpr1, "Can only extract scalar from sse regs into sse regs\n");
            if (!(dst == src1)) a->movaps(dst, src1);
            assert(node->ival >= 0 && node->ival < 4, 
                   "Integer argument to ExtractScalar must be 0, 1, 2, or 3\n");
            a->shufps(dst, src1, 
                      (uint8_t)node->ival, (uint8_t)node->ival,
                      (uint8_t)node->ival, (uint8_t)node->ival);
            break;
        case StoreVector:
        case Store:
            assert(gpr1, "Can only store using addresses in gprs\n");
            assert(!gpr2, "Can only store values in sse registers\n");
            assert(abs(node->ival) >> 32 == 0,
                   "Store may only use a 32-bit signed constant\n");
            if (node->width == 1) {
                a->movss(AsmX64::Mem(gsrc1, (int32_t)node->ival), src2);
            } else {
                int modulus = node->inputs[0]->modulus;
                int remainder = (node->inputs[0]->remainder + node->ival) % modulus;
                if ((modulus & 0xf) == 0 && (remainder & 0xf) == 0) {
                    a->movaps(AsmX64::Mem(gsrc1, (int32_t)node->ival), src2);
                } else {
                    printf("Unaligned store!\n");
                    a->movups(AsmX64::Mem(gsrc1, (int32_t)node->ival), src2);
                }
            }
            break;
        case LoadVector:
        case Load:
            assert(gpr1, "Can only load using addresses in gprs\n");
            assert(!gpr, "Can only load into sse regs\n");
            assert(abs(node->ival) >> 32 == 0,
                   "Load may only use a 32-bit signed constant\n");
            if (node->width == 1) {
                a->movss(dst, AsmX64::Mem(gsrc1, (int32_t)node->ival));
            } else {
                int modulus = node->inputs[0]->modulus;
                int remainder = (node->inputs[0]->remainder + node->ival) % modulus;
                if ((modulus & 0xf) == 0 && (remainder & 0xf) == 0) {
                    //a->movaps(dst, a->xmm0);
                    a->movaps(dst, AsmX64::Mem(gsrc1, (int32_t)node->ival));
                } else {
                    printf("Unaligned load!\n");
                    a->movups(dst, AsmX64::Mem(gsrc1, (int32_t)node->ival));
                }
            }
            break;
        case Vector:
            assert(!gpr, "Can't put vectors in gprs");

            // Can we use shufps?
            if (src1 == src2 && src3 == src4) {
                if (src1 == dst) {
                    a->shufps(dst, src3, 0, 0, 0, 0);
                } else if (src3 == dst) {
                    a->movaps(tmp, src1);
                    a->shufps(tmp, src3, 0, 0, 0, 0);
                    a->movaps(src3, tmp);
                } else {
                    a->movaps(dst, src1);
                    a->shufps(dst, src3, 0, 0, 0, 0);
                }
            } else if (dst == src1) {
                a->punpckldq(dst, src2);
                a->movaps(tmp, src3);
                a->punpckldq(tmp, src4);
                a->punpcklqdq(dst, tmp);
            } else {
                // Most general case: We're allowed to clobber the
                // high floats in the sources, because they're scalar

                a->movaps(tmp, src1);
                a->punpckldq(tmp, src2);
                a->punpckldq(src3, src4); // clobber the high words in src3
                a->punpcklqdq(tmp, src3);
                a->movaps(dst, tmp);

                // No clobber version:
                /*
                a->movaps(tmp, src1);
                a->punpckldq(tmp, src2);
                a->movaps(tmp2, src3);
                a->punpckldq(tmp2, src4);
                a->punpcklqdq(tmp, tmp2);
                a->movaps(dst, tmp);
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
void Compiler::doRegisterAssignment(
    const vector<IRNode::Ptr > &roots, 
    uint32_t reserved,
    vector<vector<IRNode::Ptr > > &order,
    vector<uint32_t> &clobberedRegs, 
    vector<uint32_t> &outputRegs) {

    // Who's currently occupying which register? First the 16 gprs, then the 16 sse registers.
    vector<IRNode::Ptr > regs(32);

    // Reserve xmm15 for the code generator to use as scratch
    assert(!(reserved & (1<<31)),
           "Register xmm15 is reserved for the code generator\n");
    reserved |= (1<<31);
        
    // Clear the tag on all nodes
    for (size_t i = 0; i < IRNode::allNodes.size(); i++) {
        IRNode::Ptr n = IRNode::allNodes[i].lock();
        if (!n) continue;
        n->tag = 0;
    }

    // Clear any previous register assignment and order, and make the
    // descendents of the roots for evaluation (sets tag to 1)
    for (size_t i = 0; i < roots.size(); i++) {
        regClear(roots[i]);
    }
    order.clear();

    // Then compute the order of evaluation (sets tag to 2)
    printf("Doing instruction scheduling\n");
    doInstructionScheduling(roots, order);
    printf("Done instruction scheduling\n");

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
    clobberedRegs.clear();
    clobberedRegs.resize(order.size(), 0);
    for (int i = 0; i < order.size(); i++) {
        clobberedRegs[i] = (1<<31);
        for (size_t j = 0; j < order[i].size(); j++) {
            IRNode::Ptr node = order[i][j];
            clobberedRegs[i] |= (1 << node->reg);
        }
    }


    // Detect what registers are used for inter-level communication
    outputRegs.clear();
    outputRegs.resize(order.size(), 0);
    
    if (outputRegs.size()) outputRegs[0] = 0;
    for (int i = 1; i < order.size(); i++) {
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

void Compiler::doInstructionScheduling(
    const vector<IRNode::Ptr > &roots, 
    vector<vector<IRNode::Ptr > > &order) {

    // Gather the nodes in a depth-first manner, and resize order to
    // be big enough. Also tag each node with the minimum depth to a root plus 100.
    for (size_t i = 0; i < roots.size(); i++) {
        if (order.size() <= roots[i]->level) order.resize(roots[i]->level+1);
        gatherDescendents(roots[i], order, 100);
    }       

    // Stable sort the nodes from deepest to shallowest without
    // breaking any data dependencies. Also retag everything 2.
    for (size_t l = 0; l < order.size(); l++) {
        for (size_t i = 0; i < order[l].size(); i++) {
            IRNode::Ptr ni = order[l][i];
            for (size_t j = i+1; j < order[l].size(); j++) {
                IRNode::Ptr nj = order[l][j];
                if (ni->tag < nj->tag &&
                    find(nj->inputs.begin(), nj->inputs.end(), ni) == nj->inputs.end()) {
                    order[l][j] = ni;
                    order[l][j-1] = nj;
                } else {
                    break;
                }
            }
            ni->tag = 2;
        }

        for (size_t i = 0; i < order[l].size(); i++) {
            IRNode::Ptr ni = order[l][i];

            // Which node should get evaluated next? We'd like to be
            // able to clobber an input. Rate each node's input
            // according to how many unevaluated outputs it
            // has. Choose the node with the input with the lowest
            // rating. 1 is ideal, because it means we can clobber
            // that input. 2 or 3 is still good because we're getting
            // closer to being able to clobber that input.

            int bestRating = 0;
            IRNode::Ptr np;
            size_t location;
            for (size_t j = i; j < order[l].size(); j++) {
                IRNode::Ptr nj = order[l][j];
                bool ready = true;
                for (size_t k = 0; k < nj->inputs.size(); k++) {
                    IRNode::Ptr nk = nj->inputs[k];
                    // If all inputs aren't evaluated yet, it's game
                    // over for this node
                    if (nk->tag != 3) ready = false;
                }
                if (!ready) continue;

                for (size_t k = 0; k < nj->inputs.size(); k++) {
                    IRNode::Ptr nk = nj->inputs[k];

                    // Can't clobber inputs from a higher level
                    if (nk->level != l) continue;

                    // Can't clobber external vars
                    if (nk->op == Var) continue;

                    // Can't clobber inputs of a different width
                    if (nk->width != nj->width) continue;

                    // Count how many outputs of this input are yet to be evaluated.
                    int remainingOutputs = 0;
                    for (size_t m = 0; m < nk->outputs.size(); m++) {
                        IRNode::Ptr nm = nk->outputs[m].lock();

                        // Ignore those not participating in this computation
                        if (!nm || !nm->tag) continue;
                        
                        // Unevaluated outputs that aren't nj means you can't clobber
                        if (nm->tag != 3) remainingOutputs++;
                    }                    

                    if (remainingOutputs < bestRating || !np) {
                        bestRating = remainingOutputs;
                        np = nj;
                        location = j;
                    }
                }
            }

            // Did we find a node to promote?
            if (np) {
                // Then bubble it up to just before ni
                while(location > i) {
                    order[l][location] = order[l][location-1];
                    location--;
                }
                order[l][i] = np;
                np->tag = 3;
            } else {
                ni->tag = 3;
            }
        }
    }

}


void Compiler::gatherDescendents(IRNode::Ptr node, vector<vector<IRNode::Ptr> > &output, int d) {
    // If I'm already in the output, bail
    if (node->tag > 1) {
        return;
    }    
    node->tag = d;
    for (size_t j = 0; j < node->inputs.size(); j++) {
        gatherDescendents(node->inputs[j], output, d+1);
    }
    output[node->level].push_back(node);
}

// Remove all assigned registers
void Compiler::regClear(IRNode::Ptr node) {
    // We don't clobber the registers assigned to external loop vars
    if (node->op == Var) return;

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
void Compiler::regAssign(IRNode::Ptr node,
                         uint32_t reserved,
                         vector<IRNode::Ptr > &regs, 
                         vector<vector<IRNode::Ptr > > &order) {

    // Check we're at a known loop level
    assert(node->level || node->constant, "Cannot assign registers to a node that depends on a variable with a loop order not yet assigned.\n");

    // Check order is larger enough
    assert(node->level < order.size(), "The order vector should have more levels that it does!\n");

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
        assert(node->inputs[i]->reg >= 0, "Cannot assign register to a node whose inputs don't have registers\n");
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
            printf("%d: ", i, opname[regs[i]->op]);
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
        printf("Child %d has %d outputs\n", i, node->inputs[i]->outputs.size());
    }
    panic("Out of registers!\n");
    
}
