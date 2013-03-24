#include "CodeGen_C.h"
#include "Substitute.h"
#include "IROperator.h"
#include "Param.h"
#include "Var.h"
#include <sstream>
#include <iostream>

namespace Halide { 
namespace Internal {

using std::ostream;
using std::endl;
using std::string;
using std::vector;
using std::ostringstream;

CodeGen_C::CodeGen_C(ostream &s) : IRPrinter(s) {}

void CodeGen_C::print_c_type(Type type) {
    assert(type.width == 1 && "Can't codegen vector types to C (yet)");
    if (type.is_float()) {
        if (type.bits == 32) {
            stream << "float";
        } else if (type.bits == 64) {
            stream << "double";
        } else {
            assert(false && "Can't represent a float with this many bits in C");
        }
            
    } else {
        switch (type.bits) {
        case 1:
            stream << "bool";
            break;
        case 8: case 16: case 32: case 64:
            if (type.is_uint()) stream << 'u';
            stream << "int" << type.bits << "_t";                
            break;
        default:
            assert(false && "Can't represent an integer with this many bits in C");                
        }
    }
}

void CodeGen_C::print_c_name(const string &name) {
    for (size_t i = 0; i < name.size(); i++) {
        if (name[i] == '.') stream << '_';
        else stream << name[i];
    }
}

void CodeGen_C::compile_header(const string &name, const vector<Argument> &args) {
    stream << "#ifndef HALIDE_" << name << '\n'
           << "#define HALIDE_" << name << '\n';

    // Throw in a definition of a buffer_t
    stream << "#ifndef BUFFER_T_DEFINED\n"
           << "#define BUFFER_T_DEFINED\n"
           << "#include <stdint.h>\n"
           << "typedef struct buffer_t {\n"
           << "    uint8_t* host;\n"
           << "    uint64_t dev;\n"
           << "    bool host_dirty;\n"
           << "    bool dev_dirty;\n"
           << "    int32_t extent[4];\n"
           << "    int32_t stride[4];\n"
           << "    int32_t min[4];\n"
           << "    int32_t elem_size;\n"
           << "} buffer_t;\n"
           << "#endif\n";

    // Now the function prototype
    stream << "extern \"C\" void " << name << "(";
    for (size_t i = 0; i < args.size(); i++) {
        if (i > 0) stream << ", ";
        if (args[i].is_buffer) {
            stream << "const buffer_t *" << args[i].name;
        } else {
            stream << "const ";
            print_c_type(args[i].type);
            stream << " " << args[i].name;
        }
    }
    stream << ");\n";

    stream << "#endif\n";    
}

void CodeGen_C::compile(Stmt s, const string &name, const vector<Argument> &args) {
    stream << "#include <iostream>\n"
           << "#include <assert.h>\n"
           << "#ifndef BUFFER_T_DEFINED\n"
           << "#define BUFFER_T_DEFINED\n"
           << "#include <stdint.h>\n"
           << "typedef struct buffer_t {\n"
           << "    uint8_t* host;\n"
           << "    uint64_t dev;\n"
           << "    bool host_dirty;\n"
           << "    bool dev_dirty;\n"
           << "    int32_t extent[4];\n"
           << "    int32_t stride[4];\n"
           << "    int32_t min[4];\n"
           << "    int32_t elem_size;\n"
           << "} buffer_t;\n"
           << "#endif\n"
           << "\n"
           << "extern \"C\" void *halide_malloc(size_t);\n"
           << "extern \"C\" void halide_free(void *);\n"
           << "\n"
           << "template<typename T> T max(T a, T b) {if (a > b) return a; return b;}\n"
           << "template<typename T> T min(T a, T b) {if (a < b) return a; return b;}\n"
           << "\n";
    // Emit the function prototype
    stream << "void " << name << "(";
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            stream << "const buffer_t *_";
            print_c_name(args[i].name);
        } else {
            stream << "const ";
            print_c_type(args[i].type);
            stream << " ";
            print_c_name(args[i].name);
        }

        if (i < args.size()-1) stream << ", ";
    }

    stream << ") {\n";

    // Unpack the buffer_t's
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            stream << "uint8_t *";
            print_c_name(args[i].name);
            stream << " = _";
            print_c_name(args[i].name);
            stream << "->host;\n";

