#include <cmath>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "CanonicalizeGPUVars.h"
#include "CodeGen_GPU_Dev.h"
#include "CodeGen_Internal.h"
#include "CodeGen_WebGPU_Dev.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

using std::ostringstream;
using std::string;
using std::vector;

namespace {

class CodeGen_WebGPU_Dev : public CodeGen_GPU_Dev {
public:
    CodeGen_WebGPU_Dev(const Target &target);

    /** Compile a GPU kernel into the module. This may be called many times
     * with different kernels, which will all be accumulated into a single
     * source module shared by a given Halide pipeline. */
    void add_kernel(Stmt stmt,
                    const string &name,
                    const vector<DeviceArgument> &args) override;

    /** (Re)initialize the GPU kernel module. This is separate from compile,
     * since a GPU device module will often have many kernels compiled into it
     * for a single pipeline. */
    void init_module() override;

    vector<char> compile_to_src() override;

    string get_current_kernel_name() override;

    void dump() override;

    string print_gpu_name(const string &name) override;

    string api_unique_name() override {
        return "webgpu";
    }

    bool kernel_run_takes_types() const override {
        return true;
    }

protected:
    class CodeGen_WGSL : public CodeGen_GPU_C {
    public:
        CodeGen_WGSL(std::ostream &s, Target t)
            : CodeGen_GPU_C(s, t) {
            vector_declaration_style = VectorDeclarationStyle::WGSLSyntax;

#define alias(x, y)                         \
    extern_function_name_map[x "_f16"] = y; \
    extern_function_name_map[x "_f32"] = y

            alias("sqrt", "sqrt");
            alias("sin", "sin");
            alias("cos", "cos");
            alias("exp", "exp");
            alias("log", "log");
            alias("abs", "abs");
            alias("floor", "floor");
            alias("ceil", "ceil");
            alias("trunc", "trunc");
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

            alias("round", "round");

            alias("fast_inverse_sqrt", "inverseSqrt");
#undef alias
        }
        void add_kernel(const Stmt &stmt,
                        const string &name,
                        const vector<DeviceArgument> &args);

    protected:
        using CodeGen_GPU_C::visit;

        std::string print_name(const std::string &) override;
        std::string print_type(Type type,
                               AppendSpaceIfNeeded append_space =
                                   DoNotAppendSpace) override;
        std::string print_reinterpret(Type type, const Expr &e) override;
        std::string print_assignment(Type t, const std::string &rhs) override;
        std::string print_const(Type t, const std::string &rhs);
        std::string print_assignment_or_const(Type t, const std::string &rhs,
                                              bool const_expr);

        void visit(const Allocate *op) override;
        void visit(const And *op) override;
        void visit(const AssertStmt *op) override;
        void visit(const Broadcast *op) override;
        void visit(const Call *op) override;
        void visit(const Cast *) override;
        void visit(const Div *op) override;
        void visit(const Evaluate *op) override;
        void visit(const IntImm *) override;
        void visit(const UIntImm *) override;
        void visit(const FloatImm *) override;
        void visit(const Free *op) override;
        void visit(const For *) override;
        void visit(const Load *op) override;
        void visit(const Min *op) override;
        void visit(const Max *op) override;
        void visit(const Or *op) override;
        void visit(const Ramp *op) override;
        void visit(const Select *op) override;
        void visit(const Store *op) override;

        string kernel_name;
        std::unordered_set<string> buffers;
        std::unordered_set<string> buffers_with_emulated_accesses;
        std::unordered_map<string, const Allocate *> workgroup_allocations;
    };

