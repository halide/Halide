#include "CodeGen_Internal.h"
#include "CSE.h"
#include "Debug.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IntegerDivisionTable.h"
#include "LLVM_Headers.h"
#include "Simplify.h"
#include "Simplify_Internal.h"
#include "runtime/constants.h"

namespace Halide {
namespace Internal {

using std::string;

using namespace llvm;

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

int get_vector_num_elements(llvm::Type *t) {
    if (t->isVectorTy()) {
        auto *vt = dyn_cast<llvm::FixedVectorType>(t);
        internal_assert(vt) << "Called get_vector_num_elements on a scalable vector type\n";
        return vt->getNumElements();
    } else {
        return 1;
    }
}

llvm::Type *get_vector_element_type(llvm::Type *t) {
    if (t->isVectorTy()) {
        return dyn_cast<llvm::VectorType>(t)->getElementType();
    } else {
        return t;
    }
}

llvm::ElementCount element_count(int e) {
    return llvm::ElementCount::getFixed(e);
}

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
        "halide_openglcompute_initialize_kernels",
        "halide_metal_initialize_kernels",
        "halide_d3d12compute_initialize_kernels",
        "halide_get_gpu_device",
        "_halide_buffer_crop",
        "_halide_buffer_retire_crop_after_extern_stage",
        "_halide_buffer_retire_crops_after_extern_stage",
        "_halide_hexagon_do_par_for",
    };
    for (const char *user_context_runtime_func : user_context_runtime_funcs) {
        if (name == user_context_runtime_func) {
            return true;
        }
    }
    // The error functions all take a user context
    return starts_with(name, "halide_error_");
}

bool can_allocation_fit_on_stack(int64_t size) {
    user_assert(size > 0) << "Allocation size should be a positive number\n";
    return (size <= (int64_t)Runtime::Internal::Constants::maximum_stack_allocation_bytes);
}

