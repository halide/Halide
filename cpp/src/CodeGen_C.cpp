#include "CodeGen_C.h"
#include "Substitute.h"
#include "IROperator.h"
#include "Param.h"
#include "Var.h"
#include <sstream>
#include <iostream>
#include "Log.h"

namespace Halide { 
namespace Internal {

using std::ostream;
using std::endl;
using std::string;
using std::vector;
using std::ostringstream;
using std::map;

CodeGen_C::CodeGen_C(ostream &s) : IRPrinter(s), id("$$ BAD ID $$") {}

string CodeGen_C::print_type(Type type) {
    ostringstream oss;
    assert(type.width == 1 && "Can't codegen vector types to C (yet)");
    if (type.is_float()) {
        if (type.bits == 32) {
            oss << "float";
        } else if (type.bits == 64) {
            oss << "double";
        } else {
            assert(false && "Can't represent a float with this many bits in C");
        }
            
    } else {
        switch (type.bits) {
        case 1:
            oss << "bool";
            break;
        case 8: case 16: case 32: case 64:
            if (type.is_uint()) oss << 'u';
            oss << "int" << type.bits << "_t";                
            break;
        default:
            assert(false && "Can't represent an integer with this many bits in C");                
        }
    }
    return oss.str();
}

string CodeGen_C::print_name(const string &name) {
    ostringstream oss;
    for (size_t i = 0; i < name.size(); i++) {
        if (name[i] == '.') oss << '_';
        else if (name[i] == '$') oss << "__";
        else oss << name[i];
    }
    return oss.str();
}

void CodeGen_C::compile_header(const string &name, const vector<Argument> &args) {
    stream << "#ifndef HALIDE_" << name << '\n'
           << "#define HALIDE_" << name << '\n';

    // Throw in a definition of a buffer_t
    stream << "#ifndef BUFFER_T_DEFINED\n"
           << "#define BUFFER_T_DEFINED\n"
           << "#include <stdint.h>\n"
           << "typedef struct buffer_t {\n"
           << "    uint64_t dev;\n"
           << "    uint8_t* host;\n"
           << "    int32_t extent[4];\n"
           << "    int32_t stride[4];\n"
           << "    int32_t min[4];\n"
           << "    int32_t elem_size;\n"
           << "    bool host_dirty;\n"
           << "    bool dev_dirty;\n"
           << "} buffer_t;\n"
           << "#endif\n";

    // Now the function prototype
    stream << "extern \"C\" void " << name << "(";
    for (size_t i = 0; i < args.size(); i++) {
        if (i > 0) stream << ", ";
        if (args[i].is_buffer) {
            stream << "const buffer_t *" << args[i].name;
        } else {
            stream << "const "
                   << print_type(args[i].type)
                   << " " << args[i].name;
        }
    }
    stream << ");\n";

    stream << "#endif\n";    
}

namespace {
const string preamble = 
    "#include <iostream>\n"
    "#include <math.h>\n"
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
    "extern \"C\" int halide_debug_to_file(const char *filename, void *data, int, int, int, int, int, int);\n"
    "extern \"C\" int halide_start_clock();\n"
    "extern \"C\" int halide_current_time();\n"
    "extern \"C\" int halide_printf(const char *fmt, ...);\n"
    "extern \"C\" inline float pow_f32(float x, float y) {return powf(x, y);}\n"
    "extern \"C\" inline float round_f32(float x) {return roundf(x);}\n"
    "\n"
    "template<typename T> T max(T a, T b) {if (a > b) return a; return b;}\n"
    "template<typename T> T min(T a, T b) {if (a < b) return a; return b;}\n"
    "template<typename T> T mod(T a, T b) {T result = a % b; if (result < 0) result += b; return result;}\n"
    "template<typename T> T sdiv(T a, T b) {return (a - mod(a, b))/b;}\n"
    "\n";
}


void CodeGen_C::compile(Stmt s, const string &name, const vector<Argument> &args) {
    stream << preamble;

    // Emit the function prototype
    stream << "extern \"C\" void " << name << "(";
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            stream << "const buffer_t *_"
                   << print_name(args[i].name);
        } else {
            stream << "const "
                   << print_type(args[i].type)
                   << " "
                   << print_name(args[i].name);
        }