    std::ostringstream src_stream;
    string cur_kernel_name;
    CodeGen_WGSL wgsl;
};

CodeGen_WebGPU_Dev::CodeGen_WebGPU_Dev(const Target &t)
    : wgsl(src_stream, t) {
}

void CodeGen_WebGPU_Dev::add_kernel(Stmt s,
                                    const string &name,
                                    const vector<DeviceArgument> &args) {
    debug(2) << "CodeGen_WebGPU_Dev::add_kernel " << name << "\n";

    // We need to scalarize/de-predicate any loads/stores, since WGSL does not
    // support predication.
    s = scalarize_predicated_loads_stores(s);
    debug(2) << "CodeGen_WebGPU_Dev: after removing predication: \n"
             << s;

    cur_kernel_name = name;
    wgsl.add_kernel(s, name, args);
}

void CodeGen_WebGPU_Dev::init_module() {
    debug(2) << "WebGPU device codegen init_module\n";

    // Wipe the internal shader source.
    src_stream.str("");
    src_stream.clear();

    // Write out the Halide math functions.
    src_stream
        << "fn float_from_bits(x : u32) -> f32 {return bitcast<f32>(x);}\n"
        << "fn nan_f32() -> f32 {return float_from_bits(0x7fc00000);}\n"
        << "fn neg_inf_f32() -> f32 {return float_from_bits(0xff800000);}\n"
        << "fn inf_f32() -> f32 {return float_from_bits(0x7f800000);}\n"
        << "fn fast_inverse_f32(x : f32) -> f32 {return 1.0 / x;}\n"
        // WGSL doesn't provide these by default, but we can exploit the nature
        // of comparison ops to construct them... although they are of dubious value
        // (since the WGSL spec says that "Implementations may assume that NaNs
        // and infinities are not present at runtime"), we'll provide these to
        // prevent outright compilation failure, and also as a convenience
        // if generating code for an implementaton that is known to preserve them.
        << "fn is_nan_f32(x : f32) -> bool {return x != x;}\n"
        << "fn is_inf_f32(x : f32) -> bool {return !is_nan_f32(x) && is_nan_f32(x - x);}\n"
        << "fn is_finite_f32(x : f32) -> bool {return !is_nan_f32(x) && !is_inf_f32(x);}\n";

    // Create pipeline-overridable constants for the workgroup size and
    // workgroup array size.
    src_stream << "\n"
               << "override wgsize_x : u32;\n"
               << "override wgsize_y : u32;\n"
               << "override wgsize_z : u32;\n"
               << "override workgroup_mem_bytes : u32;\n\n";
}

vector<char> CodeGen_WebGPU_Dev::compile_to_src() {
    string str = src_stream.str();
    debug(1) << "WGSL shader:\n"
             << str << "\n";
    vector<char> buffer(str.begin(), str.end());
    buffer.push_back(0);
    return buffer;
}

string CodeGen_WebGPU_Dev::get_current_kernel_name() {
    return cur_kernel_name;
}

void CodeGen_WebGPU_Dev::dump() {
    std::cerr << src_stream.str() << "\n";
}

string CodeGen_WebGPU_Dev::print_gpu_name(const string &name) {
    return name;
}

string CodeGen_WebGPU_Dev::CodeGen_WGSL::print_name(const string &name) {
    string new_name = c_print_name(name);

    // The double-underscore prefix is reserved in WGSL.
    if (new_name.length() > 1 && new_name[0] == '_' && new_name[1] == '_') {
        new_name = "v" + new_name;
    }

    // Prefix storage buffer and workgroup variable names with the kernel name
    // to avoid collisions.
    if (buffers.count(name) || workgroup_allocations.count(name)) {
        new_name = kernel_name + new_name;
    }

    return new_name;
}

string CodeGen_WebGPU_Dev::CodeGen_WGSL::print_type(Type type,
                                                    AppendSpaceIfNeeded space) {
    ostringstream oss;

    if (type.lanes() != 1) {
        switch (type.lanes()) {
        case 2:
        case 3:
        case 4:
            oss << "vec" << type.lanes() << "<";
            break;
        default:
            user_error << "Unsupported vector width in WGSL: " << type << "\n";
        }
    }

    if (type.is_float()) {
        user_assert(type.bits() == 32) << "WGSL only supports 32-bit floats";
        oss << "f32";
    } else {
        switch (type.bits()) {
        case 1:
            oss << "bool";
            break;
        case 8:
        case 16:
        case 32:
            oss << (type.is_uint() ? "u" : "i") << "32";
            break;
        default:
            user_error << "Invalid integer bitwidth for WGSL";
            break;
        }
    }

    if (type.lanes() != 1) {
        oss << ">";
    }

    if (space == AppendSpace) {
        oss << " ";
    }
    return oss.str();
}

string CodeGen_WebGPU_Dev::CodeGen_WGSL::print_reinterpret(Type type,
                                                           const Expr &e) {
    ostringstream oss;
    oss << "bitcast<" << print_type(type) << ">(" << print_expr(e) << ")";
    return oss.str();
}

void CodeGen_WebGPU_Dev::CodeGen_WGSL::add_kernel(
    const Stmt &s, const string &name, const vector<DeviceArgument> &args) {
    debug(2) << "Adding WGSL shader " << name << "\n";

    kernel_name = name;

    // Look for buffer accesses that will require emulation via atomics.
    class FindBufferAccessesRequiringEmulation : public IRVisitor {
        using IRVisitor::visit;

        void visit(const Load *op) override {
            if (op->type.element_of().bits() < 32) {
                needs_atomic_accesses.insert(op->name);
            }
            IRVisitor::visit(op);
        }

        void visit(const Store *op) override {
            if (op->value.type().element_of().bits() < 32) {
                needs_atomic_accesses.insert(op->name);
            }
            IRVisitor::visit(op);
        }

    public:
        std::unordered_set<string> needs_atomic_accesses;
    };

    FindBufferAccessesRequiringEmulation fbare;
    s.accept(&fbare);

    // The name of the variable that contains the non-buffer arguments.
    string args_var = "Args_" + name;

    std::ostringstream uniforms;
    uint32_t next_binding = 0;
    for (const DeviceArgument &arg : args) {
        if (arg.is_buffer) {
            // Emit buffer arguments as read_write storage buffers.
            buffers.insert(arg.name);
            std::string type_decl;
            if (fbare.needs_atomic_accesses.count(arg.name)) {
                user_warning
                    << "buffers of small integer types are currently emulated "
                    << "using atomics in the WebGPU backend, and accesses to "
                    << "them will be slow.";
                buffers_with_emulated_accesses.insert(arg.name);
                type_decl = "atomic<u32>";
            } else {
                type_decl = print_type(arg.type);
            }
            stream << "@group(0) @binding(" << next_binding << ")\n"
                   << "var<storage, read_write> " << print_name(arg.name)
                   << " : array<" << type_decl << ">;\n\n";
            Allocation alloc;
            alloc.type = arg.type;
            allocations.push(arg.name, alloc);
            next_binding++;
        } else {
            // Collect non-buffer arguments into a single uniform buffer.
            internal_assert(arg.type.bytes() <= 4)
                << "unimplemented: non-buffer args larger than 4 bytes";
            uniforms << "  " << print_name(arg.name) << " : ";
            if (arg.type == Bool()) {
                // The bool type cannot appear in a uniform, so use i32 instead.
                uniforms << "i32";
            } else {
                uniforms << print_type(arg.type);
            }
            uniforms << ",\n";
        }
    }
    if (!uniforms.str().empty()) {
        string struct_name = "ArgsStruct_" + name;
        stream << "struct " << struct_name << " {\n"
               << uniforms.str()
               << "}\n";
        stream << "@group(1) @binding(0)\n"
               << "var<uniform> "
               << args_var << " : " << struct_name << " ;\n\n";
    }

    // Emit the function prototype.
    stream << "@compute @workgroup_size(wgsize_x, wgsize_y, wgsize_z)\n";
    stream << "fn " << name << "(\n"
           << "  @builtin(local_invocation_id) local_id : vec3<u32>,\n"
           << "  @builtin(workgroup_id) group_id : vec3<u32>,\n"
           << ")\n";

    open_scope();

    stream << get_indent() << "_ = workgroup_mem_bytes;\n";

    // Redeclare non-buffer arguments at function scope.
    for (const DeviceArgument &arg : args) {
        if (!arg.is_buffer) {
            stream << get_indent() << "let " << print_name(arg.name)
                   << " = " << print_type(arg.type)
                   << "(" << args_var << "." << print_name(arg.name) << ");\n";
        }
    }

    // Generate function body.
    print(s);

    close_scope("shader " + name);

    for (const auto &[name, alloc] : workgroup_allocations) {
        std::stringstream length;
        if (is_const(alloc->extents[0])) {
            length << alloc->extents[0];
        } else {
            length << "workgroup_mem_bytes / " << alloc->type.bytes();
        }
        stream << "var<workgroup> " << print_name(name)
               << " : array<" << print_type(alloc->type) << ", "
               << length.str() << ">;\n";
    }
    workgroup_allocations.clear();

    for (const auto &arg : args) {
        // Remove buffer arguments from allocation scope and the buffer list.
        if (arg.is_buffer) {
            buffers.erase(arg.name);
            allocations.pop(arg.name);
        }
    }
}

void CodeGen_WebGPU_Dev::CodeGen_WGSL::visit(const Allocate *op) {
    if (op->memory_type == MemoryType::GPUShared) {
        internal_assert(!workgroup_allocations.count(op->name));
        workgroup_allocations.insert({op->name, op});
        op->body.accept(this);
    } else {
        open_scope();

        debug(2) << "Allocate " << op->name << " on device\n";

        // Allocation is not a shared memory allocation, just make a local
        // declaration.
        // It must have a constant size.
        int32_t size = op->constant_allocation_size();
        user_assert(size > 0)
            << "Allocation " << op->name << " has a dynamic size. "
            << "Only fixed-size allocations are supported on the gpu. "
            << "Try storing into shared memory instead.";

        stream << get_indent() << "var " << print_name(op->name)
               << " : array<" << print_type(op->type) << ", " << size << ">;\n";

        Allocation alloc;
        alloc.type = op->type;
        allocations.push(op->name, alloc);

        op->body.accept(this);

        // Should have been freed internally
        internal_assert(!allocations.contains(op->name));

        close_scope("alloc " + print_name(op->name));
    }
}

void CodeGen_WebGPU_Dev::CodeGen_WGSL::visit(const And *op) {
    const Expr &a = op->a;
    const Expr &b = op->b;
    const Type &t = op->type;
    if (t.is_scalar()) {
        visit_binop(t, a, b, "&");
    } else {
        internal_assert(a.type() == b.type());
        string sa = print_expr(a);
        string sb = print_expr(b);
        string rhs = print_type(t) + "(";
        for (int i = 0; i < t.lanes(); i++) {
            const string si = std::to_string(i);
            rhs += sa + "[" + si + "] & " + sb + "[" + si + "], ";
        }
        rhs += ")";
        print_assignment(t, rhs);
    }
}

void CodeGen_WebGPU_Dev::CodeGen_WGSL::visit(const AssertStmt *op) {
    user_warning << "Ignoring assertion inside WebGPU kernel: " << op->condition << "\n";
}

void CodeGen_WebGPU_Dev::CodeGen_WGSL::visit(const Broadcast *op) {
    const string id_value = print_expr(op->value);
    const Type type = op->type.with_lanes(op->lanes);
    print_assignment(type, print_type(type) + "(" + id_value + ")");
}

void CodeGen_WebGPU_Dev::CodeGen_WGSL::visit(const Call *op) {
    if (op->is_intrinsic(Call::gpu_thread_barrier)) {
        internal_assert(op->args.size() == 1)
            << "gpu_thread_barrier() intrinsic must specify fence type.\n";

        auto fence_type_ptr = as_const_int(op->args[0]);
        internal_assert(fence_type_ptr)
            << "gpu_thread_barrier() parameter is not a constant integer.\n";
        auto fence_type = *fence_type_ptr;

        stream << get_indent();
        if (fence_type & CodeGen_GPU_Dev::MemoryFenceType::Device) {
            stream << "storageBarrier();";
        }
        if (fence_type & CodeGen_GPU_Dev::MemoryFenceType::Shared ||
            fence_type == CodeGen_GPU_Dev::MemoryFenceType::None) {
            stream << "workgroupBarrier();";
        }
        stream << "\n";
        print_assignment(op->type, "0");
    } else if (op->is_intrinsic(Call::if_then_else)) {
        internal_assert(op->args.size() == 2 || op->args.size() == 3);

        string result_id = unique_name('_');
        stream << get_indent() << "var " << result_id
               << " : " << print_type(op->args[1].type()) << ";\n";

        // TODO: The rest of this is just copied from the C backend, so maybe
        // just introduce an overloadable `print_var_decl` instead.
        string cond_id = print_expr(op->args[0]);
        stream << get_indent() << "if (" << cond_id << ")\n";
        open_scope();
        string true_case = print_expr(op->args[1]);
        stream << get_indent() << result_id << " = " << true_case << ";\n";
        close_scope("if " + cond_id);
        if (op->args.size() == 3) {
            stream << get_indent() << "else\n";
            open_scope();
            string false_case = print_expr(op->args[2]);
            stream << get_indent() << result_id << " = " << false_case << ";\n";
            close_scope("if " + cond_id + " else");
        }
        print_assignment(op->type, result_id);
    } else if (op->is_intrinsic(Call::round)) {
        Expr equiv = Call::make(op->type, "round", op->args, Call::PureExtern);
        equiv.accept(this);
    } else if (op->is_extern() && op->name == "pow_f32") {
        // pow() in WGSL has the same semantics as C if x > 0.
        // Otherwise, we need to emulate the behavior.
        Expr ox = op->args[0];
        Expr oy = op->args[1];
        Expr equiv = Call::make(op->type, "pow", {abs(ox), oy}, Call::PureExtern);
        equiv = select(ox > 0.0f,
                       equiv,
                       select(oy == 0.0f,
                              1.0f,
                              select(oy == trunc(oy),
                                     select(cast<int>(oy) % 2 == 0,
                                            equiv,
                                            -equiv),
                                     float(std::nanf("")))));
        equiv.accept(this);

    } else {
        CodeGen_GPU_C::visit(op);
    }
}

void CodeGen_WebGPU_Dev::CodeGen_WGSL::visit(const Cast *op) {
    print_assignment(op->type,
                     print_type(op->type) + "(" + print_expr(op->value) + ")");
}

void CodeGen_WebGPU_Dev::CodeGen_WGSL::visit(const Div *op) {
    if (auto bits = is_const_power_of_two_integer(op->b)) {
        // WGSL requires the RHS of a shift to be unsigned.
        Type uint_type = op->a.type().with_code(halide_type_uint);
        visit_binop(op->type, op->a, make_const(uint_type, *bits), ">>");
    } else {
        CodeGen_GPU_C::visit(op);
    }
}

void CodeGen_WebGPU_Dev::CodeGen_WGSL::visit(const IntImm *op) {
    print_const(op->type, std::to_string(op->value));
}

void CodeGen_WebGPU_Dev::CodeGen_WGSL::visit(const UIntImm *op) {
    if (op->type == Bool()) {
        if (op->value == 1) {
            id = "true";
        } else {
            id = "false";
        }
    } else {
        print_const(op->type, std::to_string(op->value) + "u");
    }
}

void CodeGen_WebGPU_Dev::CodeGen_WGSL::visit(const FloatImm *op) {
    string rhs;
    if (std::isnan(op->value)) {
        rhs = "0x7FFFFFFF";
    } else if (std::isinf(op->value)) {
        if (op->value > 0) {
            rhs = "0x7F800000";
        } else {
            rhs = "0xFF800000";
        }
    } else {
        // Write the constant as reinterpreted uint to avoid any bits lost in
        // conversion.
        union {
            uint32_t as_uint;
            float as_float;
        } u;
        u.as_float = op->value;

        ostringstream oss;
        oss << "float_from_bits("
            << u.as_uint << "u /* " << u.as_float << " */)";
        rhs = oss.str();
    }
    print_assignment(op->type, rhs);
}

namespace {
string simt_intrinsic(const string &name) {
    if (ends_with(name, gpu_thread_name(0))) {
        return "local_id.x";
    } else if (ends_with(name, gpu_thread_name(1))) {
        return "local_id.y";
    } else if (ends_with(name, gpu_thread_name(2))) {
        return "local_id.z";
    } else if (ends_with(name, gpu_block_name(0))) {
        return "group_id.x";
    } else if (ends_with(name, gpu_block_name(1))) {
        return "group_id.y";
    } else if (ends_with(name, gpu_block_name(2))) {
        return "group_id.z";
    }
    internal_error << "invalid simt_intrinsic name: " << name << "\n";
    return "";
}
}  // namespace

void CodeGen_WebGPU_Dev::CodeGen_WGSL::visit(const Evaluate *op) {
    if (is_const(op->value)) {
        return;
    }
    print_expr(op->value);
}

void CodeGen_WebGPU_Dev::CodeGen_WGSL::visit(const Free *op) {
    if (workgroup_allocations.count(op->name)) {
        return;
    } else {
        // Should have been freed internally
        internal_assert(allocations.contains(op->name));
        allocations.pop(op->name);
    }
}

void CodeGen_WebGPU_Dev::CodeGen_WGSL::visit(const For *loop) {
    user_assert(loop->for_type != ForType::GPULane)
        << "The WebGPU backend does not support the gpu_lanes() directive.";

    if (is_gpu(loop->for_type)) {
        internal_assert(is_const_zero(loop->min));

        stream << get_indent()
               << "let " << print_name(loop->name)
               << " = i32(" << simt_intrinsic(loop->name) << ");\n";

        loop->body.accept(this);

    } else {
        user_assert(loop->for_type == ForType::Serial)
            << "Can only use serial loops inside WebGPU shaders\n";

        string id_min = print_expr(loop->min);
        string id_extent = print_expr(loop->extent);
        string id_counter = print_name(loop->name);
        stream << get_indent() << "for (var "
               << id_counter << " = " << id_min << "; "
               << id_counter << " < " << id_min << " + " << id_extent << "; "
               // TODO: Use increment statement when supported by Chromium.
               << id_counter << " = " << id_counter << " + 1)\n";
        open_scope();
        loop->body.accept(this);
        close_scope("for " + print_name(loop->name));
    }
}

void CodeGen_WebGPU_Dev::CodeGen_WGSL::visit(const Load *op) {
    user_assert(is_const_one(op->predicate))
        << "Predicated loads are not supported for WebGPU.\n";

    Type result_type = op->type.element_of();

    // Get the allocation type, which may be different from the result type.
    Type alloc_type = result_type;
    if (const auto *alloc = allocations.find(op->name)) {
        alloc_type = alloc->type;
    } else if (workgroup_allocations.count(op->name)) {
        alloc_type = workgroup_allocations.at(op->name)->type;
    }

    const int bits = result_type.bits();
    const string name = print_name(op->name);
    const string bits_str = std::to_string(bits);
    const string elements = std::to_string(32 / bits);

    // Cast a loaded value to the result type if necessary,
    auto cast_if_needed = [&](const string &value) {
        if (result_type != alloc_type) {
            return print_type(result_type) + "(" + value + ")";
        } else {
            return value;
        }
    };

    // Load an 8- or 16-bit value from an array<atomic<u32>>.
    auto emulate_narrow_load = [&](const string &idx) {
        internal_assert(bits == 8 || bits == 16);
        internal_assert(!op->type.is_float());
        // Generated code (16-bit):
        //  (atomicLoad(&in.data[i/2]) >> u32((i%2)*16)) & 0xFFFFu;
        string load;
        load = "atomicLoad(&" + name + "[" + idx + " / " + elements + "])";
        load += " >> u32((" + idx + " % " + elements + ") * " + bits_str + ")";
        load = "(" + load + ") & " + std::to_string((1 << bits) - 1) + "u";
        if (op->type.is_int()) {
            // Convert to i32 and sign-extend.
            const string shift = std::to_string(32 - bits);
            load = "i32((" + load + ") << " + shift + "u) >> " + shift + "u";
        }
        return load;
    };

    // TODO: Use cache to avoid re-loading same value multiple times.

    const string idx = print_expr(op->index);
    if (op->type.is_scalar()) {
        string rhs;
        if (buffers_with_emulated_accesses.count(op->name)) {
            if (bits == 32) {
                rhs = "bitcast<" + print_type(result_type) +
                      ">(atomicLoad(&" + name + "[" + idx + "]))";
            } else {
                rhs = emulate_narrow_load(idx);
            }
        } else {
            rhs = name + "[" + idx + "]";
        }
        print_assignment(op->type, cast_if_needed(rhs));
        return;
    } else if (op->type.is_vector()) {
        id = "_" + unique_name('V');

        // TODO: Could be smarter about this for a dense ramp.
        stream << get_indent()
               << "var " << id << " : " << print_type(op->type) << ";\n";
        for (int i = 0; i < op->type.lanes(); ++i) {
            stream << get_indent() << id << "[" << i << "] = ";
            const string idx_i = idx + "[" + std::to_string(i) + "]";
            string rhs;
            if (buffers_with_emulated_accesses.count(op->name)) {
                if (bits == 32) {
                    rhs = "bitcast<" + print_type(result_type) +
                          ">(atomicLoad(&" + name + "[" + idx_i + "]))";
                } else {
                    rhs = emulate_narrow_load(idx_i);
                }
            } else {
                rhs = name + "[" + idx_i + "]";
            }
            stream << cast_if_needed(rhs) << ";\n";
        }
        return;
    }

    internal_error << "unhandled type of load for WGSL";
}

void CodeGen_WebGPU_Dev::CodeGen_WGSL::visit(const Max *op) {
    print_expr(Call::make(op->type, "max", {op->a, op->b}, Call::Extern));
}

void CodeGen_WebGPU_Dev::CodeGen_WGSL::visit(const Min *op) {
    print_expr(Call::make(op->type, "min", {op->a, op->b}, Call::Extern));
}

void CodeGen_WebGPU_Dev::CodeGen_WGSL::visit(const Or *op) {
    const Expr &a = op->a;
    const Expr &b = op->b;
    const Type &t = op->type;
    if (t.is_scalar()) {
        visit_binop(t, a, b, "|");
    } else {
        internal_assert(a.type() == b.type());
        string sa = print_expr(a);
        string sb = print_expr(b);
        string rhs = print_type(t) + "(";
        for (int i = 0; i < t.lanes(); i++) {
            const string si = std::to_string(i);
            rhs += sa + "[" + si + "] | " + sb + "[" + si + "], ";
        }
        rhs += ")";
        print_assignment(t, rhs);
    }
}

void CodeGen_WebGPU_Dev::CodeGen_WGSL::visit(const Ramp *op) {
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

void CodeGen_WebGPU_Dev::CodeGen_WGSL::visit(const Select *op) {
    string true_val = print_expr(op->true_value);
    string false_val = print_expr(op->false_value);
    string cond = print_expr(op->condition);
    string select = "select(" + false_val + ", " + true_val + ", " + cond + ")";
    print_assignment(op->type, select);
}

void CodeGen_WebGPU_Dev::CodeGen_WGSL::visit(const Store *op) {
    user_assert(is_const_one(op->predicate))
        << "Predicated stores are not supported for WebGPU.\n";

    Type value_type = op->value.type().element_of();

    // Get the allocation type, which may be different from the value type.
    Type alloc_type = value_type;
    if (const auto *alloc = allocations.find(op->name)) {
        alloc_type = alloc->type;
    } else if (workgroup_allocations.count(op->name)) {
        alloc_type = workgroup_allocations.at(op->name)->type;
    }

    // Cast a value to the store type if necessary,
    auto cast_if_needed = [&](const string &value) {
        if (alloc_type != value_type) {
            return print_type(alloc_type) + "(" + value + ")";
        } else {
            return value;
        }
    };

    const int bits = value_type.bits();
    const string name = print_name(op->name);
    const string bits_str = std::to_string(bits);
    const string elements = std::to_string(32 / bits);

    // Store an 8- or 16-bit value to an array<atomic<u32>>.
    auto emulate_narrow_store = [&](const string &idx, const string &value) {
        internal_assert(bits == 8 || bits == 16);
        internal_assert(!op->value.type().is_float());
        // Generated code (16-bits):
        //  let shift = u32(i % 2) * 16u;
        //  var old = atomicLoad(&out[i / 2u]);
        //  while (true) {
        //    let mask = ((old >> shift) ^ bitcast<u32>(value)) & 0xFFFFu;
        //    let newval = old ^ (mask << shift);
        //    let result = atomicCompareExchangeWeak(&out[i / 2u], old, newval);
        //    if (result.exchanged) {
        //      break;
        //    }
        //    old = result.old_value;
        // }
        const string shift = "_" + unique_name('S');
        const string old = "_" + unique_name('O');
        stream << get_indent() << "let " << shift << " = u32(" << idx << " % "
               << elements << ") * " << bits_str << "u;\n";
        stream << get_indent() << "var " << old << " = atomicLoad(&"
               << name << "[" << idx << " / " << elements << "]);\n";
        stream << get_indent() << "for (;;) {\n";
        stream << get_indent() << "  let mask = ((" << old << " >> "
               << shift << ") ^ bitcast<u32>(" << value << ")) & "
               << std::to_string((1 << bits) - 1) << "u;\n";
        stream << get_indent() << "  let newval = " << old << " ^ (mask << "
               << shift << ");\n";
        stream << get_indent() << "  let result = atomicCompareExchangeWeak(&"
               << name << "[" << idx << " / " << elements << "], "
               << old << ", newval);\n";
        stream << get_indent() << "  if (result.exchanged) { break; }\n";
        stream << get_indent() << "  " << old << " = result.old_value;\n";
        stream << get_indent() << "}\n";
    };

    const string idx = print_expr(op->index);
    const string rhs = print_expr(op->value);

    if (op->value.type().is_scalar()) {
        if (buffers_with_emulated_accesses.count(op->name)) {
            if (bits == 32) {
                stream << get_indent() << "atomicStore(&"
                       << name << "[" << idx << "], "
                       << "bitcast<u32>(" << rhs << "));\n";
            } else {
                emulate_narrow_store(idx, rhs);
            }
        } else {
            stream << get_indent() << name << "[" << idx << "] = ";
            stream << cast_if_needed(rhs) << ";\n";
        }
    } else if (op->value.type().is_vector()) {
        // TODO: Could be smarter about this for a dense ramp.
        for (int i = 0; i < op->value.type().lanes(); ++i) {
            const string idx_i = idx + "[" + std::to_string(i) + "]";
            string value_i = rhs + "[" + std::to_string(i) + "]";
            if (buffers_with_emulated_accesses.count(op->name)) {
                if (bits == 32) {
                    stream << get_indent() << "atomicStore(&"
                           << name << "[" << idx_i << "], "
                           << "bitcast<u32>(" << value_i << "));\n";
                } else {
                    emulate_narrow_store(idx_i, value_i);
                }
            } else {
                stream << get_indent() << name << "[" << idx_i << "] = ";
                stream << cast_if_needed(value_i) << ";\n";
            }
        }
    }

    // Need a cache clear on stores to avoid reusing stale loaded
    // values from before the store.
    cache.clear();
}

string CodeGen_WebGPU_Dev::CodeGen_WGSL::print_assignment(
    Type t, const std::string &rhs) {
    return print_assignment_or_const(t, rhs, false);
}

string CodeGen_WebGPU_Dev::CodeGen_WGSL::print_const(
    Type t, const std::string &rhs) {
    return print_assignment_or_const(t, rhs, true);
}

string CodeGen_WebGPU_Dev::CodeGen_WGSL::print_assignment_or_const(
    Type t, const std::string &rhs, bool const_expr) {
    auto cached = cache.find(rhs);
    if (cached == cache.end()) {
        id = unique_name('_');
        stream << get_indent() << (const_expr ? "const" : "let") << " " << id
               << " : " << print_type(t) << " = " << rhs << ";\n";
        cache[rhs] = id;
    } else {
        id = cached->second;
    }
    return id;
}

}  // namespace

std::unique_ptr<CodeGen_GPU_Dev> new_CodeGen_WebGPU_Dev(const Target &target) {
    return std::make_unique<CodeGen_WebGPU_Dev>(target);
}

}  // namespace Internal
}  // namespace Halide
