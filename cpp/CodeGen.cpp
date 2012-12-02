#include <iostream>
#include "IRPrinter.h"
#include "CodeGen.h"
#include "llvm/Analysis/Verifier.h"
#include "IROperator.h"
#include "Util.h"
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetLibraryInfo.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Bitcode/ReaderWriter.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/CodeGen/CommandFlags.h>
#include <llvm/DataLayout.h>
#include <sstream>

namespace HalideInternal {

    using namespace llvm;
    using std::ostringstream;

    LLVMContext CodeGen::context;

    CodeGen::CodeGen() : 
        module(NULL), function(NULL), builder(context), value(NULL), buffer_t(NULL) {
        // Define some types
        void_t = llvm::Type::getVoidTy(context);
        i1 = llvm::Type::getInt1Ty(context);
        i8 = llvm::Type::getInt8Ty(context);
        i16 = llvm::Type::getInt16Ty(context);
        i32 = llvm::Type::getInt32Ty(context);
        i64 = llvm::Type::getInt64Ty(context);
        f16 = llvm::Type::getHalfTy(context);
        f32 = llvm::Type::getFloatTy(context);
        f64 = llvm::Type::getDoubleTy(context);

        // Initialize the targets we want to generate code for
        if (!llvm_initialized) {            
            InitializeNativeTarget();
            LLVMInitializeX86Target();
            LLVMInitializeX86AsmPrinter();
            LLVMInitializeX86TargetMC();
            LLVMInitializeARMTarget();
            LLVMInitializeARMAsmPrinter();
            LLVMInitializeARMTargetMC();
            LLVMInitializeNVPTXTarget();
            LLVMInitializeNVPTXAsmPrinter();
            LLVMInitializeNVPTXTargetMC();
            llvm_initialized = true;
        }
    }

    bool CodeGen::llvm_initialized = false;

    void CodeGen::compile(Stmt stmt, string name, const vector<Argument> &args) {
        assert(module && "The CodeGen subclass should have made an initial module before calling CodeGen::compile");

        // Start the module off with a definition of a buffer_t
        define_buffer_t();

        // Now deduce the types of the arguments to our function
        vector<llvm::Type *> arg_types(args.size());
        for (size_t i = 0; i < args.size(); i++) {
            if (args[i].is_buffer) {
                arg_types[i] = buffer_t->getPointerTo();
            } else {
                arg_types[i] = llvm_type_of(args[i].type);
            }
        }

        // Make our function
        function_name = name;
        FunctionType *func_t = FunctionType::get(void_t, arg_types, false);
        function = Function::Create(func_t, Function::ExternalLinkage, name, module);

        // Make the initial basic block
        BasicBlock *block = BasicBlock::Create(context, "entry", function);
        builder.SetInsertPoint(block);

        // Put the arguments in the symbol table
        {
            size_t i = 0;
            for (Function::arg_iterator iter = function->arg_begin();
                 iter != function->arg_end();
                 iter++) {                        

                if (args[i].is_buffer) {
                    unpack_buffer(args[i].name, iter);
                } else {
                    sym_push(args[i].name, iter);
                }
                i++;
            }
        }

        // Ok, we have a module, function, context, and a builder
        // pointing at a brand new basic block. We're good to go.
        stmt.accept(this);

        // Now we need to end the function
        builder.CreateRetVoid();

        module->setModuleIdentifier("halide_" + name);

        // Now verify the function is ok
        verifyFunction(*function);
        verifyModule(*module);
    }

    ExecutionEngine *CodeGen::execution_engine = NULL;

