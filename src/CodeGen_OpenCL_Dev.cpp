#include <algorithm>
#include <array>
#include <sstream>
#include <utility>

#include "CSE.h"
#include "CanonicalizeGPUVars.h"
#include "CodeGen_GPU_Dev.h"
#include "CodeGen_Internal.h"
#include "CodeGen_OpenCL_Dev.h"
#include "Debug.h"
#include "EliminateBoolVectors.h"
#include "EmulateFloat16Math.h"
#include "ExprUsesVar.h"
#include "Float16.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

using std::ostringstream;
using std::sort;
using std::string;
using std::vector;

namespace {

class CodeGen_OpenCL_Dev : public CodeGen_GPU_Dev {
public:
    CodeGen_OpenCL_Dev(const Target &target);

    /** Compile a GPU kernel into the module. This may be called many times
     * with different kernels, which will all be accumulated into a single
     * source module shared by a given Halide pipeline. */
    void add_kernel(Stmt stmt,
                    const std::string &name,
                    const std::vector<DeviceArgument> &args) override;

    /** (Re)initialize the GPU kernel module. This is separate from compile,
     * since a GPU device module will often have many kernels compiled into it
     * for a single pipeline. */
    void init_module() override;

    std::vector<char> compile_to_src() override;

    std::string get_current_kernel_name() override;

    void dump() override;

    std::string print_gpu_name(const std::string &name) override;

    std::string api_unique_name() override {
        return "opencl";
    }

protected:
    class CodeGen_OpenCL_C : public CodeGen_GPU_C {
    public:
        CodeGen_OpenCL_C(std::ostream &s, Target t)
            : CodeGen_GPU_C(s, t) {
            integer_suffix_style = IntegerSuffixStyle::OpenCL;
            vector_declaration_style = VectorDeclarationStyle::OpenCLSyntax;

#define alias(x, y) \
            extern_function_name_map[x "_f16"] = y; \
            extern_function_name_map[x "_f32"] = y; \
            extern_function_name_map[x "_f64"] = y
            alias("sqrt", "sqrt");
            alias("sin", "sin");
            alias("cos", "cos");
            alias("exp", "exp");
            alias("log", "log");
            alias("abs", "fabs"); // f-prefix! (although it's handled as an intrinsic).
            alias("floor", "floor");
            alias("ceil", "ceil");
            alias("trunc", "trunc");
            alias("pow", "pow");
            alias("asin", "asin");
            alias("acos", "acos");
            alias("tan", "tan");
            alias("atan", "atan");
            alias("atan2", "atan2");
            alias("sinh", "sinh");
            alias("asinh", "asinh");
            alias("cosh", "cosh");
            alias("acosh", "acosh");
            alias("tanh", "tanh");
            alias("atanh", "atanh");

            alias("is_nan", "isnan");
            alias("is_inf", "isinf");
            alias("is_finite", "isfinite");

            alias("fast_inverse", "native_recip");
            alias("fast_inverse_sqrt", "native_rsqrt");
#undef alias
        }
        void add_kernel(Stmt stmt,
                        const std::string &name,
                        const std::vector<DeviceArgument> &args);

    protected:
        using CodeGen_GPU_C::visit;
        std::string print_type(Type type, AppendSpaceIfNeeded append_space = DoNotAppendSpace) override;
        std::string print_reinterpret(Type type, const Expr &e) override;
        std::string print_extern_call(const Call *op) override;
        std::string print_array_access(const std::string &name,
                                       const Type &type,
                                       const std::string &id_index);
        void add_vector_typedefs(const std::set<Type> &vector_types) override;

        std::string get_memory_space(const std::string &);

        std::string shared_name;

        void visit(const For *) override;
        void visit(const Ramp *op) override;
        void visit(const Broadcast *op) override;
        void visit(const Call *op) override;
        void visit(const Load *op) override;
        void visit(const Store *op) override;
        void visit(const Cast *op) override;
        void visit(const Select *op) override;
        void visit(const EQ *) override;
        void visit(const NE *) override;
        void visit(const LT *) override;
        void visit(const LE *) override;
        void visit(const GT *) override;
        void visit(const GE *) override;
        void visit(const Allocate *op) override;
        void visit(const Free *op) override;
        void visit(const AssertStmt *op) override;
        void visit(const Shuffle *op) override;
        void visit(const Min *op) override;
        void visit(const Max *op) override;
        void visit(const Atomic *op) override;
    };

