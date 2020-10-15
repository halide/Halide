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
std::string to_string(bool value, int base_indent) {
    return value ? "true" : "false";
}



template<>
std::string to_string(Halide::Range r, int base_indent) {
    std::stringstream os;
    os << "{ min: " << r.min << ", extent: " << r.extent << "}";
    return os.str();
}

template<>
std::string to_string(Halide::Internal::Call::CallType c, int base_indent) {
    using CallType = Halide::Internal::Call::CallType;
    std::map<CallType, std::string> ct_map =
        {{CallType::Image, "Image"},
          {CallType::Extern, "Extern"},
          {CallType::ExternCPlusPlus, "ExternCPlusPlus"},
          {CallType::PureExtern, "PureExtern"},
          {CallType::Halide, "Halide"},
          {CallType::Intrinsic, "Intrinsic"},
          {CallType::PureIntrinsic, "PureIntrinsic"}
        };
    return ct_map[c];
}


} // anonymous namespace


using std::string;
using std::vector;

struct StmtToJson : public IRVisitor {
    int indent;
    std::ofstream stream;

    StmtToJson(const std::string &filename) : 
        indent(0) {
        stream.open(filename.c_str());
    }
    ~StmtToJson() override {
        stream.close();
    }

    string get_indent() {
        return ::Halide::Internal::get_indent(indent);
    }

    inline void increase_indent() {
        indent++;
    }

    inline void decrease_indent() {
        indent--;
    }

    inline string quoted_str(const string &str) {
        std::stringstream s;
        s << "\"" << str << "\"";
        return s.str();
    }

    inline void print_key(const string &str) {
        stream << get_indent() << quoted_str(str) << " : ";
    }

    inline void open_obj(string node_type) {
        // Indent is already done, so no need to get_indent()
        // for the first line
        stream << "{\n";
        increase_indent();
        print_key("_node_type");
        stream << quoted_str(node_type) << ",\n";
    }

    inline void close_obj() {
        decrease_indent();
        stream << get_indent() << "}\n";
    }

    inline void print_type(Type t) {
        print_key("type");
        stream << quoted_str(to_string(t)) << ",\n";
    }

    void print(const LoweredFunc &f) {
        std::map<LinkageType, std::string> linkage_map =
            {{LinkageType::External, "External"},
             {LinkageType::ExternalPlusMetadata, "ExternalPlusMetadata"},
             {LinkageType::Internal, "Internal"}};
        open_obj("LoweredFunc");
        print_key("name");
        stream << quoted_str(f.name) << ",\n";
        print_key("name_mangling");
        stream << quoted_str(to_string(f.name_mangling)) << ",\n";
        print_key("linkage");
        stream << quoted_str(linkage_map[f.linkage]) << ",\n";
        print_key("args");
        print_vector(f.args);
        print_key("body");
        print(f.body);
        close_obj();
    }

    void print(const Module &m) {
        open_obj("Module");
        //TODO: all other parts
        print_key("name");
        stream << quoted_str(m.name()) << ",\n";
        print_key("target");
        // for some reason, this outputs "target(<actual target>)"
        auto target_str = to_string(m.target());
        target_str = target_str.substr(7, target_str.size()-8);
        stream << quoted_str(target_str) << ",\n";
        print_key("functions");
        stream << "[\n" << get_indent();
        increase_indent();
        for (auto &f: m.functions()) {
            print(f);
        }
        decrease_indent();
        stream << get_indent() << "]\n";
        close_obj();

    }
    void print(const Parameter &p) {
        open_obj("Parameter");
        print_key("type");
        stream << quoted_str(to_string(p.type())) << "\n";
        print_key("is_buffer");
        stream << to_string(p.is_buffer()) << "\n";
        print_key("dimensions");
        stream << to_string(p.dimensions()) << "\n";
        print_key("name");
        stream << quoted_str(to_string(p.name())) << "\n";
        close_obj();

    }

    void print(const ModulusRemainder &m) {
        open_obj("ModulusRemainder");
        print_key("modulus");
        stream << m.modulus << ",\n";
        print_key("remainder");
        stream << m.remainder;
        close_obj();
    }

