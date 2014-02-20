#define _CONCAT_(A, B) A ## B
#define CONCAT(A, B) _CONCAT_(A, B)
#define CODEGEN_GPU_PARENT CONCAT(CodeGen_, GPU_HOST_TARGET)
#define CODEGEN_GPU_CLASS CONCAT(CodeGen_GPU_Host_, GPU_HOST_TARGET)

/** A code generator that emits GPU code from a given Halide stmt. */
class CODEGEN_GPU_CLASS : public CODEGEN_GPU_PARENT {
public:

    /** Create a GPU code generator. GPU target is selected via
     * CodeGen_GPU_Options. Processor features can be enabled using the
     * appropriate flags from CodeGen_X86_Options */
    CODEGEN_GPU_CLASS(Target);

    virtual ~CODEGEN_GPU_CLASS();

    /** Compile to an internally-held llvm module. Takes a halide
     * statement, the name of the function produced, and the arguments
     * to the function produced. After calling this, call
     * CodeGen::compile_to_file or
     * CodeGen::compile_to_function_pointer to get at the x86 machine
     * code. */
    void compile(Stmt stmt, std::string name,
                 const std::vector<Argument> &args,
                 const std::vector<Buffer> &images_to_embed);

protected:
    using CODEGEN_GPU_PARENT::visit;

    class Closure;

    /** Nodes for which we need to override default behavior for the GPU runtime */
    // @{
    void visit(const For *);
    void visit(const Allocate *);
    void visit(const Free *);
    void visit(const Pipeline *);
    void visit(const Call *);
    // @}

    // We track buffer_t's for each allocation in order to manage dirty bits
    bool track_buffers() {return true;}

    //** Runtime function handles */
    // @{
    llvm::Function *dev_malloc_fn;
    llvm::Function *dev_free_fn;
    llvm::Function *copy_to_dev_fn;
    llvm::Function *copy_to_host_fn;
    llvm::Function *dev_run_fn;
    llvm::Function *dev_sync_fn;
    // @}

    /** Finds and links in the CUDA runtime symbols prior to jitting */
    void jit_init(llvm::ExecutionEngine *ee, llvm::Module *mod);

    /** Reaches inside the module at sets it to use a single shared
     * cuda context */
    void jit_finalize(llvm::ExecutionEngine *ee, llvm::Module *mod, std::vector<void (*)()> *cleanup_routines);

    static bool lib_cuda_linked;

    static CodeGen_GPU_Dev* make_dev(Target);

    llvm::Value *get_module_state();

private:
    /** Child code generator for device kernels. */
    CodeGen_GPU_Dev *cgdev;
};

#undef CODEGEN_GPU_CLASS
#undef CODEGEN_GPU_PARENT
