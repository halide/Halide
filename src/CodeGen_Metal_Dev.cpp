#include <algorithm>
#include <sstream>
#include <utility>

#include "CanonicalizeGPUVars.h"
#include "CodeGen_GPU_Dev.h"
#include "CodeGen_Internal.h"
#include "CodeGen_Metal_Dev.h"
#include "Debug.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

using std::ostringstream;
using std::sort;
using std::string;
using std::vector;

namespace {

ostringstream nil;

class CodeGen_Metal_Dev : public CodeGen_GPU_Dev {
public:
    CodeGen_Metal_Dev(const Target &target);

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
        return "metal";
    }

    bool kernel_run_takes_types() const override {
        return true;
    }

protected:
    class CodeGen_Metal_C : public CodeGen_GPU_C {
    public:
        CodeGen_Metal_C(std::ostream &s, const Target &t)
            : CodeGen_GPU_C(s, t) {
        }
        void add_kernel(const Stmt &stmt,
                        const std::string &name,
                        const std::vector<DeviceArgument> &args);

    protected:
        using CodeGen_GPU_C::visit;
        std::string print_type(Type type, AppendSpaceIfNeeded space_option = DoNotAppendSpace) override;
        // Vectors in Metal come in two varieties, regular and packed.
        // For storage allocations and pointers used in address arithmetic,
        // packed types must be used. For temporaries, constructors, etc.
        // regular types must be used.
        // This concept also potentially applies to half types, which are
        // often only supported for storage, not arithmetic,
        // hence the method name.
        std::string print_storage_type(Type type);
        std::string print_type_maybe_storage(Type type, bool storage, AppendSpaceIfNeeded space);
        std::string print_reinterpret(Type type, const Expr &e) override;
        std::string print_extern_call(const Call *op) override;

        std::string get_memory_space(const std::string &);

        std::string shared_name;

        void visit(const Min *) override;
        void visit(const Max *) override;
        void visit(const Div *) override;
        void visit(const Mod *) override;
        void visit(const For *) override;
        void visit(const Ramp *op) override;
        void visit(const Broadcast *op) override;
        void visit(const Call *op) override;
        void visit(const Load *op) override;
        void visit(const Store *op) override;
        void visit(const Select *op) override;
        void visit(const Allocate *op) override;
        void visit(const Free *op) override;
        void visit(const Cast *op) override;
        void visit(const VectorReduce *op) override;
        void visit(const Atomic *op) override;
        void visit(const FloatImm *op) override;
    };

    std::ostringstream src_stream;
    std::string cur_kernel_name;
    CodeGen_Metal_C metal_c;
};

CodeGen_Metal_Dev::CodeGen_Metal_Dev(const Target &t)
    : metal_c(src_stream, t) {
}

string CodeGen_Metal_Dev::CodeGen_Metal_C::print_type_maybe_storage(Type type, bool storage, AppendSpaceIfNeeded space) {
    ostringstream oss;

    // Storage uses packed vector types.
    if (storage && type.lanes() != 1) {
        oss << "packed_";
    }
    if (type.is_float()) {
        if (type.bits() == 16) {
            oss << "half";
        } else if (type.bits() == 32) {
            oss << "float";
        } else if (type.bits() == 64) {
            oss << "double";
        } else {
            user_error << "Can't represent a float with this many bits in Metal C: " << type << "\n";
        }

    } else {
        if (type.is_uint() && type.bits() > 1) {
            oss << "u";
        }
        switch (type.bits()) {
        case 1:
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
            user_error << "Metal does not support 64-bit integers.\n";
            break;
        default:
            user_error << "Can't represent an integer with this many bits in Metal C: " << type << "\n";
        }
    }
    if (type.lanes() != 1) {
        switch (type.lanes()) {
        case 2:
        case 3:
        case 4:
            oss << type.lanes();
            break;
        default:
            user_error << "Unsupported vector width in Metal C: " << type << "\n";
        }
    }
    if (space == AppendSpace) {
        oss << " ";
    }
    return oss.str();
}

string CodeGen_Metal_Dev::CodeGen_Metal_C::print_type(Type type, AppendSpaceIfNeeded space) {
    return print_type_maybe_storage(type, false, space);
}

