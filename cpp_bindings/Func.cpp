#include <llvm-c/Core.h> // for LLVMModuleRef and LLVMValueRef
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/Transforms/IPO.h>
#include <llvm-c/Transforms/PassManagerBuilder.h>
#include <sys/time.h>

#include "../src/buffer.h"
#include "Func.h"
#include "FuncContents.h"
#include "Util.h"
#include "Var.h"
#include "Image.h"
#include "Uniform.h"
#include <sstream>

#include <dlfcn.h>
#include <unistd.h>

namespace Halide {
    
    extern "C" { typedef struct CUctx_st *CUcontext; }
    CUcontext cuda_ctx = 0;
    
    bool use_gpu() {
        char* target = getenv("HL_TARGET");
        return (target != NULL && strncasecmp(target, "ptx", 3) == 0);
    }

    bool use_avx() {
        char *target = getenv("HL_TARGET");
        if (target == NULL || strncasecmp(target, "x86_64", 6) == 0) {
        #ifdef __x86_64__
            int func = 1, ax, bx, cx, dx;
            __asm__ __volatile__ ("cpuid":                              
                                  "=a" (ax), 
                                  "=b" (bx), 
                                  "=c" (cx), 
                                  "=d" (dx) : 
                                  "a" (func));      
            // bit 28 of ecx indicates avx support
            return cx & 0x10000000;
            #endif
        }
        return false;
    }

    std::string getTarget() {
        // First check an environment variable
        char *target = getenv("HL_TARGET");
        if (target) return std::string(target);

        // Failing that, assume it's whatever this library was built for, with no options
        #ifdef __arm__
        return "armv7l";
        #endif

        #ifdef __x86_64__
        return "x86_64";
        #endif

        assert(0 && "Could not detect target. Try setting HL_TARGET\n");
        return "";
    }
    
    ML_FUNC2(makeVectorizeTransform);
    ML_FUNC2(makeUnrollTransform);
    ML_FUNC4(makeBoundTransform);
    ML_FUNC5(makeSplitTransform);
    ML_FUNC2(makeReorderTransform);
    ML_FUNC3(makeChunkTransform);
    ML_FUNC1(makeRootTransform);
    ML_FUNC2(makeParallelTransform);
    
    ML_FUNC1(doConstantFold);
  
    ML_FUNC0(makeEnv);
    
    ML_FUNC4(makeSchedule);
    ML_FUNC3(doLower);

    ML_FUNC0(makeNoviceGuru);
    ML_FUNC2(composeFunction);

    ML_FUNC1(printStmt);
    ML_FUNC1(printSchedule);
    ML_FUNC1(makeBufferArg); // name
    ML_FUNC2(makeScalarArg); // name, type
    ML_FUNC4(doCompile); // target with opts, name, args, stmt
    ML_FUNC4(doCompileToFile); // target with opts, name, args, stmt

    ML_FUNC1(serializeStmt); // stmt
    ML_FUNC3(serializeEntry); // name, args, stmt
    ML_FUNC1(serializeEnv);

    struct FuncRef::Contents {
        Contents(const Func &f) :
            f(f) {}
        Contents(const Func &f, const Expr &a) :
            f(f), args(vec(a)) {fixArgs();}
        Contents(const Func &f, const Expr &a, const Expr &b) :
            f(f), args(vec(a, b)) {fixArgs();}
        Contents(const Func &f, const Expr &a, const Expr &b, const Expr &c) :
            f(f), args(vec(a, b, c)) {fixArgs();}
        Contents(const Func &f, const Expr &a, const Expr &b, const Expr &c, const Expr &d) :
            f(f), args(vec(a, b, c, d)) {fixArgs();}
        Contents(const Func &f, const Expr &a, const Expr &b, const Expr &c, const Expr &d, const Expr &e) :
            f(f), args(vec(a, b, c, d, e)) {fixArgs();}
        Contents(const Func &f, const std::vector<Expr> &args) : f(f), args(args) {fixArgs();}

        void fixArgs() {
            for (size_t i = 0; i < args.size(); i++) {
                if (args[i].type() != Int(32)) {
                    args[i] = cast<int>(args[i]);
                }
            }
        }

