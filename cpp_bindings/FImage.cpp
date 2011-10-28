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
ML_FUNC1(makeUIntImm);
ML_FUNC1(makeFloatType);
ML_FUNC1(makeIntType);
ML_FUNC1(makeUIntType);
ML_FUNC2(makeCast);
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
ML_FUNC3(makeDebug);
ML_FUNC1(printStmt);
ML_FUNC1(printSchedule);
ML_FUNC1(makeVar);
ML_FUNC3(makeLoad); // buffer id, idx
ML_FUNC3(makeStore); // value, buffer id, idx
ML_FUNC1(makeBufferArg); // name
ML_FUNC2(doCompile); // stmt
ML_FUNC1(inferType);

ML_FUNC0(makeList); 
ML_FUNC2(addToList); // cons
ML_FUNC2(makePair);
ML_FUNC3(makeTriple);

ML_FUNC4(makeFor); // var name, min, n, stmt
ML_FUNC2(makeVectorizeTransform);
ML_FUNC2(makeUnrollTransform);
ML_FUNC5(makeSplitTransform);
ML_FUNC3(makeTransposeTransform);
ML_FUNC4(makeChunkTransform);
ML_FUNC1(doConstantFold);

// Function call stuff
ML_FUNC3(makeCall);
ML_FUNC3(makeDefinition);
ML_FUNC0(makeEnv);
ML_FUNC2(addDefinitionToEnv);