    void *CodeGen::compile_to_function_pointer() {
        assert(module && "No module defined. Must call compile before calling compile_to_function_pointer");
               
        FunctionPassManager function_pass_manager(module);
        PassManager module_pass_manager;

        // Create the execution engine if it hasn't already been done
        if (!execution_engine) {
            std::string error_string;
            EngineBuilder engine_builder(module);
            engine_builder.setErrorStr(&error_string);
            engine_builder.setEngineKind(EngineKind::JIT);
            engine_builder.setUseMCJIT(true);
            //engine_builder.setOptLevel(CodeGenOpt::Aggressive);
            execution_engine = engine_builder.create();
            if (!execution_engine) std::cout << error_string << std::endl;
            assert(execution_engine && "Couldn't create execution engine");
        } else { 
            std::cout << "Adding module to existing execution engine" << std::endl;
            // Execution engine is already created. Add this module to it.
            execution_engine->addModule(module);
        }

        // Make sure things marked as always-inline get inlined
        module_pass_manager.add(createAlwaysInlinerPass());
        
        PassManagerBuilder b;
        b.OptLevel = 3;
        b.populateFunctionPassManager(function_pass_manager);
        b.populateModulePassManager(module_pass_manager);
                
        Function *fn = module->getFunction(function_name);
        assert(fn && "Could not find function inside llvm module");
        
        // Run optimization passes
        module_pass_manager.run(*module);        
        function_pass_manager.doInitialization();
        function_pass_manager.run(*fn);
        function_pass_manager.doFinalization();

        return execution_engine->getPointerToFunction(fn);
    }

    void CodeGen::compile_to_bitcode(const string &filename) {
        assert(module && "No module defined. Must call compile before calling compile_to_file");        
        string error_string;
        raw_fd_ostream out(filename.c_str(), error_string);
        WriteBitcodeToFile(module, out);
    }

    void CodeGen::compile_to_native(const string &filename, bool assembly) {
         // Get the target specific parser.
        std::string error_string;
        std::cout << module->getTargetTriple() << std::endl;
        const Target *target = TargetRegistry::lookupTarget(module->getTargetTriple(), error_string);
        if (!target) std::cout << error_string << std::endl;
        assert(target && "Could not create target");

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
        options.FloatABIType = FloatABI::Default;
        options.NoZerosInBSS = false;
        options.GuaranteedTailCallOpt = false;
        options.DisableTailCalls = false;
        options.StackAlignmentOverride = 0;
        options.RealignStack = false;
        options.TrapFuncName = "";
        options.PositionIndependentExecutable = true;
        options.EnableSegmentedStacks = false;
        options.UseInitArray = false;
        options.SSPBufferSize = 0;
        
        TargetMachine *target_machine =
            target->createTargetMachine(module->getTargetTriple(), 
                                        "", // -mcpu
                                        "", // features, e.g. avx
                                        options, 
                                        Reloc::Default, 
                                        CodeModel::Default, 
                                        CodeGenOpt::Aggressive);
                                
        assert(target_machine && "Could not allocate target machine!");

        // Figure out where we are going to send the output.
        raw_fd_ostream raw_out(filename.c_str(), error_string);
        formatted_raw_ostream out(raw_out);

        // Build up all of the passes that we want to do to the module.
        PassManager pass_manager;

        // Add an appropriate TargetLibraryInfo pass for the module's triple.
        pass_manager.add(new TargetLibraryInfo(Triple(module->getTargetTriple())));       
        pass_manager.add(new TargetTransformInfo(target_machine->getScalarTargetTransformInfo(),
                                                 target_machine->getVectorTargetTransformInfo()));
        pass_manager.add(new DataLayout(module));

        // Override default to generate verbose assembly.
        target_machine->setAsmVerbosityDefault(true);

        // Ask the target to add backend passes as necessary.        
        TargetMachine::CodeGenFileType file_type =
            assembly ? TargetMachine::CGFT_AssemblyFile : 
            TargetMachine::CGFT_ObjectFile;
        target_machine->addPassesToEmitFile(pass_manager, out, file_type);

        pass_manager.run(*module);

        delete target_machine;
    }

    void CodeGen::sym_push(const string &name, llvm::Value *value) {
        value->setName(name);
        symbol_table.push(name, value);
    }

