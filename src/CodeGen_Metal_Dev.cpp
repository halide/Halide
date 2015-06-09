#include <sstream>
#include <algorithm>

#include "CodeGen_Metal_Dev.h"
#include "CodeGen_Internal.h"
#include "Debug.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

using std::ostringstream;
using std::string;
using std::vector;
using std::sort;

static ostringstream nil;

CodeGen_Metal_Dev::CodeGen_Metal_Dev(Target t) :
    metal_c(src_stream), target(t) {
}

string CodeGen_Metal_Dev::CodeGen_Metal_C::print_type(Type type) {
    ostringstream oss;
    if (type.is_float()) {
        if (type.bits == 16) {
            oss << "half";
        } else if (type.bits == 32) {
            oss << "float";
        } else if (type.bits == 64) {
            oss << "double";
        } else {
            user_error << "Can't represent a float with this many bits in Metal C: " << type << "\n";
        }

    } else {
        if (type.is_uint() && type.bits > 1) oss << 'u';
        switch (type.bits) {
        case 1:
            user_assert(type.width == 1) << "Vector of bool not valid in Metal C (yet)\n";
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
            user_error << "Can't represent an integer with this many bits in Metal C: " << type << "\n";
        }
    }
    if (type.width != 1) {
        switch (type.width) {
        case 2:
        case 3:
        case 4:
        case 8:
        case 16:
            oss << type.width;
            break;
        default:
            user_error <<  "Unsupported vector width in Metal C: " << type << "\n";
        }
    }
    return oss.str();
}

string CodeGen_Metal_Dev::CodeGen_Metal_C::print_reinterpret(Type type, Expr e) {
    ostringstream oss;
    oss << "as_" << print_type(type) << "(" << print_expr(e) << ")";
    return oss.str();
}



namespace {
string simt_intrinsic(const string &name) {
    if (ends_with(name, ".__thread_id_x")) {
        return "get_local_id(0)";
    } else if (ends_with(name, ".__thread_id_y")) {
        return "get_local_id(1)";
    } else if (ends_with(name, ".__thread_id_z")) {
        return "get_local_id(2)";
    } else if (ends_with(name, ".__thread_id_w")) {
        return "get_local_id(3)";
    } else if (ends_with(name, ".__block_id_x")) {
        return "get_group_id(0)";
    } else if (ends_with(name, ".__block_id_y")) {
        return "get_group_id(1)";
    } else if (ends_with(name, ".__block_id_z")) {
        return "get_group_id(2)";
    } else if (ends_with(name, ".__block_id_w")) {
        return "get_group_id(3)";
    }
    internal_error << "simt_intrinsic called on bad variable name: " << name << "\n";
    return "";
}
}


void CodeGen_Metal_Dev::CodeGen_Metal_C::visit(const Div *op) {
    if (op->type.is_int()) {
      print_expr(Call::make(op->type, "sdiv_" + print_type(op->type), { op->a, op->b }, Call::Extern));
    } else {
        visit_binop(op->type, op->a, op->b, "/");
    }
}

void CodeGen_Metal_Dev::CodeGen_Metal_C::visit(const Mod *op) {
    if (op->type.is_int()) {
      print_expr(Call::make(op->type, "smod_" + print_type(op->type), { op->a, op->b }, Call::Extern));
    } else {
        visit_binop(op->type, op->a, op->b, "%");
    }
}