        if (i < args.size()-1) stream << ", ";
    }

    stream << ") {\n";

    // Unpack the buffer_t's
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            string name = print_name(args[i].name);
            string type = print_type(args[i].type);
            stream << type
                   << " *"
                   << name
                   << " = ("
                   << type
                   << " *)(_"
                   << name
                   << "->host);\n";
            allocation_types.push(args[i].name, args[i].type);

            for (int j = 0; j < 4; j++) {
                stream << "const int32_t "
                       << name
                       << "_min_" << j << " = _"
                       << name
                       << "->min[" << j << "];\n";
            }
            for (int j = 0; j < 4; j++) {
                stream << "const int32_t "
                       << name
                       << "_extent_" << j << " = _"
                       << name
                       << "->extent[" << j << "];\n";
            }
            for (int j = 0; j < 4; j++) {
                stream << "const int32_t "
                       << name
                       << "_stride_" << j << " = _"
                       << name
                       << "->stride[" << j << "];\n";
            }
        }
    }        

    print(s);

    stream << "}\n";
}

string CodeGen_C::print_expr(Expr e) {
    id = "$$ BAD ID $$";
    e.accept(this);
    return id;
}

void CodeGen_C::print_stmt(Stmt s) {
    s.accept(this);
}

void CodeGen_C::print_assignment(Type t, const std::string &rhs) {

    map<string, string>::iterator cached = cache.find(rhs);

    if (cached == cache.end()) {
        id = unique_name('V');
        do_indent();
        stream << print_type(t)
               << " " << id 
               << " = " << rhs << ";\n";    
        cache[rhs] = id;
    } else {
        id = cached->second;
    }
}

void CodeGen_C::open_scope() {
    cache.clear();
    do_indent();
    indent++;
    stream << "{\n";    
}

void CodeGen_C::close_scope() {
    cache.clear();
    indent--;
    do_indent();
    stream << "}\n";
}

void CodeGen_C::visit(const Variable *op) {
    ostringstream oss;
    for (size_t i = 0; i < op->name.size(); i++) {
        if (op->name[i] == '.') oss << '_';
        else if (op->name[i] == '$') oss << "__";
        else oss << op->name[i];
    }    
    id = oss.str();
}

void CodeGen_C::visit(const Cast *op) {     
    print_assignment(op->type, "(" + print_type(op->type) + ")" + print_expr(op->value));
}

void CodeGen_C::visit_binop(Type t, Expr a, Expr b, const char * op) {
    print_assignment(t, print_expr(a) + " " + op + " " + print_expr(b));
}

void CodeGen_C::visit(const Add *op) {
    visit_binop(op->type, op->a, op->b, "+");
}

void CodeGen_C::visit(const Sub *op) {
    visit_binop(op->type, op->a, op->b, "-");
}

void CodeGen_C::visit(const Mul *op) {
    visit_binop(op->type, op->a, op->b, "*");
}

void CodeGen_C::visit(const Div *op) {
    int bits;
    if (is_const_power_of_two(op->b, &bits)) {
        ostringstream oss;
        oss << print_expr(op->a) << " >> " << bits;
        print_assignment(op->type, oss.str());
    } else if (op->type.is_int()) {
        print_expr(new Call(op->type, "sdiv", vec(op->a, op->b)));
    } else {
        visit_binop(op->type, op->a, op->b, "/");
    }
}

void CodeGen_C::visit(const Mod *op) {
    int bits;
    if (is_const_power_of_two(op->b, &bits)) {
        ostringstream oss;
        oss << print_expr(op->a) << " & " << ((1 << bits)-1);
        print_assignment(op->type, oss.str());
    } else {
        print_expr(new Call(op->type, "mod", vec(op->a, op->b)));
    }
}

void CodeGen_C::visit(const Max *op) {
    print_expr(new Call(op->type, "max", vec(op->a, op->b)));
}

void CodeGen_C::visit(const Min *op) {
    print_expr(new Call(op->type, "min", vec(op->a, op->b)));
}

void CodeGen_C::visit(const EQ *op) {
    visit_binop(op->type, op->a, op->b, "==");
}