    std::ostringstream src_stream;
    std::string cur_kernel_name;
    CodeGen_OpenCL_C clc;
};

CodeGen_OpenCL_Dev::CodeGen_OpenCL_Dev(const Target &t)
    : clc(src_stream, t) {
}

string CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::print_type(Type type, AppendSpaceIfNeeded space) {
    ostringstream oss;
    if (type.is_float()) {
        if (type.bits() == 16) {
            user_assert(target.has_feature(Target::CLHalf))
                << "OpenCL kernel uses half type, but CLHalf target flag not enabled\n";
            oss << "half";
        } else if (type.bits() == 32) {
            oss << "float";
        } else if (type.bits() == 64) {
            user_assert(target.has_feature(Target::CLDoubles))
                << "OpenCL kernel uses double type, but CLDoubles target flag not enabled\n";
            oss << "double";
        } else {
            user_error << "Can't represent a float with this many bits in OpenCL C: " << type << "\n";
        }

    } else {
        if (type.is_uint() && type.bits() > 1) {
            oss << "u";
        }
        switch (type.bits()) {
        case 1:
            internal_assert(type.lanes() == 1) << "Encountered vector of bool\n";
            oss << "bool";
            break;
        case 8:
            oss << "char";
            break;
        case 16:
            oss << "short";
            break;
        case 32:
            oss << "int";
            break;
        case 64:
            oss << "long";
            break;
        default:
            user_error << "Can't represent an integer with this many bits in OpenCL C: " << type << "\n";
        }
    }
    if (type.lanes() != 1) {
        switch (type.lanes()) {
        case 2:
        case 3:
        case 4:
        case 8:
        case 16:
            oss << type.lanes();
            break;
        default:
            user_error << "Unsupported vector width in OpenCL C: " << type << "\n";
        }
    }
    if (space == AppendSpace) {
        oss << " ";
    }
    return oss.str();
}

// These are built-in types in OpenCL
void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::add_vector_typedefs(const std::set<Type> &vector_types) {
}

string CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::print_reinterpret(Type type, const Expr &e) {
    ostringstream oss;
    oss << "as_" << print_type(type) << "(" << print_expr(e) << ")";
    return oss.str();
}

namespace {
string simt_intrinsic(const string &name) {
    if (ends_with(name, gpu_thread_name(0))) {
        return "get_local_id(0)";
    } else if (ends_with(name, gpu_thread_name(1))) {
        return "get_local_id(1)";
    } else if (ends_with(name, gpu_thread_name(2))) {
        return "get_local_id(2)";
    } else if (ends_with(name, gpu_block_name(0))) {
        return "get_group_id(0)";
    } else if (ends_with(name, gpu_block_name(1))) {
        return "get_group_id(1)";
    } else if (ends_with(name, gpu_block_name(2))) {
        return "get_group_id(2)";
    }
    internal_error << "simt_intrinsic called on bad variable name: " << name << "\n";
    return "";
}
}  // namespace

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const For *loop) {
    user_assert(loop->for_type != ForType::GPULane)
        << "The OpenCL backend does not support the gpu_lanes() scheduling directive.";

    if (is_gpu(loop->for_type)) {
        internal_assert(is_const_zero(loop->min));

        stream << get_indent() << print_type(Int(32)) << " " << print_name(loop->name)
               << " = " << simt_intrinsic(loop->name) << ";\n";

        loop->body.accept(this);

    } else {
        user_assert(loop->for_type != ForType::Parallel) << "Cannot use parallel loops inside OpenCL kernel\n";
        CodeGen_GPU_C::visit(loop);
    }
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const Ramp *op) {
    string id_base = print_expr(op->base);
    string id_stride = print_expr(op->stride);

    ostringstream rhs;
    rhs << id_base << " + " << id_stride << " * ("
        << print_type(op->type.with_lanes(op->lanes)) << ")(0";
    // Note 0 written above.
    for (int i = 1; i < op->lanes; ++i) {
        rhs << ", " << i;
    }
    rhs << ")";
    print_assignment(op->type.with_lanes(op->lanes), rhs.str());
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const Broadcast *op) {
    string id_value = print_expr(op->value);

    print_assignment(op->type.with_lanes(op->lanes), id_value);
}

namespace {
// Mapping of integer vector indices to OpenCL ".s" syntax.
const char *vector_elements = "0123456789ABCDEF";

}  // namespace

string CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::get_memory_space(const string &buf) {
    if (buf == shared_name) {
        return "__local";
    } else {
        return "__address_space_" + print_name(buf);
    }
}