    // Take an llvm Value representing a pointer to a buffer_t,
    // and populate the symbol table with its constituent parts
    void CodeGen::unpack_buffer(string name, llvm::Value *buffer) {
        sym_push(name + ".host", buffer_host(buffer));
        sym_push(name + ".dev", buffer_dev(buffer));
        sym_push(name + ".host_dirty", buffer_host_dirty(buffer));
        sym_push(name + ".dev_dirty", buffer_dev_dirty(buffer));
        sym_push(name + ".extent.0", buffer_extent(buffer, 0));
        sym_push(name + ".extent.1", buffer_extent(buffer, 1));
        sym_push(name + ".extent.2", buffer_extent(buffer, 2));
        sym_push(name + ".extent.3", buffer_extent(buffer, 3));
        sym_push(name + ".stride.0", buffer_stride(buffer, 0));
        sym_push(name + ".stride.1", buffer_stride(buffer, 1));
        sym_push(name + ".stride.2", buffer_stride(buffer, 2));
        sym_push(name + ".stride.3", buffer_stride(buffer, 3));
        sym_push(name + ".min.0", buffer_min(buffer, 0));
        sym_push(name + ".min.1", buffer_min(buffer, 1));
        sym_push(name + ".min.2", buffer_min(buffer, 2));
        sym_push(name + ".min.3", buffer_min(buffer, 3));
        sym_push(name + ".elem_size", buffer_elem_size(buffer));
    }

    // Add a definition of buffer_t to the module if it isn't already there
    void CodeGen::define_buffer_t() {
        buffer_t = module->getTypeByName("struct.buffer_t");
        if (!buffer_t) {
            buffer_t = StructType::create(context, "struct.buffer_t");
        }

        vector<llvm::Type *> fields;
        fields.push_back(i8->getPointerTo());
        fields.push_back(i64);
        fields.push_back(i8);
        fields.push_back(i8);

        ArrayType* i32x4 = ArrayType::get(i32, 4);        
        fields.push_back(i32x4); // extent
        fields.push_back(i32x4); // stride
        fields.push_back(i32x4); // min
        fields.push_back(i32); // elem_size

        if (buffer_t->isOpaque()) {
            buffer_t->setBody(fields, false);
        }
    }
       
    // Given an llvm value representing a pointer to a buffer_t, extract various subfields
    Value *CodeGen::buffer_host(Value *buffer) {
        Value *ptr = builder.CreateConstGEP2_32(buffer, 0, 0);
        return builder.CreateLoad(ptr);
    }

    Value *CodeGen::buffer_dev(Value *buffer) {
        Value *ptr = builder.CreateConstGEP2_32(buffer, 0, 1);
        return builder.CreateLoad(ptr);
    }

    Value *CodeGen::buffer_host_dirty(Value *buffer) {
        Value *ptr = builder.CreateConstGEP2_32(buffer, 0, 2);
        return builder.CreateLoad(ptr);
    }

    Value *CodeGen::buffer_dev_dirty(Value *buffer) {
        Value *ptr = builder.CreateConstGEP2_32(buffer, 0, 3);
        return builder.CreateLoad(ptr);
    }

    Value *CodeGen::buffer_extent(Value *buffer, int i) {
        llvm::Value *zero = ConstantInt::get(i32, 0);
        llvm::Value *field = ConstantInt::get(i32, 4);
        llvm::Value *idx = ConstantInt::get(i32, i);
        vector<llvm::Value *> args = vec(zero, field, idx);
        Value *ptr = builder.CreateGEP(buffer, args);
        return builder.CreateLoad(ptr);
    }

    Value *CodeGen::buffer_stride(Value *buffer, int i) {
        llvm::Value *zero = ConstantInt::get(i32, 0);
        llvm::Value *field = ConstantInt::get(i32, 5);
        llvm::Value *idx = ConstantInt::get(i32, i);
        vector<llvm::Value *> args = vec(zero, field, idx);
        Value *ptr = builder.CreateGEP(buffer, args);
        return builder.CreateLoad(ptr);
    }

