#include "StmtToJson.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "IRPrinter.h"
#include "Scope.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdio.h>

namespace Halide {
namespace Internal {

namespace {

inline std::string get_indent(int indent) {
    std::stringstream s;
    for (int i = 0; i < indent; i++) {
        s << "    ";
    }
    return s.str();
}

template<typename T>
std::string to_string(T value, int base_indent = 0) {
    std::ostringstream os;
    os << value;
    return os.str();
}

template<>
std::string to_string(Halide::Internal::Parameter p, int base_indent) {
    std::ostringstream os;
    os << "{\n";
    os << get_indent(base_indent+1) << "type: " << to_string(p.type()) << "\n";
    os << get_indent(base_indent+1) << "is_buffer: " << to_string(p.is_buffer()) << "\n";
    os << get_indent(base_indent+1) << "dimensions: " << to_string(p.dimensions()) << "\n";
    os << get_indent(base_indent+1) << "name: \"" << to_string(p.name()) << "\"\n";
    os << get_indent(base_indent) << "}";

    return os.str();
}

template<>
std::string to_string(Halide::Internal::ModulusRemainder m, int base_indent) {
    std::ostringstream os;
    os << "{ modulus: " << m.modulus
        << ", remainder: " << m.remainder << "}";
    return os.str();
}

template<>
std::string to_string(Halide::Range r, int base_indent) {
    std::stringstream os;
    os << "{ min: " << r.min << ", extent: " << r.extent << "}";
    return os.str();
}

template<>
std::string to_string(Halide::Internal::PrefetchDirective p, int base_indent) {
    std::stringstream os;
    std::map<Halide::PrefetchBoundStrategy, std::string> strategy_str =
        {{Halide::PrefetchBoundStrategy::Clamp, "Clamp"},
         {Halide::PrefetchBoundStrategy::GuardWithIf, "GuardWithIf"},
         {Halide::PrefetchBoundStrategy::NonFaulting, "NonFaulting"}
        };
    os << "{\n";
    os << get_indent(base_indent+1) << "name: " << p.name << ",\n";
    os << get_indent(base_indent+1) << "var: " << p.var << ",\n";
    //TODO: this needs to use the JSON visitor..
    os << get_indent(base_indent+1) << "offset: " << p.offset << ",\n";
    os << get_indent(base_indent+1) << "strategy: " << strategy_str[p.strategy] << ",\n";
    os << get_indent(base_indent+1) << "param: " << to_string(p.param) << "\n";
    os << get_indent(base_indent) << "}";

    return os.str();
}

} // anonymous namespace


using std::string;
using std::vector;

class StmtToJson : public IRVisitor {
    int indent;
    std::stringstream stream;

    string get_indent() {
        return ::Halide::Internal::get_indent(indent);
    }

    inline void increase_indent() {
        indent++;
    }

    inline void decrease_indent() {
        indent--;
    }

    inline string quoted_str(string str) {
        std::stringstream s;
        s << "\"" << str << "\"";
        return s.str();
    }

    inline void open_obj(string node_type) {
        stream << get_indent() << "{\n";
        increase_indent();
        stream << get_indent() << "_node_type : " << quoted_str(node_type) << ",\n";
    }

    inline void close_obj() {
        decrease_indent();
        stream << get_indent() << "}\n";
    }

    inline void print_type(Type t) {
        stream << get_indent() << "type : " << quoted_str(to_string(t)) << ",\n";
    }

    template<typename T>
    void print_immediate(string node_type, Type t, T value) {
        open_obj(node_type);
        print_type(t);
        stream << get_indent() << "value : " << to_string(value) << "\n";
        close_obj();
    }


    void visit(const IntImm *e) override {
        print_immediate("IntImm", e->type, e);
    }
    void visit(const UIntImm *e) override {
        print_immediate("UIntImm", e->type, e);
    }
    void visit(const FloatImm *e) override {
        print_immediate("FloatImm", e->type, e);
    }

    void visit(const StringImm *e) override {
        print_immediate("StringImm", e->type, e);
    }

    void visit(const Cast *e) override {
        open_obj("Cast");
        print_type(e->type);
        stream << get_indent() << "value : ";
        e->accept(this); 
        close_obj();
    }

    void visit(const Variable *e) override {
        internal_assert(false);
    }

    template<typename T>
    inline void print_binop(string node_type, T e) {
        open_obj(node_type);
        print_type(e->type);
        stream << get_indent() << "a : ";
        e->a.accept(this);
        stream << get_indent() << "b : ";
        e->b.accept(this);
        close_obj();
    }

    void visit(const Add *e) override {
        print_binop("Add", e);
    }

    void visit(const Sub *e) override {
        print_binop("Sub", e);
    }

    void visit(const Mul *e) override {
        print_binop("Mul", e);
    }