Expr lower_int_uint_div(const Expr &a, const Expr &b, bool round_to_zero) {
    // Detect if it's a small int division
    internal_assert(a.type() == b.type());
    const int64_t *const_int_divisor = as_const_int(b);
    const uint64_t *const_uint_divisor = as_const_uint(b);

    Type t = a.type();
    internal_assert(!t.is_float())
        << "lower_int_uint_div is not meant to handle floating-point case.\n";

    int shift_amount;
    if (is_const_power_of_two_integer(b, &shift_amount) &&
        (t.is_int() || t.is_uint())) {
        if (round_to_zero) {
            Expr result = a;
            // Normally a right-shift isn't right for division rounding to
            // zero. It does the wrong thing for negative values. Add a fudge so
            // that a right-shift becomes correct.
            result += (result >> (t.bits() - 1)) & (b - 1);
            return result >> shift_amount;
        } else {
            return a >> make_const(UInt(a.type().bits()), shift_amount);
        }
    } else if (const_int_divisor &&
               t.is_int() &&
               (t.bits() == 8 || t.bits() == 16 || t.bits() == 32) &&
               *const_int_divisor > 1 &&
               ((t.bits() > 8 && *const_int_divisor < 256) || *const_int_divisor < 128)) {

        int64_t multiplier;
        int shift;
        if (t.bits() == 32) {
            if (round_to_zero) {
                multiplier = IntegerDivision::table_srz32[*const_int_divisor][2];
                shift = IntegerDivision::table_srz32[*const_int_divisor][3];
            } else {
                multiplier = IntegerDivision::table_s32[*const_int_divisor][2];
                shift = IntegerDivision::table_s32[*const_int_divisor][3];
            }
        } else if (t.bits() == 16) {
            if (round_to_zero) {
                multiplier = IntegerDivision::table_srz16[*const_int_divisor][2];
                shift = IntegerDivision::table_srz16[*const_int_divisor][3];
            } else {
                multiplier = IntegerDivision::table_s16[*const_int_divisor][2];
                shift = IntegerDivision::table_s16[*const_int_divisor][3];
            }
        } else {
            // 8 bit
            if (round_to_zero) {
                multiplier = IntegerDivision::table_srz8[*const_int_divisor][2];
                shift = IntegerDivision::table_srz8[*const_int_divisor][3];
            } else {
                multiplier = IntegerDivision::table_s8[*const_int_divisor][2];
                shift = IntegerDivision::table_s8[*const_int_divisor][3];
            }
        }
        Expr num = a;

        // Make an all-ones mask if the numerator is negative
        Type num_as_uint_t = num.type().with_code(Type::UInt);
        Expr sign = cast(num_as_uint_t, num >> make_const(UInt(t.bits()), t.bits() - 1));

        if (!round_to_zero) {
            // Flip the numerator bits if the mask is high.
            num = cast(num_as_uint_t, num);
            num = num ^ sign;
        }

        // Multiply and keep the high half of the
        // result, and then apply the shift.
        Expr mult = make_const(num.type(), multiplier);
        num = mul_shift_right(num, mult, shift + num.type().bits());

        if (round_to_zero) {
            // Add one if the numerator was negative
            num -= sign;
        } else {
            // Maybe flip the bits back again.
            num = cast(a.type(), num ^ sign);
        }

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
        Expr val = mul_shift_right(num, mult, (method == 1 ? shift : 0) + num.type().bits());

        if (method == 2) {
            // Average with original numerator.
            val = Call::make(val.type(), Call::sorted_avg, {val, num}, Call::PureIntrinsic);
        } else if (method == 3) {
            // Average with original numerator, rounding up. This
            // method exists because this is cheaper than averaging
            // with the original numerator on x86, where there's an
            // average-round-up instruction (pavg), but no
            // average-round-down instruction. Using method 2,
            // sorted_avg lowers to three instructions on x86.
            //
            // On ARM and other architectures with both
            // average-round-up and average-round-down instructions
            // there's no reason to prefer either method 2 or method 3
            // over the other.
            val = rounding_halving_add(val, num);
        }

        // Do the final shift
        if (shift && (method == 2 || method == 3)) {
            val = val >> make_const(UInt(t.bits()), shift);
        }

        return val;
    } else if (round_to_zero) {
        // Return the input division unchanged.
        return Call::make(a.type(), Call::div_round_to_zero, {a, b}, Call::PureIntrinsic);
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
        return a & simplify(b - 1);
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

std::pair<Expr, Expr> unsigned_long_div_mod_round_to_zero(Expr &num, const Expr &den,
                                                          const uint64_t *upper_bound) {
    internal_assert(num.type() == den.type());
    internal_assert(num.type().is_uint());
    Type ty = num.type();
    Expr q = make_zero(ty);
    Expr leading_zeros = cast(ty, count_leading_zeros(den));
    // Each iteration of the loop below checks for a bit in the result.
    const int times = ty.bits();
    int start = 1;
    if (upper_bound) {
        // Set start to times - (index of most significant bit in max_val)
        // as for each iteration:
        //     (1 << shift) <= upper_bound
        start = times;
        uint64_t max_val = *upper_bound;
        while (max_val >>= 1) {
            --start;
        }
        debug(1) << "Max value for long division: " << *upper_bound
                 << ". Evaluate only first " << 1 + times - start << " bits.\n";
    }
    Expr r = num;
    for (int i = start; i <= times; i++) {
        // Check if the bit at 'shift' index should be set in the result.
        int shift = times - i;
        Expr shift_expr = make_const(ty, shift);
        Expr new_r = r - (den << shift_expr);
        // Don't drop any set bits from den after shift. The bit is set if
        // den << shift is no more than remainder.
        Expr bit_set = ((shift_expr <= leading_zeros) && r >= (den << shift_expr));
        // Update the  and the quotient.
        r = select(bit_set, new_r, r);
        q = select(bit_set, make_const(ty, uint64_t(1) << shift) | q, q);
    }
    return {q, r};
}

std::pair<Expr, Expr> long_div_mod_round_to_zero(const Expr &num, const Expr &den,
                                                 const uint64_t *max_abs) {
    debug(1) << "Using long div: (num: " << num << "); (den: " << den << ")\n";
    internal_assert(num.type() == den.type());
    Expr abs_num = (num.type().is_int()) ? abs(num) : num;
    Expr abs_den = (den.type().is_int()) ? abs(den) : den;
    std::pair<Expr, Expr> qr = unsigned_long_div_mod_round_to_zero(abs_num, abs_den, max_abs);
    Expr q = qr.first;
    Expr r = qr.second;
    // Correct the signs for quotient and remainder for signed integer division.
    if (num.type().is_int()) {
        Expr num_neg = num >> make_const(UInt(num.type().bits()), (num.type().bits() - 1));
        Expr den_neg = den >> make_const(UInt(num.type().bits()), (num.type().bits() - 1));
        q = cast(num.type(), q) * ((num_neg ^ den_neg) | 1);
        r = cast(num.type(), r) * (num_neg | 1);
    }
    q = simplify(common_subexpression_elimination(q));
    r = simplify(common_subexpression_elimination(r));
    return {q, r};
}

Expr lower_euclidean_div(Expr a, Expr b) {
    internal_assert(a.type() == b.type());

    Expr q;

    if (a.type().is_uint()) {
        // IROperator's div_round_to_zero will replace this with a / b for
        // unsigned ops, so create the intrinsic directly.
        Expr b_is_const_zero = (b == 0);
        if (!can_prove(!b_is_const_zero)) {
            b = b | cast(a.type(), b_is_const_zero);
        }
        q = Call::make(a.type(), Call::div_round_to_zero, {a, b}, Call::PureIntrinsic);
        q = select(b_is_const_zero, 0, q);
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

        Expr a_neg = a >> make_const(UInt(a.type().bits()), (a.type().bits() - 1));
        Expr b_neg = b >> make_const(UInt(b.type().bits()), (b.type().bits() - 1));
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
        q = Call::make(a.type(), Call::div_round_to_zero, {a, b}, Call::PureIntrinsic);
        // If a is negative, either add or subtract one, depending on
        // the sign of b, to fix the rounding. This can't overflow,
        // because we move the result towards zero in either case (we
        // add zero or one when q is negative, and subtract zero or
        // one when it's positive).
        q += a_neg & (~b_neg - b_neg);
        // Set the result to zero when b is zero
        q = q & ~b_zero;
    }

    q = simplify(common_subexpression_elimination(q));

    return q;
}

Expr lower_euclidean_mod(Expr a, Expr b) {
    Expr q;

    if (a.type().is_uint()) {
        Expr b_is_const_zero = (b == 0);
        if (!can_prove(!b_is_const_zero)) {
            b = b | cast(a.type(), b_is_const_zero);
        }
        q = Call::make(a.type(), Call::mod_round_to_zero, {a, b}, Call::PureIntrinsic);
        q = select(b_is_const_zero, make_zero(a.type()), q);
    } else {
        internal_assert(a.type().is_int());

        Expr zero = make_zero(a.type());
        Expr minus_one = make_const(a.type(), -1);

        Expr a_neg = a >> make_const(UInt(a.type().bits()), (a.type().bits() - 1));
        Expr b_neg = b >> make_const(UInt(a.type().bits()), (a.type().bits() - 1));
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
        q = Call::make(a.type(), Call::mod_round_to_zero, {a, (b | b_zero)}, Call::PureIntrinsic);
        // If a is negative, we either need to add b - 1 to the
        // result, or -b - 1, depending on the sign of b.
        q += (a_neg & ((b ^ b_neg) + ~b_neg));
        // If b is zero, return zero by masking off the current result.
        q = q & ~b_zero;
    }

    q = simplify(common_subexpression_elimination(q));

    return q;
}

Expr lower_signed_shift_left(const Expr &a, const Expr &b) {
    internal_assert(b.type().is_int());
    const int64_t *const_int_b = as_const_int(b);
    if (const_int_b) {
        Expr val;
        const uint64_t b_unsigned = std::abs(*const_int_b);
        if (*const_int_b >= 0) {
            val = a << make_const(UInt(a.type().bits()), b_unsigned);
        } else if (*const_int_b < 0) {
            val = a >> make_const(UInt(a.type().bits()), b_unsigned);
        }
        return common_subexpression_elimination(val);
    } else {
        // The abs() below uses Halide's abs operator. This eliminates the overflow
        // case for the most negative value because its result is unsigned.
        Expr b_unsigned = abs(b);
        Expr val = select(b >= 0, a << b_unsigned, a >> b_unsigned);
        return common_subexpression_elimination(val);
    }
}

Expr lower_signed_shift_right(const Expr &a, const Expr &b) {
    internal_assert(b.type().is_int());
    const int64_t *const_int_b = as_const_int(b);
    if (const_int_b) {
        Expr val;
        const uint64_t b_unsigned = std::abs(*const_int_b);
        if (*const_int_b >= 0) {
            val = a >> make_const(UInt(a.type().bits()), b_unsigned);
        } else if (*const_int_b < 0) {
            val = a << make_const(UInt(a.type().bits()), b_unsigned);
        }
        return common_subexpression_elimination(val);
    } else {
        // The abs() below uses Halide's abs operator. This eliminates the overflow
        // case for the most negative value because its result is unsigned.
        Expr b_unsigned = abs(b);
        Expr val = select(b >= 0, a >> b_unsigned, a << b_unsigned);
        return common_subexpression_elimination(val);
    }
}

Expr lower_mux(const Call *mux) {
    internal_assert(mux->args.size() >= 2);
    Expr equiv = mux->args.back();
    Expr index = mux->args[0];
    int num_vals = (int)mux->args.size() - 1;
    for (int i = num_vals - 1; i >= 0; i--) {
        equiv = select(index == make_const(index.type(), i), mux->args[i + 1], equiv);
    }
    return equiv;
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
        result = c->getString().str();
        return true;
    }
    return false;
}

void get_target_options(const llvm::Module &module, llvm::TargetOptions &options, std::string &mcpu, std::string &mattrs) {
    bool use_soft_float_abi = false;
    get_md_bool(module.getModuleFlag("halide_use_soft_float_abi"), use_soft_float_abi);
    get_md_string(module.getModuleFlag("halide_mcpu"), mcpu);
    get_md_string(module.getModuleFlag("halide_mattrs"), mattrs);
    std::string mabi;
    get_md_string(module.getModuleFlag("halide_mabi"), mabi);
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
#if LLVM_VERSION >= 130
    // nothing
#else
    options.StackAlignmentOverride = 0;
#endif
    options.FunctionSections = true;
    options.UseInitArray = true;
    options.FloatABIType =
        use_soft_float_abi ? llvm::FloatABI::Soft : llvm::FloatABI::Hard;
    options.RelaxELFRelocations = false;
    options.MCOptions.ABIName = mabi;
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

    bool use_large_code_model = false;
    get_md_bool(module.getModuleFlag("halide_use_large_code_model"), use_large_code_model);

    auto *tm = llvm_target->createTargetMachine(module.getTargetTriple(),
                                                mcpu, mattrs,
                                                options,
                                                use_pic ? llvm::Reloc::PIC_ : llvm::Reloc::Static,
                                                use_large_code_model ? llvm::CodeModel::Large : llvm::CodeModel::Small,
                                                llvm::CodeGenOpt::Aggressive);
    return std::unique_ptr<llvm::TargetMachine>(tm);
}

void set_function_attributes_for_target(llvm::Function *fn, const Target &t) {
    // Turn off approximate reciprocals for division. It's too
    // inaccurate even for us.
    fn->addFnAttr("reciprocal-estimates", "none");
}

void embed_bitcode(llvm::Module *M, const string &halide_command) {
    // Save llvm.compiler.used and remote it.
    SmallVector<Constant *, 2> used_array;
#if LLVM_VERSION >= 130
    SmallVector<GlobalValue *, 4> used_globals;
#else
    SmallPtrSet<GlobalValue *, 4> used_globals;
#endif
    llvm::Type *used_element_type = llvm::Type::getInt8Ty(M->getContext())->getPointerTo(0);
    GlobalVariable *used = collectUsedGlobalVariables(*M, used_globals, true);
    for (auto *GV : used_globals) {
        if (GV->getName() != "llvm.embedded.module" &&
            GV->getName() != "llvm.cmdline") {
            used_array.push_back(
                ConstantExpr::getPointerBitCastOrAddrSpaceCast(GV, used_element_type));
        }
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