namespace {
std::string image_type_suffix(const Type &type) {
    if (type.is_int()) {
        return "i";
    } else if (type.is_uint()) {
        return "ui";
    } else if (type.is_float()) {
        return "f";
    } else {
        internal_error << "Invalid type for image: " << type << "\n";
    }
    return "";
}
}  // namespace

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const Call *op) {
    if (op->is_intrinsic(Call::bool_to_mask)) {
        if (op->args[0].type().is_vector()) {
            // The argument is already a mask of the right width. Just
            // sign-extend to the expected type.
            op->args[0].accept(this);
        } else {
            // The argument is a scalar bool. Casting it to an int
            // produces zero or one. Convert it to -1 of the requested
            // type.
            Expr equiv = -Cast::make(op->type, op->args[0]);
            equiv.accept(this);
        }
    } else if (op->is_intrinsic(Call::cast_mask)) {
        // Sign-extension is fine
        Expr equiv = Cast::make(op->type, op->args[0]);
        equiv.accept(this);
    } else if (op->is_intrinsic(Call::select_mask)) {
        internal_assert(op->args.size() == 3);
        string cond = print_expr(op->args[0]);
        string true_val = print_expr(op->args[1]);
        string false_val = print_expr(op->args[2]);

        // Yes, you read this right. OpenCL's select function is declared
        // 'select(false_case, true_case, condition)'.
        ostringstream rhs;
        rhs << "select(" << false_val << ", " << true_val << ", " << cond << ")";
        print_assignment(op->type, rhs.str());
    } else if (op->is_intrinsic(Call::absd)) {
        ostringstream rhs;
        rhs << "abs_diff(" << print_expr(op->args[0]) << ", " << print_expr(op->args[1]) << ")";
        print_assignment(op->type, rhs.str());
    } else if (op->is_intrinsic(Call::gpu_thread_barrier)) {
        internal_assert(op->args.size() == 1) << "gpu_thread_barrier() intrinsic must specify memory fence type.\n";

        auto fence_type_ptr = as_const_int(op->args[0]);
        internal_assert(fence_type_ptr) << "gpu_thread_barrier() parameter is not a constant integer.\n";
        auto fence_type = *fence_type_ptr;

        stream << get_indent() << "barrier(0";
        if (fence_type & CodeGen_GPU_Dev::MemoryFenceType::Device) {
            stream << " | CLK_GLOBAL_MEM_FENCE";
        }
        if (fence_type & CodeGen_GPU_Dev::MemoryFenceType::Shared) {
            stream << " | CLK_LOCAL_MEM_FENCE";
        }
        stream << ");\n";
        print_assignment(op->type, "0");
    } else if (op->is_intrinsic(Call::shift_left) || op->is_intrinsic(Call::shift_right)) {
        // Some OpenCL implementations forbid mixing signed-and-unsigned shift values;
        // if the RHS is uint, quietly cast it back to int if the LHS is int
        if (op->args[0].type().is_int() && op->args[1].type().is_uint()) {
            Type t = op->args[0].type().with_code(halide_type_int);
            Expr shift = cast(t, op->args[1]);
            // Emit code here, because CodeGen_C visitor will attempt to lower signed shift
            // back to unsigned.
            string a0 = print_expr(op->args[0]);
            string a1 = print_expr(shift);
            if (op->is_intrinsic(Call::shift_left)) {
                print_assignment(op->type, a0 + " << " + a1);
            } else {
                print_assignment(op->type, a0 + " >> " + a1);
            }
        } else {
            CodeGen_GPU_C::visit(op);
        }
    } else if (op->is_intrinsic(Call::image_load)) {
        // image_load(<image name>, <buffer>, <x>, <x-extent>, <y>,
        // <y-extent>, <z>, <z-extent>)
        int dims = (op->args.size() - 2) / 2;
        internal_assert(dims >= 1 && dims <= 3);
        const StringImm *string_imm = op->args[0].as<StringImm>();
        if (!string_imm) {
            internal_assert(op->args[0].as<Broadcast>());
            string_imm = op->args[0].as<Broadcast>()->value.as<StringImm>();
        }
        internal_assert(string_imm);
        Type arg_type = op->args[2].type();
        internal_assert(arg_type.lanes() <= 16);
        internal_assert(arg_type.lanes() == op->type.lanes());

        std::array<string, 3> coord;
        for (int i = 0; i < dims; i++) {
            coord[i] = print_expr(op->args[i * 2 + 2]);
        }
        vector<string> results(arg_type.lanes());
        // For vectorized reads, codegen as a sequence of read_image calls
        for (int i = 0; i < arg_type.lanes(); i++) {
            ostringstream rhs;
            rhs << "read_image" << image_type_suffix(op->type) << "(" << print_name(string_imm->value) << ", ";
            string idx = arg_type.is_vector() ? string(".s") + vector_elements[i] : "";
            switch (dims) {
            case 1:
                rhs << coord[0] << idx << ").s0";
                break;
            case 2:
                rhs << "(int2)(" << coord[0] << idx << ", " << coord[1] << idx << ")).s0";
                break;
            case 3:
                rhs << "(int4)(" << coord[0] << idx << ", " << coord[1] << idx
                    << ", " << coord[2] << idx << ", 0)).s0";
                break;
            default:
                internal_error << "Unsupported dims";
                break;
            }
            print_assignment(op->type.with_bits(32).with_lanes(1), rhs.str());
            results[i] = id;
        }

        if (op->type.is_vector()) {
            // Combine all results into a single vector
            ostringstream rhs;
            rhs << "(" << print_type(op->type) << ")(";
            for (int i = 0; i < op->type.lanes(); i++) {
                rhs << results[i];
                if (i < op->type.lanes() - 1) {
                    rhs << ", ";
                }
            }
            rhs << ")";
            print_assignment(op->type, rhs.str());
        }
        if (op->type.bits() != 32) {
            // Widen to the correct type
            print_assignment(op->type, "convert_" + print_type(op->type) + "(" + id + ")");
        }
    } else if (op->is_intrinsic(Call::image_store)) {
        // image_store(<image name>, <buffer>, <x>, <y>, <z>, <value>)
        const StringImm *string_imm = op->args[0].as<StringImm>();
        if (!string_imm) {
            internal_assert(op->args[0].as<Broadcast>());
            string_imm = op->args[0].as<Broadcast>()->value.as<StringImm>();
        }
        internal_assert(string_imm);
        int dims = op->args.size() - 3;
        internal_assert(dims >= 1 && dims <= 3);
        Type arg_type = op->args[2].type();
        internal_assert(arg_type.lanes() <= 16);
        Type value_type = op->args.back().type();
        internal_assert(arg_type.lanes() == value_type.lanes());

        std::array<string, 3> coord;
        for (int i = 0; i < dims; i++) {
            coord[i] = print_expr(op->args[i + 2]);
        }
        string value = print_expr(op->args.back());
        // For vectorized writes, codegen as a sequence of write_image calls
        for (int i = 0; i < arg_type.lanes(); i++) {
            ostringstream write_image;
            write_image << "write_image" << image_type_suffix(op->type)
                        << "(" << print_name(string_imm->value) << ", ";
            string idx = arg_type.is_vector() ? string(".s") + vector_elements[i] : "";
            switch (dims) {
            case 1:
                write_image << coord[0] << idx;
                break;
            case 2:
                write_image << "(int2)(" << coord[0] << idx << ", " << coord[1] << idx << ")";
                break;
            case 3:
                write_image << "(int4)(" << coord[0] << idx << ", " << coord[1] << idx
                            << ", " << coord[2] << idx << ", 0)";
                break;
            default:
                internal_error << "Unsupported dims";
                break;
            }
            write_image << ", (" << print_type(value_type.with_bits(32).with_lanes(4))
                        << ")(" << value << idx << ", 0, 0, 0));\n";
            //  do_indent();
            stream << write_image.str();
        }
    } else if (op->is_intrinsic(Call::round)) {
        // In OpenCL, rint matches our rounding semantics
        Expr equiv = Call::make(op->type, "rint", op->args, Call::PureExtern);
        equiv.accept(this);
    } else {
        CodeGen_GPU_C::visit(op);
    }
}

string CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::print_extern_call(const Call *op) {
    internal_assert(!function_takes_user_context(op->name)) << op->name;
    return CodeGen_GPU_C::print_extern_call(op);
}

string CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::print_array_access(const string &name,
                                                                const Type &type,
                                                                const string &id_index) {
    ostringstream rhs;
    const auto *alloc = allocations.find(name);
    bool type_cast_needed = !(alloc && alloc->type == type);

    if (type_cast_needed) {
        rhs << "((" << get_memory_space(name) << " "
            << print_type(type) << " *)"
            << print_name(name)
            << ")";
    } else {
        rhs << print_name(name);
    }
    rhs << "[" << id_index << "]";

    return rhs.str();
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const Load *op) {
    user_assert(is_const_one(op->predicate)) << "Predicated load is not supported inside OpenCL kernel.\n";

    // If we're loading a contiguous ramp into a vector, use vload instead.
    Expr ramp_base = strided_ramp_base(op->index);
    if (ramp_base.defined()) {
        internal_assert(op->type.is_vector());

        ostringstream rhs;
        if ((op->alignment.modulus % op->type.lanes() == 0) &&
            (op->alignment.remainder % op->type.lanes() == 0)) {
            // Get the rhs just for the cache.
            string id_ramp_base = print_expr(ramp_base / op->type.lanes());
            string array_indexing = print_array_access(op->name, op->type, id_ramp_base);

            rhs << array_indexing;
        } else {
            string id_ramp_base = print_expr(ramp_base);
            rhs << "vload" << op->type.lanes()
                << "(0, (" << get_memory_space(op->name) << " "
                << print_type(op->type.element_of()) << "*)"
                << print_name(op->name) << " + " << id_ramp_base << ")";
        }
        print_assignment(op->type, rhs.str());
        return;
    }

    string id_index = print_expr(op->index);

    // Get the rhs just for the cache.
    string array_indexing = print_array_access(op->name, op->type, id_index);

    std::map<string, string>::iterator cached = cache.find(array_indexing);
    if (cached != cache.end()) {
        id = cached->second;
        return;
    }

    if (op->index.type().is_vector()) {
        // If index is a vector, gather vector elements.
        internal_assert(op->type.is_vector());

        id = "_" + unique_name('V');
        cache[array_indexing] = id;

        stream << get_indent() << print_type(op->type)
               << " " << id << ";\n";

        for (int i = 0; i < op->type.lanes(); ++i) {
            stream << get_indent();
            stream
                << id << ".s" << vector_elements[i]
                << " = ((" << get_memory_space(op->name) << " "
                << print_type(op->type.element_of()) << "*)"
                << print_name(op->name) << ")"
                << "[" << id_index << ".s" << vector_elements[i] << "];\n";
        }
    } else {
        print_assignment(op->type, array_indexing);
    }
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const Store *op) {
    user_assert(is_const_one(op->predicate)) << "Predicated store is not supported inside OpenCL kernel.\n";

    if (emit_atomic_stores) {
        // Currently only support scalar atomics.
        user_assert(op->value.type().is_scalar()) << "OpenCL atomic store does not support vectorization.\n";
        user_assert(op->value.type().bits() >= 32) << "OpenCL only support 32 and 64 bit atomics.\n";
        if (op->value.type().bits() == 64) {
            user_assert(target.has_feature(Target::CLAtomics64))
                << "Enable feature CLAtomics64 for 64-bit atomics in OpenCL.\n";
        }
        // Detect whether we can describe this as an atomic-read-modify-write,
        // otherwise fallback to a compare-and-swap loop.
        // Current only test for atomic add.
        Expr val_expr = op->value;
        Type t = val_expr.type();
        Expr equiv_load = Load::make(t, op->name, op->index, Buffer<>(), op->param, op->predicate, op->alignment);
        Expr delta = simplify(common_subexpression_elimination(op->value - equiv_load));
        // For atomicAdd, we check if op->value - store[index] is independent of store.
        // The atomicAdd operations in OpenCL only supports integers so we also check that.
        bool is_atomic_add = t.is_int_or_uint() && !expr_uses_var(delta, op->name);
        const auto *alloc = allocations.find(op->name);
        bool type_cast_needed = !(alloc && alloc->type == t);
        auto print_store_var = [&]() {
            if (type_cast_needed) {
                stream << "(("
                       << get_memory_space(op->name) << " "
                       << print_type(t)
                       << " *)"
                       << print_name(op->name)
                       << ")";
            } else {
                stream << print_name(op->name);
            }
        };
        if (is_atomic_add) {
            string id_index = print_expr(op->index);
            string id_delta = print_expr(delta);
            stream << get_indent();
            // atomic_add(&x[i], delta);
            if (t.bits() == 32) {
                stream << "atomic_add(&";
            } else {
                stream << "atom_add(&";
            }

            print_store_var();
            stream << "[" << id_index << "]";
            stream << "," << id_delta << ");\n";
        } else {
            // CmpXchg loop
            // {
            //   union {unsigned int i; float f;} old_val;
            //   union {unsigned int i; float f;} new_val;
            //   do {
            //     old_val.f = x[id_index];
            //     new_val.f = ...
            //   } while(atomic_cmpxchg((volatile address_space unsigned int*)&x[id_index], old_val.i, new_val.i) != old_val.i);
            // }
            stream << get_indent() << "{\n";
            indent += 2;
            string id_index = print_expr(op->index);
            std::string int_type = t.bits() == 32 ? "int" : "long";
            if (t.is_float() || t.is_uint()) {
                int_type = "unsigned " + int_type;
            }
            if (t.is_float()) {
                stream << get_indent() << "union {" << int_type << " i; " << print_type(t) << " f;} old_val;\n";
                stream << get_indent() << "union {" << int_type << " i; " << print_type(t) << " f;} new_val;\n";
            } else {
                stream << get_indent() << int_type << " old_val;\n";
                stream << get_indent() << int_type << " new_val;\n";
            }
            stream << get_indent() << "do {\n";
            indent += 2;
            stream << get_indent();
            if (t.is_float()) {
                stream << "old_val.f = ";
            } else {
                stream << "old_val = ";
            }
            print_store_var();
            stream << "[" << id_index << "];\n";
            string id_value = print_expr(op->value);
            stream << get_indent();
            if (t.is_float()) {
                stream << "new_val.f = ";
            } else {
                stream << "new_val = ";
            }
            stream << id_value << ";\n";
            indent -= 2;
            std::string old_val = t.is_float() ? "old_val.i" : "old_val";
            std::string new_val = t.is_float() ? "new_val.i" : "new_val";
            stream << get_indent()
                   << "} while(atomic_cmpxchg((volatile "
                   << get_memory_space(op->name) << " " << int_type << "*)&"
                   << print_name(op->name) << "[" << id_index << "], "
                   << old_val << ", " << new_val << ") != " << old_val << ");\n"
                   << get_indent() << "}\n";
            indent -= 2;
        }
        cache.clear();
        return;
    }

    string id_value = print_expr(op->value);
    Type t = op->value.type();

    // If we're writing a contiguous ramp, use vstore instead.
    Expr ramp_base = strided_ramp_base(op->index);
    if (ramp_base.defined()) {
        internal_assert(op->value.type().is_vector());

        if ((op->alignment.modulus % op->value.type().lanes() == 0) &&
            (op->alignment.remainder % op->value.type().lanes() == 0)) {
            string id_ramp_base = print_expr(ramp_base / op->value.type().lanes());
            string array_indexing = print_array_access(op->name, t, id_ramp_base);
            stream << get_indent() << array_indexing << " = " << id_value << ";\n";
        } else {
            string id_ramp_base = print_expr(ramp_base);
            stream << get_indent() << "vstore" << t.lanes() << "("
                   << id_value << ","
                   << 0 << ", (" << get_memory_space(op->name) << " "
                   << print_type(t.element_of()) << "*)"
                   << print_name(op->name) << " + " << id_ramp_base
                   << ");\n";
        }
    } else if (op->index.type().is_vector()) {
        // If index is a vector, scatter vector elements.
        internal_assert(t.is_vector());

        string id_index = print_expr(op->index);

        for (int i = 0; i < t.lanes(); ++i) {
            stream << get_indent() << "((" << get_memory_space(op->name) << " "
                   << print_type(t.element_of()) << " *)"
                   << print_name(op->name)
                   << ")["
                   << id_index << ".s" << vector_elements[i] << "] = "
                   << id_value << ".s" << vector_elements[i] << ";\n";
        }
    } else {
        string id_index = print_expr(op->index);
        stream << get_indent();
        std::string array_indexing = print_array_access(op->name, t, id_index);
        stream << array_indexing << " = " << id_value << ";\n";
    }

    cache.clear();
}