    void visit(const Div *e) override {
        print_binop("Div", e);
    }

    void visit(const Mod *e) override {
        print_binop("Mod", e);
    }

    void visit(const Min *e) override {
        print_binop("Min", e);
    }

    void visit(const Max *e) override {
        print_binop("Max", e);
    }

    void visit(const EQ *e) override {
        print_binop("EQ", e);
    }
    void visit(const NE *e) override {
        print_binop("NE", e);
    }

    void visit(const LT *e) override {
        print_binop("LT", e);
    }

    void visit(const LE *e) override {
        print_binop("LE", e);
    }

    void visit(const GT *e) override {
        print_binop("GT", e);
    }

    void visit(const GE *e) override {
        print_binop("GE", e);
    }

    void visit(const And *e) override {
        print_binop("And", e);
    }

    void visit(const Or *e) override {
        print_binop("Or", e);
    }

    void visit(const Not *e) override {
        open_obj("Not");
        print_type(e->type);
        stream << get_indent() << "value : ";
        e->accept(this);
        close_obj();
    }

    void visit(const Select *e) override {
        open_obj("Select");
        print_type(e->type);
        stream << get_indent() << "condition : ";
        e->condition.accept(this);
        stream << get_indent() << "true_value : ";
        e->true_value.accept(this);
        stream << get_indent() << "false_value : ";
        e->false_value.accept(this);
        close_obj();
    }

    void visit(const Load *e) override {
        open_obj("Load");
        print_type(e->type);
        stream << get_indent() << "name : " << quoted_str(e->name) << "\n";
        stream << get_indent() << "index : ";
        e->index.accept(this);
        stream << get_indent() << "image : ";
        // TODO: implement
        stream << "\"UNIMPLEMENTED\",\n";
        //stream << to_string(e->image) << ",\n";

        stream << get_indent() << "param : ";
        stream << to_string(e->param) << ",\n";

        stream << get_indent() << "alignment : ";
        stream << to_string(e->alignment) << "\n";
        close_obj();
    }

    void visit(const Ramp *e) override {
        open_obj("Ramp");
        print_type(e->type);
        stream << get_indent() << "base : ";
        e->base.accept(this);
        stream << get_indent() << "stride: ";
        e->stride.accept(this);
        stream << get_indent() << "lanes : " << e->lanes << "\n";
        close_obj();
    }

    void visit(const Broadcast *e) override {
        open_obj("Broadcast");
        print_type(e->type);
        stream << get_indent() << "value : ";
        e->value.accept(this);
        stream << get_indent() << "lanes : " << e->lanes << "\n";
        close_obj();
    }

    void visit(const Call *e) override {
        internal_error << "Todo: Call node\n";
    }

    void visit(const Let *e) override {
        open_obj("Let");
        print_type(e->type);
        stream << get_indent() << "name : " << quoted_str(e->name) << ",\n";
        stream << get_indent() << "value : ";
        e->value.accept(this);
        stream << get_indent() << "body : ";
        e->body.accept(this);
        close_obj();
    }

    void visit(const LetStmt *s) override {
        open_obj("LetStmt");
        stream << get_indent() << "name : " << quoted_str(s->name) << ",\n";
        stream << get_indent() << "value : ";
        s->value.accept(this);
        stream << get_indent() << "body : ";
        s->body.accept(this);
        close_obj();
    }

    void visit(const AssertStmt *s) override {
        open_obj("AssertStmt");
        stream << get_indent() << "condition : ";
        s->condition.accept(this);
        stream << get_indent() << "message : ";
        s->message.accept(this);
        close_obj();
    }

    void visit(const ProducerConsumer *) override {
        internal_error << "Should not see ProducerConsumer in backend\n";
    }

    void visit(const For *s) override {
        open_obj("For");
        stream << get_indent() << "name : "
               << quoted_str(s->name) << "\n,";
        stream << get_indent() << "min : ";
        s->min.accept(this);
        stream << get_indent() << "extent : ";
        s->extent.accept(this);
        stream << get_indent() << "for_type : "
               << quoted_str(to_string(s->for_type)) << ",\n";
        stream << get_indent() << "device_api : "
               << quoted_str(to_string(s->device_api)) << ",\n";
        stream << get_indent() << "body : ";
        s->body.accept(this);
        close_obj();
    }

    void visit(const Store *s) override {
        open_obj("Store");
        stream << get_indent() << "name : "
               << quoted_str(s->name) << ",\n";
        stream << get_indent() << "predicate : ";
        s->predicate.accept(this);
        stream << get_indent() << "value : ";
        s->value.accept(this);
        stream << get_indent() << "index : ";
        s->index.accept(this);
        stream << get_indent() << "param : "
               << to_string(s->param)<< ",\n";
        stream << get_indent() << "alignment : "
               << quoted_str(to_string(s->alignment)) << ",\n";
        close_obj();
    }
    void visit(const Provide *) override {
        internal_error << "Should not see Provide in backend\n";
    }

