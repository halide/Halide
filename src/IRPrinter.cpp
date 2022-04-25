#include <iostream>
#include <sstream>

#include "IRPrinter.h"

#include "AssociativeOpsTable.h"
#include "Associativity.h"
#include "Closure.h"
#include "IROperator.h"
#include "Module.h"
#include "Target.h"
#include "Util.h"

namespace Halide {

using std::ostream;
using std::ostringstream;
using std::string;
using std::vector;

ostream &operator<<(ostream &out, const Type &type) {
    switch (type.code()) {
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
        if (type.handle_type) {
            out << "(" << type.handle_type->inner_name.name << " *)";
        } else {
            out << "(void *)";
        }
        break;
    case Type::BFloat:
        out << "bfloat";
        break;
    }
    if (!type.is_handle()) {
        out << type.bits();
    }
    if (type.lanes() > 1) {
        out << "x" << type.lanes();
    }
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

ostream &operator<<(ostream &stream, const Buffer<> &buffer) {
    bool include_data = Internal::ends_with(buffer.name(), "_gpu_source_kernels");
    stream << "buffer " << buffer.name() << " = {";
    if (include_data) {
        std::string str((const char *)buffer.data(), buffer.size_in_bytes());
        stream << "\n"
               << str << "\n";
    } else {
        stream << "...";
    }
    stream << "}\n";
    return stream;
}

ostream &operator<<(ostream &stream, const Module &m) {
    for (const auto &s : m.submodules()) {
        stream << s << "\n";
    }

    stream << "module name=" << m.name() << ", target=" << m.target().to_string() << "\n";
    for (const auto &b : m.buffers()) {
        stream << b << "\n";
    }
    for (const auto &f : m.functions()) {
        stream << f << "\n";
    }
    return stream;
}

ostream &operator<<(ostream &out, const DeviceAPI &api) {
    switch (api) {
    case DeviceAPI::Host:
    case DeviceAPI::None:
        break;
    case DeviceAPI::Default_GPU:
        out << "<Default_GPU>";
        break;
    case DeviceAPI::CUDA:
        out << "<CUDA>";
        break;
    case DeviceAPI::OpenCL:
        out << "<OpenCL>";
        break;
    case DeviceAPI::OpenGLCompute:
        out << "<OpenGLCompute>";
        break;
    case DeviceAPI::Metal:
        out << "<Metal>";
        break;
    case DeviceAPI::Hexagon:
        out << "<Hexagon>";
        break;
    case DeviceAPI::HexagonDma:
        out << "<HexagonDma>";
        break;
    case DeviceAPI::D3D12Compute:
        out << "<D3D12Compute>";
        break;
    }
    return out;
}

std::ostream &operator<<(std::ostream &out, const MemoryType &t) {
    switch (t) {
    case MemoryType::Auto:
        out << "Auto";
        break;
    case MemoryType::Heap:
        out << "Heap";
        break;
    case MemoryType::Stack:
        out << "Stack";
        break;
    case MemoryType::Register:
        out << "Register";
        break;
    case MemoryType::GPUShared:
        out << "GPUShared";
        break;
    case MemoryType::GPUTexture:
        out << "GPUTexture";
        break;
    case MemoryType::LockedCache:
        out << "LockedCache";
        break;
    case MemoryType::VTCM:
        out << "VTCM";
        break;
    case MemoryType::AMXTile:
        out << "AMXTile";
        break;
    }
    return out;
}

std::ostream &operator<<(std::ostream &out, const TailStrategy &t) {
    switch (t) {
    case TailStrategy::Auto:
        out << "Auto";
        break;
    case TailStrategy::GuardWithIf:
        out << "GuardWithIf";
        break;
    case TailStrategy::Predicate:
        out << "Predicate";
        break;
    case TailStrategy::PredicateLoads:
        out << "PredicateLoads";
        break;
    case TailStrategy::PredicateStores:
        out << "PredicateStores";
        break;
    case TailStrategy::ShiftInwards:
        out << "ShiftInwards";
        break;
    case TailStrategy::RoundUp:
        out << "RoundUp";
        break;
    }
    return out;
}

ostream &operator<<(ostream &stream, const LoopLevel &loop_level) {
    return stream << "loop_level("
                  << (loop_level.defined() ? loop_level.to_string() : "undefined")
                  << ")";
}

ostream &operator<<(ostream &stream, const Target &target) {
    return stream << "target(" << target.to_string() << ")";
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

    Stmt store = Store::make("buf", (x * 17) / (x - 3), y - 1, Parameter(), const_true(), ModulusRemainder());
    Stmt for_loop = For::make("x", -2, y + 2, ForType::Parallel, DeviceAPI::Host, store);
    vector<Expr> args(1);
    args[0] = x % 3;
    Expr call = Call::make(i32, "buf", args, Call::Extern);
    Stmt store2 = Store::make("out", call + 1, x, Parameter(), const_true(), ModulusRemainder(3, 5));
    Stmt for_loop2 = For::make("x", 0, y, ForType::Vectorized, DeviceAPI::Host, store2);

    Stmt producer = ProducerConsumer::make_produce("buf", for_loop);
    Stmt consumer = ProducerConsumer::make_consume("buf", for_loop2);
    Stmt pipeline = Block::make(producer, consumer);

    Stmt assertion = AssertStmt::make(y >= 3, Call::make(Int(32), "halide_error_param_too_small_i64",
                                                         {string("y"), y, 3}, Call::Extern));
    Stmt block = Block::make(assertion, pipeline);
    Stmt let_stmt = LetStmt::make("y", 17, block);
    Stmt allocate = Allocate::make("buf", f32, MemoryType::Stack, {1023}, const_true(), let_stmt);

    ostringstream source;
    source << allocate;
    std::string correct_source =
        "allocate buf[float32 * 1023] in Stack\n"
        "let y = 17\n"
        "assert(y >= 3, halide_error_param_too_small_i64(\"y\", y, 3))\n"
        "produce buf {\n"
        " parallel (x, -2, y + 2) {\n"
        "  buf[y - 1] = (x*17)/(x - 3)\n"
        " }\n"
        "}\n"
        "consume buf {\n"
        " vectorized (x, 0, y) {\n"
        "  out[x] = buf(x % 3) + 1\n"
        " }\n"
        "}\n";

    if (source.str() != correct_source) {
        internal_error << "Correct output:\n"
                       << correct_source
                       << "Actual output:\n"
                       << source.str();
    }
    std::cout << "IRPrinter test passed\n";
}

ostream &operator<<(ostream &stream, const AssociativePattern &p) {
    stream << "{\n";
    for (size_t i = 0; i < p.ops.size(); ++i) {
        stream << "  op_" << i << " -> " << p.ops[i] << ", id_" << i << " -> " << p.identities[i] << "\n";
    }
    stream << "  is commutative? " << p.is_commutative << "\n";
    stream << "}\n";
    return stream;
}

ostream &operator<<(ostream &stream, const AssociativeOp &op) {
    stream << "Pattern:\n"
           << op.pattern;
    stream << "is associative? " << op.is_associative << "\n";
    for (size_t i = 0; i < op.xs.size(); ++i) {
        stream << "  " << op.xs[i].var << " -> " << op.xs[i].expr << "\n";
        stream << "  " << op.ys[i].var << " -> " << op.ys[i].expr << "\n";
    }
    stream << "\n";
    return stream;
}

ostream &operator<<(ostream &out, const ForType &type) {
    switch (type) {
    case ForType::Serial:
        out << "for";
        break;
    case ForType::Parallel:
        out << "parallel";
        break;
    case ForType::Unrolled:
        out << "unrolled";
        break;
    case ForType::Vectorized:
        out << "vectorized";
        break;
    case ForType::Extern:
        out << "extern";
        break;
    case ForType::GPUBlock:
        out << "gpu_block";
        break;
    case ForType::GPUThread:
        out << "gpu_thread";
        break;
    case ForType::GPULane:
        out << "gpu_lane";
        break;
    }
    return out;
}

ostream &operator<<(ostream &out, const VectorReduce::Operator &op) {
    switch (op) {
    case VectorReduce::Add:
        out << "Add";
        break;
    case VectorReduce::SaturatingAdd:
        out << "SaturatingAdd";
        break;
    case VectorReduce::Mul:
        out << "Mul";
        break;
    case VectorReduce::Min:
        out << "Min";
        break;
    case VectorReduce::Max:
        out << "Max";
        break;
    case VectorReduce::And:
        out << "And";
        break;
    case VectorReduce::Or:
        out << "Or";
        break;
    }
    return out;
}

ostream &operator<<(ostream &out, const NameMangling &m) {
    switch (m) {
    case NameMangling::Default:
        out << "default";
        break;
    case NameMangling::C:
        out << "c";
        break;
    case NameMangling::CPlusPlus:
        out << "c++";
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

ostream &operator<<(ostream &stream, const LoweredFunc &function) {
    stream << function.linkage << " func " << function.name << " (";
    for (size_t i = 0; i < function.args.size(); i++) {
        stream << function.args[i].name;
        if (i + 1 < function.args.size()) {
            stream << ", ";
        }
    }
    stream << ") {\n";
    stream << function.body;
    stream << "}\n\n";
    return stream;
}

std::ostream &operator<<(std::ostream &stream, const LinkageType &type) {
    switch (type) {
    case LinkageType::ExternalPlusArgv:
        stream << "external_plus_argv";
        break;
    case LinkageType::ExternalPlusMetadata:
        stream << "external_plus_metadata";
        break;
    case LinkageType::External:
        stream << "external";
        break;
    case LinkageType::Internal:
        stream << "internal";
        break;
    }
    return stream;
}

std::ostream &operator<<(std::ostream &stream, const Indentation &indentation) {
    for (int i = 0; i < indentation.indent; i++) {
        stream << " ";
    }
    return stream;
}

std::ostream &operator<<(std::ostream &out, const DimType &t) {
    switch (t) {
    case DimType::PureVar:
        out << "PureVar";
        break;
    case DimType::PureRVar:
        out << "PureRVar";
        break;
    case DimType::ImpureRVar:
        out << "ImpureRVar";
        break;
    }
    return out;
}

std::ostream &operator<<(std::ostream &out, const Closure &c) {
    for (const auto &v : c.vars) {
        out << "var: " << v.first << "\n";
    }
    for (const auto &b : c.buffers) {
        out << "buffer: " << b.first << " " << b.second.size;
        if (b.second.read) {
            out << " (read)";
        }
        if (b.second.write) {
            out << " (write)";
        }
        if (b.second.memory_type == MemoryType::GPUTexture) {
            out << " <texture>";
        }
        out << " dims=" << (int)b.second.dimensions;
        out << "\n";
    }
    return out;
}

IRPrinter::IRPrinter(ostream &s)
    : stream(s) {
    s.setf(std::ios::fixed, std::ios::floatfield);
}

void IRPrinter::print(const Expr &ir) {
    ScopedValue<bool> old(implicit_parens, false);
    ir.accept(this);
}

void IRPrinter::print_no_parens(const Expr &ir) {
    ScopedValue<bool> old(implicit_parens, true);
    ir.accept(this);
}

void IRPrinter::print(const Stmt &ir) {
    ir.accept(this);
}

void IRPrinter::print_list(const std::vector<Expr> &exprs) {
    for (size_t i = 0; i < exprs.size(); i++) {
        print_no_parens(exprs[i]);
        if (i < exprs.size() - 1) {
            stream << ", ";
        }
    }
}

void IRPrinter::visit(const IntImm *op) {
    if (op->type == Int(32)) {
        stream << op->value;
    } else {
        stream << "(" << op->type << ")" << op->value;
    }
}

void IRPrinter::visit(const UIntImm *op) {
    stream << "(" << op->type << ")" << op->value;
}

void IRPrinter::visit(const FloatImm *op) {
    switch (op->type.bits()) {
    case 64:
        stream << op->value;
        break;
    case 32:
        stream << op->value << "f";
        break;
    case 16:
        stream << op->value << "h";
        break;
    default:
        internal_error << "Bad bit-width for float: " << op->type << "\n";
    }
}

void IRPrinter::visit(const StringImm *op) {
    stream << "\"";
    for (unsigned char c : op->value) {
        if (c >= ' ' && c <= '~' && c != '\\' && c != '"') {
            stream << c;
        } else {
            stream << "\\";
            switch (c) {
            case '"':
                stream << "\"";
                break;
            case '\\':
                stream << "\\";
                break;
            case '\t':
                stream << "t";
                break;
            case '\r':
                stream << "r";
                break;
            case '\n':
                stream << "n";
                break;
            default:
                string hex_digits = "0123456789ABCDEF";
                stream << "x" << hex_digits[c >> 4] << hex_digits[c & 0xf];
            }
        }
    }
    stream << "\"";
}

void IRPrinter::visit(const Cast *op) {
    stream << op->type << "(";
    print(op->value);
    stream << ")";
}

void IRPrinter::visit(const Variable *op) {
    if (!known_type.contains(op->name) &&
        (op->type != Int(32))) {
        // Handle types already have parens
        if (op->type.is_handle()) {
            stream << op->type;
        } else {
            stream << "(" << op->type << ")";
        }
    }
    stream << op->name;
}

void IRPrinter::open() {
    if (!implicit_parens) {
        stream << "(";
    }
}

void IRPrinter::close() {
    if (!implicit_parens) {
        stream << ")";
    }
}

void IRPrinter::visit(const Add *op) {
    open();
    print(op->a);
    stream << " + ";
    print(op->b);
    close();
}

void IRPrinter::visit(const Sub *op) {
    open();
    print(op->a);
    stream << " - ";
    print(op->b);
    close();
}

void IRPrinter::visit(const Mul *op) {
    open();
    print(op->a);
    stream << "*";
    print(op->b);
    close();
}

void IRPrinter::visit(const Div *op) {
    open();
    print(op->a);
    stream << "/";
    print(op->b);
    close();
}

void IRPrinter::visit(const Mod *op) {
    open();
    print(op->a);
    stream << " % ";
    print(op->b);
    close();
}

void IRPrinter::visit(const Min *op) {
    stream << "min(";
    print_no_parens(op->a);
    stream << ", ";
    print_no_parens(op->b);
    stream << ")";
}

void IRPrinter::visit(const Max *op) {
    stream << "max(";
    print_no_parens(op->a);
    stream << ", ";
    print_no_parens(op->b);
    stream << ")";
}

void IRPrinter::visit(const EQ *op) {
    open();
    print(op->a);
    stream << " == ";
    print(op->b);
    close();
}

void IRPrinter::visit(const NE *op) {
    open();
    print(op->a);
    stream << " != ";
    print(op->b);
    close();
}

void IRPrinter::visit(const LT *op) {
    open();
    print(op->a);
    stream << " < ";
    print(op->b);
    close();
}

void IRPrinter::visit(const LE *op) {
    open();
    print(op->a);
    stream << " <= ";
    print(op->b);
    close();
}

void IRPrinter::visit(const GT *op) {
    open();
    print(op->a);
    stream << " > ";
    print(op->b);
    close();
}

void IRPrinter::visit(const GE *op) {
    open();
    print(op->a);
    stream << " >= ";
    print(op->b);
    close();
}

void IRPrinter::visit(const And *op) {
    open();
    print(op->a);
    stream << " && ";
    print(op->b);
    close();
}

void IRPrinter::visit(const Or *op) {
    open();
    print(op->a);
    stream << " || ";
    print(op->b);
    close();
}

void IRPrinter::visit(const Not *op) {
    stream << "!";
    print(op->a);
}

void IRPrinter::visit(const Select *op) {
    stream << "select(";
    print_no_parens(op->condition);
    stream << ", ";
    print_no_parens(op->true_value);
    stream << ", ";
    print_no_parens(op->false_value);
    stream << ")";
}

void IRPrinter::visit(const Load *op) {
    const bool has_pred = !is_const_one(op->predicate);
    const bool show_alignment = op->type.is_vector() && op->alignment.modulus > 1;
    if (has_pred) {
        open();
    }
    if (!known_type.contains(op->name)) {
        stream << "(" << op->type << ")";
    }
    stream << op->name << "[";
    print_no_parens(op->index);
    if (show_alignment) {
        stream << " aligned(" << op->alignment.modulus << ", " << op->alignment.remainder << ")";
    }
    stream << "]";
    if (has_pred) {
        stream << " if ";
        print(op->predicate);
        close();
    }
}

void IRPrinter::visit(const Ramp *op) {
    stream << "ramp(";
    print_no_parens(op->base);
    stream << ", ";
    print_no_parens(op->stride);
    stream << ", " << op->lanes << ")";
}

void IRPrinter::visit(const Broadcast *op) {
    stream << "x" << op->lanes << "(";
    print_no_parens(op->value);
    stream << ")";
}

void IRPrinter::visit(const Call *op) {
    // TODO: Print indication of C vs C++?
    if (!known_type.contains(op->name) &&
        (op->type != Int(32))) {
        if (op->type.is_handle()) {
            stream << op->type;  // Already has parens
        } else {
            stream << "(" << op->type << ")";
        }
    }
    stream << op->name << "(";
    print_list(op->args);
    stream << ")";
}

void IRPrinter::visit(const Let *op) {
    ScopedBinding<> bind(known_type, op->name);
    open();
    stream << "let " << op->name << " = ";
    print(op->value);
    stream << " in ";
    print(op->body);
    close();
}

void IRPrinter::visit(const LetStmt *op) {
    ScopedBinding<> bind(known_type, op->name);
    stream << get_indent() << "let " << op->name << " = ";
    print_no_parens(op->value);
    stream << "\n";

    print(op->body);
}

void IRPrinter::visit(const AssertStmt *op) {
    stream << get_indent() << "assert(";
    print_no_parens(op->condition);
    stream << ", ";
    print_no_parens(op->message);
    stream << ")\n";
}

void IRPrinter::visit(const ProducerConsumer *op) {
    stream << get_indent();
    if (op->is_producer) {
        stream << "produce " << op->name << " {\n";
    } else {
        stream << "consume " << op->name << " {\n";
    }
    indent++;
    print(op->body);
    indent--;
    stream << get_indent() << "}\n";
}

void IRPrinter::visit(const For *op) {
    ScopedBinding<> bind(known_type, op->name);
    stream << get_indent() << op->for_type << op->device_api << " (" << op->name << ", ";
    print_no_parens(op->min);
    stream << ", ";
    print_no_parens(op->extent);
    stream << ") {\n";

    indent++;
    print(op->body);
    indent--;

    stream << get_indent() << "}\n";
}

void IRPrinter::visit(const Acquire *op) {
    stream << get_indent() << "acquire (";
    print_no_parens(op->semaphore);
    stream << ", ";
    print_no_parens(op->count);
    stream << ") {\n";
    indent++;
    print(op->body);
    indent--;
    stream << get_indent() << "}\n";
}

void IRPrinter::print_lets(const Let *let) {
    stream << get_indent();
    ScopedBinding<> bind(known_type, let->name);
    stream << "let " << let->name << " = ";
    print_no_parens(let->value);
    stream << " in\n";
    if (const Let *next = let->body.as<Let>()) {
        print_lets(next);
    } else {
        stream << get_indent();
        print_no_parens(let->body);
        stream << "\n";
    }
}

void IRPrinter::visit(const Store *op) {
    stream << get_indent();
    const bool has_pred = !is_const_one(op->predicate);
    const bool show_alignment = op->value.type().is_vector() && (op->alignment.modulus > 1);
    if (has_pred) {
        stream << "predicate (";
        print_no_parens(op->predicate);
        stream << ")\n";
        indent++;
        stream << get_indent();
    }
    stream << op->name << "[";
    print_no_parens(op->index);
    if (show_alignment) {
        stream << " aligned("
               << op->alignment.modulus
               << ", "
               << op->alignment.remainder << ")";
    }
    stream << "] = ";
    if (const Let *let = op->value.as<Let>()) {
        // Use some nicer line breaks for containing Lets
        stream << "\n";
        indent += 2;
        print_lets(let);
        indent -= 2;
    } else {
        // Just print the value in-line
        print_no_parens(op->value);
    }
    stream << "\n";
    if (has_pred) {
        indent--;
    }
}

void IRPrinter::visit(const Provide *op) {
    stream << get_indent();
    const bool has_pred = !is_const_one(op->predicate);
    if (has_pred) {
        stream << "predicate (";
        print_no_parens(op->predicate);
        stream << ")\n";
        indent++;
        stream << get_indent();
    }
    stream << op->name << "(";
    print_list(op->args);
    stream << ") = ";
    if (op->values.size() > 1) {
        stream << "{";
    }
    print_list(op->values);
    if (op->values.size() > 1) {
        stream << "}";
    }

    stream << "\n";
    if (has_pred) {
        indent--;
    }
}

void IRPrinter::visit(const Allocate *op) {
    ScopedBinding<> bind(known_type, op->name);
    stream << get_indent() << "allocate " << op->name << "[" << op->type;
    for (const auto &extent : op->extents) {
        stream << " * ";
        print(extent);
    }
    stream << "]";
    if (op->memory_type != MemoryType::Auto) {
        stream << " in " << op->memory_type;
    }
    if (!is_const_one(op->condition)) {
        stream << " if ";
        print(op->condition);
    }
    if (op->new_expr.defined()) {
        stream << "\n";
        stream << get_indent() << " custom_new { ";
        print_no_parens(op->new_expr);
        stream << " }";
    }
    if (!op->free_function.empty()) {
        stream << "\n";
        stream << get_indent() << " custom_delete { " << op->free_function << "(" << op->name << "); }";
    }
    stream << "\n";
    print(op->body);
}

void IRPrinter::visit(const Free *op) {
    stream << get_indent() << "free " << op->name;
    stream << "\n";
}

void IRPrinter::visit(const Realize *op) {
    ScopedBinding<> bind(known_type, op->name);
    stream << get_indent() << "realize " << op->name << "(";
    for (size_t i = 0; i < op->bounds.size(); i++) {
        stream << "[";
        print_no_parens(op->bounds[i].min);
        stream << ", ";
        print_no_parens(op->bounds[i].extent);
        stream << "]";
        if (i < op->bounds.size() - 1) {
            stream << ", ";
        }
    }
    stream << ")";
    if (op->memory_type != MemoryType::Auto) {
        stream << " in " << op->memory_type;
    }
    if (!is_const_one(op->condition)) {
        stream << " if ";
        print(op->condition);
    }
    stream << " {\n";

    indent++;
    print(op->body);
    indent--;

    stream << get_indent() << "}\n";
}

void IRPrinter::visit(const Prefetch *op) {
    stream << get_indent();
    const bool has_cond = !is_const_one(op->condition);
    if (has_cond) {
        stream << "if (";
        print_no_parens(op->condition);
        stream << ") {\n";
        indent++;
        stream << get_indent();
    }
    stream << "prefetch " << op->name << ", " << op->prefetch.at << ", " << op->prefetch.from << ", (";
    for (size_t i = 0; i < op->bounds.size(); i++) {
        stream << "[";
        print_no_parens(op->bounds[i].min);
        stream << ", ";
        print_no_parens(op->bounds[i].extent);
        stream << "]";
        if (i < op->bounds.size() - 1) {
            stream << ", ";
        }
    }
    stream << ")\n";
    if (has_cond) {
        indent--;
        stream << get_indent() << "}\n";
    }
    print(op->body);
}

void IRPrinter::visit(const Block *op) {
    print(op->first);
    print(op->rest);
}

void IRPrinter::visit(const Fork *op) {
    vector<Stmt> stmts;
    stmts.push_back(op->first);
    Stmt rest = op->rest;
    while (const Fork *f = rest.as<Fork>()) {
        stmts.push_back(f->first);
        rest = f->rest;
    }
    stmts.push_back(rest);

    stream << get_indent() << "fork ";
    for (const Stmt &s : stmts) {
        stream << "{\n";
        indent++;
        print(s);
        indent--;
        stream << get_indent() << "} ";
    }
    stream << "\n";
}

void IRPrinter::visit(const IfThenElse *op) {
    stream << get_indent();
    while (true) {
        stream << "if (";
        print_no_parens(op->condition);
        stream << ") {\n";
        indent++;
        print(op->then_case);
        indent--;

        if (!op->else_case.defined()) {
            break;
        }

        if (const IfThenElse *nested_if = op->else_case.as<IfThenElse>()) {
            stream << get_indent() << "} else ";
            op = nested_if;
        } else {
            stream << get_indent() << "} else {\n";
            indent++;
            print(op->else_case);
            indent--;
            break;
        }
    }

    stream << get_indent() << "}\n";
}

void IRPrinter::visit(const Evaluate *op) {
    stream << get_indent();
    print_no_parens(op->value);
    stream << "\n";
}

void IRPrinter::visit(const Shuffle *op) {
    if (op->is_concat()) {
        stream << "concat_vectors(";
        print_list(op->vectors);
        stream << ")";
    } else if (op->is_interleave()) {
        stream << "interleave_vectors(";
        print_list(op->vectors);
        stream << ")";
    } else if (op->is_extract_element()) {
        stream << "extract_element(";
        print_list(op->vectors);
        stream << ", " << op->indices[0] << ")";
    } else if (op->is_slice()) {
        stream << "slice_vectors(";
        print_list(op->vectors);
        stream << ", " << op->slice_begin()
               << ", " << op->slice_stride()
               << ", " << op->indices.size()
               << ")";
    } else if (op->is_broadcast()) {
        stream << "broadcast(";
        print_list(op->vectors);
        stream << ", " << op->broadcast_factor() << ")";
    } else {
        stream << "shuffle(";
        print_list(op->vectors);
        stream << ", ";
        for (size_t i = 0; i < op->indices.size(); i++) {
            print_no_parens(op->indices[i]);
            if (i < op->indices.size() - 1) {
                stream << ", ";
            }
        }
        stream << ")";
    }
}

void IRPrinter::visit(const VectorReduce *op) {
    stream << "("
           << op->type
           << ")vector_reduce("
           << op->op
           << ", "
           << op->value
           << ")";
}

void IRPrinter::visit(const Atomic *op) {
    if (op->mutex_name.empty()) {
        stream << get_indent() << "atomic {\n";
    } else {
        stream << get_indent() << "atomic (";
        stream << op->mutex_name;
        stream << ") {\n";
    }
    indent += 2;
    print(op->body);
    indent -= 2;
    stream << get_indent() << "}\n";
}

}  // namespace Internal
}  // namespace Halide