        // A pointer to the function object that this lhs defines.
        Func f;
        std::vector<Expr> args;
    };
    
    FuncRef::FuncRef(const Func &f) :
        contents(new FuncRef::Contents(f)) {
    }

    FuncRef::FuncRef(const Func &f, const Expr &a) : 
        contents(new FuncRef::Contents(f, a)) {
    }

    FuncRef::FuncRef(const Func &f, const Expr &a, const Expr &b) :
        contents(new FuncRef::Contents(f, a, b)) {
    }

    FuncRef::FuncRef(const Func &f, const Expr &a, const Expr &b, const Expr &c) :
        contents(new FuncRef::Contents(f, a, b, c)) {
    }

    FuncRef::FuncRef(const Func &f, const Expr &a, const Expr &b, const Expr &c, const Expr &d) : 
        contents(new FuncRef::Contents(f, a, b, c, d)) {
    }

    FuncRef::FuncRef(const Func &f, const Expr &a, const Expr &b, const Expr &c, const Expr &d, const Expr &e) : 
        contents(new FuncRef::Contents(f, a, b, c, d, e)) {
    }

    FuncRef::FuncRef(const Func &f, const std::vector<Expr> &args) :
        contents(new FuncRef::Contents(f, args)) {
    }

    FuncRef::FuncRef(const FuncRef &other) :
        contents(other.contents) {
    }

    void FuncRef::operator=(const Expr &e) {
        contents->f.define(contents->args, e);
    }

    void FuncRef::operator+=(const Expr &e) {
        std::vector<Expr> gather_args(contents->args.size());
        for (size_t i = 0; i < gather_args.size(); i++) {
            gather_args[i] = contents->args[i].isVar() ? contents->args[i] : Var();
        }
        if (!contents->f.rhs().isDefined()) {
            Expr init = cast(e.type(), 0);
            init.addImplicitArgs(e.implicitArgs());
            contents->f.define(gather_args, init);
        }
        contents->f.define(contents->args, contents->f(contents->args) + e);
    }

    void FuncRef::operator*=(const Expr &e) {
        std::vector<Expr> gather_args(contents->args.size());
        for (size_t i = 0; i < gather_args.size(); i++) {
            gather_args[i] = contents->args[i].isVar() ? contents->args[i] : Var();
        }
        if (!contents->f.rhs().isDefined()) {
            Expr init = cast(e.type(), 1);
            init.addImplicitArgs(e.implicitArgs());
            contents->f.define(gather_args, init);
        }
        contents->f.define(contents->args, contents->f(contents->args) * e);
    }

    const Func &FuncRef::f() const {
        return contents->f;
    }

    const std::vector<Expr> &FuncRef::args() const {
        return contents->args;
    }

    Func::Func() : contents(new FuncContents()) {
    }
 
    Func::Func(const std::string &name) : contents(new FuncContents(sanitizeName(name))) {
    }

    Func::Func(const char *name) : contents(new FuncContents(sanitizeName(name))) {
    }

    Func::Func(const Type &t) : contents(new FuncContents(t)) {
    }

    Func::Func(const std::string &name, Type t) : contents(new FuncContents(sanitizeName(name), t)) {
    }

    Func::Func(const char *name, Type t) : contents(new FuncContents(sanitizeName(name), t)) {
    }

    Func::Func(FuncContents* c) : contents(c) {}

    bool Func::operator==(const Func &other) const {
        return other.contents == contents;
    }

    const Expr &Func::rhs() const {
        return contents->rhs;
    }

    const Type &Func::returnType() const {
        return contents->returnType;
    }

    const std::vector<Expr> &Func::args() const {
        return contents->args;
    }
    
    const Var &Func::arg(int i) const {
        const Expr& e = args()[i];
        assert (e.isVar());
        return e.vars()[0];
    }

    const std::string &Func::name() const { 
        return contents->name;
    }

