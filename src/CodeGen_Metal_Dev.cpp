#include <algorithm>
#include <sstream>

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

static ostringstream nil;

CodeGen_Metal_Dev::CodeGen_Metal_Dev(Target t) :
    metal_c(src_stream, t) {
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
        if (type.is_uint() && type.bits() > 1) oss << 'u';
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
            user_error <<  "Unsupported vector width in Metal C: " << type << "\n";
        }
    }
    if (space == AppendSpace) {
        oss << ' ';
    }
    return oss.str();
}

string CodeGen_Metal_Dev::CodeGen_Metal_C::print_type(Type type, AppendSpaceIfNeeded space) {
    return print_type_maybe_storage(type, false, space);
}

string CodeGen_Metal_Dev::CodeGen_Metal_C::print_storage_type(Type type) {
    return print_type_maybe_storage(type, true, DoNotAppendSpace);
}

string CodeGen_Metal_Dev::CodeGen_Metal_C::print_reinterpret(Type type, Expr e) {
    ostringstream oss;

    string temp = unique_name('V');
    string expr = print_expr(e);
    do_indent();
    stream << print_type(e.type()) << " " << temp << " = " << expr << ";\n";
    oss << "*(" << print_type(type) << " thread *)(&" << temp << ")";
    return oss.str();
}



namespace {
string simt_intrinsic(const string &name) {
    if (ends_with(name, ".__thread_id_x")) {
        return "tid_in_tgroup.x";
    } else if (ends_with(name, ".__thread_id_y")) {
        return "tid_in_tgroup.y";
    } else if (ends_with(name, ".__thread_id_z")) {
        return "tid_in_tgroup.z";
    } else if (ends_with(name, ".__thread_id_w")) {
        user_error << "Metal does not support more than three dimensions in a kernel (threads).\n";
    } else if (ends_with(name, ".__block_id_x")) {
        return "tgroup_index.x";
    } else if (ends_with(name, ".__block_id_y")) {
        return "tgroup_index.y";
    } else if (ends_with(name, ".__block_id_z")) {
        return "tgroup_index.z";
    } else if (ends_with(name, ".__block_id_w")) {
        user_error << "Metal does not support more than three dimensions in a kernel (groups).\n";
    }
    internal_error << "simt_intrinsic called on bad variable name: " << name << "\n";
    return "";
}
}  // namespace

