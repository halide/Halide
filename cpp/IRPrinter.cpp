#include "IRPrinter.h"
#include "IR.h"

#include <iostream>

namespace HalideInternal {

    using std::endl;

    ostream &operator<<(ostream &out, Type type) {
        switch (type.t) {
        case Type::Int:
            out << 'i';
            break;
        case Type::UInt:
            out << 'u';
            break;
        case Type::Float:
            out << 'f';
            break;
        default:
            assert(false && "Malformed type");
        }
        out << type.bits;
        if (type.width > 1) out << 'x' << type.width;
        return out;
    }

    ostream &operator<<(ostream &out, For::ForType type) {
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
        default:
            assert(false && "Malformed for type");
        }
        return out;
    }

    ostream &operator<<(ostream &stream, const IR *ir) {
        IRPrinter p(ir, stream);
        return stream;
    }

    void do_indent(ostream &out, int indent) {
        for (int i = 0; i < indent; i++) out << ' ';
    }

    IRPrinter::IRPrinter(const IR *ir, ostream &s) : stream(s), indent(0) {
        ir->accept(this);
    }
    
    void IRPrinter::visit(const IntImm *op) {
        stream << op->value;
    }
    
    void IRPrinter::visit(const FloatImm *op) {
        stream << op->value;
    }
    
    void IRPrinter::visit(const Cast *op) { 
        stream << op->type << '(';
        op->value->accept(this);
        stream << ')';
    }
    
    void IRPrinter::visit(const Var *op) {
        // omit the type
        stream << op->name;
    }
    
    void IRPrinter::visit(const Add *op) {
        stream << '(';
        op->a->accept(this);
        stream << " + ";
        op->b->accept(this);
        stream << ')';
    }

    void IRPrinter::visit(const Sub *op) {
        stream << '(';
        op->a->accept(this);
        stream << " - ";
        op->b->accept(this);
        stream << ')';
    }

    void IRPrinter::visit(const Mul *op) {
        stream << '(';
        op->a->accept(this);
        stream << "*";
        op->b->accept(this);
        stream << ')';
    }

    void IRPrinter::visit(const Div *op) {
        stream << '(';
        op->a->accept(this);
        stream << "/";
        op->b->accept(this);
        stream << ')';
    }
            
    void IRPrinter::visit(const Mod *op) {
        stream << '(';
        op->a->accept(this);
        stream << " % ";
        op->b->accept(this);
        stream << ')';
    }

    void IRPrinter::visit(const Min *op) {
        stream << "min(";
        op->a->accept(this);
        stream << ", ";
        op->b->accept(this);
        stream << ")";
    }

    void IRPrinter::visit(const Max *op) {
        stream << "max(";
        op->a->accept(this);
        stream << ", ";
        op->b->accept(this);
        stream << ")";
    }

    void IRPrinter::visit(const EQ *op) {
        stream << '(';
        op->a->accept(this);
        stream << " == ";
        op->b->accept(this);
        stream << ')';
    }

    void IRPrinter::visit(const NE *op) {
        stream << '(';
        op->a->accept(this);
        stream << " != ";
        op->b->accept(this);
        stream << ')';
    }

    void IRPrinter::visit(const LT *op) {
        stream << '(';
        op->a->accept(this);
        stream << " < ";
        op->b->accept(this);
        stream << ')';
    }

    void IRPrinter::visit(const LE *op) {
        stream << '(';
        op->a->accept(this);
        stream << " <= ";
        op->b->accept(this);
        stream << ')';
    }

    void IRPrinter::visit(const GT *op) {
        stream << '(';
        op->a->accept(this);
        stream << " > ";
        op->b->accept(this);
        stream << ')';
    }

    void IRPrinter::visit(const GE *op) {
        stream << '(';
        op->a->accept(this);
        stream << " >= ";
        op->b->accept(this);
        stream << ')';
    }

    void IRPrinter::visit(const And *op) {
        stream << '(';
        op->a->accept(this);
        stream << " && ";
        op->b->accept(this);
        stream << ')';
    }

    void IRPrinter::visit(const Or *op) {
        stream << '(';
        op->a->accept(this);
        stream << " || ";
        op->b->accept(this);
        stream << ')';
    }

    void IRPrinter::visit(const Not *op) {
        stream << '!';
        op->a->accept(this);
    }

    void IRPrinter::visit(const Select *op) {
        stream << "select(";
        op->condition->accept(this);
        stream << ", ";
        op->true_value->accept(this);
        stream << ", ";
        op->false_value->accept(this);
        stream << ")";
    }

    void IRPrinter::visit(const Load *op) {
        stream << op->buffer << "[";
        op->index->accept(this);
        stream << "]";
    }

    void IRPrinter::visit(const Ramp *op) {
        stream << "ramp(";
        op->base->accept(this);
        stream << ", ";
        op->stride->accept(this);
        stream << ", " << op->width << ")";
    }

    void IRPrinter::visit(const Call *op) {
        stream << op->buffer << "(";
        for (size_t i = 0; i < op->args.size(); i++) {
            op->args[i]->accept(this);
            if (i < op->args.size() - 1) {
                stream << ", ";
            }
        }
        stream << ")";
    }

    void IRPrinter::visit(const Let *op) {
        stream << "(let " << op->name << " = ";
        op->value->accept(this);
        stream << " in ";
        op->body->accept(this);
        stream << ")";
    }

    void IRPrinter::visit(const LetStmt *op) {
        do_indent(stream, indent);
        stream << "let " << op->name << " = ";
        op->value->accept(this);
        stream << endl;

        do_indent(stream, indent);
        op->body->accept(this);
        stream << endl;
    }

    void IRPrinter::visit(const PrintStmt *op) {
        do_indent(stream, indent);
        stream << "print(" << op->prefix;
        for (size_t i = 0; i < op->args.size(); i++) {
            stream << ", ";
            op->args[i]->accept(this);
        }
        stream << ")" << endl;
    }

    void IRPrinter::visit(const AssertStmt *op) {
        do_indent(stream, indent);
        stream << "assert(";
        op->condition->accept(this);
        stream << ", " << op->message << endl;
    }

    void IRPrinter::visit(const Pipeline *op) {
        indent += 2;

        do_indent(stream, indent - 2);
        stream << "produce " << op->buffer << " {" << endl;
        op->produce->accept(this);

        if (op->update) {
            do_indent(stream, indent - 2);
            stream << "} update {" << endl;
            op->update->accept(this);            
        }
        
        do_indent(stream, indent - 2);
        stream << "} consume {" << endl;
        op->consume->accept(this);
        
        do_indent(stream, indent - 2);        
        stream << "}" << endl;

        indent -= 2;
    }

    void IRPrinter::visit(const For *op) {

        do_indent(stream, indent);
        stream << op->for_type << " (" << op->name << ", ";
        op->min->accept(this);
        stream << ", ";
        op->extent->accept(this);
        stream << ") {" << endl;
        
        indent += 2;
        op->body->accept(this);
        indent -= 2;

        do_indent(stream, indent);
        stream << "}" << endl;
    }

    void IRPrinter::visit(const Store *op) {
        do_indent(stream, indent);
        stream << op->buffer << "[";
        op->index->accept(this);
        stream << "] = ";
        op->value->accept(this);
        stream << endl;
    }

    void IRPrinter::visit(const Provide *op) {
        do_indent(stream, indent);
        stream << op->buffer << "(";
        for (size_t i = 0; i < op->args.size(); i++) {
            op->args[i]->accept(this);
            if (i < op->args.size() - 1) stream << ", ";
        }
        stream << ") = ";
        op->value->accept(this);
        stream << endl;
    }

    void IRPrinter::visit(const Allocate *op) {
        do_indent(stream, indent);
        stream << "allocate " << op->buffer << "[" << op->type << " * ";
        op->size->accept(this);
        stream << "]" << endl;
        op->body->accept(this);

        do_indent(stream, indent);
        stream << "free" << op->buffer << endl;
    }

    void IRPrinter::visit(const Realize *op) {
        do_indent(stream, indent);
        stream << "realize " << op->buffer << "(";
        for (size_t i = 0; i < op->bounds.size(); i++) {
            stream << "[";
            op->bounds[i].first->accept(this);
            stream << ", ";
            op->bounds[i].second->accept(this);
            stream << "]";
            if (i < op->bounds.size() - 1) stream << ", ";
        }
        stream << ") {" << endl;

        indent += 2;
        op->body->accept(this);
        indent -= 2;

        do_indent(stream, indent);
        stream << "}" << endl;
    }

    void IRPrinter::visit(const Block *op) {
        op->first->accept(this);
        if (op->rest) op->rest->accept(this);
    }

    void IRPrinter::test() {
        Type i32 = Int(32);
        Var x(i32, "x");
        IntImm three(3);
        Add add(&x, &three);
        IRPrinter p(&add, std::cout);
    }

    
}
