#include "CodeGen_Internal.h"
#include "Log.h"

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
        log(3) << "Adding " << op->name << " to closure\n";
        reads[op->name] = op->type;
    } else {
        log(3) << "Not adding " << op->name << " to closure\n";
    }
}

void Closure::visit(const Store *op) {
    op->index.accept(this);
    op->value.accept(this);
    if (!ignore.contains(op->name)) {
        log(3) << "Adding " << op->name << " to closure\n";
        writes[op->name] = op->value.type();
    } else {
        log(3) << "Not adding " << op->name << " to closure\n";
    }
}

void Closure::visit(const Allocate *op) {
    ignore.push(op->name, 0);
    op->size.accept(this);
    op->body.accept(this);
    ignore.pop(op->name);
}

void Closure::visit(const Variable *op) {            
    if (ignore.contains(op->name)) {
        log(3) << "Not adding " << op->name << " to closure\n";
    } else {
        log(3) << "Adding " << op->name << " to closure\n";
        vars[op->name] = op->type;
    }
}

Closure Closure::make(Stmt s, const string &loop_variable, bool track_buffers, llvm::StructType *buffer_t) {
    Closure c;
    c.buffer_t = buffer_t;
    c.track_buffers = track_buffers;
    c.ignore.push(loop_variable, 0);
    s.accept(&c);
    return c;
}

vector<llvm::Type*> Closure::llvm_types(LLVMContext *context) {
    vector<llvm::Type *> res;
    map<string, Type>::const_iterator iter;
    for (iter = vars.begin(); iter != vars.end(); ++iter) {
        res.push_back(llvm_type_of(context, iter->second));
    }
    for (iter = reads.begin(); iter != reads.end(); ++iter) {
        res.push_back(llvm_type_of(context, iter->second)->getPointerTo());
        // Some backends (ptx) track more than a host pointer
        if (track_buffers) {
            res.push_back(buffer_t->getPointerTo());
        }
    }
    for (iter = writes.begin(); iter != writes.end(); ++iter) {
        res.push_back(llvm_type_of(context, iter->second)->getPointerTo());
        if (track_buffers) {
            res.push_back(buffer_t->getPointerTo());
        }
    }
    return res;
}

vector<string> Closure::names() {
    vector<string> res;
    map<string, Type>::const_iterator iter;
    for (iter = vars.begin(); iter != vars.end(); ++iter) {
        log(2) << "vars:  " << iter->first << "\n";
        res.push_back(iter->first);
    }
    for (iter = reads.begin(); iter != reads.end(); ++iter) {
        log(2) << "reads: " << iter->first << "\n";
        res.push_back(iter->first + ".host");
        // Some backends (ptx) track a whole buffer as well as a host pointer
        if (track_buffers) res.push_back(iter->first + ".buffer");
    }
    for (iter = writes.begin(); iter != writes.end(); ++iter) {
        log(2) << "writes: " << iter->first << "\n";
        res.push_back(iter->first + ".host");
        if (track_buffers) res.push_back(iter->first + ".buffer");
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
        if (!ends_with(nm[i], ".buffer") || src.contains(nm[i])) {
            Value *val = src.get(nm[i]);
            Value *ptr = builder->CreateConstInBoundsGEP2_32(dst, 0, idx);
            if (val->getType() != ty[i]) {
                val = builder->CreateBitCast(val, ty[i]);
            }            
            builder->CreateStore(val, ptr);
        }
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
                                                  vec<Value *>(MDString::get(context, nm[i]))));
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
                assert(false && "There is no llvm type matching this floating-point bit width");
                return NULL;
            }
        } else {
            return llvm::Type::getIntNTy(*c, t.bits);
        }
    } else {
        llvm::Type *element_type = llvm_type_of(c, t.element_of());
        return VectorType::get(element_type, t.width);
    }
}

}
}