string CodeGen_Metal_Dev::CodeGen_Metal_C::print_extern_call(const Call *op) {
    internal_assert(!function_takes_user_context(op->name));
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

void CodeGen_Metal_Dev::CodeGen_Metal_C::visit(const Div *op) {
    int bits;
    if (is_const_power_of_two_integer(op->b, &bits)) {
        ostringstream oss;
        oss << print_expr(op->a) << " >> " << bits;
        print_assignment(op->type, oss.str());
    } else if (op->type.is_int()) {
        print_expr(lower_euclidean_div(op->a, op->b));
    } else {
        visit_binop(op->type, op->a, op->b, "/");
    }
}

void CodeGen_Metal_Dev::CodeGen_Metal_C::visit(const Mod *op) {
    int bits;
    if (is_const_power_of_two_integer(op->b, &bits)) {
        ostringstream oss;
        oss << print_expr(op->a) << " & " << ((1 << bits)-1);
        print_assignment(op->type, oss.str());
    } else if (op->type.is_int()) {
        print_expr(lower_euclidean_mod(op->a, op->b));
    } else {
        visit_binop(op->type, op->a, op->b, "%");
    }
}

void CodeGen_Metal_Dev::CodeGen_Metal_C::visit(const For *loop) {
    if (is_gpu_var(loop->name)) {
        internal_assert((loop->for_type == ForType::GPUBlock) ||
                        (loop->for_type == ForType::GPUThread))
            << "kernel loop must be either gpu block or gpu thread\n";
        internal_assert(is_zero(loop->min));

        do_indent();
        stream << print_type(Int(32)) << " " << print_name(loop->name)
               << " = " << simt_intrinsic(loop->name) << ";\n";

        loop->body.accept(this);

    } else {
        user_assert(loop->for_type != ForType::Parallel) << "Cannot use parallel loops inside Metal kernel\n";
        CodeGen_C::visit(loop);
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
        do_indent();
        stream << "threadgroup_barrier(mem_flags::mem_threadgroup);\n";
        print_assignment(op->type, "0");
    } else {
        CodeGen_C::visit(op);
    }
}

namespace {

// If e is a ramp expression with stride 1, return the base, otherwise undefined.
Expr is_ramp_one(Expr e) {
    const Ramp *r = e.as<Ramp>();
    if (r == nullptr) {
        return Expr();
    }

    if (is_one(r->stride)) {
        return r->base;
    }

    return Expr();
}
}


string CodeGen_Metal_Dev::CodeGen_Metal_C::get_memory_space(const string &buf) {
    return "__address_space_" + print_name(buf);
}

void CodeGen_Metal_Dev::CodeGen_Metal_C::visit(const Load *op) {
    user_assert(is_one(op->predicate)) << "Predicated load is not supported inside Metal kernel.\n";
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
    bool type_cast_needed = !(allocations.contains(op->name) &&
                              allocations.get(op->name).type == op->type);
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

        do_indent();
        stream << print_type(op->type)
               << " " << id << ";\n";

        for (int i = 0; i < op->type.lanes(); ++i) {
            do_indent();
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
    user_assert(is_one(op->predicate)) << "Predicated store is not supported inside Metal kernel.\n";
    user_assert(op->value.type().lanes() <= 4) << "Vectorization by widths greater than 4 is not supported by Metal -- type is " << op->value.type() << ".\n";

    string id_value = print_expr(op->value);
    Type t = op->value.type();

    // If we're writing a contiguous ramp, store through a pointer of vector type.
    Expr ramp_base = is_ramp_one(op->index);
    if (ramp_base.defined()) {
        internal_assert(op->value.type().is_vector());
        string id_ramp_base = print_expr(ramp_base);

        do_indent();
        stream << "*(" << get_memory_space(op->name) << " " << print_storage_type(t) << " *)(("
               << get_memory_space(op->name) << " " << print_type(t.element_of()) << " *)" << print_name(op->name)
               << " + " << id_ramp_base << ") = " << id_value << ";\n";
    } else if (op->index.type().is_vector()) {
        // If index is a vector, scatter vector elements.
        internal_assert(t.is_vector());

        string id_index = print_expr(op->index);

        for (int i = 0; i < t.lanes(); ++i) {
            do_indent();
            stream << "((" << get_memory_space(op->name) << " "
                   << print_storage_type(t.element_of()) << " *)"
                   << print_name(op->name)
                   << ")["
                   << id_index << "[" << i << "]] = "
                   << id_value << "[" << i << "];\n";
        }
    } else {
        bool type_cast_needed = !(allocations.contains(op->name) &&
                                  allocations.get(op->name).type == t);

        string id_index = print_expr(op->index);
        string id_value = print_expr(op->value);
        do_indent();

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

    if (op->name == "__shared") {
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

        do_indent();
        stream << print_storage_type(op->type) << ' '
               << print_name(op->name) << "[" << size << "];\n";
        do_indent();
        stream << "#define " << get_memory_space(op->name) << " thread\n";

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
    if (op->name == "__shared") {
        return;
    } else {
        // Should have been freed internally
        internal_assert(allocations.contains(op->name));
        allocations.pop(op->name);
        do_indent();
        stream << "#undef " << get_memory_space(op->name) << "\n";
    }
}

void CodeGen_Metal_Dev::CodeGen_Metal_C::visit(const Cast *op) {
    print_assignment(op->type, print_type(op->type) + "(" + print_expr(op->value) + ")");
}

void CodeGen_Metal_Dev::CodeGen_Metal_C::visit(const Atomic *op) {
    // It might be possible to support atomic but this is not trivial.
    // Metal requires atomic data types to be wrapped in an atomic integer data type
    user_assert(false) << "Atomic updates are not supported inside Metal kernels";
}

void CodeGen_Metal_Dev::add_kernel(Stmt s,
                                   const string &name,
                                   const vector<DeviceArgument> &args) {
    debug(2) << "CodeGen_Metal_Dev::compile " << name << "\n";

    // TODO: do we have to uniquify these names, or can we trust that they are safe?
    cur_kernel_name = name;
    metal_c.add_kernel(s, name, args);
}

namespace {
struct BufferSize {
    string name;
    size_t size;

    BufferSize() : size(0) {}
    BufferSize(string name, size_t size) : name(name), size(size) {}

    bool operator < (const BufferSize &r) const {
        return size < r.size;
    }
};
}  // namespace

void CodeGen_Metal_Dev::CodeGen_Metal_C::add_kernel(Stmt s,
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
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer &&
            CodeGen_GPU_Dev::is_buffer_constant(s, args[i].name) &&
            args[i].size > 0) {
            constants.push_back(BufferSize(args[i].name, args[i].size));
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
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            vector<BufferSize>::iterator constant = constants.begin();
            while (constant != constants.end() &&
                   constant->name != args[i].name) {
                constant++;
            }

            if (constant != constants.end()) {
                stream << "#if " << constant->size << " < MAX_CONSTANT_BUFFER_SIZE && "
                       << constant - constants.begin() << " < MAX_CONSTANT_ARGS\n";
                stream << "#define " << get_memory_space(args[i].name) << " constant\n";
                stream << "#else\n";
                stream << "#define " << get_memory_space(args[i].name) << " device\n";
                stream << "#endif\n";
            } else {
                stream << "#define " << get_memory_space(args[i].name) << " device\n";
            }
        }
    }

    // Emit a struct to hold the scalar args of the kernel
    bool any_scalar_args = false;
    for (size_t i = 0; i < args.size(); i++) {
        if (!args[i].is_buffer) {
            if (!any_scalar_args) {
                stream << "struct " + name + "_args {\n";
                any_scalar_args = true;
            }
            stream << print_type(args[i].type)
                   << " "
                   << print_name(args[i].name)
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

    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            stream << ",\n";
            stream << " " << get_memory_space(args[i].name) << " ";
            if (!args[i].write) stream << "const ";
            stream << print_storage_type(args[i].type) << " *"
                   << print_name(args[i].name) << " [[ buffer(" << buffer_index++ << ") ]]";
            Allocation alloc;
            alloc.type = args[i].type;
            allocations.push(args[i].name, alloc);
        }
    }
    stream << ",\n" << " threadgroup int16_t* __shared [[ threadgroup(0) ]]";

    stream << ")\n";

    open_scope();

    // Unpack args struct into local variables to match naming of generated code.
    for (size_t i = 0; i < args.size(); i++) {
        if (!args[i].is_buffer) {
            stream << print_type(args[i].type)
                   << " "
                   << print_name(args[i].name)
                   << " = _scalar_args->" << print_name(args[i].name)
                   << ";\n";
        }
    }

    print(s);
    close_scope("kernel " + name);

    for (size_t i = 0; i < args.size(); i++) {
        // Remove buffer arguments from allocation scope
        if (args[i].is_buffer) {
            allocations.pop(args[i].name);
        }
    }

    // Undef all the buffer address spaces, in case they're different in another kernel.
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            stream << "#undef " << get_memory_space(args[i].name) << "\n";
        }
    }
}