    template<typename T>
    inline void print_vector(const vector<T> &v) {
        stream << get_indent() << "[\n";
        increase_indent();
        for (auto &e: v) {
            stream << to_string(e) << ",\n";
        }
        decrease_indent();
        stream << get_indent() << "]\n";
    }


    template<>
    inline void print_vector(const vector<Expr> &v) {
        stream << get_indent() << "[\n";
        increase_indent();
        for (auto &e: v) {
            e->accept(this);
            stream << ",\n";
        }
        decrease_indent();
        stream << get_indent() << "]\n";
    }

    void visit(const Allocate *s) override {
        open_obj("Allocate");
        stream << get_indent() << "name : " << quoted_str(s->name) << ",\n";
        print_type(s->type);
        stream << get_indent() << "memory_type : "
               << quoted_str(to_string(s->memory_type))
               << ",\n";
        stream << get_indent() << "extents : ";
        print_vector(s->extents);
        stream << get_indent() << "condition : ";
        s->condition.accept(this);
        stream << get_indent() << "new_expr : ";
        if (s->new_expr.defined()) {
            s->new_expr.accept(this);
        } else {
            stream << "{ }";
        }
        stream << get_indent() << "free_function : "
               << quoted_str(s->free_function)
               << ",\n";
        stream << get_indent() << "body : ";
        s->body.accept(this);
        close_obj();
    }

    void visit(const Free *s) override {
        open_obj("Free");
        stream << get_indent() << "name : " << quoted_str(s->name) << "\n";
        close_obj();
    }

    void visit(const Realize *) override {
        internal_error << "Should not see Realize in backend\n";
    }

    void visit(const Block *s) override {
        open_obj("Block");
        stream << get_indent() << "first : ";
        s->first.accept(this);
        stream << get_indent() << "rest : ";
        if (s->rest.defined()) {
            s->rest.accept(this);
        } else {
            stream << "{ }\n";
        }
        close_obj();
    }

    void visit(const IfThenElse *s) override {
        open_obj("IfThenElse");
        stream << get_indent() << "condition : ";
        s->condition.accept(this);
        stream << get_indent() << "then_case";
        s->condition.accept(this);
        stream << get_indent() << "else_case";
        if (s->else_case.defined()) {
            s->else_case.accept(this);
        } else {
            stream << "{ }\n";
        }
    }

    void visit(const Evaluate *s) override {
        open_obj("Evaluate");
        stream << get_indent() << "value : ";
        s->value.accept(this);
        close_obj();
    }

    void visit(const Shuffle *e) override {
        open_obj("Shuffle");
        stream << get_indent() << "vectors: ";
        print_vector(e->vectors);
        stream << "indices : [";
        for (size_t i = 0; i < e->vectors.size() - 1; i++) {
            stream << e->indices[i] << ", ";
        }
        stream << e->indices.back() << "]\n";
        close_obj();
    }

    void visit(const VectorReduce *e) override {
        open_obj("VectorReduce");
        stream << get_indent() << "value : ";
        e->value.accept(this);
        stream << "op : ";
        stream << quoted_str(to_string(e->op));
        close_obj();
    }

    void visit(const Prefetch *s) override {
        open_obj("Prefetch");
        stream << get_indent() << "name : "
               << quoted_str(s->name) << ",\n";
        stream << get_indent() << "types : ";
        print_vector(s->types);
        stream << get_indent() << "bounds : ";
        print_vector(s->bounds);
        stream << ",\n";
        stream << get_indent() << "prefetch : "
               << to_string(s->prefetch);
        stream << get_indent() << "condition : ";
        s->condition.accept(this);
        stream << get_indent() << "body : ";
        s->body.accept(this);
        close_obj();

    }

    void visit(const Fork *s) override {
        open_obj("Fork");
        stream << get_indent() << "first : ";
        s->first.accept(this);
        stream << get_indent() << "rest : ";
        s->rest.accept(this);
        close_obj();
    }

    void visit(const Acquire *s) override {
        open_obj("Acquire");
        stream << get_indent() << "sempahore : ";
        s->semaphore.accept(this);
        stream << get_indent() << "count : ";
        s->count.accept(this);
        stream << get_indent() << "body : ";
        s->body.accept(this);
        close_obj();
    }
    void visit(const Atomic *s) override {
        open_obj("Atomic");
        stream << get_indent() << "producer_name : "
               << quoted_str(s->producer_name) << ",\n";
        stream << get_indent() << "mutex_name : "
               << quoted_str(s->mutex_name) << ",\n";
        stream << get_indent() << "body : ";
        s->body.accept(this);
        close_obj();
    }
};



} // namespace Internal
} // namespace Halide
