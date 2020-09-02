#include "CodeGen_Internal.h"
#include "CSE.h"
#include "Debug.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IntegerDivisionTable.h"
#include "LLVM_Headers.h"
#include "Simplify.h"
#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

using namespace llvm;

namespace {

vector<llvm::Type *> llvm_types(const Closure &closure, llvm::StructType *halide_buffer_t_type, LLVMContext &context) {
    vector<llvm::Type *> res;
    for (const auto &v : closure.vars) {
        res.push_back(llvm_type_of(&context, v.second));
    }
    for (const auto &b : closure.buffers) {
        res.push_back(llvm_type_of(&context, b.second.type)->getPointerTo());
        res.push_back(halide_buffer_t_type->getPointerTo());
    }
    return res;
}

}  // namespace

StructType *build_closure_type(const Closure &closure,
                               llvm::StructType *halide_buffer_t_type,
                               LLVMContext *context) {
    StructType *struct_t = StructType::create(*context, "closure_t");
    struct_t->setBody(llvm_types(closure, halide_buffer_t_type, *context), false);
    return struct_t;
}

void pack_closure(llvm::StructType *type,
                  Value *dst,
                  const Closure &closure,
                  const Scope<Value *> &src,
                  llvm::StructType *halide_buffer_t_type,
                  IRBuilder<> *builder) {
    // type, type of dst should be a pointer to a struct of the type returned by build_type
    int idx = 0;
    for (const auto &v : closure.vars) {
        llvm::Type *t = type->elements()[idx];
        Value *ptr = builder->CreateConstInBoundsGEP2_32(type, dst, 0, idx++);
        Value *val = src.get(v.first);
        val = builder->CreateBitCast(val, t);
        builder->CreateStore(val, ptr);
    }
    for (const auto &b : closure.buffers) {
        // For buffers we pass through base address (the symbol with
        // the same name as the buffer), and the .buffer symbol (GPU
        // code might implicitly need it).
        // FIXME: This dependence should be explicitly encoded in the IR.
        {
            llvm::Type *t = type->elements()[idx];
            Value *ptr = builder->CreateConstInBoundsGEP2_32(type, dst, 0, idx++);
            Value *val = src.get(b.first);
            val = builder->CreateBitCast(val, t);
            builder->CreateStore(val, ptr);
        }
        {
            llvm::PointerType *t = halide_buffer_t_type->getPointerTo();
            Value *ptr = builder->CreateConstInBoundsGEP2_32(type, dst, 0, idx++);
            Value *val = nullptr;
            if (src.contains(b.first + ".buffer")) {
                val = src.get(b.first + ".buffer");
                val = builder->CreateBitCast(val, t);
            } else {
                val = ConstantPointerNull::get(t);
            }
            builder->CreateStore(val, ptr);
        }
    }
}

void unpack_closure(const Closure &closure,
                    Scope<Value *> &dst,
                    llvm::StructType *type,
                    Value *src,
                    IRBuilder<> *builder) {
    // type, type of src should be a pointer to a struct of the type returned by build_type
    int idx = 0;
    for (const auto &v : closure.vars) {
        Value *ptr = builder->CreateConstInBoundsGEP2_32(type, src, 0, idx++);
        LoadInst *load = builder->CreateLoad(ptr);
        dst.push(v.first, load);
        load->setName(v.first);
    }
    for (const auto &b : closure.buffers) {
        {
            Value *ptr = builder->CreateConstInBoundsGEP2_32(type, src, 0, idx++);
            LoadInst *load = builder->CreateLoad(ptr);
            dst.push(b.first, load);
            load->setName(b.first);
        }
        {
            Value *ptr = builder->CreateConstInBoundsGEP2_32(type, src, 0, idx++);
            LoadInst *load = builder->CreateLoad(ptr);
            dst.push(b.first + ".buffer", load);
            load->setName(b.first + ".buffer");
        }
    }
}