string CodeGen_Metal_Dev::CodeGen_Metal_C::print_storage_type(Type type) {
    return print_type_maybe_storage(type, true, DoNotAppendSpace);
}

string CodeGen_Metal_Dev::CodeGen_Metal_C::print_reinterpret(Type type, const Expr &e) {
    ostringstream oss;

    string temp = unique_name('V');
    string expr = print_expr(e);
    stream << get_indent() << print_type(e.type()) << " " << temp << " = " << expr << ";\n";
    oss << "*(" << print_type(type) << " thread *)(&" << temp << ")";
    return oss.str();
}

namespace {
string simt_intrinsic(const string &name) {
    if (ends_with(name, gpu_thread_name(0))) {
        return "tid_in_tgroup.x";
    } else if (ends_with(name, gpu_thread_name(1))) {
        return "tid_in_tgroup.y";
    } else if (ends_with(name, gpu_thread_name(2))) {
        return "tid_in_tgroup.z";
    } else if (ends_with(name, gpu_block_name(0))) {
        return "tgroup_index.x";
    } else if (ends_with(name, gpu_block_name(1))) {
        return "tgroup_index.y";
    } else if (ends_with(name, gpu_block_name(2))) {
        return "tgroup_index.z";
    }
    internal_error << "simt_intrinsic called on bad variable name: " << name << "\n";
    return "";
}
}  // namespace

string CodeGen_Metal_Dev::CodeGen_Metal_C::print_extern_call(const Call *op) {
    internal_assert(!function_takes_user_context(op->name)) << op->name;
    vector<string> args(op->args.size());
    for (size_t i = 0; i < op->args.size(); i++) {
        args[i] = print_expr(op->args[i]);
    }
    ostringstream rhs;
    rhs << op->name << "(" << with_commas(args) << ")";
    return rhs.str();
}

void CodeGen_Metal_Dev::CodeGen_Metal_C::visit(const Max *op) {
    print_expr(Call::make(op->type, "max", {op->a, op->b}, Call::Extern));
}

void CodeGen_Metal_Dev::CodeGen_Metal_C::visit(const Min *op) {
    print_expr(Call::make(op->type, "min", {op->a, op->b}, Call::Extern));
}

void CodeGen_Metal_Dev::CodeGen_Metal_C::visit(const VectorReduce *op) {
    if (op->op == VectorReduce::Add && op->type.is_float() && (op->type.lanes() == 1)) {
        if (const Mul *maybe_mul = op->value.as<Mul>()) {
            string a = print_expr(maybe_mul->a);
            string b = print_expr(maybe_mul->b);
            ostringstream rhs;
            rhs << "dot(" << a << ", " << b << ")";
            print_assignment(op->type, rhs.str());
            return;
        }
    }
    CodeGen_GPU_C::visit(op);
}

void CodeGen_Metal_Dev::CodeGen_Metal_C::visit(const Div *op) {
    if (auto bits = is_const_power_of_two_integer(op->b)) {
        ostringstream oss;
        oss << print_expr(op->a) << " >> " << *bits;
        print_assignment(op->type, oss.str());
    } else if (op->type.is_int()) {
        print_expr(lower_euclidean_div(op->a, op->b));
    } else {
        visit_binop(op->type, op->a, op->b, "/");
    }
}

void CodeGen_Metal_Dev::CodeGen_Metal_C::visit(const Mod *op) {
    if (auto bits = is_const_power_of_two_integer(op->b)) {
        ostringstream oss;
        oss << print_expr(op->a) << " & " << (((uint64_t)1 << *bits) - 1);
        print_assignment(op->type, oss.str());
    } else if (op->type.is_int()) {
        print_expr(lower_euclidean_mod(op->a, op->b));
    } else {
        visit_binop(op->type, op->a, op->b, "%");
    }
}