    Value *CodeGen::buffer_min(Value *buffer, int i) {
        llvm::Value *zero = ConstantInt::get(i32, 0);
        llvm::Value *field = ConstantInt::get(i32, 6);
        llvm::Value *idx = ConstantInt::get(i32, i);
        vector<llvm::Value *> args = vec(zero, field, idx);
        Value *ptr = builder.CreateGEP(buffer, args);
        return builder.CreateLoad(ptr);
    }

    Value *CodeGen::buffer_elem_size(Value *buffer) {
        Value *ptr = builder.CreateConstGEP2_32(buffer, 0, 7);
        return builder.CreateLoad(ptr);
    }

    llvm::Type *CodeGen::llvm_type_of(Type t) {
        if (t.width == 1) {
            if (t.is_float()) {
                switch (t.bits) {
                case 16:
                    return f16;
                case 32:
                    return f32;
                case 64:
                    return f64;
                default:
                    assert(false && "There is no llvm type matching this floating-point bit width");
                    return NULL;
                }
            } else {
                return llvm::Type::getIntNTy(context, t.bits);
            }
        } else {
            llvm::Type *element_type = llvm_type_of(Type::element_of(t));
            return VectorType::get(element_type, t.width);
        }
    }

    Value *CodeGen::codegen(Expr e) {
        assert(e.defined());
        std::cout << "Codegen: " << e.type() << ", " << e << std::endl;
        value = NULL;
        e.accept(this);
        assert(value && "Codegen of an expr did not produce an llvm value");
        return value;
    }

    void CodeGen::codegen(Stmt s) {
        assert(s.defined());
        std::cout << "Codegen: " << s;
        value = NULL;
        s.accept(this);
    }

    void CodeGen::visit(const IntImm *op) {
        value = ConstantInt::getSigned(i32, op->value);
    }

    void CodeGen::visit(const FloatImm *op) {
        value = ConstantFP::get(context, APFloat(op->value));
    }

    void CodeGen::visit(const Cast *op) {
        // do nothing for now
        value = codegen(op->value);
    }

    void CodeGen::visit(const Var *op) {
        // look in the symbol table
        value = symbol_table.get(op->name);
    }

    void CodeGen::visit(const Add *op) {
        if (op->type.is_float()) {
            value = builder.CreateFAdd(codegen(op->a), codegen(op->b));
        } else {
            value = builder.CreateAdd(codegen(op->a), codegen(op->b));
        }
    }

    void CodeGen::visit(const Sub *op) {
        if (op->type.is_float()) {
            value = builder.CreateFSub(codegen(op->a), codegen(op->b));
        } else {
            value = builder.CreateSub(codegen(op->a), codegen(op->b));
        }
    }

    void CodeGen::visit(const Mul *op) {
        if (op->type.is_float()) {
            value = builder.CreateFMul(codegen(op->a), codegen(op->b));
        } else {
            value = builder.CreateMul(codegen(op->a), codegen(op->b));
        }
    }

    void CodeGen::visit(const Div *op) {
        if (op->type.is_float()) {
            value = builder.CreateFDiv(codegen(op->a), codegen(op->b));
        } else if (op->type.is_uint()) {
            value = builder.CreateUDiv(codegen(op->a), codegen(op->b));
        } else {
            value = builder.CreateSDiv(codegen(op->a), codegen(op->b));
        }
    }

    void CodeGen::visit(const Mod *op) {
        Value *a = codegen(op->a);
        Value *b = codegen(op->b);

        if (op->type.is_int()) {
            value = builder.CreateFRem(a, b);
        } else if (op->type.is_uint()) {
            value = builder.CreateURem(a, b);
        } else {
            Expr modulus = op->b;
            const Broadcast *broadcast = modulus.as<Broadcast>();
            const IntImm *int_imm = broadcast ? broadcast->value.as<IntImm>() : modulus.as<IntImm>();
            if (int_imm) {
                // if we're modding by a power of two, we can use the unsigned version
                bool is_power_of_two = true;
                for (int v = int_imm->value; v > 1; v >>= 1) {
                    if (v & 1) is_power_of_two = false;
                }
                if (is_power_of_two) {
                    value = builder.CreateURem(a, b);
                    return;
                }
            }

            // to ensure the result of a signed mod is positive, we have to mod, add the modulus, then mod again
            value = builder.CreateSRem(a, b);
            value = builder.CreateAdd(value, b);
            value = builder.CreateSRem(value, b);
        }
    }