    void Func::define(const std::vector<Expr> &_args, const Expr &r) {
        //printf("Defining %s\n", name().c_str());

        // Make a local copy of the argument list
        std::vector<Expr> args = _args;

        // Add any implicit arguments 
        //printf("Adding %d implicit arguments\n", r.implicitArgs());
        for (int i = 0; i < r.implicitArgs(); i++) {
            args.push_back(Var(std::string("iv") + int_to_str(i))); 
        }

        // Check that all free variables in the rhs appear in the lhs
        std::vector<Var> argVars;
        for (int i = 0; i < args.size(); i++) {
            const std::vector<Var> &vars = args[i].vars();
            for (int j = 0; j < vars.size(); j++) {
                set_add(argVars, vars[j]);
            }
        }
        std::vector<Var> rhsVars = r.vars();
        for (int i = 0; i < rhsVars.size(); i++) {
            if (rhsVars[i].name().at(0) == '.') continue; // skip uniforms injected as vars
            if (!set_contains(argVars, rhsVars[i])) {
                printf("argVars does not contain %s\n", rhsVars[i].name().c_str());
            }
            assert(set_contains(argVars, rhsVars[i]) &&
                   "All free variables in right side of function definition must be bound on left");
        }
        
        //printf("Defining %s\n", name().c_str());

        // Are we talking about a scatter or a gather here?
        bool gather = true;
        //printf("%u args %u rvars\n", (unsigned)args.size(), (unsigned)r.rdom().dimensions());
        for (size_t i = 0; i < args.size(); i++) {            
            if (!args[i].isVar()) {
                gather = false;
            }
        }
        if (r.rdom().dimensions() > 0) gather = false;

        if (gather) {
            //printf("Gather definition for %s\n", name().c_str());
            contents->args = args;
            contents->rhs = r;            
            contents->returnType = r.type();
        } else {
            //printf("Scatter definition for %s\n", name().c_str());
            assert(rhs().isDefined() && "Must provide a base-case definition for function before the reduction case");
                       
            // Make an update function as a handle for scheduling
            contents->update.reset(new Func(uniqueName('p')));
            contents->update->contents->args = args;
            contents->update->contents->rhs = cast(contents->returnType, r);
            contents->update->contents->returnType = contents->returnType;
        }
    }

    bool Func::isReduction() const {
        return contents->update;
    }

    Func &Func::update() const {
        assert(isReduction());
        return *contents->update;
    }

    Func &Func::tile(const Var &x, const Var &y, 
                     const Var &xi, const Var &yi, 
                     const Expr &f1, const Expr &f2) {
        split(x, x, xi, f1);
        split(y, y, yi, f2);
        reorder(xi, yi, x, y);
        return *this;
    }

    Func &Func::tile(const Var &x, const Var &y, 
                     const Var &xo, const Var &yo,
                     const Var &xi, const Var &yi, 
                     const Expr &f1, const Expr &f2) {
        split(x, xo, xi, f1);
        split(y, yo, yi, f2);
        reorder(xi, yi, xo, yo);
        return *this;
    }

    Func &Func::vectorize(const Var &v) {
        MLVal t = makeVectorizeTransform((name()),
                                         (v.name()));
        contents->guru = composeFunction(t, contents->guru);
        return *this;
    }

    Func &Func::vectorize(const Var &v, int factor) {
        if (factor == 1) return *this;
        Var vi;
        split(v, v, vi, factor);
        vectorize(vi);        
        return *this;
    }

    Func &Func::unroll(const Var &v) {
        MLVal t = makeUnrollTransform((name()),
                                      (v.name()));        
        contents->guru = composeFunction(t, contents->guru);
        return *this;
    }

    Func &Func::unroll(const Var &v, int factor) {
        if (factor == 1) return *this;
        Var vi;
        split(v, v, vi, factor);
        unroll(vi);
        return *this;
    }

    Func &Func::split(const Var &old, const Var &newout, const Var &newin, const Expr &factor) {
        MLVal t = makeSplitTransform(name(),
                                     old.name(),
                                     newout.name(),
                                     newin.name(),
                                     factor.node());
        contents->guru = composeFunction(t, contents->guru);
        return *this;
    }

    Func &Func::reorder(const std::vector<Var> &vars) {
        MLVal list = makeList();
        for (size_t i = vars.size(); i > 0; i--) {
            list = addToList(list, vars[i-1].name());
        }
        MLVal t = makeReorderTransform(name(), list);
        contents->guru = composeFunction(t, contents->guru);
        return *this;
    }