void CodeGen_Metal_Dev::CodeGen_Metal_C::visit(const For *loop) {
    user_assert(loop->for_type != ForType::GPULane)
        << "The Metal backend does not support the gpu_lanes() scheduling directive.";

    if (is_gpu(loop->for_type)) {
        internal_assert(is_const_zero(loop->min));

        stream << get_indent() << print_type(Int(32)) << " " << print_name(loop->name)
               << " = " << simt_intrinsic(loop->name) << ";\n";

        loop->body.accept(this);

    } else {
        user_assert(loop->for_type != ForType::Parallel) << "Cannot use parallel loops inside Metal kernel\n";
        CodeGen_GPU_C::visit(loop);
    }
}

void CodeGen_Metal_Dev::CodeGen_Metal_C::visit(const Ramp *op) {
    string id_base = print_expr(op->base);
    string id_stride = print_expr(op->stride);

    ostringstream rhs;
    rhs << id_base << " + " << id_stride << " * "
        << print_type(op->type.with_lanes(op->lanes)) << "(0";
    // Note 0 written above.
    for (int i = 1; i < op->lanes; ++i) {
        rhs << ", " << i;
    }
    rhs << ")";
    print_assignment(op->type.with_lanes(op->lanes), rhs.str());
}

void CodeGen_Metal_Dev::CodeGen_Metal_C::visit(const Broadcast *op) {
    string id_value = print_expr(op->value);

    ostringstream rhs;
    rhs << print_type(op->type.with_lanes(op->lanes)) << "(" << id_value << ")";

    print_assignment(op->type.with_lanes(op->lanes), rhs.str());
}

void CodeGen_Metal_Dev::CodeGen_Metal_C::visit(const Call *op) {
    if (op->is_intrinsic(Call::gpu_thread_barrier)) {
        internal_assert(op->args.size() == 1) << "gpu_thread_barrier() intrinsic must specify memory fence type.\n";

        auto fence_type_ptr = as_const_int(op->args[0]);
        internal_assert(fence_type_ptr) << "gpu_thread_barrier() parameter is not a constant integer.\n";
        auto fence_type = *fence_type_ptr;

        // This is quite annoying: even though the MSL docs claim these flags can be combined,
        // Metal compilers prior to Metal 1.2 give compiler errors.  So, we do not combine them,
        // and rather use a preprocessor definition to do the right thing.

        stream << get_indent() << "threadgroup_barrier(";
        if (fence_type & CodeGen_GPU_Dev::MemoryFenceType::Device &&
            fence_type & CodeGen_GPU_Dev::MemoryFenceType::Shared) {
            stream << "_halide_mem_fence_device_and_threadgroup";
        } else if (fence_type & CodeGen_GPU_Dev::MemoryFenceType::Device) {
            stream << "mem_flags::mem_device";
        } else if (fence_type & CodeGen_GPU_Dev::MemoryFenceType::Shared) {
            stream << "mem_flags::mem_threadgroup";
        } else {
            stream << "mem_flags::mem_none";
        }
        stream << ");\n";
        print_assignment(op->type, "0");
    } else {
        CodeGen_GPU_C::visit(op);
    }
}

namespace {

// If e is a ramp expression with stride 1, return the base, otherwise undefined.
Expr is_ramp_one(const Expr &e) {
    const Ramp *r = e.as<Ramp>();
    if (r == nullptr) {
        return Expr();
    }

    if (is_const_one(r->stride)) {
        return r->base;
    }

    return Expr();
}
}  // namespace

string CodeGen_Metal_Dev::CodeGen_Metal_C::get_memory_space(const string &buf) {
    if (buf == shared_name) {
        return "threadgroup";
    } else {
        return "__address_space_" + print_name(buf);
    }
}