void CodeGen_C::visit(const NE *op) {
    visit_binop(op->type, op->a, op->b, "!=");
}

void CodeGen_C::visit(const LT *op) {
    visit_binop(op->type, op->a, op->b, "<");
}

void CodeGen_C::visit(const LE *op) {
    visit_binop(op->type, op->a, op->b, "<=");
}

void CodeGen_C::visit(const GT *op) {
    visit_binop(op->type, op->a, op->b, ">");
}

void CodeGen_C::visit(const GE *op) {
    visit_binop(op->type, op->a, op->b, ">=");
}

void CodeGen_C::visit(const Or *op) {
    visit_binop(op->type, op->a, op->b, "||");
}

void CodeGen_C::visit(const And *op) {
    visit_binop(op->type, op->a, op->b, "&&");
}

void CodeGen_C::visit(const Not *op) {
    print_assignment(op->type, "!(" + print_expr(op->a) + ")");
}

void CodeGen_C::visit(const IntImm *op) {
    ostringstream oss;
    oss << op->value;
    id = oss.str();
}

void CodeGen_C::visit(const FloatImm *op) {
    ostringstream oss;
    oss.setf(std::ios::fixed, std::ios::floatfield);
    oss << op->value << 'f';
    id = oss.str();
}

void CodeGen_C::visit(const Call *op) {

    ostringstream rhs;

    // Handle intrinsics first
    if (op->name == "debug to file") {
        assert(op->args.size() == 8);
        string filename = op->args[0].as<Call>()->name;
        string func = op->args[1].as<Call>()->name;


        vector<string> args(6);
        for (size_t i = 0; i < args.size(); i++) {        
            args[i] = print_expr(op->args[i+2]);
        }

        rhs << "halide_debug_to_file(\"" + filename + "\", " + func;
        for (size_t i = 0; i < args.size(); i++) {
            rhs << ", " << args[i];
        }
        rhs << ")";
    } else {
        // Generic calls
        vector<string> args(op->args.size());
        for (size_t i = 0; i < op->args.size(); i++) {        
            args[i] = print_expr(op->args[i]);
        }
        rhs << print_name(op->name) << "(";
        for (size_t i = 0; i < op->args.size(); i++) {
            if (i > 0) rhs << ", ";
            rhs << args[i];
        }
        rhs << ")";
    }

    print_assignment(op->type, rhs.str());
}

void CodeGen_C::visit(const Load *op) {
    bool type_cast_needed = !(allocation_types.contains(op->name) && 
                              allocation_types.get(op->name) == op->type);
    ostringstream rhs;
    if (type_cast_needed) {
        rhs << "(("
            << print_type(op->type)
            << " *)"
            << print_name(op->name)
            << ")";
    } else {
        rhs << print_name(op->name);
    }
    rhs << "[" 
        << print_expr(op->index) 
        << "]";

    print_assignment(op->type, rhs.str());
}

void CodeGen_C::visit(const Store *op) {

    Type t = op->value.type();

    bool type_cast_needed = !(allocation_types.contains(op->name) && 
                              allocation_types.get(op->name) == t);

    string id_index = print_expr(op->index);
    string id_value = print_expr(op->value);
    do_indent();    

    if (type_cast_needed) {
        stream << "(("
               << print_type(t)
               << " *)"
               << print_name(op->name)
               << ")";
    } else {
        stream << print_name(op->name);
    }
    stream << "[" 
           << id_index
           << "] = " 
           << id_value
           << ";\n";
}

void CodeGen_C::visit(const Let *op) {
    string id_value = print_expr(op->value);
    Expr new_var = new Variable(op->value.type(), id_value);
    Expr body = substitute(op->name, new_var, op->body);   
    print_expr(body);
}

void CodeGen_C::visit(const Select *op) {
    ostringstream rhs;
    rhs << print_type(op->type)
        << "(" << print_expr(op->condition)
        << " ? " << print_expr(op->true_value)
        << " : " << print_expr(op->false_value)
        << ")";
    print_assignment(op->type, rhs.str());
}

void CodeGen_C::visit(const LetStmt *op) {
    string id_value = print_expr(op->value);
    Expr new_var = new Variable(op->value.type(), id_value);
    Stmt body = substitute(op->name, new_var, op->body);   
    body.accept(this);
}

