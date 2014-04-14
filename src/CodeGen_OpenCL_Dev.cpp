#include <sstream>

#include "CodeGen_OpenCL_Dev.h"
#include "Debug.h"

// TODO: This needs a runtime controlled switch based on the device extension
// string.
//#define ENABLE_CL_KHR_FP64

namespace Halide {
namespace Internal {

using std::ostringstream;
using std::string;
using std::vector;

static ostringstream nil;

CodeGen_OpenCL_Dev::CodeGen_OpenCL_Dev() {
    clc = new CodeGen_OpenCL_C(src_stream);
}

string CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::print_type(Type type) {
    ostringstream oss;
    if (type.is_float()) {
        if (type.bits == 16) {
            oss << "half";
        } else if (type.bits == 32) {
            oss << "float";
        } else if (type.bits == 64) {
            oss << "double";
        } else {
            assert(false && "Can't represent a float with this many bits in OpenCL C");
        }

    } else {
        if (type.is_uint() && type.bits > 1) oss << 'u';
        switch (type.bits) {
        case 1:
            assert(type.width == 1 && "Vector of bool not valid in OpenCL C (yet)");
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
            assert(false && "Can't represent an integer with this many bits in OpenCL C");
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
            assert(false && "Unsupported vector width in OpenCL C");
        }
    }
    return oss.str();
}

string CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::print_reinterpret(Type type, Expr e) {
    ostringstream oss;
    oss << "as_" << print_type(type) << "(" << print_expr(e) << ")";
    return oss.str();
}



namespace {
Expr simt_intrinsic(const string &name) {
    if (ends_with(name, ".threadidx")) {
        return Call::make(Int(32), "get_local_id", vec(Expr(0)), Call::Extern);
    } else if (ends_with(name, ".threadidy")) {
        return Call::make(Int(32), "get_local_id", vec(Expr(1)), Call::Extern);
    } else if (ends_with(name, ".threadidz")) {
        return Call::make(Int(32), "get_local_id", vec(Expr(2)), Call::Extern);
    } else if (ends_with(name, ".threadidw")) {
        return Call::make(Int(32), "get_local_id", vec(Expr(3)), Call::Extern);
    } else if (ends_with(name, ".blockidx")) {
        return Call::make(Int(32), "get_group_id", vec(Expr(0)), Call::Extern);
    } else if (ends_with(name, ".blockidy")) {
        return Call::make(Int(32), "get_group_id", vec(Expr(1)), Call::Extern);
    } else if (ends_with(name, ".blockidz")) {
        return Call::make(Int(32), "get_group_id", vec(Expr(2)), Call::Extern);
    } else if (ends_with(name, ".blockidw")) {
        return Call::make(Int(32), "get_group_id", vec(Expr(3)), Call::Extern);
    }
    assert(false && "simt_intrinsic called on bad variable name");
    return Expr();
}
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const For *loop) {
    if (is_gpu_var(loop->name)) {
        debug(0) << "Dropping loop " << loop->name << " (" << loop->min << ", " << loop->extent << ")\n";
        assert(loop->for_type == For::Parallel && "kernel loop must be parallel");

        Expr simt_idx = simt_intrinsic(loop->name);
        Expr loop_var = Add::make(loop->min, simt_idx);
        Expr cond = LT::make(simt_idx, loop->extent);
        debug(0) << "for -> if (" << cond << ")\n";

        string id_idx = print_expr(simt_idx);
        string id_cond = print_expr(cond);

        do_indent();
        stream << "if (" << id_cond << ")\n";

        open_scope();
        do_indent();
        stream << print_type(Int(32)) << " " << print_name(loop->name) << " = " << id_idx << ";\n";
        loop->body.accept(this);
        close_scope("for " + id_cond);

    } else {
    	assert(loop->for_type != For::Parallel && "Cannot emit parallel loops in OpenCL C");
    	CodeGen_C::visit(loop);
    }
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const Ramp *op) {
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

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const Broadcast *op) {
    string id_value = print_expr(op->value);
    
    print_assignment(op->type.vector_of(op->width), id_value);
}

namespace {
// Mapping of integer vector indices to OpenCL ".s" syntax.
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

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const Load *op) {
    // If we're loading a contiguous ramp into a vector, use vload instead.
    Expr ramp_base = is_ramp1(op->index);
    if (ramp_base.defined()) {
        assert(op->type.is_vector());
        string id_ramp_base = print_expr(ramp_base);

        ostringstream rhs;
        rhs << "vload" << op->type.width
            << "(0, "
            << "(__global " << print_type(op->type.element_of()) << "*)" 
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
        rhs << "((" << print_type(op->type) << " *)"
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
        assert(op->type.is_vector());

        id = unique_name('V');
        cache[rhs.str()] = id;

        do_indent();
        stream << print_type(op->type)
               << " " << id << ";\n";

        for (int i = 0; i < op->type.width; ++i) {
            do_indent();
            stream
                << id << ".s" << vector_elements[i]
                << " = " 
                << "((__global " << print_type(op->type.element_of()) << "*)" 
                << print_name(op->name) << ")"
                << "[" << id_index << ".s" << vector_elements[i] << "];\n";
        }
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const Store *op) {
    string id_value = print_expr(op->value);
    Type t = op->value.type();

    // If we're writing a contiguous ramp, use vstore instead.
    Expr ramp_base = is_ramp1(op->index);
    if (ramp_base.defined()) {
        assert(op->value.type().is_vector());
        string id_ramp_base = print_expr(ramp_base);

        do_indent();
        stream << "vstore" << t.width << "(" 
               << id_value << ","
               << 0 << ", "
               << "(__global " << print_type(t.element_of()) << "*)"
               << print_name(op->name) << " + " << id_ramp_base
               << ");\n";

        return;
    }

    if (op->index.type().is_vector()) {
        // If index is a vector, scatter vector elements.
        assert(t.is_vector());

        string id_index = print_expr(op->index);

        for (int i = 0; i < t.width; ++i) {
            do_indent();
            stream << "((__global " << print_type(t.element_of()) << " *)"
                   << print_name(op->name)
                   << ")";

            stream << "[" << id_index << ".s" << vector_elements[i] << "] = "
                   << id_value << ".s" << vector_elements[i]
                   << ";\n";
        }
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::visit(const Cast *op) {
    if (op->type.is_vector()) {
        print_assignment(op->type, "convert_" + print_type(op->type) + "(" + print_expr(op->value) + ")");
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_OpenCL_Dev::add_kernel(Stmt s, string name, const vector<Argument> &args) {
    debug(0) << "hi CodeGen_OpenCL_Dev::compile! " << name << "\n";

    // TODO: do we have to uniquify these names, or can we trust that they are safe?
    cur_kernel_name = name;
    clc->add_kernel(s, name, args);
}

namespace {
const string kernel_preamble = "";
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::add_kernel(Stmt s, string name, const vector<Argument> &args) {
    cache.clear();

    debug(0) << "hi! " << name << "\n";

    stream << kernel_preamble;

    // Emit the function prototype
    stream << "__kernel void " << name << "(\n";
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            stream << " __global " << print_type(args[i].type) << " *"
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
    stream << ",\n" << "__local uchar* shared";

    stream << ") {\n";

    print(s);

    stream << "}\n";

    // Remove buffer arguments from allocation scope
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            allocations.pop(args[i].name);
        }
    }
}

void CodeGen_OpenCL_Dev::init_module() {
    debug(0) << "OpenCL device codegen init_module\n";

    // wipe the internal kernel source
    src_stream.str("");
    src_stream.clear();

    // This identifies the program as OpenCL C (as opposed to SPIR).
    src_stream << "/*OpenCL C*/\n";

#ifdef ENABLE_CL_KHR_FP64
    src_stream << "#pragma OPENCL EXTENSION cl_khr_fp64 : enable\n";
#endif
    src_stream << "#pragma OPENCL FP_CONTRACT ON\n";
        
    // Write out the Halide math functions.
    src_stream << "float nan_f32() { return NAN; }\n"
               << "float neg_inf_f32() { return -INFINITY; }\n"
               << "float inf_f32() { return INFINITY; }\n"
               << "float float_from_bits(unsigned int x) {return as_float(x);}\n"
               << "#define sqrt_f32 sqrt \n"
               << "#define sin_f32 sin \n"
               << "#define cos_f32 cos \n"
               << "#define exp_f32 exp \n"
               << "#define log_f32 log \n"
               << "#define abs_f32 fabs \n"
               << "#define floor_f32 floor \n"
               << "#define ceil_f32 ceil \n"
               << "#define round_f32 round \n"
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
               << "#define atanh_f32 atanh \n";

#ifdef ENABLE_CL_KHR_FP64
    src_stream << "#define sqrt_f64 sqrt\n"
               << "#define sin_f64 sin\n"
               << "#define cos_f64 cos\n"
               << "#define exp_f64 exp\n"
               << "#define log_f64 log\n"
               << "#define abs_f64 fabs\n"
               << "#define floor_f64 floor\n"
               << "#define ceil_f64 ceil\n"
               << "#define round_f64 round\n"
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
#endif

    src_stream << '\n';

    // Add at least one kernel to avoid errors on some implementations for functions
    // without any GPU schedules.
    src_stream << "__kernel void _at_least_one_kernel(int x) { }\n";

    cur_kernel_name = "";
}

vector<char> CodeGen_OpenCL_Dev::compile_to_src() {
    string str = src_stream.str();
    vector<char> buffer(str.begin(), str.end());
    buffer.push_back(0);
    return buffer;
}

string CodeGen_OpenCL_Dev::get_current_kernel_name() {
    return cur_kernel_name;
}

void CodeGen_OpenCL_Dev::dump() {
    std::cerr << src_stream.str() << std::endl;
}

}}