llvm::Type *llvm_type_of(LLVMContext *c, Halide::Type t) {
    if (t.lanes() == 1) {
        if (t.is_float() && !t.is_bfloat()) {
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
        return get_vector_type(element_type, t.lanes());
    }
}

#if LLVM_VERSION >= 120
int get_vector_num_elements(llvm::Type *t) {
    if (t->isVectorTy()) {
        auto *vt = dyn_cast<llvm::FixedVectorType>(t);
        internal_assert(vt) << "Called get_vector_num_elements on a scalable vector type\n";
        return vt->getNumElements();
    } else {
        return 1;
    }
}
#else
int get_vector_num_elements(llvm::Type *t) {
    if (t->isVectorTy()) {
        return dyn_cast<llvm::VectorType>(t)->getNumElements();
    } else {
        return 1;
    }
}
#endif

llvm::Type *get_vector_element_type(llvm::Type *t) {
    if (t->isVectorTy()) {
        return dyn_cast<llvm::VectorType>(t)->getElementType();
    } else {
        return t;
    }
}

#if LLVM_VERSION >= 120
const llvm::ElementCount element_count(int e) {
    return llvm::ElementCount::getFixed(e);
}
#elif LLVM_VERSION >= 110
const llvm::ElementCount element_count(int e) {
    return llvm::ElementCount(e, /*scalable*/ false);
}
#else
int element_count(int e) {
    return e;
}
#endif

llvm::Type *get_vector_type(llvm::Type *t, int n) {
    return VectorType::get(t, element_count(n));
}

// Returns true if the given function name is one of the Halide runtime
// functions that takes a user_context pointer as its first parameter.
bool function_takes_user_context(const std::string &name) {
    static const char *user_context_runtime_funcs[] = {
        "halide_buffer_copy",
        "halide_copy_to_host",
        "halide_copy_to_device",
        "halide_current_time_ns",
        "halide_debug_to_file",
        "halide_device_free",
        "halide_device_host_nop_free",
        "halide_device_free_as_destructor",
        "halide_device_and_host_free",
        "halide_device_and_host_free_as_destructor",
        "halide_device_malloc",
        "halide_device_and_host_malloc",
        "halide_device_sync",
        "halide_do_par_for",
        "halide_do_loop_task",
        "halide_do_task",
        "halide_do_async_consumer",
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
        "halide_trace_helper",
        "halide_memoization_cache_lookup",
        "halide_memoization_cache_store",
        "halide_memoization_cache_release",
        "halide_cuda_run",
        "halide_opencl_run",
        "halide_opengl_run",
        "halide_openglcompute_run",
        "halide_metal_run",
        "halide_d3d12compute_run",
        "halide_msan_annotate_buffer_is_initialized_as_destructor",
        "halide_msan_annotate_buffer_is_initialized",
        "halide_msan_annotate_memory_is_initialized",
        "halide_msan_check_buffer_is_initialized",
        "halide_msan_check_memory_is_initialized",
        "halide_hexagon_initialize_kernels",
        "halide_hexagon_run",
        "halide_hexagon_device_release",
        "halide_hexagon_power_hvx_on",
        "halide_hexagon_power_hvx_on_mode",
        "halide_hexagon_power_hvx_on_perf",
        "halide_hexagon_power_hvx_off",
        "halide_hexagon_power_hvx_off_as_destructor",
        "halide_qurt_hvx_lock",
        "halide_qurt_hvx_unlock",
        "halide_qurt_hvx_unlock_as_destructor",
        "halide_vtcm_malloc",
        "halide_vtcm_free",
        "halide_cuda_initialize_kernels",
        "halide_opencl_initialize_kernels",
        "halide_opengl_initialize_kernels",
        "halide_openglcompute_initialize_kernels",
        "halide_metal_initialize_kernels",
        "halide_d3d12compute_initialize_kernels",
        "halide_get_gpu_device",
        "_halide_buffer_crop",
        "_halide_buffer_retire_crop_after_extern_stage",
        "_halide_buffer_retire_crops_after_extern_stage",
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

bool can_allocation_fit_on_stack(int64_t size) {
    user_assert(size > 0) << "Allocation size should be a positive number\n";
    return (size <= 1024 * 16);
}

Expr lower_int_uint_div(const Expr &a, const Expr &b) {
    // Detect if it's a small int division
    const int64_t *const_int_divisor = as_const_int(b);
    const uint64_t *const_uint_divisor = as_const_uint(b);

    Type t = a.type();
    internal_assert(!t.is_float())
        << "lower_int_uint_div is not meant to handle floating-point case.\n";

    int shift_amount;
    if (is_const_power_of_two_integer(b, &shift_amount) &&
        (t.is_int() || t.is_uint())) {
        return a >> make_const(a.type(), shift_amount);
    } else if (const_int_divisor &&
               t.is_int() &&
               (t.bits() == 8 || t.bits() == 16 || t.bits() == 32) &&
               *const_int_divisor > 1 &&
               ((t.bits() > 8 && *const_int_divisor < 256) || *const_int_divisor < 128)) {

        int64_t multiplier, shift;
        if (t.bits() == 32) {
            multiplier = IntegerDivision::table_s32[*const_int_divisor][2];
            shift = IntegerDivision::table_s32[*const_int_divisor][3];
        } else if (t.bits() == 16) {
            multiplier = IntegerDivision::table_s16[*const_int_divisor][2];
            shift = IntegerDivision::table_s16[*const_int_divisor][3];
        } else {
            // 8 bit
            multiplier = IntegerDivision::table_s8[*const_int_divisor][2];
            shift = IntegerDivision::table_s8[*const_int_divisor][3];
        }
        Expr num = a;

        // Make an all-ones mask if the numerator is negative
        Type num_as_uint_t = num.type().with_code(Type::UInt);
        Expr sign = cast(num_as_uint_t, num >> make_const(t, t.bits() - 1));

        // Flip the numerator bits if the mask is high.
        num = cast(num_as_uint_t, num);
        num = num ^ sign;

        // Multiply and keep the high half of the
        // result, and then apply the shift.
        Expr mult = make_const(num.type(), multiplier);
        num = Call::make(num.type(), Call::mulhi_shr, {num, mult, make_const(UInt(num.type().bits()), shift)},
                         Call::PureIntrinsic);

        // Maybe flip the bits back again.
        num = cast(a.type(), num ^ sign);

        return num;
    } else if (const_uint_divisor &&
               t.is_uint() &&
               (t.bits() == 8 || t.bits() == 16 || t.bits() == 32) &&
               *const_uint_divisor > 1 && *const_uint_divisor < 256) {

        int64_t method, multiplier, shift;
        if (t.bits() == 32) {
            method = IntegerDivision::table_u32[*const_uint_divisor][1];
            multiplier = IntegerDivision::table_u32[*const_uint_divisor][2];
            shift = IntegerDivision::table_u32[*const_uint_divisor][3];
        } else if (t.bits() == 16) {
            method = IntegerDivision::table_u16[*const_uint_divisor][1];
            multiplier = IntegerDivision::table_u16[*const_uint_divisor][2];
            shift = IntegerDivision::table_u16[*const_uint_divisor][3];
        } else {
            method = IntegerDivision::table_u8[*const_uint_divisor][1];
            multiplier = IntegerDivision::table_u8[*const_uint_divisor][2];
            shift = IntegerDivision::table_u8[*const_uint_divisor][3];
        }

        internal_assert(method != 0)
            << "method 0 division is for powers of two and should have been handled elsewhere\n";
        const Expr &num = a;

        // Widen, multiply, narrow
        Expr mult = make_const(num.type(), multiplier);
        Expr val = Call::make(num.type(), Call::mulhi_shr,
                              {num, mult, make_const(UInt(num.type().bits()), method == 1 ? (int)shift : 0)},
                              Call::PureIntrinsic);

        if (method == 2) {
            // Average with original numerator.
            val = Call::make(val.type(), Call::sorted_avg, {val, num}, Call::PureIntrinsic);

            // Do the final shift
            if (shift) {
                val = val >> make_const(t, shift);
            }
        }

        return val;
    } else {
        return lower_euclidean_div(a, b);
    }
}

Expr lower_int_uint_mod(const Expr &a, const Expr &b) {
    // Detect if it's a small int modulus
    const int64_t *const_int_divisor = as_const_int(b);
    const uint64_t *const_uint_divisor = as_const_uint(b);

    Type t = a.type();
    internal_assert(!t.is_float())
        << "lower_int_uint_div is not meant to handle floating-point case.\n";

    int bits;
    if (is_const_power_of_two_integer(b, &bits)) {
        return a & (b - 1);
    } else if (const_int_divisor &&
               t.is_int() &&
               (t.bits() == 8 || t.bits() == 16 || t.bits() == 32) &&
               *const_int_divisor > 1 &&
               ((t.bits() > 8 && *const_int_divisor < 256) || *const_int_divisor < 128)) {
        // We can use our fast signed integer division
        return common_subexpression_elimination(a - (a / b) * b);
    } else if (const_uint_divisor &&
               t.is_uint() &&
               (t.bits() == 8 || t.bits() == 16 || t.bits() == 32) &&
               *const_uint_divisor > 1 && *const_uint_divisor < 256) {
        // We can use our fast unsigned integer division
        return common_subexpression_elimination(a - (a / b) * b);
    } else {
        // To match our definition of division, mod should be between 0
        // and |b|.
        return lower_euclidean_mod(a, b);
    }
}

Expr lower_euclidean_div(Expr a, Expr b) {
    internal_assert(a.type() == b.type());

    Expr q;

    if (a.type().is_uint()) {
        // IROperator's div_round_to_zero will replace this with a / b for
        // unsigned ops, so create the intrinsic directly.
        Expr b_is_zero = (b == 0);
        if (!can_prove(!b_is_zero)) {
            b = b | cast(a.type(), b_is_zero);
        }
        q = Call::make(a.type(), Call::div_round_to_zero, {a, b}, Call::Intrinsic);
        q = select(b_is_zero, 0, q);
    } else {
        internal_assert(a.type().is_int());

        // Signed integer division sucks. It should be defined such
        // that it satisifies (a/b)*b + a%b = a, where 0 <= a%b < |b|,
        // i.e. Euclidean division.
        //
        // We additionally define division by zero to be zero, and
        // division of the most negative integer by -1 to be the most
        // negative integer.

        // See div_imp in IROperator.h for the C code we're trying to match.

        Expr zero = make_zero(a.type());
        Expr minus_one = make_const(a.type(), -1);

        Expr a_neg = a >> make_const(a.type(), (a.type().bits() - 1));
        Expr b_neg = b >> make_const(a.type(), (a.type().bits() - 1));
        Expr b_zero = select(b == zero, minus_one, zero);

        // Give the simplifier the chance to skip some of this nonsense
        if (can_prove(b != zero)) {
            b_zero = zero;
        }
        if (can_prove(a >= zero)) {
            a_neg = zero;
        } else if (can_prove(a < zero)) {
            a_neg = minus_one;
        }
        if (can_prove(b >= zero)) {
            b_neg = zero;
        } else if (can_prove(b < zero)) {
            b_neg = minus_one;
        }

        // If b is zero, set it to one instead to avoid faulting
        b -= b_zero;
        // If a is negative, add one to it to get the rounding to work out.
        a -= a_neg;
        // Do the C-style division
        q = Call::make(a.type(), Call::div_round_to_zero, {a, b}, Call::Intrinsic);
        // If a is negative, either add or subtract one, depending on
        // the sign of b, to fix the rounding. This can't overflow,
        // because we move the result towards zero in either case (we
        // add zero or one when q is negative, and subtract zero or
        // one when it's positive).
        q += a_neg & (~b_neg - b_neg);
        // Set the result to zero when b is zero
        q = q & ~b_zero;
    }

    q = common_subexpression_elimination(q);

    return q;
}

Expr lower_euclidean_mod(Expr a, Expr b) {
    Expr q;

    if (a.type().is_uint()) {
        Expr b_is_zero = (b == 0);
        if (!can_prove(!b_is_zero)) {
            b = b | cast(a.type(), b_is_zero);
        }
        q = Call::make(a.type(), Call::mod_round_to_zero, {a, b}, Call::Intrinsic);
        q = select(b_is_zero, make_zero(a.type()), q);
    } else {
        internal_assert(a.type().is_int());

        Expr zero = make_zero(a.type());
        Expr minus_one = make_const(a.type(), -1);

        Expr a_neg = a >> make_const(a.type(), (a.type().bits() - 1));
        Expr b_neg = b >> make_const(a.type(), (a.type().bits() - 1));
        Expr b_zero = select(b == zero, minus_one, zero);

        // Give the simplifier the chance to skip some of this nonsense
        if (can_prove(b != zero)) {
            b_zero = zero;
        }
        if (can_prove(a >= zero)) {
            a_neg = zero;
        } else if (can_prove(a < zero)) {
            a_neg = minus_one;
        }
        if (can_prove(b >= zero)) {
            b_neg = zero;
        } else if (can_prove(b < zero)) {
            b_neg = minus_one;
        }

        // If a is negative, add one to get the rounding to work out
        a -= a_neg;
        // Do the mod, avoiding taking mod by zero
        q = Call::make(a.type(), Call::mod_round_to_zero, {a, (b | b_zero)}, Call::Intrinsic);
        // If a is negative, we either need to add b - 1 to the
        // result, or -b - 1, depending on the sign of b.
        q += (a_neg & ((b ^ b_neg) + ~b_neg));
        // If b is zero, return zero by masking off the current result.
        q = q & ~b_zero;
    }

    q = common_subexpression_elimination(q);

    return q;
}

Expr lower_signed_shift_left(const Expr &a, const Expr &b) {
    internal_assert(b.type().is_int());
    const int64_t *const_int_b = as_const_int(b);
    if (const_int_b) {
        Type t = UInt(a.type().bits(), a.type().lanes());
        Expr val;
        const uint64_t b_unsigned = std::abs(*const_int_b);
        if (*const_int_b >= 0) {
            val = a << make_const(t, b_unsigned);
        } else if (*const_int_b < 0) {
            val = a >> make_const(t, b_unsigned);
        }
        return common_subexpression_elimination(val);
    } else {
        // The abs() below uses Halide's abs operator. This eliminates the overflow
        // case for the most negative value because its result is unsigned.
        Expr b_unsigned = abs(b);
        Expr val = select(b >= 0, a << b_unsigned, a >> b_unsigned);
        return simplify(common_subexpression_elimination(val));
    }
}

Expr lower_signed_shift_right(const Expr &a, const Expr &b) {
    internal_assert(b.type().is_int());
    const int64_t *const_int_b = as_const_int(b);
    if (const_int_b) {
        Type t = UInt(a.type().bits(), a.type().lanes());
        Expr val;
        const uint64_t b_unsigned = std::abs(*const_int_b);
        if (*const_int_b >= 0) {
            val = a >> make_const(t, b_unsigned);
        } else if (*const_int_b < 0) {
            val = a << make_const(t, b_unsigned);
        }
        return common_subexpression_elimination(val);
    } else {
        // The abs() below uses Halide's abs operator. This eliminates the overflow
        // case for the most negative value because its result is unsigned.
        Expr b_unsigned = abs(b);
        Expr val = select(b >= 0, a >> b_unsigned, a << b_unsigned);
        return simplify(common_subexpression_elimination(val));
    }
}

bool get_md_bool(llvm::Metadata *value, bool &result) {
    if (!value) {
        return false;
    }
    llvm::ConstantAsMetadata *cam = llvm::cast<llvm::ConstantAsMetadata>(value);
    if (!cam) {
        return false;
    }
    llvm::ConstantInt *c = llvm::cast<llvm::ConstantInt>(cam->getValue());
    if (!c) {
        return false;
    }
    result = !c->isZero();
    return true;
}

bool get_md_string(llvm::Metadata *value, std::string &result) {
    if (!value) {
        result = "";
        return false;
    }
    llvm::MDString *c = llvm::dyn_cast<llvm::MDString>(value);
    if (c) {
#if LLVM_VERSION >= 110
        result = c->getString().str();
#else
        result = c->getString();
#endif
        return true;
    }
    return false;
}

void get_target_options(const llvm::Module &module, llvm::TargetOptions &options, std::string &mcpu, std::string &mattrs) {
    bool use_soft_float_abi = false;
    get_md_bool(module.getModuleFlag("halide_use_soft_float_abi"), use_soft_float_abi);
    get_md_string(module.getModuleFlag("halide_mcpu"), mcpu);
    get_md_string(module.getModuleFlag("halide_mattrs"), mattrs);
    bool use_pic = true;
    get_md_bool(module.getModuleFlag("halide_use_pic"), use_pic);

    bool per_instruction_fast_math_flags = false;
    get_md_bool(module.getModuleFlag("halide_per_instruction_fast_math_flags"), per_instruction_fast_math_flags);

    options = llvm::TargetOptions();
    options.AllowFPOpFusion = per_instruction_fast_math_flags ? llvm::FPOpFusion::Strict : llvm::FPOpFusion::Fast;
    options.UnsafeFPMath = !per_instruction_fast_math_flags;
    options.NoInfsFPMath = !per_instruction_fast_math_flags;
    options.NoNaNsFPMath = !per_instruction_fast_math_flags;
    options.HonorSignDependentRoundingFPMathOption = !per_instruction_fast_math_flags;
    options.NoZerosInBSS = false;
    options.GuaranteedTailCallOpt = false;
    options.StackAlignmentOverride = 0;
    options.FunctionSections = true;
    options.UseInitArray = true;
    options.FloatABIType =
        use_soft_float_abi ? llvm::FloatABI::Soft : llvm::FloatABI::Hard;
    options.RelaxELFRelocations = false;
}

void clone_target_options(const llvm::Module &from, llvm::Module &to) {
    to.setTargetTriple(from.getTargetTriple());

    llvm::LLVMContext &context = to.getContext();

    bool use_soft_float_abi = false;
    if (get_md_bool(from.getModuleFlag("halide_use_soft_float_abi"), use_soft_float_abi)) {
        to.addModuleFlag(llvm::Module::Warning, "halide_use_soft_float_abi", use_soft_float_abi ? 1 : 0);
    }

    std::string mcpu;
    if (get_md_string(from.getModuleFlag("halide_mcpu"), mcpu)) {
        to.addModuleFlag(llvm::Module::Warning, "halide_mcpu", llvm::MDString::get(context, mcpu));
    }

    std::string mattrs;
    if (get_md_string(from.getModuleFlag("halide_mattrs"), mattrs)) {
        to.addModuleFlag(llvm::Module::Warning, "halide_mattrs", llvm::MDString::get(context, mattrs));
    }

    bool use_pic = true;
    if (get_md_bool(from.getModuleFlag("halide_use_pic"), use_pic)) {
        to.addModuleFlag(llvm::Module::Warning, "halide_use_pic", use_pic ? 1 : 0);
    }
}

std::unique_ptr<llvm::TargetMachine> make_target_machine(const llvm::Module &module) {
    std::string error_string;

    const llvm::Target *llvm_target = llvm::TargetRegistry::lookupTarget(module.getTargetTriple(), error_string);
    if (!llvm_target) {
        std::cout << error_string << "\n";
        llvm::TargetRegistry::printRegisteredTargetsForVersion(llvm::outs());
    }
    auto triple = llvm::Triple(module.getTargetTriple());
    internal_assert(llvm_target) << "Could not create LLVM target for " << triple.str() << "\n";

    llvm::TargetOptions options;
    std::string mcpu = "";
    std::string mattrs = "";
    get_target_options(module, options, mcpu, mattrs);

    bool use_pic = true;
    get_md_bool(module.getModuleFlag("halide_use_pic"), use_pic);

    auto *tm = llvm_target->createTargetMachine(module.getTargetTriple(),
                                                mcpu, mattrs,
                                                options,
                                                use_pic ? llvm::Reloc::PIC_ : llvm::Reloc::Static,
#ifdef HALIDE_USE_CODEMODEL_LARGE
                                                llvm::CodeModel::Large,
#else
                                                llvm::CodeModel::Small,
#endif
                                                llvm::CodeGenOpt::Aggressive);
    return std::unique_ptr<llvm::TargetMachine>(tm);
}

void set_function_attributes_for_target(llvm::Function *fn, Target t) {
    // Turn off approximate reciprocals for division. It's too
    // inaccurate even for us.
    fn->addFnAttr("reciprocal-estimates", "none");
}

void embed_bitcode(llvm::Module *M, const string &halide_command) {
    // Save llvm.compiler.used and remote it.
    SmallVector<Constant *, 2> used_array;
    SmallPtrSet<GlobalValue *, 4> used_globals;
    llvm::Type *used_element_type = llvm::Type::getInt8Ty(M->getContext())->getPointerTo(0);
    GlobalVariable *used = collectUsedGlobalVariables(*M, used_globals, true);
    for (auto *GV : used_globals) {
        if (GV->getName() != "llvm.embedded.module" &&
            GV->getName() != "llvm.cmdline")
            used_array.push_back(
                ConstantExpr::getPointerBitCastOrAddrSpaceCast(GV, used_element_type));
    }
    if (used) {
        used->eraseFromParent();
    }

    // Embed the bitcode for the llvm module.
    std::string data;
    Triple triple(M->getTargetTriple());
    // Create a constant that contains the bitcode.
    llvm::raw_string_ostream OS(data);
    llvm::WriteBitcodeToFile(*M, OS, /* ShouldPreserveUseListOrder */ true);
    ArrayRef<uint8_t> module_data =
        ArrayRef<uint8_t>((const uint8_t *)OS.str().data(), OS.str().size());

    llvm::Constant *module_constant =
        llvm::ConstantDataArray::get(M->getContext(), module_data);
    llvm::GlobalVariable *GV = new llvm::GlobalVariable(
        *M, module_constant->getType(), true, llvm::GlobalValue::PrivateLinkage,
        module_constant);
    GV->setSection((triple.getObjectFormat() == Triple::MachO) ? "__LLVM,__bitcode" : ".llvmbc");
    used_array.push_back(
        ConstantExpr::getPointerBitCastOrAddrSpaceCast(GV, used_element_type));
    if (llvm::GlobalVariable *old =
            M->getGlobalVariable("llvm.embedded.module", true)) {
        internal_assert(old->hasOneUse()) << "llvm.embedded.module can only be used once in llvm.compiler.used";
        GV->takeName(old);
        old->eraseFromParent();
    } else {
        GV->setName("llvm.embedded.module");
    }

    // Embed command-line options.
    ArrayRef<uint8_t> command_line_data(const_cast<uint8_t *>(reinterpret_cast<const uint8_t *>(halide_command.data())),
                                        halide_command.size());
    llvm::Constant *command_line_constant =
        llvm::ConstantDataArray::get(M->getContext(), command_line_data);
    GV = new llvm::GlobalVariable(*M, command_line_constant->getType(), true,
                                  llvm::GlobalValue::PrivateLinkage,
                                  command_line_constant);

    GV->setSection((triple.getObjectFormat() == Triple::MachO) ? "__LLVM,__cmdline" : ".llvmcmd");
    used_array.push_back(
        ConstantExpr::getPointerBitCastOrAddrSpaceCast(GV, used_element_type));
    if (llvm::GlobalVariable *old =
            M->getGlobalVariable("llvm.cmdline", true)) {
        internal_assert(old->hasOneUse()) << "llvm.cmdline can only be used once in llvm.compiler.used";
        GV->takeName(old);
        old->eraseFromParent();
    } else {
        GV->setName("llvm.cmdline");
    }

    if (!used_array.empty()) {
        // Recreate llvm.compiler.used.
        ArrayType *ATy = ArrayType::get(used_element_type, used_array.size());
        auto *new_used = new GlobalVariable(
            *M, ATy, false, llvm::GlobalValue::AppendingLinkage,
            llvm::ConstantArray::get(ATy, used_array), "llvm.compiler.used");
        new_used->setSection("llvm.metadata");
    }
}

}  // namespace Internal
}  // namespace Halide