    void print(const PrefetchDirective &p) {
        std::map<Halide::PrefetchBoundStrategy, std::string> strategy_str =
            {{Halide::PrefetchBoundStrategy::Clamp, "Clamp"},
             {Halide::PrefetchBoundStrategy::GuardWithIf, "GuardWithIf"},
             {Halide::PrefetchBoundStrategy::NonFaulting, "NonFaulting"}
            };
        open_obj("PrefetchDirective");
        print_key("name");
        stream << quoted_str(p.name) << ",\n";
        print_key("var");
        stream << quoted_str(p.var) << ",\n";
        print_key("offset");
        p.offset.accept(this);
        stream << ",\n";
        print_key("strategy");
        stream << strategy_str[p.strategy] << ",\n";
        print_key("param");
        print(p.param);
        stream << "\n";
        close_obj();
    }

    void print(const ForType &ft) {
        std::map<ForType, std::string> ft_str =
            {{ForType::Serial, "Serial"},
             {ForType::Parallel, "Parallel"},
             {ForType::Vectorized, "Vectorized"},
             {ForType::Unrolled, "Unrolled"},
             {ForType::Extern, "Extern"},
             {ForType::GPUBlock, "GPUBlock"},
             {ForType::GPUThread, "GPUThread"},
             {ForType::GPULane, "GPULane"}};
        stream << quoted_str(ft_str[ft]);
    }

    template<typename T>
    void print_immediate(string node_type, Type t, T v) {
        open_obj(node_type);
        print_type(t);
        print_key("value");
        stream << to_string(v->value) << "\n";
        close_obj();
    }

    template<>
    void print_immediate(string node_type, Type t, const StringImm* v) {
        open_obj(node_type);
        print_type(t);
        print_key("value");
        stream << quoted_str(v->value) << "\n";
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
        print_key("value");
        //e->value.accept(this); 
        print(e->value);
        close_obj();
    }

    void visit(const Variable *e) override {
        open_obj("Variable");
        print_type(e->type);
        print_key("name");
        stream << quoted_str(e->name) << ",\n";
        if (e->param.defined()) {
            print_key("param");
            print(e->param);
            stream << ",\n";
        }
        // TODO: buffer
        internal_assert(!e->image.defined());
        //stream << get_indent() << "image : "
        //       << "TODO" << ",\n";
        // TODO: reduction_domain
        //stream << get_indent() << "reduction_domain : "
        //       << "TODO" << "\n";
        internal_assert(!e->reduction_domain.defined());
        close_obj();
    }

    template<typename T>
    inline void print_binop(string node_type, T e) {
        open_obj(node_type);
        print_type(e->type);
        print_key("a");
        e->a.accept(this);
        print_key("b");
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
        print_key("a");
        e->a.accept(this);
        close_obj();
    }

    void visit(const Select *e) override {
        open_obj("Select");
        print_type(e->type);
        print_key("condition");
        e->condition.accept(this);
        print_key("true_value");
        e->true_value.accept(this);
        print_key("false_value");
        e->false_value.accept(this);
        close_obj();
    }

    void visit(const Load *e) override {
        open_obj("Load");
        print_type(e->type);
        print_key("name");
        stream << quoted_str(e->name) << "\n";
        print_key("index");
        e->index.accept(this);
        print_key("image");
        // TODO: implement
        stream << "\"UNIMPLEMENTED\",\n";
        //stream << to_string(e->image) << ",\n";

        print_key("param");
        print(e->param);
        stream << ",\n";

        print_key("alignment");
        print(e->alignment);
        close_obj();
    }

    void visit(const Ramp *e) override {
        open_obj("Ramp");
        print_type(e->type);
        print_key("base");
        e->base.accept(this);
        print_key("stride");
        e->stride.accept(this);
        print_key("lanes");
        stream << e->lanes << "\n";
        close_obj();
    }

    void visit(const Broadcast *e) override {
        open_obj("Broadcast");
        print_type(e->type);
        print_key("value");
        e->value.accept(this);
        print_key("lanes");
        stream << e->lanes << "\n";
        close_obj();
    }

    void visit(const Call *e) override {
        open_obj("Call");
        print_type(e->type);
        print_key("name");
        stream << quoted_str(e->name) << ",\n";
        print_key("args");
        print_vector(e->args);
        stream << get_indent() << ",\n";
        print_key("call_type");
        stream << quoted_str(to_string(e->call_type)) << ",\n";
        // We assume that a call to another func or a call to an image
        // has already been lowered.
        internal_assert(!e->func.defined()) << "Call to a func should not exist at backend\n";
        internal_assert(!e->image.defined()) << "Call to an image should not exist at backend\n";
        if (e->param.defined()) {
            print_key("param");
            print(e->param);
            stream << "\n";
        }
        close_obj();
    }