    void CodeGen::visit(const Min *op) {
        // Min and max should probably be overridden in an architecture-specific way
        value = codegen(new Select(op->a < op->b, op->a, op->b));
    }

    void CodeGen::visit(const Max *op) {
        value = codegen(new Select(op->a > op->b, op->a, op->b));
    }

    void CodeGen::visit(const EQ *op) {
        Value *a = codegen(op->a);
        Value *b = codegen(op->b);
        Type t = op->a.type();
        if (t.is_float()) {
            value = builder.CreateFCmpOEQ(a, b);
        } else {
            value = builder.CreateICmpEQ(a, b);
        }
    }

    void CodeGen::visit(const NE *op) {
        Value *a = codegen(op->a);
        Value *b = codegen(op->b);
        Type t = op->a.type();
        if (t.is_float()) {
            value = builder.CreateFCmpONE(a, b);
        } else {
            value = builder.CreateICmpNE(a, b);
        }
    }

    void CodeGen::visit(const LT *op) {
        Value *a = codegen(op->a);
        Value *b = codegen(op->b);
        Type t = op->a.type();
        if (t.is_float()) {
            value = builder.CreateFCmpOLT(a, b);
        } else if (t.is_int()) {
            value = builder.CreateICmpSLT(a, b);
        } else {
            value = builder.CreateICmpULT(a, b);
        }
    }

    void CodeGen::visit(const LE *op) {
        Value *a = codegen(op->a);
        Value *b = codegen(op->b);
        Type t = op->a.type();
        if (t.is_float()) {
            value = builder.CreateFCmpOLE(a, b);
        } else if (t.is_int()) {
            value = builder.CreateICmpSLE(a, b);
        } else {
            value = builder.CreateICmpULE(a, b);
        }
    }

    void CodeGen::visit(const GT *op) {
        Value *a = codegen(op->a);
        Value *b = codegen(op->b);
        Type t = op->a.type();
        if (t.is_float()) {
            value = builder.CreateFCmpOGT(a, b);
        } else if (t.is_int()) {
            value = builder.CreateICmpSGT(a, b);
        } else {
            value = builder.CreateICmpUGT(a, b);
        }
    }

    void CodeGen::visit(const GE *op) {
        Value *a = codegen(op->a);
        Value *b = codegen(op->b);
        Type t = op->a.type();
        if (t.is_float()) {
            value = builder.CreateFCmpOGE(a, b);
        } else if (t.is_int()) {
            value = builder.CreateICmpSGE(a, b);
        } else {
            value = builder.CreateICmpUGE(a, b);
        }
    }

    void CodeGen::visit(const And *op) {
        value = builder.CreateAnd(codegen(op->a), codegen(op->b));
    }

    void CodeGen::visit(const Or *op) {
        value = builder.CreateOr(codegen(op->a), codegen(op->b));
    }

    void CodeGen::visit(const Not *op) {
        value = builder.CreateNot(codegen(op->a));
    }

    void CodeGen::visit(const Select *op) {
        value = builder.CreateSelect(codegen(op->condition), 
                                     codegen(op->true_value), 
                                     codegen(op->false_value));
    }