namespace {
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const EQ *op) {
    visit_binop(eliminated_bool_type(op->type, op->a.type()), op->a, op->b, "==");
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const NE *op) {
    visit_binop(eliminated_bool_type(op->type, op->a.type()), op->a, op->b, "!=");
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const LT *op) {
    visit_binop(eliminated_bool_type(op->type, op->a.type()), op->a, op->b, "<");
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const LE *op) {
    visit_binop(eliminated_bool_type(op->type, op->a.type()), op->a, op->b, "<=");
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const GT *op) {
    visit_binop(eliminated_bool_type(op->type, op->a.type()), op->a, op->b, ">");
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const GE *op) {
    visit_binop(eliminated_bool_type(op->type, op->a.type()), op->a, op->b, ">=");
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const Cast *op) {
    if (!target.has_feature(Target::CLHalf) &&
        ((op->type.is_float() && op->type.bits() < 32) ||
         (op->value.type().is_float() && op->value.type().bits() < 32))) {
        Expr equiv = lower_float16_cast(op);
        equiv.accept(this);
        return;
    }

    if (op->type.is_vector()) {
        print_assignment(op->type, "convert_" + print_type(op->type) + "(" + print_expr(op->value) + ")");
    } else {
        CodeGen_GPU_C::visit(op);
    }
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const Select *op) {
    if (op->type.is_vector()) {
        // A vector of bool was recursively introduced while
        // performing codegen. Eliminate it.
        Expr equiv = eliminate_bool_vectors(op);
        equiv.accept(this);
        return;
    }
    CodeGen_GPU_C::visit(op);
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const Allocate *op) {
    user_assert(!op->new_expr.defined()) << "Allocate node inside OpenCL kernel has custom new expression.\n"
                                         << "(Memoization is not supported inside GPU kernels at present.)\n";

    if (op->memory_type == MemoryType::GPUShared) {
        // Already handled
        op->body.accept(this);
    } else {
        open_scope();

        debug(2) << "Allocate " << op->name << " on device\n";

        debug(3) << "Pushing allocation called " << op->name << " onto the symbol table\n";

        // Allocation is not a shared memory allocation, just make a local declaration.
        // It must have a constant size.
        int32_t size = op->constant_allocation_size();
        user_assert(size > 0)
            << "Allocation " << op->name << " has a dynamic size. "
            << "Only fixed-size allocations are supported on the gpu. "
            << "Try storing into shared memory instead.";

        stream << get_indent() << print_type(op->type) << " "
               << print_name(op->name) << "[" << size << "];\n";
        stream << get_indent() << "#define " << get_memory_space(op->name) << " __private\n";

        Allocation alloc;
        alloc.type = op->type;
        allocations.push(op->name, alloc);

        op->body.accept(this);

        // Should have been freed internally
        internal_assert(!allocations.contains(op->name));

        close_scope("alloc " + print_name(op->name));
    }
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const Free *op) {
    if (op->name == shared_name) {
        return;
    } else {
        // Should have been freed internally
        internal_assert(allocations.contains(op->name));
        allocations.pop(op->name);
        stream << get_indent() << "#undef " << get_memory_space(op->name) << "\n";
    }
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const AssertStmt *op) {
    user_warning << "Ignoring assertion inside OpenCL kernel: " << op->condition << "\n";
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const Shuffle *op) {
    if (op->is_interleave()) {
        int op_lanes = op->type.lanes();
        internal_assert(!op->vectors.empty());
        int arg_lanes = op->vectors[0].type().lanes();
        if (op->vectors.size() == 1) {
            // 1 argument, just do a simple assignment
            internal_assert(op_lanes == arg_lanes);
            print_assignment(op->type, print_expr(op->vectors[0]));
        } else if (op->vectors.size() == 2) {
            // 2 arguments, set the .even to the first arg and the
            // .odd to the second arg
            internal_assert(op->vectors[1].type().lanes() == arg_lanes);
            internal_assert(op_lanes / 2 == arg_lanes);
            string a1 = print_expr(op->vectors[0]);
            string a2 = print_expr(op->vectors[1]);
            id = unique_name('_');
            stream << get_indent() << print_type(op->type) << " " << id << ";\n";
            stream << get_indent() << id << ".even = " << a1 << ";\n";
            stream << get_indent() << id << ".odd = " << a2 << ";\n";
        } else {
            // 3+ arguments, interleave via a vector literal
            // selecting the appropriate elements of the vectors
            int dest_lanes = op->type.lanes();
            internal_assert(dest_lanes <= 16);
            int num_vectors = op->vectors.size();
            vector<string> arg_exprs(num_vectors);
            for (int i = 0; i < num_vectors; i++) {
                internal_assert(op->vectors[i].type().lanes() == arg_lanes);
                arg_exprs[i] = print_expr(op->vectors[i]);
            }
            internal_assert(num_vectors * arg_lanes >= dest_lanes);
            id = unique_name('_');
            stream << get_indent() << print_type(op->type) << " " << id;
            stream << " = (" << print_type(op->type) << ")(";
            for (int i = 0; i < dest_lanes; i++) {
                int arg = i % num_vectors;
                int arg_idx = i / num_vectors;
                internal_assert(arg_idx <= arg_lanes);
                stream << arg_exprs[arg] << ".s" << vector_elements[arg_idx];
                if (i != dest_lanes - 1) {
                    stream << ", ";
                }
            }
            stream << ");\n";
        }
    } else if (op->is_extract_element()) {
        // OpenCL requires using .s<n> format for extracting an element
        ostringstream rhs;
        rhs << print_expr(op->vectors[0]);
        rhs << ".s" << op->indices[0];
        print_assignment(op->type, rhs.str());
    } else {
        CodeGen_GPU_C::visit(op);
    }
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const Max *op) {
    print_expr(Call::make(op->type, "max", {op->a, op->b}, Call::Extern));
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const Min *op) {
    print_expr(Call::make(op->type, "min", {op->a, op->b}, Call::Extern));
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const Atomic *op) {
    // Most GPUs require all the threads in a warp to perform the same operations,
    // which means our mutex will lead to deadlock.
    user_assert(op->mutex_name.empty())
        << "The atomic update requires a mutex lock, which is not supported in OpenCL.\n";

    // Issue atomic stores.
    ScopedValue<bool> old_emit_atomic_stores(emit_atomic_stores, true);
    CodeGen_GPU_C::visit(op);
}