    void visit(const Let *e) override {
        open_obj("Let");
        print_type(e->type);
        print_key("name");
        stream << quoted_str(e->name) << ",\n";
        print_key("value");
        e->value.accept(this);
        print_key("body");
        e->body.accept(this);
        close_obj();
    }

    void visit(const LetStmt *s) override {
        open_obj("LetStmt");
        print_key("name");
        stream << quoted_str(s->name) << ",\n";
        print_key("value");
        s->value.accept(this);
        print_key("body");
        s->body.accept(this);
        close_obj();
    }

    void visit(const AssertStmt *s) override {
        open_obj("AssertStmt");
        print_key("condition");
        s->condition.accept(this);
        stream << get_indent() << ", ";
        print_key("message");
        s->message.accept(this);
        close_obj();
    }

    void visit(const ProducerConsumer *s) override {
        open_obj("ProducerConsumer");
        print_key("name");
        stream << quoted_str(s->name) << ",\n";
        print_key("is_producer");
        stream << to_string(s->is_producer) << ",\n";
        print_key("body");
        s->body.accept(this);
        close_obj();
    }

    void visit(const For *s) override {
        open_obj("For");
        print_key("name");
        stream << quoted_str(s->name) << "\n,";
        print_key("min");
        s->min.accept(this);
        print_key("extent");
        s->extent.accept(this);
        print_key("for_type");
        print(s->for_type);
        stream << ",\n";
        print_key("device_api");
        stream << quoted_str(to_string(s->device_api)) << ",\n";
        print_key("body");
        s->body.accept(this);
        close_obj();
    }

    void visit(const Store *s) override {
        open_obj("Store");
        print_key("name");
        stream << quoted_str(s->name) << ",\n";
        print_key("predicate");
        s->predicate.accept(this);
        print_key("value");
        s->value.accept(this);
        print_key("index");
        s->index.accept(this);
        print_key("param");
        print(s->param);
        stream << ",\n";
        print_key("alignment");
        print(s->alignment);
        close_obj();
    }
    void visit(const Provide *) override {
        internal_error << "Should not see Provide in backend\n";
    }

    void print(const Region &r) {
        // A region is a vector of Ranges
        stream << "[\n";
        increase_indent();
        for (auto &g : r) {
            stream << get_indent();
            open_obj("Range");
            print_key("min");
            print(g.min);
            stream << ",\n";
            print_key("extent");
            print(g.extent);
            stream << ",\n";
            close_obj();
            stream << ",\n";
        }
        decrease_indent();
        stream << "]";
    }

    void print(const ArgumentEstimates &e) {
        open_obj("ArgumentEstimates");
        print_key("scalar_def");
        print(e.scalar_def);
        stream << ",\n";
        print_key("scalar_min");
        print(e.scalar_min);
        stream << ",\n";
        print_key("scalar_max");
        print(e.scalar_max);
        stream << ",\n";
        print_key("scalar_estimate");
        print(e.scalar_estimate);
        stream << ",\n";
        print_key("buffer_estimates");
        print(e.buffer_estimates);
        close_obj();
    }

    void print(const LoweredArgument &a) {
        std::map<Argument::Kind, std::string> kind_str =
            {{Argument::Kind::InputScalar, "InputScalar"},
             {Argument::Kind::InputBuffer, "InputBuffer"},
             {Argument::Kind::OutputBuffer, "OutputBuffer"}};
        open_obj("LoweredArgument");
        print_key("name");
        stream << quoted_str(a.name) << ",\n";
        print_key("kind");
        stream << quoted_str(kind_str[a.kind]) << ",\n";
        print_key("dimensions");
        stream << (uint64_t)a.dimensions << ",\n";
        print_type(a.type);
        print_key("argument_estimates");
        print(a.argument_estimates);
        stream << ",\n";
        print_key("alignment");
        print(a.alignment);
        close_obj();
    }

    template<typename T>
    inline void print_vector(const vector<T> &v) {
        stream << "[\n";
        increase_indent();
        for (auto &e: v) {
            stream << to_string(e) << ",\n";
        }
        decrease_indent();
        stream << get_indent() << "]";
    }


    template<>
    inline void print_vector(const vector<Expr> &v) {
        stream << "[\n";
        increase_indent();
        for (auto &e: v) {
            stream << get_indent();
            e->accept(this);
            stream << get_indent() << ",\n";
        }
        decrease_indent();
        stream << get_indent() << "]\n";
    }