void CodeGen_Metal_Dev::CodeGen_Metal_C::visit(const Load *op) {
    user_assert(is_const_one(op->predicate)) << "Predicated load is not supported inside Metal kernel.\n";
    user_assert(op->type.lanes() <= 4) << "Vectorization by widths greater than 4 is not supported by Metal -- type is " << op->type << ".\n";

    // If we're loading a contiguous ramp, load from a vector type pointer.
    Expr ramp_base = is_ramp_one(op->index);
    if (ramp_base.defined()) {
        internal_assert(op->type.is_vector());
        string id_ramp_base = print_expr(ramp_base);

        ostringstream rhs;
        rhs << "*(" << get_memory_space(op->name) << " " << print_storage_type(op->type) << " *)(("
            << get_memory_space(op->name) << " " << print_type(op->type.element_of()) << " *)" << print_name(op->name)
            << " + " << id_ramp_base << ")";
        print_assignment(op->type, rhs.str());

        return;
    }

    string id_index = print_expr(op->index);

    // Get the rhs just for the cache.
    const auto *alloc = allocations.find(op->name);
    bool type_cast_needed = !(alloc &&
                              alloc->type == op->type);
    ostringstream rhs;
    if (type_cast_needed) {
        rhs << "((" << get_memory_space(op->name) << " "
            << print_storage_type(op->type) << " *)"
            << print_name(op->name)
            << ")";
    } else {
        rhs << print_name(op->name);
    }
    rhs << "[" << id_index << "]";

    std::map<string, string>::iterator cached = cache.find(rhs.str());
    if (cached != cache.end()) {
        id = cached->second;
        return;
    }

    if (op->index.type().is_vector()) {
        // If index is a vector, gather vector elements.
        internal_assert(op->type.is_vector());

        // This has to be underscore as print_name prepends an underscore to
        // names without one and that results in a name mismatch if a Load
        // appears as the value of a Let
        id = unique_name('_');
        cache[rhs.str()] = id;

        stream << get_indent() << print_type(op->type)
               << " " << id << ";\n";

        for (int i = 0; i < op->type.lanes(); ++i) {
            stream << get_indent();
            stream
                << id << "[" << i << "]"
                << " = ((" << get_memory_space(op->name) << " "
                << print_type(op->type.element_of()) << "*)"
                << print_name(op->name) << ")"
                << "[" << id_index << "[" << i << "]];\n";
        }
    } else {
        print_assignment(op->type, rhs.str());
    }
}

void CodeGen_Metal_Dev::CodeGen_Metal_C::visit(const Store *op) {
    user_assert(is_const_one(op->predicate)) << "Predicated store is not supported inside Metal kernel.\n";
    user_assert(op->value.type().lanes() <= 4) << "Vectorization by widths greater than 4 is not supported by Metal -- type is " << op->value.type() << ".\n";

    string id_value = print_expr(op->value);
    Type t = op->value.type();

    // If we're writing a contiguous ramp, store through a pointer of vector type.
    Expr ramp_base = is_ramp_one(op->index);
    if (ramp_base.defined()) {
        internal_assert(op->value.type().is_vector());
        string id_ramp_base = print_expr(ramp_base);

        stream << get_indent() << "*(" << get_memory_space(op->name) << " " << print_storage_type(t) << " *)(("
               << get_memory_space(op->name) << " " << print_type(t.element_of()) << " *)" << print_name(op->name)
               << " + " << id_ramp_base << ") = " << id_value << ";\n";
    } else if (op->index.type().is_vector()) {
        // If index is a vector, scatter vector elements.
        internal_assert(t.is_vector());

        string id_index = print_expr(op->index);

        for (int i = 0; i < t.lanes(); ++i) {
            stream << get_indent() << "((" << get_memory_space(op->name) << " "
                   << print_storage_type(t.element_of()) << " *)"
                   << print_name(op->name)
                   << ")["
                   << id_index << "[" << i << "]] = "
                   << id_value << "[" << i << "];\n";
        }
    } else {
        const auto *alloc = allocations.find(op->name);
        bool type_cast_needed = !(alloc && alloc->type == t);

        string id_index = print_expr(op->index);
        stream << get_indent();

        if (type_cast_needed) {
            stream << "(("
                   << get_memory_space(op->name) << " "
                   << print_storage_type(t)
                   << " *)"
                   << print_name(op->name)
                   << ")";
        } else {
            stream << print_name(op->name);
        }
        stream << "[" << id_index << "] = "
               << id_value << ";\n";
    }

    cache.clear();
}

void CodeGen_Metal_Dev::CodeGen_Metal_C::visit(const Select *op) {
    ostringstream rhs;
    string true_val = print_expr(op->true_value);
    string false_val = print_expr(op->false_value);
    string cond = print_expr(op->condition);
    rhs << "(" << print_type(op->type) << ")"
        << "select(" << false_val
        << ", " << true_val
        << ", " << cond
        << ")";
    print_assignment(op->type, rhs.str());
}