void CodeGen_Metal_Dev::init_module() {
    debug(2) << "Metal device codegen init_module\n";

    // wipe the internal kernel source
    src_stream.str("");
    src_stream.clear();

    // Write out the Halide math functions.
    src_stream << "#include <metal_stdlib>\n"
               << "using namespace metal;\n" // Seems like the right way to go.
               << "namespace {\n"
               << "constexpr float float_from_bits(unsigned int x) {return as_type<float>(x);}\n"
               << "constexpr float nan_f32() { return as_type<float>(0x7fc00000); }\n" // Quiet NaN with minimum fractional value.
               << "constexpr float neg_inf_f32() { return float_from_bits(0xff800000); }\n"
               << "constexpr float inf_f32() { return float_from_bits(0x7f800000); }\n"
               << "float fast_inverse_f32(float x) { return 1.0f / x; }\n"
               << "#define sqrt_f32 sqrt\n"
               << "#define sin_f32 sin\n"
               << "#define cos_f32 cos\n"
               << "#define exp_f32 exp\n"
               << "#define log_f32 log\n"
               << "#define abs_f32 fabs\n"
               << "#define floor_f32 floor\n"
               << "#define ceil_f32 ceil\n"
               << "#define round_f32 round\n"
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
               << "}\n"; // close namespace

    metal_c.add_common_macros(src_stream);

    // __shared always has address space threadgroup.
    src_stream << "#define __address_space___shared threadgroup\n";

    src_stream << '\n';

    cur_kernel_name = "";
}

vector<char> CodeGen_Metal_Dev::compile_to_src() {
    string str = src_stream.str();
    debug(1) << "Metal kernel:\n" << str << "\n";
    vector<char> buffer(str.begin(), str.end());
    buffer.push_back(0);
    return buffer;
}

string CodeGen_Metal_Dev::get_current_kernel_name() {
    return cur_kernel_name;
}

void CodeGen_Metal_Dev::dump() {
    std::cerr << src_stream.str() << std::endl;
}

std::string CodeGen_Metal_Dev::print_gpu_name(const std::string &name) {
    return name;
}

}  // namespace Internal
}  // namespace Halide