    Func &Func::reorder(const Var &v1, const Var &v2) {
        return reorder(vec(v1, v2));
    }

    Func &Func::reorder(const Var &v1, const Var &v2, const Var &v3) {
        return reorder(vec(v1, v2, v3));
    }

    Func &Func::reorder(const Var &v1, const Var &v2, const Var &v3, const Var &v4) {
        return reorder(vec(v1, v2, v3, v4));
    }

    Func &Func::reorder(const Var &v1, const Var &v2, const Var &v3, const Var &v4, const Var &v5) {
        return reorder(vec(v1, v2, v3, v4, v5));
    }

    Func &Func::chunk(const Var &caller_var) {
        return chunk(caller_var, caller_var);
    }
    
    Func &Func::chunk(const Var &caller_store_var, const Var &caller_compute_var) {
        MLVal t = makeChunkTransform(name(), caller_store_var.name(), caller_compute_var.name());
        contents->guru = composeFunction(t, contents->guru);
        return *this;
    }

    Func &Func::root() {
        MLVal t = makeRootTransform(name());
        contents->guru = composeFunction(t, contents->guru);
        return *this;
    }

    Func &Func::reset() {
        contents->guru = makeIdentity();
        return *this;
    }

    Func &Func::parallel(const Var &caller_var) {
        MLVal t = makeParallelTransform(name(), caller_var.name());
        contents->guru = composeFunction(t, contents->guru);
        return *this;
    }

    Func &Func::rename(const Var &oldname, const Var &newname) {
        Var dummy;
        return split(oldname, newname, dummy, 1);
    }

    Func &Func::cuda(const Var &b, const Var &t) {
        Var tidx("threadidx");
        Var bidx("blockidx");
        rename(b, bidx);
        rename(t, tidx);
        parallel(bidx);
        parallel(tidx);
        return *this;
    }

    Func &Func::cuda(const Var &bx, const Var &by, 
                     const Var &tx, const Var &ty) {
        Var tidx("threadidx");
        Var bidx("blockidx");
        Var tidy("threadidy");
        Var bidy("blockidy");
        rename(bx, bidx);
        rename(tx, tidx);
        rename(by, bidy);
        rename(ty, tidy);
        parallel(bidx);
        parallel(bidy);
        parallel(tidx);
        parallel(tidy);
        return *this;
    }


    Func &Func::cudaTile(const Var &x, int xFactor) {
        Var tidx("threadidx");
        Var bidx("blockidx");
        split(x, bidx, tidx, xFactor);
        parallel(bidx);
        parallel(tidx);
        return *this;
    }

    Func &Func::cudaTile(const Var &x, const Var &y, int xFactor, int yFactor) {
        Var tidx("threadidx");
        Var bidx("blockidx");
        Var tidy("threadidy");
        Var bidy("blockidy");
        tile(x, y, bidx, bidy, tidx, tidy, xFactor, yFactor);
        parallel(bidx);
        parallel(tidx);
        parallel(bidy);
        parallel(tidy);
        return *this;
    }

    DynImage Func::realize(int a) {
        DynImage im(returnType(), a);
        realize(im);
        return im;
    }

    DynImage Func::realize(int a, int b) {
        DynImage im(returnType(), a, b);
        realize(im);
        return im;
    }

    DynImage Func::realize(int a, int b, int c) {
        DynImage im(returnType(), a, b, c);
        realize(im);
        return im;
    }


    DynImage Func::realize(int a, int b, int c, int d) {
        DynImage im(returnType(), a, b, c, d);
        realize(im);
        return im;
    }

    DynImage Func::realize(std::vector<int> sizes) {
        DynImage im(returnType(), sizes);
        realize(im);
        return im;
    }

    MLVal Func::buildEnv() {
        MLVal env = makeEnv();
        env = contents->addDefinition(env);

        vector<Func> fs = funcs();
        set_union(fs, transitiveFuncs());
        for (size_t i = 0; i < fs.size(); i++) {
            Func f = fs[i];
            // Don't consider recursive dependencies.
            if (f == *this) continue;
            env = f.contents->addDefinition(env);
        }

        return env;
    }

