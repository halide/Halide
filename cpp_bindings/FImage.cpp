#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif

#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif

#include <fcntl.h>

#include "FImage.h"
#include <llvm-c/ExecutionEngine.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/PassManager.h>
#include <llvm/Analysis/Passes.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Target/TargetData.h>
#include <llvm/Assembly/PrintModulePass.h>
#include <llvm/Support/raw_ostream.h>

#include "elf.h"

// declare the functions that live on the ml side

ML_FUNC1(makeIntImm);
ML_FUNC1(makeFloatImm);
ML_FUNC2(makeAdd);
ML_FUNC2(makeSub);
ML_FUNC2(makeMul);
ML_FUNC2(makeDiv);
ML_FUNC2(makeEQ);
ML_FUNC2(makeNE);
ML_FUNC2(makeLT);
ML_FUNC2(makeGT);
ML_FUNC2(makeGE);
ML_FUNC2(makeLE);
ML_FUNC3(makeSelect);
ML_FUNC1(doPrint);
ML_FUNC1(makeVar);
ML_FUNC2(makeLoad); // buffer id, idx
ML_FUNC3(makeStore); // value, buffer id, idx
ML_FUNC1(makeBufferArg); // name
ML_FUNC2(doCompile); // stmt

ML_FUNC0(makeList); 
ML_FUNC2(addToList); // cons
ML_FUNC2(makePair);
ML_FUNC3(makeTriple);

ML_FUNC4(makeFor); // var name, min, n, stmt
ML_FUNC2(doVectorize);
ML_FUNC2(doUnroll);
ML_FUNC5(doSplit);
ML_FUNC1(doConstantFold);

// Function call stuff
ML_FUNC4(makePipeline);
ML_FUNC2(makeCall);
ML_FUNC3(makeDefinition);
ML_FUNC0(makeEnv);
ML_FUNC2(addDefinitionToEnv);

ML_FUNC3(doLower);

namespace FImage {

    template<typename T>
    void unify(std::vector<T *> &a, const std::vector<T *> &b) {
        for (size_t i = 0; i < b.size(); i++) {
            bool is_in_a = false;
            for (size_t j = 0; j < a.size(); j++) {
                if (a[j] == b[i]) is_in_a = true;
            }
            if (!is_in_a) a.push_back(b[i]);
        }
    }

    template<> int Named<'v'>::_instances = 0;
    template<> int Named<'f'>::_instances = 0;
    template<> int Named<'i'>::_instances = 0;

    // An Expr is a wrapper around the node structure used by the compiler
    Expr::Expr() {}

    Expr::Expr(MLVal n) : node(n) {}

    Expr::Expr(int32_t val) {
        node = makeIntImm(MLVal::fromInt(val));
    }

    Expr::Expr(unsigned val) {
        node = makeIntImm(MLVal::fromInt(val));
    }

    Expr::Expr(float val) {
        node = makeFloatImm(MLVal::fromFloat(val));
    }

    // declare that this node has a child for bookkeeping
    void Expr::child(const Expr &c) {
        unify(bufs, c.bufs);
        unify(vars, c.vars);
    }

    void Expr::operator+=(const Expr & other) {
        node = makeAdd(node, other.node);
        child(other);
    }
    
    void Expr::operator*=(const Expr & other) {
        node = makeMul(node, other.node);
        child(other);
    }

    void Expr::operator/=(const Expr & other) {
        node = makeDiv(node, other.node);
        child(other);
    }

    void Expr::operator-=(const Expr & other) {
        node = makeSub(node, other.node);
        child(other);
    }


    Expr operator+(const Expr & a, const Expr & b) {
        Expr e(makeAdd(a.node, b.node));
        e.child(a); 
        e.child(b); 
        return e;
    }

    Expr operator-(const Expr & a, const Expr & b) {
        Expr e(makeSub(a.node, b.node));
        e.child(a);
        e.child(b);
        return e;
    }

    Expr operator*(const Expr & a, const Expr & b) {
        Expr e(makeMul(a.node, b.node));
        e.child(a);
        e.child(b);
        return e;
    }

    Expr operator/(const Expr & a, const Expr & b) {
        Expr e(makeDiv(a.node, b.node));
        e.child(a);
        e.child(b);
        return e;
    }

    Expr operator>(const Expr & a, const Expr & b) {
        Expr e(makeGT(a.node, b.node));
        e.child(a);
        e.child(b);
        return e;
    }
    
    Expr operator<(const Expr & a, const Expr & b) {
        Expr e(makeLT(a.node, b.node));
        e.child(a);
        e.child(b);
        return e;
    }
    
    Expr operator>=(const Expr & a, const Expr & b) {
        Expr e(makeGE(a.node, b.node));
        e.child(a);
        e.child(b);
        return e;
    }
    
    Expr operator<=(const Expr & a, const Expr & b) {
        Expr e(makeLE(a.node, b.node));
        e.child(a);
        e.child(b);
        return e;
    }
    