void CodeGen_Metal_Dev::CodeGen_Metal_C::visit(const Allocate *op) {

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

        stream << get_indent() << print_storage_type(op->type) << " "
               << print_name(op->name) << "[" << size << "];\n";
        stream << get_indent() << "#define " << get_memory_space(op->name) << " thread\n";

        Allocation alloc;
        alloc.type = op->type;
        allocations.push(op->name, alloc);

        op->body.accept(this);

        // Should have been freed internally
        internal_assert(!allocations.contains(op->name));

        close_scope("alloc " + print_name(op->name));
    }
}

void CodeGen_Metal_Dev::CodeGen_Metal_C::visit(const Free *op) {
    if (op->name == shared_name) {
        return;
    } else {
        // Should have been freed internally
        internal_assert(allocations.contains(op->name));
        allocations.pop(op->name);
        stream << get_indent() << "#undef " << get_memory_space(op->name) << "\n";
    }
}

void CodeGen_Metal_Dev::CodeGen_Metal_C::visit(const Cast *op) {
    print_assignment(op->type, print_type(op->type) + "(" + print_expr(op->value) + ")");
}

void CodeGen_Metal_Dev::CodeGen_Metal_C::visit(const Atomic *op) {
    // It might be possible to support atomic but this is not trivial.
    // Metal requires atomic data types to be wrapped in an atomic integer data type.
    user_assert(false) << "Atomic updates are not supported inside Metal kernels";
}