    MLVal Func::buildGuru() {
        MLVal guru = makeNoviceGuru();
        guru = contents->applyGuru(guru);

        vector<Func> fs = funcs();
        set_union(fs, transitiveFuncs());
        for (size_t i = 0; i < fs.size(); i++) {
            Func f = fs[i];
            // Don't consider recursive dependencies.
            if (f == *this) continue;
            guru = f.contents->applyGuru(guru);
        }

        return guru;
    }

    // Returns a stmt, args pair
    MLVal Func::lower() {
        // Make a region to evaluate this over
        MLVal sizes = makeList();        
        for (size_t i = args().size(); i > 0; i--) {                
            char buf[256];
            snprintf(buf, 256, ".result.dim.%d", ((int)i)-1);
            sizes = addToList(sizes, Expr(Var(buf)).node());
        }

        //printf("Building environment and guru...\n");

        // Output is always scheduled root
        root();

        // Build the guru and the environment
        MLVal env = buildEnv();
        MLVal guru = buildGuru();

        MLVal sched = makeSchedule(name(), sizes, env, guru);

        //printf("Done transforming schedule\n");
        //printSchedule(sched);        

        return doLower(name(), env, sched);        
    }

    std::vector<DynUniform> Func::uniforms() const {
        std::vector<DynUniform> v = rhs().uniforms();
        if (isReduction()) {
            set_union(v, update().rhs().uniforms());
            for (size_t i = 0; i < update().args().size(); i++) {
                set_union(v, update().args()[i].uniforms());
            }
        }
        return v;
    }

    std::vector<DynImage> Func::images() const {
        std::vector<DynImage> v = rhs().images();
        if (isReduction()) {
            set_union(v, update().rhs().images());
            for (size_t i = 0; i < update().args().size(); i++) {
                set_union(v, update().args()[i].images());
            }
        }
        return v;
    }

    std::vector<Func> Func::funcs() const {
        std::vector<Func> v = rhs().funcs();
        if (isReduction()) {
            set_union(v, update().rhs().funcs());
            for (size_t i = 0; i < update().args().size(); i++) {
                set_union(v, update().args()[i].funcs());
            }
        }
        return v;
    }

    std::vector<Func> Func::transitiveFuncs() const {
        std::vector<Func> v = rhs().transitiveFuncs();
        if (isReduction()) {
            set_union(v, update().rhs().transitiveFuncs());
            for (size_t i = 0; i < update().args().size(); i++) {
                set_union(v, update().args()[i].transitiveFuncs());
            }
        }
        return v;
    }

    std::vector<UniformImage> Func::uniformImages() const {
        std::vector<UniformImage> v = rhs().uniformImages();
        if (isReduction()) {
            set_union(v, update().rhs().uniformImages());
            for (size_t i = 0; i < update().args().size(); i++) {
                set_union(v, update().args()[i].uniformImages());
            }
        }
        return v;
    }

    MLVal Func::inferArguments() {        
        std::vector<DynUniform> uns = uniforms();
        std::vector<DynImage> ims = images();
        std::vector<UniformImage> unims = uniformImages();

        MLVal fargs = makeList();
        fargs = addToList(fargs, makeBufferArg("result"));
        for (size_t i = unims.size(); i > 0; i--) {
            MLVal arg = makeBufferArg(unims[i-1].name());
            fargs = addToList(fargs, arg);
        }
        for (size_t i = ims.size(); i > 0; i--) {
            MLVal arg = makeBufferArg(ims[i-1].name());
            fargs = addToList(fargs, arg);
        }
        for (size_t i = uns.size(); i > 0; i--) {
            const DynUniform &u = uns[i-1];
            MLVal arg = makeScalarArg(u.name(), u.type().mlval);
            fargs = addToList(fargs, arg);
        }
        return fargs;
    }

    Func::Arg::Arg(const UniformImage &u) : arg(makeBufferArg(u.name())) {}
    Func::Arg::Arg(const DynUniform &u) : arg(makeScalarArg(u.name(), u.type().mlval)) {}
    Func::Arg::Arg(const DynImage &u) : arg(makeBufferArg(u.name())) {}