    Value *CodeGen::codegen_buffer_pointer(string buffer, Type type, Value *index) {
        // Find the base address from the symbol table
        Value *base_address = symbol_table.get(buffer + ".host");
        llvm::Type *base_address_type = base_address->getType();
        unsigned address_space = base_address_type->getPointerAddressSpace();

        llvm::Type *load_type = llvm_type_of(type)->getPointerTo(address_space);

        // If the type doesn't match the expected type, we need to pointer cast
        if (load_type != base_address_type) {
            std::cout << "Bit-casting pointer type" << std::endl;
            base_address = builder.CreatePointerCast(base_address, load_type);            
        }

        return builder.CreateGEP(base_address, index);
    }


    void CodeGen::visit(const Load *op) {
        // There are several cases. Different architectures may wish to override some

        if (op->type.is_scalar()) {
            // 1) Scalar loads
            Value *index = codegen(op->index);
            Value *ptr = codegen_buffer_pointer(op->buffer, op->type, index);
            value = builder.CreateLoad(ptr);
        } else {
            // TODO 
            /*
            ModulusRemainder mod_rem(op);
            mod_rem.modulus;
            mod_rem.remainder;
            // 2) Aligned dense vector loads 
            
            // 3) Unaligned dense vector loads with known alignment
            
            // 4) Unaligned dense vector loads with unknown alignment
            
            // 5) General gathers
            */
            assert(false && "Vector loads not yet implemented");            
        }

    }

    void CodeGen::visit(const Ramp *op) {        
        assert(false && "Ramp not yet implemented");
    }

    void CodeGen::visit(const Call *op) {
        assert(op->call_type == Call::Extern && "Can only codegen extern calls");

        // First, codegen the args
        vector<Value *> args(op->args.size());
        for (size_t i = 0; i < op->args.size(); i++) {
            args[i] = codegen(op->args[i]);
        }

        Function *fn = module->getFunction(op->name);
        
        llvm::Type *result_type = llvm_type_of(op->type);

        // If we can't find it, declare it extern "C"
        if (!fn) {
            std::cout << "Didn't find " << op->name << " in initial module. Assuming it's extern." << std::endl;
            vector<llvm::Type *> arg_types(args.size());
            for (size_t i = 0; i < args.size(); i++) {
                arg_types[i] = args[i]->getType();
            }
            FunctionType *func_t = FunctionType::get(result_type, arg_types, false);
            
            fn = Function::Create(func_t, Function::ExternalLinkage, op->name, module);
            fn->setCallingConv(CallingConv::C);            
        }

        if (op->type.is_scalar()) {
            value = builder.CreateCall(fn, args);
        } else {
            // Check if a vector version of the function already
            // exists. We use the naming convention that a N-wide
            // version of a function foo is called fooxN.
            ostringstream ss;
            ss << op->name << 'x' << op->type.width;
            Function *vec_fn = module->getFunction(ss.str());
            if (vec_fn) {
                value = builder.CreateCall(vec_fn, args);
                fn = vec_fn;
            } else {
                // Scalarize. Extract each simd lane in turn and do
                // one scalar call to the function.
                value = UndefValue::get(result_type);
                for (int i = 0; i < op->type.width; i++) {
                    Value *idx = ConstantInt::get(i32, i);
                    vector<Value *> arg_lane(args.size());
                    for (size_t j = 0; j < args.size(); j++) {
                        arg_lane[j] = builder.CreateExtractElement(args[j], idx);
                    }
                    Value *result_lane = builder.CreateCall(fn, arg_lane);
                    value = builder.CreateInsertElement(value, result_lane, idx);
                }
            }            
        }
    }

    void CodeGen::visit(const Let *op) {
        sym_push(op->name, codegen(op->value));
        value = codegen(op->body);
        symbol_table.pop(op->name);
    }

    void CodeGen::visit(const LetStmt *op) {
        sym_push(op->name, codegen(op->value));
        codegen(op->body);
        symbol_table.pop(op->name);
    }

    void CodeGen::visit(const PrintStmt *op) {
        assert(false && "PrintStmt not yet implemented");
    }

    void CodeGen::visit(const AssertStmt *op) {
        assert(false && "AssertStmt not yet implemented");
    }

