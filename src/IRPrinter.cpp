#include <iostream>
#include <sstream>

#include "IRPrinter.h"
#include "IROperator.h"
#include "IR.h"

namespace Halide {

using std::ostream;
using std::vector;
using std::string;
using std::ostringstream;

ostream &operator<<(ostream &out, const Type &type) {
    switch (type.code) {
    case Type::Int:
        out << "int";
        break;
    case Type::UInt:
        out << "uint";
        break;
    case Type::Float:
        out << "float";
        break;
    case Type::Handle:
        out << "handle";
        break;
    }
    out << type.bits;
    if (type.width > 1) out << 'x' << type.width;
    return out;
}

ostream &operator<<(ostream &stream, const Expr &ir) {
    if (!ir.defined()) {
        stream << "(undefined)";
    } else {
        Internal::IRPrinter p(stream);
        p.print(ir);
    }
    return stream;
}

namespace Internal {

void IRPrinter::test() {
    Type i32 = Int(32);
    Type f32 = Float(32);
    Expr x = Variable::make(Int(32), "x");
    Expr y = Variable::make(Int(32), "y");
    ostringstream expr_source;
    expr_source << (x + 3) * (y / 2 + 17);
    internal_assert(expr_source.str() == "((x + 3)*((y/2) + 17))");

    Stmt store = Store::make("buf", (x * 17) / (x - 3), y - 1);
    Stmt for_loop = For::make("x", -2, y + 2, For::Parallel, store);
    vector<Expr> args(1); args[0] = x % 3;
    Expr call = Call::make(i32, "buf", args, Call::Extern);
    Stmt store2 = Store::make("out", call + 1, x);
    Stmt for_loop2 = For::make("x", 0, y, For::Vectorized , store2);
    Stmt pipeline = Pipeline::make("buf", for_loop, Stmt(), for_loop2);
    Stmt assertion = AssertStmt::make(y > 3, vec<Expr>(Expr("y is greater than "), 3));
    Stmt block = Block::make(assertion, pipeline);
    Stmt let_stmt = LetStmt::make("y", 17, block);
    Stmt allocate = Allocate::make("buf", f32, vec(Expr(1023)), const_true(), let_stmt);

    ostringstream source;
    source << allocate;
    std::string correct_source = \
        "allocate buf[float32 * 1023]\n"
        "let y = 17\n"
        "assert((y > 3), stringify(\"y is greater than \", 3))\n"
        "produce buf {\n"
        "  parallel (x, -2, (y + 2)) {\n"
        "    buf[(y - 1)] = ((x*17)/(x - 3))\n"
        "  }\n"
        "}\n"
        "vectorized (x, 0, y) {\n"
        "  out[x] = (buf((x % 3)) + 1)\n"
        "}\n";

    if (source.str() != correct_source) {
        internal_error << "Correct output:\n" << correct_source
                       << "Actual output:\n" << source.str();

    }
    std::cout << "IRPrinter test passed\n";
}

ostream &operator<<(ostream &out, const For::ForType &type) {
    switch (type) {
    case For::Serial:
        out << "for";
        break;
    case For::Parallel:
        out << "parallel";
        break;
    case For::Unrolled:
        out << "unrolled";
        break;
    case For::Vectorized:
        out << "vectorized";
        break;
    }
    return out;
}

ostream &operator<<(ostream &stream, const Stmt &ir) {
    if (!ir.defined()) {
        stream << "(undefined)\n";
    } else {
        Internal::IRPrinter p(stream);
        p.print(ir);
    }
    return stream;
}

IRPrinter::IRPrinter(ostream &s) : stream(s), indent(0) {
    s.setf(std::ios::fixed, std::ios::floatfield);
}

void IRPrinter::print(Expr ir) {
    ir.accept(this);
}

void IRPrinter::print(Stmt ir) {
    ir.accept(this);
}


void IRPrinter::do_indent() {
    for (int i = 0; i < indent; i++) stream << ' ';
}

void IRPrinter::visit(const IntImm *op) {
    stream << op->value;
}

void IRPrinter::visit(const FloatImm *op) {
    stream << op->value << 'f';
}

void IRPrinter::visit(const StringImm *op) {
    stream << '"';
    for (size_t i = 0; i < op->value.size(); i++) {
        unsigned char c = op->value[i];
        if (c >= ' ' && c <= '~' && c != '\\' && c != '"') {
            stream << c;
        } else {
            stream << '\\';
            switch (c) {
            case '"':
                stream << '"';
                break;
            case '\\':
                stream << '\\';
                break;
            case '\t':
                stream << 't';
                break;
            case '\r':
                stream << 'r';
                break;
            case '\n':
                stream << 'n';
                break;
            default:
                string hex_digits = "0123456789ABCDEF";
                stream << 'x' << hex_digits[c >> 4] << hex_digits[c & 0xf];
            }
        }
    }
    stream << '"';
}

void IRPrinter::visit(const Cast *op) {
    stream << op->type << '(';
    print(op->value);
    stream << ')';
}

void IRPrinter::visit(const Variable *op) {
    // omit the type
    // stream << op->name << "." << op->type;
    stream << "<" << op->type << "> " << op->name;
}

void IRPrinter::visit(const Add *op) {
    stream << '(';
    print(op->a);
    stream << " + ";
    print(op->b);
    stream << ')';
}

void IRPrinter::visit(const Sub *op) {
    stream << '(';
    print(op->a);
    stream << " - ";
    print(op->b);
    stream << ')';
}

void IRPrinter::visit(const Mul *op) {
    stream << '(';
    print(op->a);
    stream << "*";
    print(op->b);
    stream << ')';
}

void IRPrinter::visit(const Div *op) {
    stream << '(';
    print(op->a);
    stream << "/";
    print(op->b);
    stream << ')';
}

void IRPrinter::visit(const Mod *op) {
    stream << '(';
    print(op->a);
    stream << " % ";
    print(op->b);
    stream << ')';
}

void IRPrinter::visit(const Min *op) {
    stream << "min(";
    print(op->a);
    stream << ", ";
    print(op->b);
    stream << ")";
}

void IRPrinter::visit(const Max *op) {
    stream << "max(";
    print(op->a);
    stream << ", ";
    print(op->b);
    stream << ")";
}

void IRPrinter::visit(const EQ *op) {
    stream << '(';
    print(op->a);
    stream << " == ";
    print(op->b);
    stream << ')';
}

void IRPrinter::visit(const NE *op) {
    stream << '(';
    print(op->a);
    stream << " != ";
    print(op->b);
    stream << ')';
}

void IRPrinter::visit(const LT *op) {
    stream << '(';
    print(op->a);
    stream << " < ";
    print(op->b);
    stream << ')';
}

void IRPrinter::visit(const LE *op) {
    stream << '(';
    print(op->a);
    stream << " <= ";
    print(op->b);
    stream << ')';
}

void IRPrinter::visit(const GT *op) {
    stream << '(';
    print(op->a);
    stream << " > ";
    print(op->b);
    stream << ')';
}

void IRPrinter::visit(const GE *op) {
    stream << '(';
    print(op->a);
    stream << " >= ";
    print(op->b);
    stream << ')';
}

void IRPrinter::visit(const And *op) {
    stream << '(';
    print(op->a);
    stream << " && ";
    print(op->b);
    stream << ')';
}

void IRPrinter::visit(const Or *op) {
    stream << '(';
    print(op->a);
    stream << " || ";
    print(op->b);
    stream << ')';
}

void IRPrinter::visit(const Not *op) {
    stream << '!';
    print(op->a);
}

void IRPrinter::visit(const Select *op) {
    stream << "select(";
    print(op->condition);
    stream << ", ";
    print(op->true_value);
    stream << ", ";
    print(op->false_value);
    stream << ")";
}

void IRPrinter::visit(const Load *op) {
    stream << op->name << "[";
    print(op->index);
    stream << "]";
}

void IRPrinter::visit(const Ramp *op) {
    stream << "ramp(";
    print(op->base);
    stream << ", ";
    print(op->stride);
    stream << ", " << op->width << ")";
}

void IRPrinter::visit(const Broadcast *op) {
    stream << "x" << op->width << "(";
    print(op->value);
    stream << ")";
}

void IRPrinter::visit(const Call *op) {
    // Special-case some intrinsics for readability
    if (op->call_type == Call::Intrinsic) {
        if (op->name == Call::extract_buffer_min) {
            print(op->args[0]);
            stream << ".min[" << op->args[1] << "]";
            return;
        } else if (op->name == Call::extract_buffer_max) {
            print(op->args[0]);
            stream << ".max[" << op->args[1] << "]";
            return;
        }
    }

    stream << "<" << op->type << "> " << op->name << "(";
    for (size_t i = 0; i < op->args.size(); i++) {
        print(op->args[i]);
        if (i < op->args.size() - 1) {
            stream << ", ";
        }
    }
    stream << ")";
}

void IRPrinter::visit(const Let *op) {
    stream << "<" << op->value.type() << "> ";
    stream << "(let " << op->name << " = ";
    print(op->value);
    stream << " in\n";
    print(op->body);
    stream << ")";
}

void IRPrinter::visit(const LetStmt *op) {
    do_indent();
    stream << "let " << op->name << " = ";
    print(op->value);
    stream << '\n';

    print(op->body);
}

void IRPrinter::visit(const AssertStmt *op) {
    do_indent();
    stream << "assert(";
    print(op->condition);
    stream << ", ";
    print(op->message);
    stream << ")\n";
}

void IRPrinter::visit(const Pipeline *op) {

    do_indent();
    stream << "produce " << op->name << " {\n";
    indent += 2;
    print(op->produce);
    indent -= 2;

    if (op->update.defined()) {
        do_indent();
        stream << "} update " << op->name << " {\n";
        indent += 2;
        print(op->update);
        indent -= 2;
    }

    do_indent();
    stream << "}\n";

    print(op->consume);

}

void IRPrinter::visit(const For *op) {

    do_indent();
    stream << op->for_type << " (" << op->name << ", ";
    print(op->min);
    stream << ", ";
    print(op->extent);
    stream << ") {\n";

    indent += 2;
    print(op->body);
    indent -= 2;

    do_indent();
    stream << "}\n";
}

void IRPrinter::visit(const Store *op) {
    do_indent();
    stream << op->name << "[";
    print(op->index);
    stream << "] = ";
    print(op->value);
    stream << '\n';
}

void IRPrinter::visit(const Provide *op) {
    do_indent();
    stream << op->name << "(";
    for (size_t i = 0; i < op->args.size(); i++) {
        print(op->args[i]);
        if (i < op->args.size() - 1) stream << ", ";
    }
    stream << ") = ";
    if (op->values.size() > 1) {
        stream << "{";
    }
    for (size_t i = 0; i < op->values.size(); i++) {
        if (i > 0) {
            stream << ", ";
        }
        print(op->values[i]);
    }
    if (op->values.size() > 1) {
        stream << "}";
    }

    stream << '\n';
}

void IRPrinter::visit(const Allocate *op) {
    do_indent();
    stream << "allocate " << op->name << "[" << op->type;
    for (size_t i = 0; i < op->extents.size(); i++) {
        stream  << " * ";
        print(op->extents[i]);
    }
    stream << "]";
    if (!is_one(op->condition)) {
        stream << " if ";
        print(op->condition);
    }
    stream << "\n";
    print(op->body);
}

void IRPrinter::visit(const Free *op) {
    do_indent();
    stream << "free " << op->name << '\n';
}

void IRPrinter::visit(const Realize *op) {
    do_indent();
    stream << "realize " << op->name << "(";
    for (size_t i = 0; i < op->bounds.size(); i++) {
        stream << "[";
        print(op->bounds[i].min);
        stream << ", ";
        print(op->bounds[i].extent);
        stream << "]";
        if (i < op->bounds.size() - 1) stream << ", ";
    }
    stream << ")";
    if (!is_one(op->condition)) {
        stream << " if ";
        print(op->condition);
    }
    stream << " {\n";

    indent += 2;
    print(op->body);
    indent -= 2;

    do_indent();
    stream << "}\n";
}

void IRPrinter::visit(const Block *op) {
    print(op->first);
    if (op->rest.defined()) print(op->rest);
}

void IRPrinter::visit(const IfThenElse *op) {
    do_indent();
    while (1) {
        stream << "if (" << op->condition << ") {\n";
        indent += 2;
        print(op->then_case);
        indent -= 2;

        if (!op->else_case.defined()) {
            break;
        }

        if (const IfThenElse *nested_if = op->else_case.as<IfThenElse>()) {
            do_indent();
            stream << "} else ";
            op = nested_if;
        } else {
            do_indent();
            stream << "} else {\n";
            indent += 2;
            print(op->else_case);
            indent -= 2;
            break;
        }
    }

    do_indent();
    stream << "}\n";

}

void IRPrinter::visit(const Evaluate *op) {
    do_indent();
    print(op->value);
    stream << "\n";
}

}}