            for (int j = 0; j < 4; j++) {
                stream << "const int32_t ";
                print_c_name(args[i].name);
                stream << "_min_" << j << " = _";
                print_c_name(args[i].name);
                stream << "->min[" << j << "];\n";
            }
            for (int j = 0; j < 4; j++) {
                stream << "const int32_t ";
                print_c_name(args[i].name);
                stream << "_extent_" << j << " = _";
                print_c_name(args[i].name);
                stream << "->extent[" << j << "];\n";
            }
            for (int j = 0; j < 4; j++) {
                stream << "const int32_t ";
                print_c_name(args[i].name);
                stream << "_stride_" << j << " = _";
                print_c_name(args[i].name);
                stream << "->stride[" << j << "];\n";
            }
        }
    }        

    print(s);

    stream << "}\n";
}

void CodeGen_C::visit(const Variable *op) {
    print_c_name(op->name);
}

void CodeGen_C::visit(const Cast *op) { 
    stream << '(';
    print_c_type(op->type);
    stream << ")(";
    print(op->value);
    stream << ')';
}

void CodeGen_C::visit(const Load *op) {
    stream << "((";
    print_c_type(op->type);
    stream << " *)";
    print_c_name(op->name);
    stream << ")[";
    print(op->index);
    stream << "]";
}

void CodeGen_C::visit(const Store *op) {
    do_indent();
    Type t = op->value.type();
    stream << "((";
    print_c_type(t);
    stream << " *)";
    print_c_name(op->name);
    stream << ")[";
    print(op->index);
    stream << "] = ";
    print(op->value);
    stream << ";\n";
}

void CodeGen_C::visit(const Let *op) {
    // Let expressions don't really work in C
    // Just do a substitution instead
    Expr e = substitute(op->name, op->value, op->body);
    e.accept(this);
}

void CodeGen_C::visit(const Select *op) {
    stream << "(";
    print(op->condition);
    stream << " ? ";
    print(op->true_value);
    stream << " : ";
    print(op->false_value);
    stream << ")";
}

void CodeGen_C::visit(const LetStmt *op) {
    do_indent();
    stream << "{\n";
    indent += 2;

    do_indent();
    stream << "const ";
    print_c_type(op->value.type());
    stream << " ";
    print_c_name(op->name);
    stream << " = ";
    op->value.accept(this);
    stream << ";\n";

    op->body.accept(this);

    do_indent();
    stream << "}\n";

    indent -= 2;
}

void CodeGen_C::visit(const PrintStmt *op) {
    do_indent();
        
    string format_string;
    stream << "std::cout << " << op->prefix;

    for (size_t i = 0; i < op->args.size(); i++) {
        stream << " << ";
        op->args[i].accept(this);
    }
    stream << ";\n";
}

void CodeGen_C::visit(const AssertStmt *op) {
    do_indent();
    stream << "assert(";
    op->condition.accept(this);
    stream << " && \"" << op->message << "\");\n";
}

void CodeGen_C::visit(const Pipeline *op) {

    do_indent();
    stream << "// produce " << op->name << '\n';
    op->produce.accept(this);

    if (op->update.defined()) {
        do_indent();
        stream << "// update " << op->name << '\n';
        op->update.accept(this);            
    }
        
    do_indent();
    stream << "// consume " << op->name << '\n';
    op->consume.accept(this);
}

void CodeGen_C::visit(const For *op) {
    if (op->for_type == For::Parallel) {
        do_indent();
        stream << "#pragma omp parallel for\n";
    } else {
        assert(op->for_type == For::Serial && "Can only emit serial or parallel for loops to C");
    }       

    do_indent();
    stream << "for (int ";
    print_c_name(op->name);
    stream << " = ";
    op->min.accept(this);
    stream << "; ";
    print_c_name(op->name);
    stream << " < ";
    op->min.accept(this);
    stream << " + ";
    op->extent.accept(this);
    stream << "; ";
    print_c_name(op->name);
    stream << "++) {\n";
        
    indent += 2;
    op->body.accept(this);
    indent -= 2;

    do_indent();
    stream << "}\n";
}

void CodeGen_C::visit(const Provide *op) {
    assert(false && "Cannot emit Provide statements as C");
}