    std::string Func::serialize() {
        return std::string(serializeEnv(buildEnv()));
    }

    std::string Func::serializeLowered() {
        MLVal stmt = lower();
        MLVal args = inferArguments();
        return std::string(serializeEntry(name(), args, stmt));
    }

    void Func::compileToFile(const std::string &moduleName, std::string target) { 
        MLVal stmt = lower();
        MLVal args = inferArguments();
        if (target.empty()) target = getTarget();
        doCompileToFile(target, moduleName, args, stmt);
    }

    void Func::compileToFile(const std::string &moduleName, std::vector<Func::Arg> uniforms, std::string target) { 
        MLVal stmt = lower();

        MLVal args = makeList();
        args = addToList(args, makeBufferArg("result"));
        for (size_t i = uniforms.size(); i > 0; i--) {
            args = addToList(args, uniforms[i-1].arg);
        }

        if (target.empty()) target = getTarget();
        doCompileToFile(target, moduleName, args, stmt);
    }

    void Func::setErrorHandler(void (*handler)(char *)) {
        contents->errorHandler = handler;
    }

    void Func::compileJIT() {

        // If JITting doesn't work well on this platform (ARM), try
        // compiling to a shared lib instead and manually linking it
        // in. Also useful for debugging.
        if (getenv("HL_PSEUDOJIT") && getenv("HL_PSEUDOJIT") == std::string("1")) {
            fprintf(stderr, "Pseudo-jitting via static compilation to a shared object\n");
            
            std::string name = contents->name + "_pseudojit";
            std::string so_name = "./" + name + ".so";
            std::string entrypoint_name = name + "_c_wrapper";
            
            // Compile the object, unless HL_PSEUDOJIT_LOAD_PRECOMPILED is set
            if (!getenv("HL_PSEUDOJIT_LOAD_PRECOMPILED")) {
                compileToFile(name.c_str(), getTarget());
                char cmd1[1024], cmd2[1024];

                std::string obj_name = "./" + name + ".o";
                if (getenv("HL_BACKEND") && getenv("HL_BACKEND") == std::string("c")) {
                    std::string c_name = "./" + name + ".c";
                    snprintf(cmd1, 1024, "g++ -c -O3 %s -fPIC -o %s", c_name.c_str(), obj_name.c_str());
                } else {
                    std::string bc_name = "./" + name + ".bc";
                    snprintf(cmd1, 1024, 
                             "opt -O3 -always-inline %s | llc -O3 -relocation-model=pic %s -filetype=obj > %s", 
                             bc_name.c_str(), use_avx() ? "-mcpu=corei7 -mattr=+avx" : "", obj_name.c_str());
                }
                snprintf(cmd2, 1024, "gcc -shared %s -o %s", obj_name.c_str(), so_name.c_str());
                fprintf(stderr, "%s\n", cmd1);
                assert(0 == system(cmd1));
                fprintf(stderr, "%s\n", cmd2);
                assert(0 == system(cmd2));
            }
            void *handle = dlopen(so_name.c_str(), RTLD_LAZY);
            fprintf(stderr, "dlopen(%s)\n", so_name.c_str());
            if (!handle) perror("dlopen");
            assert(handle && "Could not open shared object file when pseudojitting");
            void *ptr = dlsym(handle, entrypoint_name.c_str());
            assert(ptr && "Could not find entrypoint in shared object file when pseudojitting");
            contents->functionPtr = (void (*)(void *))ptr;

            // Hook up any custom error handler
            if (contents->errorHandler) {
                ptr = dlsym(handle, "set_error_handler");
                assert(ptr && "Could not find set_error_handler in shared object file when pseudojitting");
                typedef void (*handler_t)(char *);
                void (*setErrorHandlerFn)(handler_t) = (void (*)(void (*)(char *)))ptr;
                setErrorHandlerFn(contents->errorHandler);
            }
            
            return;
        }

        if (!FuncContents::ee) {
            LLVMInitializeNativeTarget();
            LLVMInitializeX86AsmPrinter();
            LLVMInitializeARMAsmPrinter();
        }

        // Use the function definitions and the schedule to create the
        // blob of imperative IR
        MLVal stmt = lower();
        
        // Hook up uniforms, images, etc and turn them into the
        // argument list for the llvm function
        MLVal args = inferArguments();

        // Create the llvm module and entrypoint from the imperative IR
        MLVal tuple;
        tuple = doCompile(getTarget(), name(), args, stmt);

        // Extract the llvm module and entrypoint function
        MLVal first, second;
        MLVal::unpackPair(tuple, first, second);
        LLVMModuleRef module = (LLVMModuleRef)(first.asVoidPtr());
        LLVMValueRef func = (LLVMValueRef)(second.asVoidPtr());

        // Create the execution engine if it hasn't already been done
        if (!FuncContents::ee) {
            
            char *errStr = NULL;
            bool error = LLVMCreateJITCompilerForModule(&FuncContents::ee, module, 3, &errStr);
            if (error) {
                printf("Couldn't create execution engine: %s\n", errStr);
            }

            FuncContents::fPassMgr = LLVMCreateFunctionPassManagerForModule(module);
            FuncContents::mPassMgr = LLVMCreatePassManager();

            // Make sure to include the always-inliner pass so that
            // unaligned_load and other similar one-opcode functions
            // always get inlined.
            LLVMAddAlwaysInlinerPass(contents->mPassMgr);

            LLVMPassManagerBuilderRef builder = LLVMPassManagerBuilderCreate();  
            LLVMPassManagerBuilderSetOptLevel(builder, 3);
            LLVMPassManagerBuilderPopulateFunctionPassManager(builder, contents->fPassMgr);
            LLVMPassManagerBuilderPopulateModulePassManager(builder, contents->mPassMgr);

        } else { 
            // Execution engine is already created. Add this module to it.
            LLVMAddModule(FuncContents::ee, module);
        }            
        
        std::string functionName = name() + "_c_wrapper";
        LLVMValueRef inner = LLVMGetNamedFunction(module, functionName.c_str());
        
        if (use_gpu()) {
            // Remap the cuda_ctx of PTX host modules to a shared location for all instances.
            // CUDA behaves much better when you don't initialize >2 contexts.

            LLVMValueRef ctx = LLVMGetNamedGlobal(module, "cuda_ctx");
            if (ctx) {
                LLVMAddGlobalMapping(FuncContents::ee, ctx, (void *)&cuda_ctx);
            }

            // Make sure extern cuda calls inside the module point to
            // the right things. This is done manually instead of
            // relying on llvm calling dlsym because that solution
            // doesn't seem to work on linux with cuda 4.2. It also
            // means that if the user forgets to link to libcuda at
            // compile time then this code will go look for it.
            if (!FuncContents::libCuda && !FuncContents::libCudaLinked) {
                // First check if libCuda has already been linked
                // in. If so we shouldn't need to set any mappings.
                if (dlsym(NULL, "cuInit")) {
                    // TODO: Andrew: This code path not tested yet,
                    // because I can't get linking to libcuda working
                    // right on my machine.
                    fprintf(stderr, "This program was linked to libcuda already\n");
                    FuncContents::libCudaLinked = true;
                } else {
                    fprintf(stderr, "Looking for libcuda.so...\n");
                    FuncContents::libCuda = dlopen("libcuda.so", RTLD_LAZY);
                    if (!FuncContents::libCuda) {
                        // TODO: check this works on OS X
                        fprintf(stderr, "Looking for libcuda.dylib...\n");
                        FuncContents::libCuda = dlopen("libcuda.dylib", RTLD_LAZY);
                    }
                    // TODO: look for cuda.dll or some such thing on windows
                }
            }
            
            if (!FuncContents::libCuda && !FuncContents::libCudaLinked) {
                fprintf(stderr, 
                        "Error opening libcuda. Attempting to continue anyway."
                        "Might get missing symbols.\n");
            } else if (FuncContents::libCudaLinked) {
                // Shouldn't need to do anything. llvm will call dlsym
                // on the current process for us.
            } else {
                for (LLVMValueRef f = LLVMGetFirstFunction(module); f;
                     f = LLVMGetNextFunction(f)) {
                    const char *name = LLVMGetValueName(f);
                    if (LLVMGetLinkage(f) == LLVMExternalLinkage &&
                        name[0] == 'c' && name[1] == 'u') {
                        // Starts with "cu" and has extern linkage. Might be a cuda function.
                        fprintf(stderr, "Linking %s\n", name);
                        void *ptr = dlsym(FuncContents::libCuda, name);
                        if (ptr) {
                            LLVMAddGlobalMapping(FuncContents::ee, f, ptr);
                        }
                    }
                }
            }
        }
        
        assert(inner && "Could not find c wrapper inside llvm module");
        
        // Run optimization passes

        // Turning on this code will dump the result of all the optimization passes to a file
        // std::string errstr;

        LLVMRunPassManager(FuncContents::mPassMgr, module);
        LLVMInitializeFunctionPassManager(FuncContents::fPassMgr);
        LLVMRunFunctionPassManager(FuncContents::fPassMgr, inner);
        LLVMFinalizeFunctionPassManager(FuncContents::fPassMgr);

        void *ptr = LLVMGetPointerToGlobal(FuncContents::ee, func);
        contents->functionPtr = (void (*)(void*))ptr;

        // Retrieve some functions inside the module that we'll want to call from C++
        LLVMValueRef copyToHost = LLVMGetNamedFunction(module, "__copy_to_host");
        if (copyToHost) {
            ptr = LLVMGetPointerToGlobal(FuncContents::ee, copyToHost);
            contents->copyToHost = (void (*)(buffer_t*))ptr;
        }

        LLVMValueRef freeBuffer = LLVMGetNamedFunction(module, "__free_buffer");
        if (freeBuffer) {
            ptr = LLVMGetPointerToGlobal(FuncContents::ee, freeBuffer);
            contents->freeBuffer = (void (*)(buffer_t*))ptr;
        }

        // If we have a custom error handler, hook it up here
        if (contents->errorHandler) {
            LLVMValueRef setErrorHandler = LLVMGetNamedFunction(module, "set_error_handler");
            assert(setErrorHandler && 
                   "Could not find the set_error_handler function in the compiled module\n");
            ptr = LLVMGetPointerToGlobal(FuncContents::ee, setErrorHandler);
            typedef void (*handler_t)(char *);
            void (*setErrorHandlerFn)(handler_t) = (void (*)(void (*)(char *)))ptr;
            setErrorHandlerFn(contents->errorHandler);
        }
    }