void CodeGen_OpenCL_Dev::add_kernel(Stmt s,
                                    const string &name,
                                    const vector<DeviceArgument> &args) {
    debug(2) << "CodeGen_OpenCL_Dev::compile " << name << "\n";

    // We need to scalarize/de-predicate any loads/stores, since OpenCL does not
    // support predication.
    s = scalarize_predicated_loads_stores(s);

    debug(2) << "CodeGen_OpenCL_Dev: after removing predication: \n"
             << s;

    // TODO: do we have to uniquify these names, or can we trust that they are safe?
    cur_kernel_name = name;
    clc.add_kernel(s, name, args);
}

namespace {
struct BufferSize {
    string name;
    size_t size = 0;

    BufferSize() = default;
    BufferSize(string name, size_t size)
        : name(std::move(name)), size(size) {
    }

    bool operator<(const BufferSize &r) const {
        return size < r.size;
    }
};
}  // namespace

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::add_kernel(Stmt s,
                                                      const string &name,
                                                      const vector<DeviceArgument> &args) {

    debug(2) << "Adding OpenCL kernel " << name << "\n";

    debug(2) << "Eliminating bool vectors\n";
    s = eliminate_bool_vectors(s);
    debug(2) << "After eliminating bool vectors:\n"
             << s << "\n";

    // We need to scalarize/de-predicate any loads/stores, since OpenCL does not
    // support predication.
    s = scalarize_predicated_loads_stores(s);

    debug(2) << "After removing predication: \n"
             << s;

    // Figure out which arguments should be passed in __constant.
    // Such arguments should be:
    // - not written to,
    // - loads are block-uniform,
    // - constant size,
    // - and all allocations together should be less than the max constant
    //   buffer size given by CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE.
    // The last condition is handled via the preprocessor in the kernel
    // declaration.
    vector<BufferSize> constants;
    for (const auto &arg : args) {
        if (arg.is_buffer &&
            CodeGen_GPU_Dev::is_buffer_constant(s, arg.name) &&
            arg.size > 0) {
            constants.emplace_back(arg.name, arg.size);
        }
    }

    // Sort the constant candidates from smallest to largest. This will put
    // as many of the constant allocations in __constant as possible.
    // Ideally, we would prioritize constant buffers by how frequently they
    // are accessed.
    sort(constants.begin(), constants.end());

    // Compute the cumulative sum of the constants.
    for (size_t i = 1; i < constants.size(); i++) {
        constants[i].size += constants[i - 1].size;
    }

    // Create preprocessor replacements for the address spaces of all our buffers.
    stream << "// Address spaces for " << name << "\n";
    for (const auto &arg : args) {
        if (arg.is_buffer) {
            vector<BufferSize>::iterator constant = constants.begin();
            while (constant != constants.end() &&
                   constant->name != arg.name) {
                constant++;
            }

            if (constant != constants.end()) {
                stream << "#if " << constant->size << " <= MAX_CONSTANT_BUFFER_SIZE && "
                       << constant - constants.begin() << " < MAX_CONSTANT_ARGS\n";
                stream << "#define " << get_memory_space(arg.name) << " __constant\n";
                stream << "#else\n";
                stream << "#define " << get_memory_space(arg.name) << " __global\n";
                stream << "#endif\n";
            } else {
                stream << "#define " << get_memory_space(arg.name) << " __global\n";
            }
        }
    }

    // Emit the function prototype.
    stream << "__kernel void " << name << "(\n";
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            if (args[i].memory_type == MemoryType::GPUTexture) {
                int dims = args[i].dimensions;
                internal_assert(dims >= 1 && dims <= 3) << "dims = " << dims << "\n";
                if (args[i].read && args[i].write) {
                    stream << " __read_write ";
                } else if (args[i].read) {
                    stream << " __read_only ";
                } else if (args[i].write) {
                    stream << " __write_only ";
                } else {
                    internal_error << "CL Image argument " << args[i].name
                                   << " is neither read nor write";
                }
                stream << "image" << dims << "d_t ";
            } else {
                stream << " " << get_memory_space(args[i].name) << " ";
                if (!args[i].write) {
                    stream << "const ";
                }
                stream << print_type(args[i].type) << " *"
                       << "restrict ";
            }
            stream << print_name(args[i].name);
            Allocation alloc;
            alloc.type = args[i].type;
            allocations.push(args[i].name, alloc);
        } else {
            Type t = args[i].type;
            string name = args[i].name;
            // Bools are passed as a uint8.
            t = t.with_bits(t.bytes() * 8);
            // float16 are passed as uints
            if (t.is_float() && t.bits() < 32) {
                t = t.with_code(halide_type_uint);
                name += "_bits";
            }
            stream << " const "
                   << print_type(t)
                   << " "
                   << print_name(name);
        }

        if (i < args.size() - 1) {
            stream << ",\n";
        }
    }

    class FindShared : public IRVisitor {
        using IRVisitor::visit;
        void visit(const Allocate *op) override {
            if (op->memory_type == MemoryType::GPUShared) {
                internal_assert(alloc == nullptr)
                    << "Found multiple shared allocations in opencl kernel\n";
                alloc = op;
            }
        }

    public:
        const Allocate *alloc = nullptr;
    } find_shared;
    s.accept(&find_shared);

    if (find_shared.alloc) {
        shared_name = find_shared.alloc->name;
    } else {
        shared_name = "__shared";
    }
    // Note that int16 below is an int32x16, not an int16_t. The type
    // is chosen to be large to maximize alignment.
    stream << ",\n"
           << " __local int16* "
           << print_name(shared_name)
           << ")\n";

    open_scope();

    // Reinterpret half args passed as uint16 back to half
    for (const auto &arg : args) {
        if (!arg.is_buffer &&
            arg.type.is_float() &&
            arg.type.bits() < 32) {
            stream << " const " << print_type(arg.type)
                   << " " << print_name(arg.name)
                   << " = half_from_bits(" << print_name(arg.name + "_bits") << ");\n";
        }
    }

    print(s);
    close_scope("kernel " + name);

    for (const auto &arg : args) {
        // Remove buffer arguments from allocation scope
        if (arg.is_buffer) {
            allocations.pop(arg.name);
        }
    }

    // Undef all the buffer address spaces, in case they're different in another kernel.
    for (const auto &arg : args) {
        if (arg.is_buffer) {
            stream << "#undef " << get_memory_space(arg.name) << "\n";
        }
    }
}