void CodeGen_C::visit(const Allocate *op) {
    do_indent();
    stream << "{\n";
    indent += 2;

    do_indent();
    print_c_type(op->type);
    stream << ' ';

    // For sizes less than 32k, do a stack allocation
    bool on_stack = false;
    int stack_size = 0;
    if (const IntImm *sz = op->size.as<IntImm>()) {
        stack_size = sz->value;
        on_stack = stack_size <= 32*1024;
    }

    if (on_stack) {
        print_c_name(op->name);
        stream << "[" << stack_size << "];\n";
    } else {        
        stream << "*";
        print_c_name(op->name);
        stream << " = (";
        print_c_type(op->type);
        stream << " *)halide_malloc(sizeof(";        
        print_c_type(op->type);
        stream << ")*";
        op->size.accept(this);
        stream << ");\n";
    }

    op->body.accept(this);            

    if (!on_stack) {
        do_indent();
        stream << "halide_free(";
        print_c_name(op->name);
        stream << ");\n";
    }

    indent -= 2;
    do_indent();
    stream << "}\n";
}

void CodeGen_C::visit(const Realize *op) {
    assert(false && "Cannot emit realize statements to C");
}

void CodeGen_C::test() {
    Argument buffer_arg("buf", true, Int(0));
    Argument float_arg("alpha", false, Float(32));
    Argument int_arg("beta", false, Int(32));
    vector<Argument> args(3);
    args[0] = buffer_arg;
    args[1] = float_arg;
    args[2] = int_arg;
    Var x("x");
    Param<float> alpha("alpha");
    Param<int> beta("beta");
    Expr e = new Select(alpha > 4.0f, 3, 2);
    Stmt s = new Store("buf", e, x);
    s = new LetStmt("x", beta+1, s);
    s = new Allocate("tmp.stack", Int(32), 127, s);
    s = new Allocate("tmp.heap", Int(32), 43 * beta, s);

    ostringstream source;
    CodeGen_C cg(source);
    cg.compile(s, "test1", args);
    string correct_source = \
        "#include <iostream>\n"
        "#include <assert.h>\n"
        "#ifndef BUFFER_T_DEFINED\n"
        "#define BUFFER_T_DEFINED\n"
        "#include <stdint.h>\n"
        "typedef struct buffer_t {\n"
        "    uint8_t* host;\n"
        "    uint64_t dev;\n"
        "    bool host_dirty;\n"
        "    bool dev_dirty;\n"
        "    int32_t extent[4];\n"
        "    int32_t stride[4];\n"
        "    int32_t min[4];\n"
        "    int32_t elem_size;\n"
        "} buffer_t;\n"
        "#endif\n"
        "\n"
        "extern \"C\" void *halide_malloc(size_t);\n"
        "extern \"C\" void halide_free(void *);\n"
        "\n"
        "template<typename T> T max(T a, T b) {if (a > b) return a; return b;}\n"
        "template<typename T> T min(T a, T b) {if (a < b) return a; return b;}\n"
        "\n"
        "void test1(const buffer_t *_buf, const float alpha, const int32_t beta) {\n"
        "uint8_t *buf = _buf->host;\n"
        "const int32_t buf_min_0 = _buf->min[0];\n"
        "const int32_t buf_min_1 = _buf->min[1];\n"
        "const int32_t buf_min_2 = _buf->min[2];\n"
        "const int32_t buf_min_3 = _buf->min[3];\n"
        "const int32_t buf_extent_0 = _buf->extent[0];\n"
        "const int32_t buf_extent_1 = _buf->extent[1];\n"
        "const int32_t buf_extent_2 = _buf->extent[2];\n"
        "const int32_t buf_extent_3 = _buf->extent[3];\n"
        "const int32_t buf_stride_0 = _buf->stride[0];\n"
        "const int32_t buf_stride_1 = _buf->stride[1];\n"
        "const int32_t buf_stride_2 = _buf->stride[2];\n"
        "const int32_t buf_stride_3 = _buf->stride[3];\n"
        "{\n"
        "  int32_t *tmp_heap = (int32_t *)halide_malloc(sizeof(int32_t)*(43*beta));\n"
        "  {\n"
        "    int32_t tmp_stack[127];\n"
        "    {\n"
        "      const int32_t x = (beta + 1);\n"
        "      ((int32_t *)buf)[x] = ((alpha > 4.000000f) ? 3 : 2);\n" 
        "      }\n" 
        "  }\n"
        "  halide_free(tmp_heap);\n" 
        "}\n"
        "}\n";
    if (source.str() != correct_source) {
        std::cout << "Correct source code:" << std::endl << correct_source;
        std::cout << "Actual source code:" << std::endl << source.str();
        assert(false);
    }        
    std::cout << "CodeGen_C test passed" << std::endl;
}

}
}