void CodeGen_Metal_Dev::CodeGen_Metal_C::visit(const FloatImm *op) {
    if (op->type.bits() == 16) {
        float16_t f(op->value);
        if (f.is_nan()) {
            id = "nan_f16()";
        } else if (f.is_infinity()) {
            if (!f.is_negative()) {
                id = "inf_f16()";
            } else {
                id = "neg_inf_f16()";
            }
        } else {
            // Write the constant as reinterpreted uint to avoid any bits lost in conversion.
            ostringstream oss;
            oss << "half_from_bits(" << f.to_bits() << " /* " << float(f) << " */)";
            print_assignment(op->type, oss.str());
        }
    } else {
        if (std::isnan(op->value)) {
            id = "nan_f32()";
        } else if (std::isinf(op->value)) {
            if (op->value > 0) {
                id = "inf_f32()";
            } else {
                id = "neg_inf_f32()";
            }
        } else {
            // Write the constant as reinterpreted uint to avoid any bits lost in conversion.
            ostringstream oss;
            union {
                uint32_t as_uint;
                float as_float;
            } u;
            u.as_float = op->value;
            if (op->type.bits() == 64) {
                user_error << "Metal does not support 64-bit floating point literals.\n";
            } else if (op->type.bits() == 32) {
                oss << "float_from_bits(" << u.as_uint << " /* " << u.as_float << " */)";
            } else {
                user_error << "Unsupported floating point literal with " << op->type.bits() << " bits.\n";
            }
            print_assignment(op->type, oss.str());
        }
    }
}
void CodeGen_Metal_Dev::add_kernel(Stmt s,
                                   const string &name,
                                   const vector<DeviceArgument> &args) {
    debug(2) << "CodeGen_Metal_Dev::compile " << name << "\n";

    // We need to scalarize/de-predicate any loads/stores, since Metal does not
    // support predication.
    s = scalarize_predicated_loads_stores(s);

    debug(2) << "CodeGen_Metal_Dev: after removing predication: \n"
             << s;

    // TODO: do we have to uniquify these names, or can we trust that they are safe?
    cur_kernel_name = name;
    metal_c.add_kernel(s, name, args);
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

void CodeGen_Metal_Dev::CodeGen_Metal_C::add_kernel(const Stmt &s,
                                                    const string &name,
                                                    const vector<DeviceArgument> &args) {

    debug(2) << "Adding Metal kernel " << name << "\n";

    // Figure out which arguments should be passed in constant.
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
    // as many of the constant allocations in constant as possible.
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
                stream << "#if " << constant->size << " < MAX_CONSTANT_BUFFER_SIZE && "
                       << constant - constants.begin() << " < MAX_CONSTANT_ARGS\n";
                stream << "#define " << get_memory_space(arg.name) << " constant\n";
                stream << "#else\n";
                stream << "#define " << get_memory_space(arg.name) << " device\n";
                stream << "#endif\n";
            } else {
                stream << "#define " << get_memory_space(arg.name) << " device\n";
            }
        }
    }

    // Emit a struct to hold the scalar args of the kernel
    bool any_scalar_args = false;
    for (const auto &arg : args) {
        if (!arg.is_buffer) {
            if (!any_scalar_args) {
                stream << "struct " + name + "_args {\n";
                any_scalar_args = true;
            }
            stream << print_type(arg.type)
                   << " "
                   << print_name(arg.name)
                   << ";\n";
        }
    }
    if (any_scalar_args) {
        stream << "};\n";
    }

    // Emit the function prototype
    stream << "kernel void " << name << "(\n";
    stream << "uint3 tgroup_index [[ threadgroup_position_in_grid ]],\n"
           << "uint3 tid_in_tgroup [[ thread_position_in_threadgroup ]]";
    size_t buffer_index = 0;
    if (any_scalar_args) {
        stream << ",\nconst device " << name << "_args *_scalar_args [[ buffer(0) ]]";
        buffer_index++;
    }

    for (const auto &arg : args) {
        if (arg.is_buffer) {
            stream << ",\n";
            stream << " " << get_memory_space(arg.name) << " ";
            if (!arg.write) {
                stream << "const ";
            }
            stream << print_storage_type(arg.type) << " *"
                   << print_name(arg.name) << " [[ buffer(" << buffer_index++ << ") ]]";
            Allocation alloc;
            alloc.type = arg.type;
            allocations.push(arg.name, alloc);
        }
    }

    class FindShared : public IRVisitor {
        using IRVisitor::visit;
        void visit(const Allocate *op) override {
            if (op->memory_type == MemoryType::GPUShared) {
                internal_assert(alloc == nullptr)
                    << "Found multiple shared allocations in metal kernel\n";
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
    // Note that int4 below is an int32x4, not an int4_t. The type
    // is chosen to be large to maximize alignment.
    stream << ",\n"
           << " threadgroup int4* "
           << print_name(shared_name) << " [[ threadgroup(0) ]]"
           << ")\n";

    open_scope();

    // Unpack args struct into local variables to match naming of generated code.
    for (const auto &arg : args) {
        if (!arg.is_buffer) {
            stream << print_type(arg.type)
                   << " "
                   << print_name(arg.name)
                   << " = _scalar_args->" << print_name(arg.name)
                   << ";\n";
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

void CodeGen_Metal_Dev::init_module() {
    debug(2) << "Metal device codegen init_module\n";

    // wipe the internal kernel source
    src_stream.str("");
    src_stream.clear();

    // Write out the Halide math functions.
    src_stream << "#pragma clang diagnostic ignored \"-Wunused-function\"\n"
               << "#include <metal_stdlib>\n"
               << "using namespace metal;\n"  // Seems like the right way to go.
               << "namespace {\n"
               << "constexpr float float_from_bits(unsigned int x) {return as_type<float>(x);}\n"
               << "constexpr float nan_f32() { return as_type<float>(0x7fc00000); }\n"  // Quiet NaN with minimum fractional value.
               << "constexpr float neg_inf_f32() { return float_from_bits(0xff800000); }\n"
               << "constexpr float inf_f32() { return float_from_bits(0x7f800000); }\n"
               << "float fast_inverse_f32(float x) { return 1.0f / x; }\n"
               << "#define is_nan_f32 isnan\n"
               << "#define is_inf_f32 isinf\n"
               << "#define is_finite_f32 isfinite\n"
               << "#define sqrt_f32 sqrt\n"
               << "#define sin_f32 sin\n"
               << "#define cos_f32 cos\n"
               << "#define exp_f32 exp\n"
               << "#define log_f32 log\n"
               << "#define abs_f32 fabs\n"
               << "#define floor_f32 floor\n"
               << "#define ceil_f32 ceil\n"
               << "#define trunc_f32 trunc\n"
               << "#define pow_f32 pow\n"
               << "#define asin_f32 asin\n"
               << "#define acos_f32 acos\n"
               << "#define tan_f32 tan\n"
               << "#define atan_f32 atan\n"
               << "#define atan2_f32 atan2\n"
               << "#define sinh_f32 sinh\n"
               << "#define asinh_f32 asinh\n"
               << "#define cosh_f32 cosh\n"
               << "#define acosh_f32 acosh\n"
               << "#define tanh_f32 tanh\n"
               << "#define atanh_f32 atanh\n"
               << "#define fast_inverse_sqrt_f32 rsqrt\n"
               << "#define is_nan_f16 isnan\n"
               << "#define is_inf_f16 isinf\n"
               << "#define is_finite_f16 isfinite\n"
               << "#define sqrt_f16 sqrt\n"
               << "#define sin_f16 sin\n"
               << "#define cos_f16 cos\n"
               << "#define exp_f16 exp\n"
               << "#define log_f16 log\n"
               << "#define abs_f16 fabs\n"
               << "#define floor_f16 floor\n"
               << "#define ceil_f16 ceil\n"
               << "#define trunc_f16 trunc\n"
               << "#define pow_f16 pow\n"
               << "#define asin_f16 asin\n"
               << "#define acos_f16 acos\n"
               << "#define tan_f16 tan\n"
               << "#define atan_f16 atan\n"
               << "#define atan2_f16 atan2\n"
               << "#define sinh_f16 sinh\n"
               << "#define asinh_f16 asinh\n"
               << "#define cosh_f16 cosh\n"
               << "#define acosh_f16 acosh\n"
               << "#define tanh_f16 tanh\n"
               << "#define atanh_f16 atanh\n"
               << "#define fast_sin_f32 fast::sin \n"
               << "#define fast_cos_f32 fast::cos \n"
               << "#define fast_tan_f32 fast::tan \n"
               << "#define fast_exp_f32 fast::exp \n"
               << "#define fast_log_f32 fast::log \n"
               << "#define fast_pow_f32 fast::pow \n"
               << "#define fast_tanh_f32 fast::tanh \n"
               << "#define fast_inverse_sqrt_f16 rsqrt\n"
               << "constexpr half half_from_bits(unsigned short x) {return as_type<half>(x);}\n"
               << "constexpr half nan_f16() { return half_from_bits(32767); }\n"
               << "constexpr half neg_inf_f16() { return half_from_bits(64512); }\n"
               << "constexpr half inf_f16() { return half_from_bits(31744); }\n"
               // This is quite annoying: even though the MSL docs claim
               // all versions of Metal support the same memory fence
               // names, the truth is that 1.0 does not.
               << "#if __METAL_VERSION__ >= 120\n"
               << "#define _halide_mem_fence_device_and_threadgroup (mem_flags::mem_device | mem_flags::mem_threadgroup)\n"
               << "#else\n"
               << "#define _halide_mem_fence_device_and_threadgroup mem_flags::mem_device_and_threadgroup\n"
               << "#endif\n"
               << "}\n";  // close namespace

    src_stream << "#define halide_maybe_unused(x) (void)(x)\n";

    src_stream << "\n";

    cur_kernel_name = "";
}

vector<char> CodeGen_Metal_Dev::compile_to_src() {
    string str = src_stream.str();
    debug(1) << "Metal kernel:\n"
             << str << "\n";
    vector<char> buffer(str.begin(), str.end());
    buffer.push_back(0);
    return buffer;
}

string CodeGen_Metal_Dev::get_current_kernel_name() {
    return cur_kernel_name;
}

void CodeGen_Metal_Dev::dump() {
    std::cerr << src_stream.str() << "\n";
}

std::string CodeGen_Metal_Dev::print_gpu_name(const std::string &name) {
    return name;
}

}  // namespace

std::unique_ptr<CodeGen_GPU_Dev> new_CodeGen_Metal_Dev(const Target &target) {
    return std::make_unique<CodeGen_Metal_Dev>(target);
}

}  // namespace Internal
}  // namespace Halide
