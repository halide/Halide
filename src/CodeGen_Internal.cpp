#include "CodeGen_Internal.h"
#include "Debug.h"

namespace Halide {
namespace Internal {

using std::string;
using std::map;
using std::vector;

using namespace llvm;

void Closure::visit(const Let *op) {
    op->value.accept(this);
    ignore.push(op->name, 0);
    op->body.accept(this);
    ignore.pop(op->name);
}

void Closure::visit(const LetStmt *op) {
    op->value.accept(this);
    ignore.push(op->name, 0);
    op->body.accept(this);
    ignore.pop(op->name);
}

void Closure::visit(const For *op) {
    ignore.push(op->name, 0);
    op->min.accept(this);
    op->extent.accept(this);
    op->body.accept(this);
    ignore.pop(op->name);
}

void Closure::visit(const Load *op) {
    op->index.accept(this);
    if (!ignore.contains(op->name)) {
        debug(3) << "Adding buffer " << op->name << " to closure\n";
        BufferRef & ref = buffers[op->name];
        ref.type = op->type; // TODO: Validate type is the same as existing refs?
        ref.read = true;

        // If reading an image/buffer, compute the size.
        if (op->image.defined()) {
            ref.dimensions = op->image.dimensions();
            ref.size = 1;
            for (int i = 0; i < op->image.dimensions(); i++) {
                ref.size += (op->image.extent(i) - 1)*op->image.stride(i);
            }
            ref.size *= op->image.type().bytes();
        }
    } else {
        debug(3) << "Not adding " << op->name << " to closure\n";
    }
}

void Closure::visit(const Store *op) {
    op->index.accept(this);
    op->value.accept(this);
    if (!ignore.contains(op->name)) {
        debug(3) << "Adding buffer " << op->name << " to closure\n";
        BufferRef & ref = buffers[op->name];
        ref.type = op->value.type(); // TODO: Validate type is the same as existing refs?
        // TODO: do we need to set ref.dimensions?
        ref.write = true;
    } else {
        debug(3) << "Not adding " << op->name << " to closure\n";
    }
}

void Closure::visit(const Allocate *op) {
    ignore.push(op->name, 0);
    for (size_t i = 0; i < op->extents.size(); i++) {
        op->extents[i].accept(this);
    }
    op->body.accept(this);
    ignore.pop(op->name);
}

void Closure::visit(const Variable *op) {
    if (ignore.contains(op->name)) {
        debug(3) << "Not adding " << op->name << " to closure\n";
    } else {
        debug(3) << "Adding " << op->name << " to closure\n";
        vars[op->name] = op->type;
    }
}

Closure::Closure(Stmt s, const string &loop_variable, llvm::StructType *buffer_t) : buffer_t(buffer_t) {
    ignore.push(loop_variable, 0);
    s.accept(this);
}

vector<llvm::Type*> Closure::llvm_types(LLVMContext *context) {
    vector<llvm::Type *> res;
    for (map<string, Type>::const_iterator iter = vars.begin(); iter != vars.end(); ++iter) {
        res.push_back(llvm_type_of(context, iter->second));
    }
    for (map<string, BufferRef>::const_iterator iter = buffers.begin(); iter != buffers.end(); ++iter) {
        res.push_back(llvm_type_of(context, iter->second.type)->getPointerTo());
        res.push_back(buffer_t->getPointerTo());
    }
    return res;
}

vector<string> Closure::names() {
    vector<string> res;
    for (map<string, Type>::const_iterator iter = vars.begin(); iter != vars.end(); ++iter) {
        debug(2) << "vars:  " << iter->first << "\n";
        res.push_back(iter->first);
    }
    for (map<string, BufferRef>::const_iterator iter = buffers.begin(); iter != buffers.end(); ++iter) {
        debug(2) << "buffers: " << iter->first << "\n";
        res.push_back(iter->first + ".host");
        res.push_back(iter->first + ".buffer");
    }
    return res;
}

StructType *Closure::build_type(LLVMContext *context) {
    StructType *struct_t = StructType::create(*context, "closure_t");
    struct_t->setBody(llvm_types(context), false);
    return struct_t;
}

void Closure::pack_struct(Value *dst, const Scope<Value *> &src, IRBuilder<> *builder) {
    // dst should be a pointer to a struct of the type returned by build_type
    int idx = 0;
    LLVMContext &context = builder->getContext();
    vector<string> nm = names();
    vector<llvm::Type*> ty = llvm_types(&context);
    for (size_t i = 0; i < nm.size(); i++) {
        Value *ptr = builder->CreateConstInBoundsGEP2_32(dst, 0, idx);
        Value *val;
        if (!ends_with(nm[i], ".buffer") || src.contains(nm[i])) {
            val = src.get(nm[i]);
            if (val->getType() != ty[i]) {
                val = builder->CreateBitCast(val, ty[i]);
            }
        } else {
            // Skip over buffers not in the symbol table. They must not be needed.
            val = ConstantPointerNull::get(buffer_t->getPointerTo());
        }
        builder->CreateStore(val, ptr);
        idx++;
    }
}

void Closure::unpack_struct(Scope<Value *> &dst,
                            Value *src,
                            IRBuilder<> *builder) {
    // src should be a pointer to a struct of the type returned by build_type
    int idx = 0;
    LLVMContext &context = builder->getContext();
    vector<string> nm = names();
    for (size_t i = 0; i < nm.size(); i++) {
        Value *ptr = builder->CreateConstInBoundsGEP2_32(src, 0, idx++);
        LoadInst *load = builder->CreateLoad(ptr);
        if (load->getType()->isPointerTy()) {
            // Give it a unique type so that tbaa tells llvm that this can't alias anything
            load->setMetadata("tbaa", MDNode::get(context,
                                                  vec<LLVMMDNodeArgumentType>(MDString::get(context, nm[i]))));
        }
        dst.push(nm[i], load);
        load->setName(nm[i]);
    }
}

llvm::Type *llvm_type_of(LLVMContext *c, Halide::Type t) {
    if (t.width == 1) {
        if (t.is_float()) {
            switch (t.bits) {
            case 16:
                return llvm::Type::getHalfTy(*c);
            case 32:
                return llvm::Type::getFloatTy(*c);
            case 64:
                return llvm::Type::getDoubleTy(*c);
            default:
                internal_error << "There is no llvm type matching this floating-point bit width: " << t << "\n";
                return NULL;
            }
        } else if (t.is_handle()) {
            return llvm::Type::getInt8PtrTy(*c);
        } else {
            return llvm::Type::getIntNTy(*c, t.bits);
        }
    } else {
        llvm::Type *element_type = llvm_type_of(c, t.element_of());
        return VectorType::get(element_type, t.width);
    }
}

bool constant_allocation_size(const std::vector<Expr> &extents, const std::string &name, int32_t &size) {
    int64_t result = 1;

    for (size_t i = 0; i < extents.size(); i++) {
        if (const IntImm *int_size = extents[i].as<IntImm>()) {
            // Check if the individual dimension is > 2^31 - 1. Not
            // currently necessary because it's an int32_t, which is
            // always smaller than 2^31 - 1. If we ever upgrade the
            // type of IntImm but not the maximum allocation size, we
            // should re-enable this.
            /*
            if ((int64_t)int_size->value > (((int64_t)(1)<<31) - 1)) {
                user_error
                    << "Dimension " << i << " for allocation " << name << " has size " <<
                    int_size->value << " which is greater than 2^31 - 1.";
            }
            */
            result *= int_size->value;
            if (result > (static_cast<int64_t>(1)<<31) - 1) {
                user_error
                    << "Total size for allocation " << name
                    << " is constant but exceeds 2^31 - 1.\n";
            }
        } else {
            return false;
        }
    }

    size = static_cast<int32_t>(result);
    return true;
}

}
}