void CodeGen_Metal_Dev::CodeGen_Metal_C::visit(const For *loop) {
    if (is_gpu_var(loop->name)) {
        internal_assert(loop->for_type == ForType::Parallel) << "kernel loop must be parallel\n";
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
    rhs << id_base << " + " << id_stride << " * ("
        << print_type(op->type.vector_of(op->width)) << ")(0";
    // Note 0 written above.
    for (int i = 1; i < op->width; ++i) {
        rhs << ", " << i;
    }
    rhs << ")";
    print_assignment(op->type.vector_of(op->width), rhs.str());
}

void CodeGen_Metal_Dev::CodeGen_Metal_C::visit(const Broadcast *op) {
    string id_value = print_expr(op->value);

    print_assignment(op->type.vector_of(op->width), id_value);
}

namespace {
// Mapping of integer vector indices to Metal ".s" syntax.
const char * vector_elements = "0123456789ABCDEF";

// If e is a ramp expression with stride 1, return the base, otherwise undefined.
Expr is_ramp1(Expr e) {
    const Ramp *r = e.as<Ramp>();
    if (r == NULL) {
        return Expr();
    }

    const IntImm *i = r->stride.as<IntImm>();
    if (i != NULL && i->value == 1) {
        return r->base;
    }

    return Expr();
}
}


string CodeGen_Metal_Dev::CodeGen_Metal_C::get_memory_space(const string &buf) {
    return "__address_space_" + print_name(buf);
}

void CodeGen_Metal_Dev::CodeGen_Metal_C::visit(const Load *op) {
    // If we're loading a contiguous ramp into a vector, use vload instead.
    Expr ramp_base = is_ramp1(op->index);
    if (ramp_base.defined()) {
        internal_assert(op->type.is_vector());
        string id_ramp_base = print_expr(ramp_base);

        ostringstream rhs;
        rhs << "vload" << op->type.width
            << "(0, (" << get_memory_space(op->name) << " "
            << print_type(op->type.element_of()) << "*)"
            << print_name(op->name) << " + " << id_ramp_base << ")";

        print_assignment(op->type, rhs.str());
        return;
    }

    string id_index = print_expr(op->index);

    // Get the rhs just for the cache.
    bool type_cast_needed = !(allocations.contains(op->name) &&
                              allocations.get(op->name) == op->type);
    ostringstream rhs;
    if (type_cast_needed) {
        rhs << "((" << get_memory_space(op->name) << " "
            << print_type(op->type) << " *)"
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

        id = unique_name('V');
        cache[rhs.str()] = id;

        do_indent();
        stream << print_type(op->type)
               << " " << id << ";\n";

        for (int i = 0; i < op->type.width; ++i) {
            do_indent();
            stream
                << id << ".s" << vector_elements[i]
                << " = ((" << get_memory_space(op->name) << " "
                << print_type(op->type.element_of()) << "*)"
                << print_name(op->name) << ")"
                << "[" << id_index << ".s" << vector_elements[i] << "];\n";
        }
    } else {
        print_assignment(op->type, rhs.str());
    }
}

void CodeGen_Metal_Dev::CodeGen_Metal_C::visit(const Store *op) {
    string id_value = print_expr(op->value);
    Type t = op->value.type();

    // If we're writing a contiguous ramp, use vstore instead.
    Expr ramp_base = is_ramp1(op->index);
    if (ramp_base.defined()) {
        internal_assert(op->value.type().is_vector());
        string id_ramp_base = print_expr(ramp_base);

        do_indent();
        stream << "vstore" << t.width << "("
               << id_value << ","
               << 0 << ", (" << get_memory_space(op->name) << " "
               << print_type(t.element_of()) << "*)"
               << print_name(op->name) << " + " << id_ramp_base
               << ");\n";

    } else if (op->index.type().is_vector()) {
        // If index is a vector, scatter vector elements.
        internal_assert(t.is_vector());

        string id_index = print_expr(op->index);

        for (int i = 0; i < t.width; ++i) {
            do_indent();
            stream << "((" << get_memory_space(op->name) << " "
                   << print_type(t.element_of()) << " *)"
                   << print_name(op->name)
                   << ")["
                   << id_index << ".s" << vector_elements[i] << "] = "
                   << id_value << ".s" << vector_elements[i] << ";\n";
        }
    } else {
        bool type_cast_needed = !(allocations.contains(op->name) &&
                                  allocations.get(op->name) == t);

        string id_index = print_expr(op->index);
        string id_value = print_expr(op->value);
        do_indent();

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
        stream << "[" << id_index << "] = "
               << id_value << ";\n";
    }

    cache.clear();
}

void CodeGen_Metal_Dev::CodeGen_Metal_C::visit(const Cast *op) {
    if (op->type.is_vector()) {
        print_assignment(op->type, "convert_" + print_type(op->type) + "(" + print_expr(op->value) + ")");
    } else {
        CodeGen_C::visit(op);
    }
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
        int32_t size;
        bool is_constant = constant_allocation_size(op->extents, op->name, size);
        user_assert(is_constant)
            << "Allocation " << op->name << " has a dynamic size. "
            << "Only fixed-size allocations are supported on the gpu. "
            << "Try storing into shared memory instead.";

        do_indent();
        stream << print_type(op->type) << ' '
               << print_name(op->name) << "[" << size << "];\n";
        do_indent();
        stream << "#define " << get_memory_space(op->name) << " __private\n";

        allocations.push(op->name, op->type);

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


void CodeGen_Metal_Dev::add_kernel(Stmt s,
                                    const string &name,
                                    const vector<GPU_Argument> &args) {
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
}

void CodeGen_Metal_Dev::CodeGen_Metal_C::add_kernel(Stmt s,
                                                      const string &name,
                                                      const vector<GPU_Argument> &args) {

    debug(2) << "Adding Metal kernel " << name << "\n";

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
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer &&
            CodeGen_GPU_Dev::is_buffer_constant(s, args[i].name) &&
            args[i].size > 0) {
            constants.push_back(BufferSize(args[i].name, args[i].size));
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
                stream << "#define " << get_memory_space(args[i].name) << " __constant\n";
                stream << "#else\n";
                stream << "#define " << get_memory_space(args[i].name) << " __global\n";
                stream << "#endif\n";
            } else {
                stream << "#define " << get_memory_space(args[i].name) << " __global\n";
            }
        }
    }

    // Emit the function prototype
    stream << "__kernel void " << name << "(\n";
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            stream << " " << get_memory_space(args[i].name) << " ";
            if (!args[i].write) stream << "const ";
            stream << print_type(args[i].type) << " *"
                   << print_name(args[i].name);
            allocations.push(args[i].name, args[i].type);
        } else {
            stream << " const "
                   << print_type(args[i].type)
                   << " "
                   << print_name(args[i].name);
        }

        if (i < args.size()-1) stream << ",\n";
    }
    stream << ",\n" << " __address_space___shared int16* __shared";

    stream << ")\n";

    open_scope();
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