ML_FUNC3(makeSchedule);
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

    Expr::Expr(MLVal n, Type t) : node(n), type(t) {}

    Expr::Expr(int32_t val) : node(makeIntImm(MLVal::fromInt(val))), type(Int(32)) {}

    Expr::Expr(uint32_t val) : node(makeIntImm(MLVal::fromInt(val))), type(UInt(32)) {}

    Expr::Expr(float val) : node(makeFloatImm(MLVal::fromFloat(val))), type(Float(32)) {}

    // declare that this node has a child for bookkeeping
    void Expr::child(const Expr &c) {
        unify(bufs, c.bufs);
        unify(vars, c.vars);
        unify(funcs, c.funcs);
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
        Expr e(makeAdd(a.node, b.node), a.type);
        e.child(a); 
        e.child(b); 
        return e;
    }

    Expr operator-(const Expr & a, const Expr & b) {
        Expr e(makeSub(a.node, b.node), a.type);
        e.child(a);
        e.child(b);
        return e;
    }

    Expr operator*(const Expr & a, const Expr & b) {
        Expr e(makeMul(a.node, b.node), a.type);
        e.child(a);
        e.child(b);
        return e;
    }

    Expr operator/(const Expr & a, const Expr & b) {
        Expr e(makeDiv(a.node, b.node), a.type);
        e.child(a);
        e.child(b);
        return e;
    }

    Expr operator>(const Expr & a, const Expr & b) {
        Expr e(makeGT(a.node, b.node), Int(1));
        e.child(a);
        e.child(b);
        return e;
    }
    
    Expr operator<(const Expr & a, const Expr & b) {
        Expr e(makeLT(a.node, b.node), Int(1));
        e.child(a);
        e.child(b);
        return e;
    }
    
    Expr operator>=(const Expr & a, const Expr & b) {
        Expr e(makeGE(a.node, b.node), Int(1));
        e.child(a);
        e.child(b);
        return e;
    }
    
    Expr operator<=(const Expr & a, const Expr & b) {
        Expr e(makeLE(a.node, b.node), Int(1));
        e.child(a);
        e.child(b);
        return e;
    }
    
    Expr operator!=(const Expr & a, const Expr & b) {
        Expr e(makeNE(a.node, b.node), Int(1));
        e.child(a);
        e.child(b);
        return e;
    }

    Expr operator==(const Expr & a, const Expr & b) {
        Expr e(makeEQ(a.node, b.node), Int(1));
        e.child(a);
        e.child(b);
        return e;
    }
    
    Expr select(const Expr & cond, const Expr & thenCase, const Expr & elseCase) {
        Expr e(makeSelect(cond.node, thenCase.node, elseCase.node), thenCase.type);
        e.child(cond);
        e.child(thenCase);
        e.child(elseCase);
        return e;
    }
    
    // Print out an expression
    void Expr::debug() {
        printStmt(node);
    }

    Var::Var() {
        node = makeVar(MLVal::fromString(name()));
        vars.push_back(this);
    }

    Var::Var(const std::string &name) : Named<'v'>(name) {
        node = makeVar(MLVal::fromString(name));
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
        Expr call(makeCall(f->rhs.type.mlval, MLVal::fromString(f->name()), exprlist), f->rhs.type);

        for (size_t i = 0; i < func_args.size(); i++) {
            call.child(func_args[i]);
        }

        // Reach through the call to extract buffer dependencies (but not free vars)
        unify(call.bufs, f->rhs.bufs);

        // Add this function call to the calls list
        call.funcs.push_back(f);
        unify(call.funcs, f->rhs.funcs);

        return call;
    }

    void Func::define(const std::vector<Expr> &func_args, const Expr &r) {
        if (!environment) {
            environment = new MLVal(makeEnv());
        }

        // Start off my rhs as the expression given.
        rhs = r;

        // TODO: Mutate the rhs: Convert scatters to scalar-valued functions by wrapping them in a let
        args = func_args;

        arglist = makeList();
        for (size_t i = func_args.size(); i > 0; i--) {
            if (1 /* dangerously assume it's a var */) {
                if (func_args[i-1].vars.size() != 1) {
                    printf("This was supposed to be a var:\n");
                    printStmt(func_args[i-1].node);
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

    void Func::trace() {
        char fmt[256];
        snprintf(fmt, 256, "Evaluating %s at: ", name().c_str());
        printf("Wrapping rhs in a debug node\n");
        rhs = Debug(rhs, fmt, args);
        definition = makeDefinition(MLVal::fromString(name()), arglist, rhs.node);
        *environment = addDefinitionToEnv(*environment, definition);
    }

    void Func::vectorize(const Var &v) {
        MLVal t = makeVectorizeTransform(MLVal::fromString(name()),
                                         MLVal::fromString(v.name()));
        schedule_transforms.push_back(t);
    }

    void Func::vectorize(const Var &v, int factor) {
        if (factor == 1) return;
        Var vi;
        split(v, v, vi, factor);
        vectorize(vi);        
    }

    void Func::unroll(const Var &v) {
        MLVal t = makeUnrollTransform(MLVal::fromString(name()),
                                      MLVal::fromString(v.name()));        
        schedule_transforms.push_back(t);
    }

    void Func::split(const Var &old, const Var &newout, const Var &newin, int factor) {
        MLVal t = makeSplitTransform(MLVal::fromString(name()),
                                     MLVal::fromString(old.name()),
                                     MLVal::fromString(newout.name()),
                                     MLVal::fromString(newin.name()),
                                     MLVal::fromInt(factor));
        schedule_transforms.push_back(t);
    }

    void Func::transpose(const Var &outer, const Var &inner) {
        MLVal t = makeTransposeTransform(MLVal::fromString(name()),
                                         MLVal::fromString(outer.name()),
                                         MLVal::fromString(inner.name()));
        schedule_transforms.push_back(t);
    }

    void Func::chunk(const Var &caller_var, const Range &region) {
        MLVal r = makeList();
        for (size_t i = region.range.size(); i > 0; i--) {
            r = addToList(r, makePair(region.range[i-1].first.node, region.range[i-1].second.node));
        }

        MLVal t = makeChunkTransform(MLVal::fromString(name()),
                                     MLVal::fromString(caller_var.name()),
                                     arglist,
                                     r);
        schedule_transforms.push_back(t);
    }

    DynImage Func::realize(int a) {
        DynImage im(a * (rhs.type.bits / 8), a);
        realize(im);
        return im;
    }

    DynImage Func::realize(int a, int b) {
        DynImage im(a * b * (rhs.type.bits / 8), a, b);
        realize(im);
        return im;
    }

    DynImage Func::realize(int a, int b, int c) {
        DynImage im(a * b * c * (rhs.type.bits / 8), a, b, c);
        realize(im);
        return im;
    }


    DynImage Func::realize(int a, int b, int c, int d) {
        DynImage im(a * b * c * (rhs.type.bits / 8), a, b, c, d);
        realize(im);
        return im;
    }

    void Func::realize(const DynImage &im) {
        static llvm::ExecutionEngine *ee = NULL;
        static llvm::FunctionPassManager *passMgr = NULL;

        if (!ee) {
            llvm::InitializeNativeTarget();
        }

        if (!function_ptr) {

            // Make a region to evaluate this over
            MLVal sizes = makeList();
            for (size_t i = im.size.size(); i > 0; i--) {                
                sizes = addToList(sizes, MLVal::fromInt(im.size[i-1]));
            }

            MLVal sched = makeSchedule(MLVal::fromString(name()),
                                       sizes,
                                       *Func::environment);

            printf("Transforming schedule...\n");
            printSchedule(sched);
            for (size_t i = 0; i < schedule_transforms.size(); i++) {
                sched = schedule_transforms[i](sched);
                printSchedule(sched);
            }
            
            for (size_t i = 0; i < rhs.funcs.size(); i++) {
                Func *f = rhs.funcs[i];
                for (size_t j = 0; j < f->schedule_transforms.size(); j++) {
                    MLVal t = f->schedule_transforms[j];
                    sched = t(sched);
                    printSchedule(sched);
                }
            }

            printf("Done transforming schedule\n");

            MLVal stmt = doLower(MLVal::fromString(name()), 
                                 *Func::environment,
                                 sched);   

            // Create a function around it with the appropriate number of args
            printf("\nMaking function...\n");           
            MLVal args = makeList();
            args = addToList(args, makeBufferArg(MLVal::fromString("result")));
            for (size_t i = rhs.bufs.size(); i > 0; i--) {
                MLVal arg = makeBufferArg(MLVal::fromString(rhs.bufs[i-1]->name()));
                args = addToList(args, arg);
            }

            printStmt(stmt);

            printf("compiling IR -> ll\n");
            MLVal tuple = doCompile(args, stmt);

            printf("Extracting the resulting module and function\n");
            MLVal first, second;
            MLVal::unpackPair(tuple, first, second);
            //LLVMModuleRef module = *((LLVMModuleRef *)(first.asVoidPtr()));
            //LLVMValueRef func = *((LLVMValueRef *)(second.asVoidPtr()));
            LLVMModuleRef module = (LLVMModuleRef)(first.asVoidPtr());
            LLVMValueRef func = (LLVMValueRef)(second.asVoidPtr());
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
            // This pass makes a mess of vector x + x
            //passMgr->add(llvm::createInstructionCombiningPass());
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
        arguments[rhs.bufs.size()] = im.data;

        printf("Calling function at %p\n", function_ptr); 
        function_ptr(&arguments[0]); 
    }

    MLVal *Func::environment = NULL;

    Type Float(unsigned char bits) {
        return Type {makeFloatType(MLVal::fromInt(bits)), bits};
    }

    Type Int(unsigned char bits) {
        return Type {makeIntType(MLVal::fromInt(bits)), bits};
    }

    Type UInt(unsigned char bits) {
        return Type {makeUIntType(MLVal::fromInt(bits)), bits};
    }

    DynImage::DynImage(size_t bytes, uint32_t a) : 
        size{a}, stride{1} {
        allocate(bytes);
    }

    DynImage::DynImage(size_t bytes, uint32_t a, uint32_t b) : 
        size{a, b}, stride{1, a} {
        allocate(bytes);
    }
    
    DynImage::DynImage(size_t bytes, uint32_t a, uint32_t b, uint32_t c) : 
        size{a, b, c}, stride{1, a, a*b} {
        allocate(bytes);
    }

    DynImage::DynImage(size_t bytes, uint32_t a, uint32_t b, uint32_t c, uint32_t d) : 
        size{a, b, c, d}, stride{1, a, a*b, a*b*c} {
        allocate(bytes);
    }


    void DynImage::allocate(size_t bytes) {
        buffer.reset(new std::vector<unsigned char>(bytes+16));
        data = &(*buffer)[0];
        unsigned char offset = ((size_t)data) & 0xf;
        if (offset) {
            data += 16 - offset;
        }
    }

    Expr DynImage::load(Type type, const Expr &idx) {
        Expr l(makeLoad(type.mlval, MLVal::fromString(name()), idx.node), type);
        l.child(idx);
        l.bufs.push_back(this);
        return l;
    }

    Range operator*(const Range &a, const Range &b) {
        Range region;
        region.range.resize(a.range.size() + b.range.size());
        for (size_t i = 0; i < a.range.size(); i++) {
            region.range[i] = a.range[i];
        }
        for (size_t i = 0; i < b.range.size(); i++) {
            region.range[a.range.size() + i] = b.range[i];
        }
        return region;
    }

    Expr Cast(const Type &t, const Expr &e) {
        Expr cast(makeCast(t.mlval, e.node), t);
        cast.child(e);
        return cast;
    }

    Expr Debug(Expr e, const std::string &prefix, const std::vector<Expr> &args) {
        MLVal mlargs = makeList();
        for (size_t i = args.size(); i > 0; i--) {
            mlargs = addToList(mlargs, args[i-1].node);
        }

        Expr d(makeDebug(e.node, MLVal::fromString(prefix), mlargs), e.type);        
        d.child(e);
        for (size_t i = 0; i < args.size(); i++) {
            d.child(args[i]);
        }
        return d;
    }

    Expr Debug(Expr expr, const std::string &prefix) {
        std::vector<Expr> args;
        return Debug(expr, prefix, args);
    }

    Expr Debug(Expr expr, const std::string &prefix, Expr a) {
        std::vector<Expr> args {a};
        return Debug(expr, prefix, args);
    }

    Expr Debug(Expr expr, const std::string &prefix, Expr a, Expr b) {
        std::vector<Expr> args {a, b};
        return Debug(expr, prefix, args);
    }

    Expr Debug(Expr expr, const std::string &prefix, Expr a, Expr b, Expr c) {
        std::vector<Expr> args {a, b, c};
        return Debug(expr, prefix, args);
    }


    Expr Debug(Expr expr, const std::string &prefix, Expr a, Expr b, Expr c, Expr d) {
        std::vector<Expr> args {a, b, c, d};
        return Debug(expr, prefix, args);
    }

    Expr Debug(Expr expr, const std::string &prefix, Expr a, Expr b, Expr c, Expr d, Expr e) {
        std::vector<Expr> args {a, b, c, d, e};
        return Debug(expr, prefix, args);
    }


    template<>
    Type TypeOf<float>() {
        return Float(32);
    }

    template<>
    Type TypeOf<double>() {
        return Float(64);
    }

    template<>
    Type TypeOf<unsigned char>() {
        return UInt(8);
    }

    template<>
    Type TypeOf<unsigned short>() {
        return UInt(16);
    }

    template<>
    Type TypeOf<unsigned int>() {
        return UInt(32);
    }

    template<>
    Type TypeOf<bool>() {
        return Int(1);
    }

    template<>
    Type TypeOf<char>() {
        return Int(8);
    }

    template<>
    Type TypeOf<short>() {
        return Int(16);
    }

    template<>
    Type TypeOf<int>() {
        return Int(32);
    }

    template<>
    Type TypeOf<signed char>() {
        return Int(8);
    }

}
