#include "CodeGen_OpenCL_Dev.h"
#include "Debug.h"

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
    assert(type.width == 1 && "Can't codegen vector types to OpenCL C (yet)");
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

void CodeGen_OpenCL_Dev::compile(Stmt s, string name, const vector<Argument> &args) {
    debug(0) << "hi CodeGen_OpenCL_Dev::compile! " << name << "\n";

    // TODO: do we have to uniquify these names, or can we trust that they are safe?
    cur_kernel_name = name;
    clc->compile(s, name, args);
}

namespace {
const string preamble = ""; // nothing for now
}

void CodeGen_OpenCL_Dev::CodeGen_OpenCL_C::compile(Stmt s, string name, const vector<Argument> &args) {
    debug(0) << "hi! " << name << "\n";

    stream << preamble;

    // Emit the function prototype
    stream << "__kernel void " << name << "(";
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            stream << "__global " << print_type(args[i].type) << " *"
                   << print_name(args[i].name);
        } else {
            stream << "const "
                   << print_type(args[i].type)
                   << " "
                   << print_name(args[i].name);
        }

        if (i < args.size()-1) stream << ", ";
    }
    stream << ", " << "__local uchar* shared";

    stream << ") {\n";

    print(s);

    stream << "}\n";
}

void CodeGen_OpenCL_Dev::init_module() {
    debug(0) << "OpenCL device codegen init_module\n";

    // wipe the internal kernel source
    src_stream.str("");
    src_stream.clear();

    cur_kernel_name = "";
}

string CodeGen_OpenCL_Dev::compile_to_src() {
    return src_stream.str();
}

string CodeGen_OpenCL_Dev::get_current_kernel_name() {
    return cur_kernel_name;
}

void CodeGen_OpenCL_Dev::dump() {
    std::cerr << src_stream.str() << std::endl;
}

}}
