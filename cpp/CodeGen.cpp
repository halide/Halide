#include "CodeGen.h"
#include "llvm/Analysis/Verifier.h"
#include "IROperator.h"

namespace HalideInternal {

    using namespace llvm;

    Value *SymbolTable::get(string name) {
        map<string, stack<Value *> >::iterator iter = table.find(name);
        assert(iter != table.end() && "Symbol not found");
        return iter->second.top();
    }

    void SymbolTable::push(string name, Value *value) {
        table[name].push(value);
    }

    void SymbolTable::pop(string name) {
        assert(!table[name].empty() && "Name not in symbol table");
        table[name].pop();
    }

    CodeGen::CodeGen(Stmt stmt, string name, string target_triple) : builder(context) {
        module = new Module(name.c_str(), context);

        llvm::Type *void_t = llvm::Type::getVoidTy(context);
        FunctionType *func_t = FunctionType::get(void_t, void_t, false);
        function = Function::Create(func_t, Function::ExternalLinkage, name, module);

        block = BasicBlock::Create(context, "entry", function);
        builder.SetInsertPoint(block);

        // Ok, we have a module, function, context, and a builder
        // pointing at a brand new basic block. We're good to go.
        stmt.accept(this);

        // Now verify the function is ok
        llvm::verifyFunction(*function);
    }

    llvm::Type *CodeGen::llvm_type_of(Type t) {
        if (t.width == 1) {
            if (t.is_float()) {
                switch (t.bits) {
                case 16:
                    return llvm::Type::getHalfTy(context);
                case 32:
                    return llvm::Type::getFloatTy(context);
                case 64:
                    return llvm::Type::getDoubleTy(context);
                default:
                    assert(false && "There is no llvm type matching this floating-point bit width");
                    return NULL;
                }
            } else {
                return llvm::Type::getIntNTy(context, t.width);
            }
        } else {
            llvm::Type *element_type = llvm_type_of(Type::element_of(t));
            return VectorType::get(element_type, t.width);
        }
    }

    Value *CodeGen::codegen(Expr e) {
        value = NULL;
        e.accept(this);
        assert(value && "Codegen of an expr did not produce an llvm value");
        return value;
    }

    void CodeGen::codegen(Stmt s) {
        value = NULL;
        s.accept(this);
    }

    void CodeGen::visit(const IntImm *op) {
        IntegerType *i32_t = llvm::Type::getInt32Ty(context);
        value = ConstantInt::getSigned(i32_t, op->value);
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
        if (op->type.is_float()) {
            value = builder.CreateFCmpOEQ(a, b);
        } else {
            value = builder.CreateICmpEQ(a, b);
        }
    }

    void CodeGen::visit(const NE *op) {
        Value *a = codegen(op->a);
        Value *b = codegen(op->b);
        if (op->type.is_float()) {
            value = builder.CreateFCmpONE(a, b);
        } else {
            value = builder.CreateICmpNE(a, b);
        }
    }

    void CodeGen::visit(const LT *op) {
        Value *a = codegen(op->a);
        Value *b = codegen(op->b);
        if (op->type.is_float()) {
            value = builder.CreateFCmpOLT(a, b);
        } else if (op->type.is_int()) {
            value = builder.CreateICmpSLT(a, b);
        } else {
            value = builder.CreateICmpULT(a, b);
        }
    }

    void CodeGen::visit(const LE *op) {
        Value *a = codegen(op->a);
        Value *b = codegen(op->b);
        if (op->type.is_float()) {
            value = builder.CreateFCmpOLE(a, b);
        } else if (op->type.is_int()) {
            value = builder.CreateICmpSLE(a, b);
        } else {
            value = builder.CreateICmpULE(a, b);
        }
    }

    void CodeGen::visit(const GT *op) {
        Value *a = codegen(op->a);
        Value *b = codegen(op->b);
        if (op->type.is_float()) {
            value = builder.CreateFCmpOGT(a, b);
        } else if (op->type.is_int()) {
            value = builder.CreateICmpSGT(a, b);
        } else {
            value = builder.CreateICmpUGT(a, b);
        }
    }

    void CodeGen::visit(const GE *op) {
        Value *a = codegen(op->a);
        Value *b = codegen(op->b);
        if (op->type.is_float()) {
            value = builder.CreateFCmpOGE(a, b);
        } else if (op->type.is_int()) {
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
        Value *base_address = symbol_table.get(buffer);
        llvm::Type *base_address_type = base_address->getType();
        unsigned address_space = base_address_type->getPointerAddressSpace();

        llvm::Type *load_type = llvm_type_of(type)->getPointerTo(address_space);

        // If the type doesn't match the expected type, we need to pointer cast
        if (load_type != base_address_type) {
            printf("Bit-casting pointer type\n");
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
    }

    void CodeGen::visit(const Call *op) {
        assert(op->call_type == Call::Extern && "Can only codegen extern calls");
        // ugh...
    }

    void CodeGen::visit(const Let *op) {
    }

    void CodeGen::visit(const LetStmt *op) {
    }

    void CodeGen::visit(const PrintStmt *op) {
    }

    void CodeGen::visit(const AssertStmt *op) {
    }

    void CodeGen::visit(const Pipeline *op) {
    }

    void CodeGen::visit(const For *op) {
    }

    void CodeGen::visit(const Store *op) {
    }

    void CodeGen::visit(const Allocate *op) {
    }

    void CodeGen::visit(const Block *op) {
    }        

    void CodeGen::visit(const Realize *op) {
        assert(false && "Realize encountered during codegen");
    }

    void CodeGen::visit(const Provide *op) {
        assert(false && "Provide encountered during codegen");
    }

    void CodeGen::test() {
        // corner cases to test:
        // signed mod by power of two, non-power of two
        // loads of mismatched types (e.g. load a float from something allocated as an array of ints)
    }
}