    Expr operator!=(const Expr & a, const Expr & b) {
        Expr e(makeNE(a.node, b.node));
        e.child(a);
        e.child(b);
        return e;
    }

    Expr operator==(const Expr & a, const Expr & b) {
        Expr e(makeEQ(a.node, b.node));
        e.child(a);
        e.child(b);
        return e;
    }
    
    Expr select(const Expr & cond, const Expr & thenCase, const Expr & elseCase) {
        Expr e(makeSelect(cond.node, thenCase.node, elseCase.node));
        e.child(cond);
        e.child(thenCase);
        e.child(elseCase);
        return e;
    }
    
    // Print out an expression
    void Expr::debug() {
        doPrint(node);
    }

    Var::Var() {
        node = makeVar(MLVal::fromString(name()));
        vars.push_back(this);
    }


    void FuncRef::operator=(const Expr &e) {
        f->define(func_args, e);
    }

    FuncRef::operator Expr() {
        // make a call node
        MLVal exprlist = makeList();
        for (size_t i = func_args.size(); i > 0; i--) {
            exprlist = addToList(exprlist, func_args[i-1].node);            
        }
        Expr call = makeCall(MLVal::fromString(f->name()), exprlist);

        for (size_t i = 0; i < func_args.size(); i++) {
            call.child(func_args[i]);
        }

        // Reach through the call to extract buffer dependencies (but not free vars)
        unify(call.bufs, f->rhs.bufs);

        return call;
    }

    void Func::define(const std::vector<Expr> &func_args, const Expr &r) {
        if (!environment) {
            environment = new MLVal(makeEnv());
        }

        // Start off my rhs as the expression given.
        rhs = r;

        // TODO: Mutate the rhs: Convert scatters to scalar-valued functions by wrapping them in a let
        
        MLVal arglist = makeList();
        for (size_t i = func_args.size(); i > 0; i--) {
            if (1 /* dangerously assume it's a var */) {
                if (func_args[i-1].vars.size() != 1) {
                    printf("This was supposed to be a var:\n");
                    doPrint(func_args[i-1].node);
                    exit(1);
                }
                arglist = addToList(arglist, MLVal::fromString(func_args[i-1].vars[0]->name()));
            } else {
                printf("Scalar valued functions should only have vars as arguments\n");
            }
        }

        definition = makeDefinition(MLVal::fromString(name()), arglist, rhs.node);

        *environment = addDefinitionToEnv(*environment, definition);
    }

    Image Func::realize(int a) {        
        Image im(a);
        realize(im);
        return im;
    }

    Image Func::realize(int a, int b) {
        Image im(a, b);
        realize(im);
        return im;
    }

    Image Func::realize(int a, int b, int c) {
        Image im(a, b, c);
        realize(im);
        return im;
    }

    Image Func::realize(int a, int b, int c, int d) {
        Image im(a, b, c, d);
        realize(im);
        return im;
    }