static string smod_def(string T) {
    ostringstream ss;
    ss << T << " smod_" << T << "(" << T << " a, " << T << " b) {\n";
    ss << T << " r = a % b;\n";
    ss << "if (r < 0) { r += b < 0 ? -b : b; }\n";
    ss << "return r;\n";
    ss << "}\n";
    return ss.str();
}

static string sdiv_def(string T) {
    ostringstream ss;
    ss << T << " sdiv_" << T << "(" << T << " a, " << T << " b) {\n";
    ss << T << " q = a / b;\n";
    ss << T << " r = a - q*b;\n";
    ss << T << " bs = b >> (8*sizeof(" << T << ") - 1);\n";
    ss << T << " rs = r >> (8*sizeof(" << T << ") - 1);\n";
    ss << "return q - (rs&bs) + (rs&~bs);\n";
    ss << "}\n";
    return ss.str();
}

void CodeGen_Metal_Dev::init_module() {
    debug(2) << "Metal device codegen init_module\n";

    // wipe the internal kernel source
    src_stream.str("");
    src_stream.clear();

    // This identifies the program as Metal C (as opposed to SPIR).
    src_stream << "/*Metal C*/\n";

    src_stream << "#pragma OPENCL FP_CONTRACT ON\n";

    // Write out the Halide math functions.
    src_stream << "float maxval_f32() {return FLT_MAX;}\n"
               << "float minval_f32() {return -FLT_MAX;}\n"
               << "float nan_f32() { return NAN; }\n"
               << "float neg_inf_f32() { return -INFINITY; }\n"
               << "bool is_nan_f32(float x) {return x != x; }\n"
               << "float inf_f32() { return INFINITY; }\n"
               << "float float_from_bits(unsigned int x) {return as_float(x);}\n"
               << smod_def("char") << "\n"
               << smod_def("short") << "\n"
               << smod_def("int") << "\n"
               << smod_def("long") << "\n"
               << sdiv_def("char") << "\n"
               << sdiv_def("short") << "\n"
               << sdiv_def("int") << "\n"
               << sdiv_def("long") << "\n"
               << "#define sqrt_f32 sqrt \n"
               << "#define sin_f32 sin \n"
               << "#define cos_f32 cos \n"
               << "#define exp_f32 exp \n"
               << "#define log_f32 log \n"
               << "#define abs_f32 fabs \n"
               << "#define floor_f32 floor \n"
               << "#define ceil_f32 ceil \n"
               << "#define round_f32 round \n"
               << "#define trunc_f32 trunc \n"
               << "#define pow_f32 pow\n"
               << "#define asin_f32 asin \n"
               << "#define acos_f32 acos \n"
               << "#define tan_f32 tan \n"
               << "#define atan_f32 atan \n"
               << "#define atan2_f32 atan2\n"
               << "#define sinh_f32 sinh \n"
               << "#define asinh_f32 asinh \n"
               << "#define cosh_f32 cosh \n"
               << "#define acosh_f32 acosh \n"
               << "#define tanh_f32 tanh \n"
               << "#define atanh_f32 atanh \n"
               << "#define fast_inverse_f32 native_recip \n"
               << "#define fast_inverse_sqrt_f32 native_rsqrt \n"
               << "int halide_gpu_thread_barrier() {\n"
               << "  barrier(CLK_LOCAL_MEM_FENCE);\n" // Halide only ever needs local memory fences.
               << "  return 0;\n"
               << "}\n";

    // __shared always has address space __local.
    src_stream << "#define __address_space___shared __local\n";

    if (target.has_feature(Target::CLDoubles)) {
        src_stream << "#pragma OPENCL EXTENSION cl_khr_fp64 : enable\n"
                   << "bool is_nan_f64(double x) {return x != x; }\n"
                   << "#define sqrt_f64 sqrt\n"
                   << "#define sin_f64 sin\n"
                   << "#define cos_f64 cos\n"
                   << "#define exp_f64 exp\n"
                   << "#define log_f64 log\n"
                   << "#define abs_f64 fabs\n"
                   << "#define floor_f64 floor\n"
                   << "#define ceil_f64 ceil\n"
                   << "#define round_f64 round\n"
                   << "#define trunc_f64 trunc\n"
                   << "#define pow_f64 pow\n"
                   << "#define asin_f64 asin\n"
                   << "#define acos_f64 acos\n"
                   << "#define tan_f64 tan\n"
                   << "#define atan_f64 atan\n"
                   << "#define atan2_f64 atan2\n"
                   << "#define sinh_f64 sinh\n"
                   << "#define asinh_f64 asinh\n"
                   << "#define cosh_f64 cosh\n"
                   << "#define acosh_f64 acosh\n"
                   << "#define tanh_f64 tanh\n"
                   << "#define atanh_f64 atanh\n";
    }

    src_stream << '\n';

    // Add at least one kernel to avoid errors on some implementations for functions
    // without any GPU schedules.
    src_stream << "__kernel void _at_least_one_kernel(int x) { }\n";

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

}}