void CodeGen_OpenCL_Dev::init_module() {
    debug(2) << "OpenCL device codegen init_module\n";

    // wipe the internal kernel source
    src_stream.str("");
    src_stream.clear();

    const Target &target = clc.get_target();

    // This identifies the program as OpenCL C (as opposed to SPIR).
    src_stream << "/*OpenCL C " << target.to_string() << "*/\n";

    src_stream << "#pragma OPENCL FP_CONTRACT ON\n";

    // Write out the Halide math functions.
    src_stream << "inline float float_from_bits(unsigned int x) {return as_float(x);}\n"
               << "inline float nan_f32() { return NAN; }\n"
               << "inline float neg_inf_f32() { return -INFINITY; }\n"
               << "inline float inf_f32() { return INFINITY; }\n";

    // There does not appear to be a reliable way to safely ignore unused
    // variables in OpenCL C. See https://github.com/halide/Halide/issues/4918.
    src_stream << "#define halide_maybe_unused(x)\n";

    if (target.has_feature(Target::CLDoubles)) {
        src_stream << "#pragma OPENCL EXTENSION cl_khr_fp64 : enable\n";
    }

    if (target.has_feature(Target::CLHalf)) {
        const uint16_t nan_f16 = float16_t::make_nan().to_bits();
        const uint16_t neg_inf_f16 = float16_t::make_negative_infinity().to_bits();
        const uint16_t inf_f16 = float16_t::make_infinity().to_bits();

        src_stream << "#pragma OPENCL EXTENSION cl_khr_fp16 : enable\n"
                   << "inline half half_from_bits(unsigned short x) {return __builtin_astype(x, half);}\n"
                   << "inline half nan_f16() { return half_from_bits(" << nan_f16 << "); }\n"
                   << "inline half neg_inf_f16() { return half_from_bits(" << neg_inf_f16 << "); }\n"
                   << "inline half inf_f16() { return half_from_bits(" << inf_f16 << "); }\n";
    }

    if (target.has_feature(Target::CLAtomics64)) {
        src_stream << "#pragma OPENCL EXTENSION cl_khr_int64_base_atomics : enable\n";
        src_stream << "#pragma OPENCL EXTENSION cl_khr_int64_extended_atomics : enable\n";
    }

    src_stream << "\n";

    // Add at least one kernel to avoid errors on some implementations for functions
    // without any GPU schedules.
    src_stream << "__kernel void _at_least_one_kernel(int x) { }\n";

    cur_kernel_name = "";
}

vector<char> CodeGen_OpenCL_Dev::compile_to_src() {
    string str = src_stream.str();
    debug(1) << "OpenCL kernel:\n"
             << str << "\n";
    vector<char> buffer(str.begin(), str.end());
    buffer.push_back(0);
    return buffer;
}

string CodeGen_OpenCL_Dev::get_current_kernel_name() {
    return cur_kernel_name;
}

void CodeGen_OpenCL_Dev::dump() {
    std::cerr << src_stream.str() << "\n";
}

std::string CodeGen_OpenCL_Dev::print_gpu_name(const std::string &name) {
    return name;
}

}  // namespace

std::unique_ptr<CodeGen_GPU_Dev> new_CodeGen_OpenCL_Dev(const Target &target) {
    return std::make_unique<CodeGen_OpenCL_Dev>(target);
}

}  // namespace Internal
}  // namespace Halide