    void Func::realize(Image result) {
        static llvm::ExecutionEngine *ee = NULL;
        static llvm::FunctionPassManager *passMgr = NULL;

        if (!ee) {
            llvm::InitializeNativeTarget();
        }

        if (!function_ptr) {

            // Make a region to evaluate this over
            MLVal sizes = makeList();
            for (size_t i = result.size.size(); i > 0; i--) {                
                sizes = addToList(sizes, MLVal::fromInt(result.size[i-1]));
            }

            MLVal stmt = doLower(MLVal::fromString(name()), 
                                 sizes, 
                                 *Func::environment);                                 

            // Create a function around it with the appropriate number of args
            printf("\nMaking function...\n");           
            MLVal args = makeList();
            args = addToList(args, makeBufferArg(MLVal::fromString("result")));
            for (size_t i = rhs.bufs.size(); i > 0; i--) {
                MLVal arg = makeBufferArg(MLVal::fromString(rhs.bufs[i-1]->name()));
                args = addToList(args, arg);
            }

            doPrint(stmt);

            printf("compiling IR -> ll\n");
            MLVal tuple = doCompile(args, stmt);

            printf("Extracting the resulting module and function\n");
            LLVMModuleRef module = (LLVMModuleRef)Field(tuple.getValue(), 0);
            LLVMValueRef func = (LLVMValueRef)Field(tuple.getValue(), 1);
            llvm::Function *f = llvm::unwrap<llvm::Function>(func);
            llvm::Module *m = llvm::unwrap(module);

            if (!ee) {
                std::string errStr;
                ee = llvm::EngineBuilder(m).setErrorStr(&errStr).setOptLevel(llvm::CodeGenOpt::Aggressive).create();
                if (!ee) {
                    printf("Couldn't create execution engine: %s\n", errStr.c_str()); 
                    exit(1);
                }

                // Set up the pass manager
                passMgr = new llvm::FunctionPassManager(m);

            } else { 
                ee->addModule(m);
            }            

            llvm::Function *inner = m->getFunction("_im_main");

            if (!inner) {
                printf("Could not find function _im_main");
                exit(1);
            }

            printf("optimizing ll...\n");

            std::string errstr;
            llvm::raw_fd_ostream stdout("passes.txt", errstr);
  
            passMgr->add(new llvm::TargetData(*ee->getTargetData()));
            //passMgr->add(llvm::createPrintFunctionPass("*** Before optimization ***", &stdout));

            // AliasAnalysis support for GVN
            passMgr->add(llvm::createBasicAliasAnalysisPass());
            //passMgr->add(llvm::createPrintFunctionPass("*** After basic alias analysis ***", &stdout));

            // Reassociate expressions
            passMgr->add(llvm::createReassociatePass());
            //passMgr->add(llvm::createPrintFunctionPass("*** After reassociate ***", &stdout));

            // Simplify CFG (delete unreachable blocks, etc.)
            passMgr->add(llvm::createCFGSimplificationPass());
            //passMgr->add(llvm::createPrintFunctionPass("*** After CFG simplification ***", &stdout));

            // Eliminate common sub-expressions
            passMgr->add(llvm::createGVNPass());
            //passMgr->add(llvm::createPrintFunctionPass("*** After GVN pass ***", &stdout));

            // Peephole, bit-twiddling optimizations
            passMgr->add(llvm::createInstructionCombiningPass());
            //passMgr->add(llvm::createPrintFunctionPass("*** After instruction combining ***", &stdout));
            
            passMgr->doInitialization();

            if (passMgr->run(*inner)) {
                printf("optimization did something.\n");
            } else {
                printf("optimization did nothing.\n");
            }

            passMgr->doFinalization();

            printf("compiling ll -> machine code...\n");
            void *ptr = ee->getPointerToFunction(f);
            function_ptr = (void (*)(void*))ptr;

            printf("dumping machine code to file...\n");
            saveELF("generated.o", ptr, 8192);            
            printf("Done dumping machine code to file\n");
        }

        printf("Constructing argument list...\n");
        static void *arguments[256];
        for (size_t i = 0; i < rhs.bufs.size(); i++) {
            arguments[i] = (void *)rhs.bufs[i]->data;
        }
        arguments[rhs.bufs.size()] = result.data;

        printf("Calling function at %p\n", function_ptr); 
        function_ptr(&arguments[0]); 
    }

    MLVal *Func::environment = NULL;

    Image::Image(uint32_t a) {
        size.resize(1);
        stride.resize(1);
        size[0] = a;
        stride[0] = 1;
        // TODO: enforce alignment, lazy allocation, etc, etc
        buffer.reset(new std::vector<float>(a + 8));
        data = &(*buffer)[0] + 4;
    }

    Image::Image(uint32_t a, uint32_t b) {
        size.resize(2);
        stride.resize(2);
        size[0] = a;
        size[1] = b;
        stride[0] = 1;
        stride[1] = a;
        buffer.reset(new std::vector<float>(a*b + 8));
        data = &(*buffer)[0] + 4;
    }
    
    Image::Image(uint32_t a, uint32_t b, uint32_t c) {
        size.resize(3);
        stride.resize(3);
        size[0] = a;
        size[1] = b;
        size[2] = c;
        stride[0] = 1;
        stride[1] = a;
        stride[2] = a*b;
        buffer.reset(new std::vector<float>(a*b*c + 8));
        data = &(*buffer)[0] + 4;
    }

    Image::Image(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
        size.resize(4);
        stride.resize(4);
        size[0] = a;
        size[1] = b;
        size[2] = c;
        size[3] = d;
        stride[0] = 1;
        stride[1] = a;
        stride[2] = a*b;
        stride[3] = a*b*c;
        buffer.reset(new std::vector<float>(a*b*c*d + 8));
        data = &(*buffer)[0] + 4;
    }
    
    Image::~Image() {
        //delete[] (data-4);
    }

    Expr Image::operator()(const Expr & a) {        
        Expr addr = a * stride[0];
        Expr load(makeLoad(MLVal::fromString(name()), addr.node));
        load.child(addr);
        load.bufs.push_back(this);
        return load;
    }

    Expr Image::operator()(const Expr & a, const Expr & b) {
        Expr addr = (a * stride[0]) + (b * stride[1]);
        Expr load(makeLoad(MLVal::fromString(name()), addr.node));
        load.child(addr);
        load.bufs.push_back(this);
        return load;
    }

    Expr Image::operator()(const Expr & a, const Expr & b, const Expr & c) {
        Expr addr = a * stride[0] + b * stride[1] + c * stride[2];
        Expr load(makeLoad(MLVal::fromString(name()), addr.node));
        load.child(addr);
        load.bufs.push_back(this);
        return load;
    }

    Expr Image::operator()(const Expr & a, const Expr & b, const Expr & c, const Expr & d) {
        Expr addr = a * stride[0] + b * stride[1] + c * stride[2] + d * stride[3];
        Expr load(makeLoad(MLVal::fromString(name()), addr.node));
        load.child(addr);
        load.bufs.push_back(this);
        return load;
    }

}