    template<>
    inline void print_vector(const vector<LoweredArgument> &v) {
        stream << "[\n";
        increase_indent();
        for (auto &e: v) {
            stream << get_indent();
            print(e);
            stream << get_indent() << ",\n";
        }
        decrease_indent();
        stream << get_indent() << "]\n";
    }

    void visit(const Allocate *s) override {
        open_obj("Allocate");
        print_key("name");
        stream << quoted_str(s->name) << ",\n";
        print_type(s->type);
        print_key("memory_type");
        stream  << quoted_str(to_string(s->memory_type))
               << ",\n";
        print_key("extent");
        print_vector(s->extents);
        print_key("condition");
        s->condition.accept(this);
        print_key("new_expr");
        if (s->new_expr.defined()) {
            s->new_expr.accept(this);
        } else {
            stream << "{ }";
        }
        print_key("free_function");
        stream << quoted_str(s->free_function)
               << ",\n";
        print_key("body");
        s->body.accept(this);
        close_obj();
    }

    void visit(const Free *s) override {
        open_obj("Free");
        print_key("name");
        stream << quoted_str(s->name) << "\n";
        close_obj();
    }

    void visit(const Realize *) override {
        internal_error << "Should not see Realize in backend\n";
    }

    void visit(const Block *s) override {
        open_obj("Block");
        print_key("first");
        s->first.accept(this);
        print_key("rest");
        if (s->rest.defined()) {
            s->rest.accept(this);
        } else {
            stream << "{ }\n";
        }
        close_obj();
    }

    void visit(const IfThenElse *s) override {
        open_obj("IfThenElse");
        print_key("condition");
        s->condition.accept(this);
        print_key("then_case");
        s->then_case.accept(this);
        print_key("else_case");
        if (s->else_case.defined()) {
            s->else_case.accept(this);
        } else {
            open_obj("Stmt");
            close_obj();
        }
        close_obj();
    }

    void visit(const Evaluate *s) override {
        open_obj("Evaluate");
        print_key("value");
        s->value.accept(this);
        close_obj();
    }

    void visit(const Shuffle *e) override {
        open_obj("Shuffle");
        print_key("vectors");
        print_vector(e->vectors);
        print_key("indices");
        stream << "[";
        for (size_t i = 0; i < e->vectors.size() - 1; i++) {
            stream << e->indices[i] << ", ";
        }
        stream << e->indices.back() << "]\n";
        close_obj();
    }

    void visit(const VectorReduce *e) override {
        open_obj("VectorReduce");
        print_key("value");
        e->value.accept(this);
        stream << "op : ";
        stream << quoted_str(to_string(e->op));
        close_obj();
    }

    void visit(const Prefetch *s) override {
        open_obj("Prefetch");
        print_key("name");
        stream << quoted_str(s->name) << ",\n";
        print_key("types");
        print_vector(s->types);
        print_key("bounds");
        print_vector(s->bounds);
        stream << ",\n";
        print_key("prefetch");
        print(s->prefetch);
        stream << ",\n";
        print_key("condition");
        s->condition.accept(this);
        print_key("body");
        s->body.accept(this);
        close_obj();

    }

    void visit(const Fork *s) override {
        open_obj("Fork");
        print_key("first");
        s->first.accept(this);
        print_key("rest");
        s->rest.accept(this);
        close_obj();
    }

    void visit(const Acquire *s) override {
        open_obj("Acquire");
        print_key("semaphore");
        s->semaphore.accept(this);
        print_key("count");
        s->count.accept(this);
        print_key("body");
        s->body.accept(this);
        close_obj();
    }

    void visit(const Atomic *s) override {
        open_obj("Atomic");
        print_key("producer_name");
        stream << quoted_str(s->producer_name) << ",\n";
        print_key("mutex_name");
        stream << quoted_str(s->mutex_name) << ",\n";
        print_key("body");
        s->body.accept(this);
        close_obj();
    }

    void print(const Expr e) {
        if (e.defined()) {
            e.accept(this);
        } else {
            // equivalent to Expr()
            open_obj("Expr");
            close_obj();
        }
    }
    void print(const Stmt e) {
        if (e.defined()) {
            e.accept(this);
        } else {
            // equivalent to Stmt()
            open_obj("Stmt");
            close_obj();
        }
    }


};

void print_to_json(const std::string &filename, const Module &m) {
    StmtToJson stj(filename);
    stj.print(m);
}


} // namespace Internal
} // namespace Halide
