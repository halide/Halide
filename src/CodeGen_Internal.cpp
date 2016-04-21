#include "CodeGen_Internal.h"
#include "IROperator.h"
#include "CSE.h"
#include "Debug.h"

namespace Halide {
namespace Internal {

using std::string;
using std::map;
using std::vector;
using std::pair;

using namespace llvm;

namespace {

vector<llvm::Type*> llvm_types(const Closure& closure, llvm::StructType *buffer_t, LLVMContext &context) {
    vector<llvm::Type *> res;
    for (const pair<string, Type> &i : closure.vars) {
        res.push_back(llvm_type_of(&context, i.second));
    }
    for (const pair<string, Closure::BufferRef> &i : closure.buffers) {
        res.push_back(llvm_type_of(&context, i.second.type)->getPointerTo());
        res.push_back(buffer_t->getPointerTo());
    }
    return res;
}

}  // namespace

StructType *build_closure_type(const Closure& closure, llvm::StructType *buffer_t, LLVMContext *context) {
    StructType *struct_t = StructType::create(*context, "closure_t");
    struct_t->setBody(llvm_types(closure, buffer_t, *context), false);
    return struct_t;
}

void pack_closure(llvm::Type *
#if LLVM_VERSION >= 37
                  type
#endif
                  ,
                  Value *dst,
                  const Closure& closure,
                  const Scope<Value *> &src,
                  llvm::StructType *buffer_t,
                  IRBuilder<> *builder) {
    // type, type of dst should be a pointer to a struct of the type returned by build_type
    int idx = 0;
    LLVMContext &context = builder->getContext();
    vector<string> nm = closure.names();
    vector<llvm::Type*> ty = llvm_types(closure, buffer_t, context);
    for (size_t i = 0; i < nm.size(); i++) {
#if LLVM_VERSION >= 37
        Value *ptr = builder->CreateConstInBoundsGEP2_32(type, dst, 0, idx);
#else
        Value *ptr = builder->CreateConstInBoundsGEP2_32(dst, 0, idx);
#endif
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

void unpack_closure(const Closure& closure,
                    Scope<Value *> &dst,
                    llvm::Type *
#if LLVM_VERSION >= 37
                    type
#endif
                    ,
                    Value *src,
                    IRBuilder<> *builder) {
    // type, type of src should be a pointer to a struct of the type returned by build_type
    int idx = 0;
    LLVMContext &context = builder->getContext();
    vector<string> nm = closure.names();
    for (size_t i = 0; i < nm.size(); i++) {
#if LLVM_VERSION >= 37
        Value *ptr = builder->CreateConstInBoundsGEP2_32(type, src, 0, idx++);
#else
        Value *ptr = builder->CreateConstInBoundsGEP2_32(src, 0, idx++);
#endif
        LoadInst *load = builder->CreateLoad(ptr);
        if (load->getType()->isPointerTy()) {
            // Give it a unique type so that tbaa tells llvm that this can't alias anything
            LLVMMDNodeArgumentType md_args[] = {MDString::get(context, nm[i])};
            load->setMetadata("tbaa", MDNode::get(context, md_args));
        }
        dst.push(nm[i], load);
        load->setName(nm[i]);
    }
}

llvm::Type *llvm_type_of(LLVMContext *c, Halide::Type t) {
    if (t.lanes() == 1) {
        if (t.is_float()) {
            switch (t.bits()) {
            case 16:
                return llvm::Type::getHalfTy(*c);
            case 32:
                return llvm::Type::getFloatTy(*c);
            case 64:
                return llvm::Type::getDoubleTy(*c);
            default:
                internal_error << "There is no llvm type matching this floating-point bit width: " << t << "\n";
                return nullptr;
            }
        } else if (t.is_handle()) {
            return llvm::Type::getInt8PtrTy(*c);
        } else {
            return llvm::Type::getIntNTy(*c, t.bits());
        }
    } else {
        llvm::Type *element_type = llvm_type_of(c, t.element_of());
        return VectorType::get(element_type, t.lanes());
    }
}

// Returns true if the given function name is one of the Halide runtime
// functions that takes a user_context pointer as its first parameter.
bool function_takes_user_context(const std::string &name) {
    static const char *user_context_runtime_funcs[] = {
        "halide_copy_to_host",
        "halide_copy_to_device",
        "halide_current_time_ns",
        "halide_debug_to_file",
        "halide_device_free",
        "halide_device_malloc",
        "halide_device_sync",
        "halide_do_par_for",
        "halide_do_task",
        "halide_error",
        "halide_free",
        "halide_malloc",
        "halide_print",
        "halide_profiler_memory_allocate",
        "halide_profiler_memory_free",
        "halide_profiler_pipeline_start",
        "halide_profiler_pipeline_end",
        "halide_profiler_stack_peak_update",
        "halide_spawn_thread",
        "halide_device_release",
        "halide_start_clock",
        "halide_trace",
        "halide_memoization_cache_lookup",
        "halide_memoization_cache_store",
        "halide_memoization_cache_release",
        "halide_cuda_run",
        "halide_opencl_run",
        "halide_opengl_run",
        "halide_openglcompute_run",
        "halide_renderscript_run",
        "halide_metal_run",
        "halide_cuda_initialize_kernels",
        "halide_opencl_initialize_kernels",
        "halide_opengl_initialize_kernels",
        "halide_openglcompute_initialize_kernels",
        "halide_renderscript_initialize_kernels",
        "halide_metal_initialize_kernels",
        "halide_get_gpu_device",
    };
    const int num_funcs = sizeof(user_context_runtime_funcs) /
        sizeof(user_context_runtime_funcs[0]);
    for (int i = 0; i < num_funcs; ++i) {
        if (name == user_context_runtime_funcs[i]) {
            return true;
        }
    }
    // The error functions all take a user context
    return starts_with(name, "halide_error_");
}

bool can_allocation_fit_on_stack(int32_t size) {
    user_assert(size > 0) << "Allocation size should be a positive number\n";
    return (size <= 1024 * 16);
}

Expr lower_euclidean_div(Expr a, Expr b) {
    internal_assert(a.type() == b.type());
    // IROperator's div_round_to_zero will replace this with a / b for
    // unsigned ops, so create the intrinsic directly.
    Expr q = Call::make(a.type(), Call::div_round_to_zero, {a, b}, Call::PureIntrinsic);
    if (a.type().is_int()) {
        // Signed integer division sucks. It should be defined such
        // that it satisifies (a/b)*b + a%b = a, where 0 <= a%b < |b|,
        // i.e. Euclidean division.

        // We get rounding to work by examining the implied remainder
        // and correcting the quotient.

        /* Here's the C code that we're trying to match:
           int q = a / b;
           int r = a - q * b;
           int bs = b >> (t.bits() - 1);
           int rs = r >> (t.bits() - 1);
           return q - (rs & bs) + (rs & ~bs);
        */

        Expr r = a - q*b;
        Expr bs = b >> (a.type().bits() - 1);
        Expr rs = r >> (a.type().bits() - 1);
        q = q - (rs & bs) + (rs & ~bs);
        return common_subexpression_elimination(q);
    } else {
        return q;
    }
}

Expr lower_euclidean_mod(Expr a, Expr b) {
    internal_assert(a.type() == b.type());
    // IROperator's mod_round_to_zero will replace this with a % b for
    // unsigned ops, so create the intrinsic directly.
    Expr r = Call::make(a.type(), Call::mod_round_to_zero, {a, b}, Call::PureIntrinsic);
    if (a.type().is_int()) {
        // Match this non-overflowing C code
        /*
          T r = a % b;
          r = r + (r < 0 ? abs(b) : 0);
        */

        r = select(r < 0, r + abs(b), r);
        return common_subexpression_elimination(r);
    } else {
        return r;
    }
}

}
}