    void CodeGen::visit(const Pipeline *op) {
        codegen(op->produce);
        if (op->update.defined()) codegen(op->update);
        codegen(op->consume);
    }

    /* A helper class to manage closures - used for parallel for loops */
    class CodeGen::Closure : public IRVisitor {
    private:
        map<string, llvm::Type *> result;
        Scope<int> ignore;
        CodeGen *gen;

        void visit(const Let *op) {
            ignore.push(op->name, 0);
            op->body.accept(this);
            ignore.pop(op->name);
        }
        void visit(const LetStmt *op) {
            ignore.push(op->name, 0);
            op->body.accept(this);
            ignore.pop(op->name);
        }
        void visit(const For *op) {
            ignore.push(op->name, 0);
            op->min.accept(this);
            op->extent.accept(this);
            op->body.accept(this);
            ignore.pop(op->name);
        }
        void visit(const Load *op) {
            op->index.accept(this);
            result[op->buffer + ".host"] = gen->llvm_type_of(op->type)->getPointerTo();
        }
        void visit(const Store *op) {
            op->index.accept(this);
            op->value.accept(this);
            result[op->buffer + ".host"] = gen->llvm_type_of(op->value.type())->getPointerTo();
        }
        void visit(const Var *op) {            
            if (ignore.contains(op->name)) {
                //std::cout << "Ignoring reference to: " << op->name << std::endl;
            } else {
                //std::cout << "Putting var in closure: " << op->name << std::endl;
                result[op->name] = gen->llvm_type_of(op->type);
            }
        }

    public:
        Closure(Stmt s, CodeGen *g, const string &loop_variable) : gen(g) {
            ignore.push(loop_variable, 0);
            s.accept(this);
        }

        StructType *build_type() {
            StructType *struct_t = StructType::create(gen->context, "closure_t");
            vector<llvm::Type *> fields;
            for (map<string, llvm::Type *>::const_iterator iter = result.begin(); 
                 iter != result.end(); ++iter) {
                fields.push_back(iter->second);
            }
            struct_t->setBody(fields, false);
            return struct_t;
        }

        void pack_struct(Value *dst, const Scope<Value *> &src, IRBuilder<> &builder) {
            // dst should be a pointer to a struct of the type returned by build_type
            int idx = 0;
            for (map<string, llvm::Type *>::const_iterator iter = result.begin(); 
                 iter != result.end(); ++iter) {
                // std::cout << "Putting " << iter->first << " in closure" << std::endl;
                Value *val = src.get(iter->first);
                Value *ptr = builder.CreateConstGEP2_32(dst, 0, idx++);
                if (val->getType() != iter->second) {
                    val = builder.CreateBitCast(val, iter->second);
                }
                builder.CreateStore(val, ptr);
            }
        }

        void unpack_struct(Scope<Value *> &dst, Value *src, IRBuilder<> &builder) {
            // src should be a pointer to a struct of the type returned by build_type
            int idx = 0;
            for (map<string, llvm::Type *>::const_iterator iter = result.begin(); 
                 iter != result.end(); ++iter) {
                Value *ptr = builder.CreateConstGEP2_32(src, 0, idx++);
                Value *val = builder.CreateLoad(ptr);
                dst.push(iter->first, val);
                val->setName(iter->first);
            }
        }
    };