void CodeGen_C::visit(const PrintStmt *op) {

    vector<string> args;
    for (size_t i = 0; i < op->args.size(); i++) {        
        args.push_back(print_expr(op->args[i]));
    }
    
    do_indent();
    string format_string;
    stream << "halide_printf(\"" << op->prefix;
    for (size_t i = 0; i < op->args.size(); i++) {
        if (op->args[i].type().is_int() || 
            op->args[i].type().is_uint()) {
            stream << " %d";
        } else {
            stream << " %f";
        }
    }
    stream << "\"";
    for (size_t i = 0; i < op->args.size(); i++) {
        stream << ", " << args[i];
    }
    stream << ");\n";
}

void CodeGen_C::visit(const AssertStmt *op) {
    string id_cond = print_expr(op->condition);
    do_indent();
    stream << "assert(" << id_cond
           << " && \"" << op->message 
           << "\");\n";
}

void CodeGen_C::visit(const Pipeline *op) {

    do_indent();
    stream << "// produce " << op->name << '\n';
    print_stmt(op->produce);

    if (op->update.defined()) {
        do_indent();
        stream << "// update " << op->name << '\n';
        print_stmt(op->update);
    }
        
    do_indent();
    stream << "// consume " << op->name << '\n';
    print_stmt(op->consume);
}

void CodeGen_C::visit(const For *op) {
    if (op->for_type == For::Parallel) {
        do_indent();
        stream << "#pragma omp parallel for\n";
    } else {
        assert(op->for_type == For::Serial && "Can only emit serial or parallel for loops to C");
    }       

    string id_min = print_expr(op->min);
    string id_extent = print_expr(op->extent);

    do_indent();
    stream << "for (int "
           << print_name(op->name)
           << " = " << id_min
           << "; "
           << print_name(op->name)
           << " < " << id_min
           << " + " << id_extent
           << "; "
           << print_name(op->name)
           << "++)\n";
        
    open_scope();
    op->body.accept(this);
    close_scope();

}

void CodeGen_C::visit(const Provide *op) {
    assert(false && "Cannot emit Provide statements as C");
}

void CodeGen_C::visit(const Allocate *op) {
    open_scope();

    string size_id = print_expr(op->size);

    allocation_types.push(op->name, op->type);

    do_indent();
    stream << print_type(op->type) << ' ';

    // For sizes less than 32k, do a stack allocation
    bool on_stack = false;
    int stack_size = 0;
    if (const IntImm *sz = op->size.as<IntImm>()) {
        stack_size = sz->value;
        on_stack = stack_size <= 32*1024;
    }

    if (on_stack) {
        stream << print_name(op->name)
               << "[" << size_id << "];\n";
    } else {                
        stream << "*"
               << print_name(op->name)
               << " = ("
               << print_type(op->type)
               << " *)halide_malloc(sizeof("
               << print_type(op->type)
               << ")*" << size_id << ");\n";
    }

    op->body.accept(this);            

    if (!on_stack) {
        do_indent();
        stream << "halide_free("
               << print_name(op->name)
               << ");\n";
    }

    allocation_types.pop(op->name);

    close_scope();
}

void CodeGen_C::visit(const Realize *op) {
    assert(false && "Cannot emit realize statements to C");
}

void CodeGen_C::test() {
    Argument buffer_arg("buf", true, Int(32));
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
    
    string correct_source = preamble + 
        "extern \"C\" void test1(const buffer_t *_buf, const float alpha, const int32_t beta) {\n"
        "int32_t *buf = (int32_t *)(_buf->host);\n"
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
        " int32_t V0 = 43 * beta;\n"
        " int32_t *tmp_heap = (int32_t *)halide_malloc(sizeof(int32_t)*V0);\n"
        " {\n"
        "  int32_t tmp_stack[127];\n"
        "  int32_t V1 = beta + 1;\n"
        "  bool V2 = alpha > 4.000000f;\n"
        "  int32_t V3 = int32_t(V2 ? 3 : 2);\n"
        "  buf[V1] = V3;\n"
        " }\n"
        " halide_free(tmp_heap);\n" 
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