    size_t im_size(const DynImage &im, int dim) {
        return im.size(dim);
    }
    
    size_t im_size(const UniformImage &im, int dim) {
        return im.boundImage().size(dim);
    }

    void Func::realize(const DynImage &im) {
        if (!contents->functionPtr) compileJIT();

        //printf("Constructing argument list...\n");
        void *arguments[256];
        buffer_t *buffers[256];
        size_t j = 0;
        size_t k = 0;
        
        std::vector<DynUniform> uns = uniforms();
        std::vector<DynImage> ims = images();
        std::vector<UniformImage> unims = uniformImages();

        for (size_t i = 0; i < uns.size(); i++) {
            arguments[j++] = uns[i].data();
        }
        for (size_t i = 0; i < ims.size(); i++) {
            buffers[k++] = ims[i].buffer();
            arguments[j++] = buffers[k-1];
        }               
        for (size_t i = 0; i < unims.size(); i++) {
            buffers[k++] = unims[i].boundImage().buffer();
            arguments[j++] = buffers[k-1];
        }
        buffers[k] = im.buffer();
        arguments[j] = buffers[k];

        /*
        printf("Args: ");
        for (size_t i = 0; i <= j; i++) {
            printf("%p ", arguments[i]);
        }
        printf("\n");

        printf("Calling function at %p\n", contents->functionPtr); 
        */
        contents->functionPtr(&arguments[0]);
        
        if (use_gpu()) {
            assert(contents->copyToHost);
            im.setRuntimeHooks(contents->copyToHost, contents->freeBuffer);
        }
        
        // TODO: the actual codegen entrypoint should probably set this for x86/ARM targets too
        if (!im.devDirty()) {
            im.markHostDirty();
        }
    }

}