    void CodeGen::visit(const For *op) {
        Value *min = codegen(op->min);
        Value *extent = codegen(op->extent);
        
        if (op->for_type == For::Serial) {
            Value *max = builder.CreateAdd(min, extent);
            
            BasicBlock *preheader_bb = builder.GetInsertBlock();

            // Make a new basic block for the loop
            BasicBlock *loop_bb = BasicBlock::Create(context, op->name + "_loop", function);

            // Fall through to the loop bb
            builder.CreateBr(loop_bb);
            builder.SetInsertPoint(loop_bb);

            // Make our phi node
            PHINode *phi = builder.CreatePHI(i32, 2);
            phi->addIncoming(min, preheader_bb);

            // Within the loop, the variable is equal to the phi value
            sym_push(op->name, phi);

            // Emit the loop body
            codegen(op->body);

            // Update the counter
            Value *next_var = builder.CreateAdd(phi, ConstantInt::get(i32, 1));

            // Create the block that comes after the loop
            BasicBlock *after_bb = BasicBlock::Create(context, op->name + "_after_loop", function);

            // Add the back-edge to the phi node
            phi->addIncoming(next_var, builder.GetInsertBlock());

            // Maybe exit the loop
            Value *end_condition = builder.CreateICmpNE(next_var, max);
            builder.CreateCondBr(end_condition, loop_bb, after_bb);
            builder.SetInsertPoint(after_bb);

            // Pop the loop variable from the scope
            symbol_table.pop(op->name);
        } else if (op->for_type == For::Parallel) {

            // Find every symbol that the body of this loop refers to
            // and dump it into a closure
            Closure closure(op->body, this, op->name);

            // Allocate a closure
            StructType *closure_t = closure.build_type();
            Value *ptr = builder.CreateAlloca(closure_t, ConstantInt::get(i32, 1)); 

            // Fill in the closure
            closure.pack_struct(ptr, symbol_table, builder);

            // Make a new function that does one iteration of the body of the loop
            FunctionType *func_t = FunctionType::get(void_t, vec(i32, (llvm::Type *)(i8->getPointerTo())), false);
            Function *containing_function = function;
            function = Function::Create(func_t, Function::InternalLinkage, "par_for_" + op->name, module);

            // Make the initial basic block and jump the builder into the new function
            BasicBlock *call_site = builder.GetInsertBlock();
            BasicBlock *block = BasicBlock::Create(context, "entry", function);
            builder.SetInsertPoint(block);

            // Make a new scope to use
            Scope<Value *> saved_symbol_table;
            std::swap(symbol_table, saved_symbol_table);

            // Get the function arguments

            // The loop variable is first argument of the function
            Function::arg_iterator iter = function->arg_begin();
            sym_push(op->name, iter);

            // The closure pointer is the second argument. 
            ++iter;
            iter->setName("closure");
            Value *closure_handle = builder.CreatePointerCast(iter, closure_t->getPointerTo());
            // Load everything from the closure into the new scope
            closure.unpack_struct(symbol_table, closure_handle, builder);
            
            // Generate the new function body
            codegen(op->body);
            builder.CreateRetVoid();

            // Move the builder back to the main function and call do_par_for
            builder.SetInsertPoint(call_site);
            Function *do_par_for = module->getFunction("do_par_for");
            assert(do_par_for && "Could not find do_par_for in initial module");
            ptr = builder.CreatePointerCast(ptr, i8->getPointerTo());
            vector<Value *> args = vec((Value *)function, min, extent, ptr);
            builder.CreateCall(do_par_for, args);

            // Now restore the scope
            std::swap(symbol_table, saved_symbol_table);
            function = containing_function;
        } else {
            assert(false && "Unknown type of For node. Only Serial and Parallel For nodes should survive down to codegen");
        }
    }

    void CodeGen::visit(const Store *op) {
        Value *v = codegen(op->value);
        // Scalar
        if (op->index.type().is_scalar()) {
            Value *index = codegen(op->index);
            Value *ptr = codegen_buffer_pointer(op->buffer, op->value.type(), index);
            builder.CreateStore(v, ptr);
        } else {
            // Aligned dense vector store
                        
            // Scatter
            assert(false && "Vector store not yet implemented");
        }
    }

    void CodeGen::visit(const Block *op) {
        codegen(op->first);
        if (op->rest.defined()) codegen(op->rest);
    }        

    void CodeGen::visit(const Realize *op) {
        assert(false && "Realize encountered during codegen");
    }

    void CodeGen::visit(const Provide *op) {
        assert(false && "Provide encountered during codegen");
    }

}
