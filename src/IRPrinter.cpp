#include <iostream>
#include <sstream>

#include "IRPrinter.h"

#include "AssociativeOpsTable.h"
#include "Associativity.h"
#include "Closure.h"
#include "ConstantInterval.h"
#include "Expr.h"
#include "IROperator.h"
#include "Interval.h"
#include "Module.h"
#include "ModulusRemainder.h"
#include "Target.h"
#include "Util.h"

#if _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

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
        // ensure that 'const' (etc) qualifiers are emitted when appropriate
        out << "(" << type_to_c_type(type, false) << ")";
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
    case DeviceAPI::Vulkan:
        out << "<Vulkan>";
        break;
    case DeviceAPI::WebGPU:
        out << "<WebGPU>";
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
    case TailStrategy::ShiftInwardsAndBlend:
        out << "ShiftInwardsAndBlend";
        break;
    case TailStrategy::RoundUpAndBlend:
        out << "RoundUpAndBlend";
        break;
    }
    return out;
}

std::ostream &operator<<(std::ostream &out, const Partition &p) {
    switch (p) {
    case Partition::Auto:
        out << "Auto";
        break;
    case Partition::Never:
        out << "Never";
        break;
    case Partition::Always:
        out << "Always";
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
    Stmt for_loop = For::make("x", -2, y + 2, ForType::Parallel, Partition::Auto, DeviceAPI::Host, store);
    vector<Expr> args(1);
    args[0] = x % 3;
    Expr call = Call::make(i32, "buf", args, Call::Extern);
    Stmt store2 = Store::make("out", call + 1, x, Parameter(), const_true(), ModulusRemainder(3, 5));
    Stmt for_loop2 = For::make("x", 0, y, ForType::Vectorized, Partition::Auto, DeviceAPI::Host, store2);

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

std::ostream &operator<<(std::ostream &stream, IRNodeType type) {
#define CASE(e)         \
    case IRNodeType::e: \
        stream << #e;   \
        break;
    switch (type) {
        CASE(IntImm)
        CASE(UIntImm)
        CASE(FloatImm)
        CASE(StringImm)
        CASE(Broadcast)
        CASE(Cast)
        CASE(Reinterpret)
        CASE(Variable)
        CASE(Add)
        CASE(Sub)
        CASE(Mod)
        CASE(Mul)
        CASE(Div)
        CASE(Min)
        CASE(Max)
        CASE(EQ)
        CASE(NE)
        CASE(LT)
        CASE(LE)
        CASE(GT)
        CASE(GE)
        CASE(And)
        CASE(Or)
        CASE(Not)
        CASE(Select)
        CASE(Load)
        CASE(Ramp)
        CASE(Call)
        CASE(Let)
        CASE(Shuffle)
        CASE(VectorReduce)
        // Stmts
        CASE(LetStmt)
        CASE(AssertStmt)
        CASE(ProducerConsumer)
        CASE(For)
        CASE(Acquire)
        CASE(Store)
        CASE(Provide)
        CASE(Allocate)
        CASE(Free)
        CASE(Realize)
        CASE(Block)
        CASE(Fork)
        CASE(IfThenElse)
        CASE(Evaluate)
        CASE(Prefetch)
        CASE(Atomic)
        CASE(HoistedStorage)
    }
#undef CASE
    return stream;
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
        out << "add";
        break;
    case VectorReduce::SaturatingAdd:
        out << "saturating_add";
        break;
    case VectorReduce::Mul:
        out << "mul";
        break;
    case VectorReduce::Min:
        out << "min";
        break;
    case VectorReduce::Max:
        out << "max";
        break;
    case VectorReduce::And:
        out << "and";
        break;
    case VectorReduce::Or:
        out << "or";
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

std::ostream &operator<<(std::ostream &out, const Interval &in) {
    out << "[";
    if (in.has_lower_bound()) {
        out << in.min;
    } else {
        out << "-inf";
    }
    out << ", ";
    if (in.has_upper_bound()) {
        out << in.max;
    } else {
        out << "inf";
    }
    out << "]";
    return out;
}

std::ostream &operator<<(std::ostream &out, const ConstantInterval &in) {
    out << "[";
    if (in.min_defined) {
        out << in.min;
    } else {
        out << "-inf";
    }
    out << ", ";
    if (in.max_defined) {
        out << in.max;
    } else {
        out << "inf";
    }
    out << "]";
    return out;
}

std::ostream &operator<<(std::ostream &out, const ModulusRemainder &c) {
    out << "(mod: " << c.modulus << " rem: " << c.remainder << ")";
    return out;
}

namespace {
bool supports_ansi(std::ostream &os) {
    const char *term = getenv("TERM");
    if (term) {
        // Check if the terminal supports colors
        if (!(strstr(term, "color") || strstr(term, "xterm"))) {
            return false;
        }
    }
#if _WIN32
    HANDLE h;
    if (&os == &std::cout) {
        h = GetStdHandle(STD_OUTPUT_HANDLE);
    } else if (&os == &std::cerr) {
        h = GetStdHandle(STD_ERROR_HANDLE);
    } else {
        return false;
    }

    DWORD mode;
    return GetConsoleMode(h, &mode) &&
           SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#else
    if (&os == &std::cout) {
        return isatty(fileno(stdout));
    } else if (&os == &std::cerr) {
        return isatty(fileno(stderr));
    }
    return false;
#endif
}
}  // namespace

IRPrinter::IRPrinter(ostream &s)
    : stream(s) {
    s.setf(std::ios::fixed, std::ios::floatfield);
    if (&stream == &std::cout || &stream == &std::cerr) {
        bool use_colors = false;
        const char *opt = getenv("HL_COLORS");
        if (opt) {
            int val = std::atoi(opt);
            use_colors = val != 0;
        } else {
            use_colors = supports_ansi(stream);
        }
        if (use_colors) {
            ansi = true;
            // Simple palette using standard VGA colors.
            // clang-format off
            ansi_hl        = "\033[4m";
            ansi_dim       = "\033[2m";
            ansi_kw        = "\033[35;1m";
            ansi_imm_int   = "\033[36m";
            ansi_imm_float = "\033[96m";
            ansi_imm_str   = "\033[32m";
            ansi_var       = "";
            ansi_buf       = "\033[33m";
            ansi_fn        = "\033[31m";
            ansi_type      = "\033[34m";
            ansi_reset_col = "\033[39m";
            ansi_reset     = "\033[0m";
            // clang-format on
        }
    }
}

#define ansi_helper(name)                                                          \
    template<typename T>                                                           \
    Ansi<T> IRPrinter::name(const T &t) {                                          \
        return {t, ansi ? ansi_##name : nullptr, ansi ? ansi_reset_col : nullptr}; \
    }
#define ansi_helper_r(name)                                                    \
    template<typename T>                                                       \
    Ansi<T> IRPrinter::name(const T &t) {                                      \
        return {t, ansi ? ansi_##name : nullptr, ansi ? ansi_reset : nullptr}; \
    }

ansi_helper_r(hl);
ansi_helper_r(kw);
ansi_helper(imm_int);
ansi_helper(imm_float);
ansi_helper(imm_str);
ansi_helper(var);
ansi_helper(buf);
ansi_helper(fn);
ansi_helper(type);

template<typename T>
Ansi<T> IRPrinter::typep(const T &t) {
    return {t, ansi ? "\033[34m(" : "(", ansi ? ")\033[0m" : ")"};
}

template<typename T>
Ansi<T> IRPrinter::paren(const T &t, bool bold, int depth) {
    if (!ansi) {
        return {t, nullptr, nullptr};
    }
    if (depth == -1) {
        depth = paren_depth;
    }
    const char *open = "";
    if (bold) {
        // clang-format off
        switch (depth % 6) {
        case 0: open = "\033[91;1m"; break;
        case 1: open = "\033[92;1m"; break;
        case 2: open = "\033[93;1m"; break;
        case 3: open = "\033[94;1m"; break;
        case 4: open = "\033[95;1m"; break;
        case 5: open = "\033[96;1m"; break;
        default: break;
        }
        // clang-format on
    } else {
        // clang-format off
        switch (depth % 6) {
        case 0: open = "\033[91m"; break;
        case 1: open = "\033[92m"; break;
        case 2: open = "\033[93m"; break;
        case 3: open = "\033[94m"; break;
        case 4: open = "\033[95m"; break;
        case 5: open = "\033[96m"; break;
        default: break;
        }
        // clang-format on
    }
    return {t, open, ansi_reset};
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

void IRPrinter::print_summary(const Stmt &ir) {
    ScopedValue<bool> old(is_summary, true);
    ir.accept(this);
}

void IRPrinter::print_list(const std::vector<Expr> &exprs) {
    for (size_t i = 0; i < exprs.size(); i++) {
        print_no_parens(exprs[i]);
        if (i < exprs.size() - 1) {
            stream << paren(", ");
        }
    }
}

void IRPrinter::visit(const IntImm *op) {
    if (op->type == Int(32)) {
        stream << imm_int(op->value);
    } else {
        stream << typep(op->type) << imm_int(op->value);
    }
}

void IRPrinter::visit(const UIntImm *op) {
    stream << typep(op->type) << imm_int(op->value);
}

void IRPrinter::visit(const FloatImm *op) {
    const bool use_scientific_format = (op->value != 0.0) && (std::log10(std::abs(op->value)) < -6);
    if (use_scientific_format) {
        stream << std::scientific;
    }

    stream << ansi_imm_float;

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

    stream << ansi_reset;

    if (use_scientific_format) {
        stream << std::fixed;
    }
}

void IRPrinter::visit(const StringImm *op) {
    stream << ansi_imm_str;
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
    stream << ansi_reset;
}

void IRPrinter::visit(const Cast *op) {
    stream << type(op->type);
    openf();
    print_no_parens(op->value);
    closef();
}

void IRPrinter::visit(const Reinterpret *op) {
    stream << kw("reinterpret<") << type(op->type) << kw(">");
    openf();
    print_no_parens(op->value);
    closef();
}

void IRPrinter::visit(const Variable *op) {
    if (!known_type.contains(op->name) &&
        (op->type != Int(32))) {
        // Handle types already have parens
        if (op->type.is_handle()) {
            stream << type(op->type);
        } else {
            stream << typep(op->type);
        }
    }
    stream << var(op->name);
}

void IRPrinter::open() {
    if (!implicit_parens) {
        paren_depth++;
        stream << paren("(");
    }
}

void IRPrinter::close() {
    if (!implicit_parens) {
        stream << paren(")");
        paren_depth--;
    }
}

void IRPrinter::openf() {
    paren_depth++;
    stream << paren("(");
}

void IRPrinter::openf(const char *name) {
    paren_depth++;
    stream << paren(name, false) << paren("(");
}

void IRPrinter::closef() {
    stream << paren(")");
    paren_depth--;
}

void IRPrinter::visit(const Add *op) {
    open();
    print(op->a);
    stream << paren(" + ");
    print(op->b);
    close();
}

void IRPrinter::visit(const Sub *op) {
    open();
    print(op->a);
    stream << paren(" - ");
    print(op->b);
    close();
}

void IRPrinter::visit(const Mul *op) {
    open();
    print(op->a);
    stream << paren("*");
    print(op->b);
    close();
}

void IRPrinter::visit(const Div *op) {
    open();
    print(op->a);
    stream << paren("/");
    print(op->b);
    close();
}

void IRPrinter::visit(const Mod *op) {
    open();
    print(op->a);
    stream << paren(" % ");
    print(op->b);
    close();
}

void IRPrinter::visit(const Min *op) {
    openf("min");
    print_no_parens(op->a);
    stream << paren(", ");
    print_no_parens(op->b);
    closef();
}

void IRPrinter::visit(const Max *op) {
    openf("max");
    print_no_parens(op->a);
    stream << paren(", ");
    print_no_parens(op->b);
    closef();
}

void IRPrinter::visit(const EQ *op) {
    open();
    print(op->a);
    stream << paren(" == ");
    print(op->b);
    close();
}

void IRPrinter::visit(const NE *op) {
    open();
    print(op->a);
    stream << paren(" != ");
    print(op->b);
    close();
}

void IRPrinter::visit(const LT *op) {
    open();
    print(op->a);
    stream << paren(" < ");
    print(op->b);
    close();
}

void IRPrinter::visit(const LE *op) {
    open();
    print(op->a);
    stream << paren(" <= ");
    print(op->b);
    close();
}

void IRPrinter::visit(const GT *op) {
    open();
    print(op->a);
    stream << paren(" > ");
    print(op->b);
    close();
}

void IRPrinter::visit(const GE *op) {
    open();
    print(op->a);
    stream << paren(" >= ");
    print(op->b);
    close();
}

void IRPrinter::visit(const And *op) {
    open();
    print(op->a);
    stream << paren(" && ");
    print(op->b);
    close();
}

void IRPrinter::visit(const Or *op) {
    open();
    print(op->a);
    stream << paren(" || ");
    print(op->b);
    close();
}

void IRPrinter::visit(const Not *op) {
    stream << "!";
    print(op->a);
}

void IRPrinter::visit(const Select *op) {
    openf("select");
    print_no_parens(op->condition);
    stream << paren(", ");
    print_no_parens(op->true_value);
    stream << paren(", ");
    print_no_parens(op->false_value);
    closef();
}

void IRPrinter::visit(const Load *op) {
    const bool has_pred = !is_const_one(op->predicate);
    const bool show_alignment = op->type.is_vector() && op->alignment.modulus > 1;
    if (has_pred) {
        open();
    }
    if (!known_type.contains(op->name)) {
        stream << typep(op->type);
    }
    paren_depth++;
    stream << buf(op->name) << paren("[");
    print_no_parens(op->index);
    if (show_alignment) {
        stream << ansi_dim;
        stream << " aligned("
               << imm_int(op->alignment.modulus)
               << ", "
               << imm_int(op->alignment.remainder) << ")";
        stream << ansi_reset;
    }
    stream << paren("]");
    paren_depth--;
    if (has_pred) {
        stream << kw(" if ");
        print(op->predicate);
        close();
    }
}

void IRPrinter::visit(const Ramp *op) {
    stream << kw("ramp(");
    print_no_parens(op->base);
    stream << kw(", ");
    print_no_parens(op->stride);
    stream << kw(", ") << imm_int(op->lanes);
    stream << kw(")");
}

void IRPrinter::visit(const Broadcast *op) {
    stream << ansi_kw;
    stream << "x" << op->lanes;
    stream << "(";
    stream << ansi_reset;
    paren_depth++;
    print_no_parens(op->value);
    paren_depth--;
    stream << kw(")");
}

void IRPrinter::visit(const Call *op) {

    if (op->is_intrinsic(Call::bitwise_or)) {
        open();
        print(op->args[0]);
        stream << paren(" | ");
        print(op->args[1]);
        close();
    } else if (op->is_intrinsic(Call::bitwise_and)) {
        open();
        print(op->args[0]);
        stream << paren(" & ");
        print(op->args[1]);
        close();
    } else if (op->is_intrinsic(Call::bitwise_xor)) {
        open();
        print(op->args[0]);
        stream << paren(" ^ ");
        print(op->args[1]);
        close();
    } else if (op->is_intrinsic(Call::bitwise_not)) {
        stream << ansi_kw << "~(" << ansi_reset;
        paren_depth++;
        print_no_parens(op->args[0]);
        paren_depth--;
        stream << kw(")");
    } else {

        // TODO: Print indication of C vs C++?
        if (!known_type.contains(op->name) &&
            (op->type != Int(32))) {
            if (op->type.is_handle()) {
                stream << type(op->type);  // Already has parens
            } else {
                stream << typep(op->type);
            }
        }
        openf(op->name.c_str());
        print_list(op->args);
        closef();
    }
}

void IRPrinter::visit(const Let *op) {
    ScopedBinding<> bind(known_type, op->name);
    paren_depth++;
    if (!implicit_parens) {
        stream << paren("(");
    }
    stream << paren("let ") << var(op->name) << paren(" = ");
    print(op->value);
    stream << paren(" in ");
    if (!is_summary) {
        print(op->body);
    }
    if (!implicit_parens) {
        stream << paren(")");
    }
    paren_depth--;
}

void IRPrinter::visit(const LetStmt *op) {
    ScopedBinding<> bind(known_type, op->name);
    stream << get_indent() << kw("let ") << var(op->name) << kw(" = ");
    {
        ScopedValue<int> reset_paren_depth(paren_depth, 0);
        print_no_parens(op->value);
    }
    stream << "\n";

    if (!is_summary) {
        print(op->body);
    }
}

void IRPrinter::visit(const AssertStmt *op) {
    stream << get_indent() << kw("assert(");
    print_no_parens(op->condition);
    stream << kw(", ");
    print_no_parens(op->message);
    stream << kw(")\n");
}

void IRPrinter::visit(const ProducerConsumer *op) {
    stream << get_indent();
    if (op->is_producer) {
        stream << paren("produce ") << buf(op->name) << paren(" {\n");
    } else {
        stream << paren("consume ") << buf(op->name) << paren(" {\n");
    }
    paren_depth++;
    indent++;
    print(op->body);
    indent--;
    paren_depth--;
    stream << get_indent() << paren("}\n");
}

void IRPrinter::visit(const For *op) {
    ScopedBinding<> bind(known_type, op->name);
    paren_depth++;
    stream << get_indent() << paren(op->for_type) << type(op->device_api) << paren(" (");
    stream << var(op->name) << paren(", ");
    print_no_parens(op->min);
    stream << paren(", ");
    print_no_parens(op->extent);
    closef();
    stream << " ";

    print_braced_stmt(op->body, 1);
}

void IRPrinter::visit(const Acquire *op) {
    stream << get_indent() << kw("acquire ");
    openf();
    print_no_parens(op->semaphore);
    stream << paren(", ");
    print_no_parens(op->count);
    closef();
    stream << " ";
    print_braced_stmt(op->body, 1);
}

void IRPrinter::print_lets(const Let *let) {
    stream << get_indent();
    ScopedBinding<> bind(known_type, let->name);
    stream << kw("let ") << var(let->name) << kw(" = ");
    print_no_parens(let->value);
    stream << kw(" in\n");
    if (is_summary) {
        stream << get_indent() << "...\n";
    } else if (const Let *next = let->body.as<Let>()) {
        print_lets(next);
    } else {
        stream << get_indent();
        print_no_parens(let->body);
        stream << "\n";
    }
}

void IRPrinter::print_braced_stmt(const Stmt &stmt, int extra_indent) {
    if (is_summary) {
        stream << paren("{ ... }\n");
        return;
    }

    paren_depth++;
    stream << paren("{\n");
    indent += extra_indent;
    print(stmt);
    indent -= extra_indent;
    stream << get_indent() << paren("}\n");
    paren_depth--;
}

void IRPrinter::visit(const Store *op) {
    ScopedValue<int> reset_paren_depth(paren_depth, 0);
    stream << get_indent();
    const bool has_pred = !is_const_one(op->predicate);
    const bool show_alignment = op->value.type().is_vector() && (op->alignment.modulus > 1);
    if (has_pred) {
        stream << kw("predicate (");
        print_no_parens(op->predicate);
        stream << kw(")\n");
        indent++;
        stream << get_indent();
    }
    stream << buf(op->name) << paren("[");
    print_no_parens(op->index);
    if (show_alignment) {
        stream << ansi_dim;
        stream << " aligned("
               << imm_int(op->alignment.modulus)
               << ", "
               << imm_int(op->alignment.remainder) << ")";
        stream << ansi_reset;
    }
    stream << paren("] = ");
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
    ScopedValue<int> reset_paren_depth(paren_depth, 0);
    stream << get_indent();
    const bool has_pred = !is_const_one(op->predicate);
    if (has_pred) {
        stream << kw("predicate (");
        print_no_parens(op->predicate);
        stream << kw(")\n");
        indent++;
        stream << get_indent();
    }
    stream << buf(op->name);
    stream << paren("(");
    print_list(op->args);
    stream << paren(") = ");
    if (op->values.size() > 1) {
        stream << paren("{");
    }
    print_list(op->values);
    if (op->values.size() > 1) {
        stream << paren("}");
    }

    stream << "\n";
    if (has_pred) {
        indent--;
    }
}

void IRPrinter::visit(const Allocate *op) {
    ScopedValue<int> reset_paren_depth(paren_depth, 0);
    ScopedBinding<> bind(known_type, op->name);
    stream << get_indent() << hl(kw("allocate")) << " " << buf(op->name) << "[" << type(op->type);
    bool first = true;
    for (const auto &extent : op->extents) {
        stream << " * ";
        if (first && op->padding) {
            stream << "(";
            first = false;
        }
        print(extent);
    }
    if (op->padding) {
        stream << " + " << op->padding << ")";
    }
    stream << "]";
    if (op->memory_type != MemoryType::Auto) {
        stream << kw(" in ") << type(op->memory_type);
    }
    if (!is_const_one(op->condition)) {
        stream << " if ";
        print(op->condition);
    }
    if (op->new_expr.defined()) {
        stream << "\n";
        stream << get_indent() << kw(" custom_new { ");
        print_no_parens(op->new_expr);
        stream << kw(" }");
    }
    if (!op->free_function.empty()) {
        stream << "\n";
        stream << get_indent() << kw(" custom_delete { ");
        openf(op->free_function.c_str());
        stream << buf(op->name);
        closef();
        stream << "; " << kw("}");
    }
    stream << "\n";

    if (!is_summary) {
        print(op->body);
    }
}

void IRPrinter::visit(const Free *op) {
    stream << get_indent() << hl(kw("free")) << " " << buf(op->name);
    stream << "\n";
}

void IRPrinter::visit(const Realize *op) {
    ScopedBinding<> bind(known_type, op->name);
    stream << get_indent() << kw("realize ") << buf(op->name) << kw("(");
    for (size_t i = 0; i < op->bounds.size(); i++) {
        stream << paren("[");
        print_no_parens(op->bounds[i].min);
        stream << paren(", ");
        print_no_parens(op->bounds[i].extent);
        stream << paren("]");
        if (i < op->bounds.size() - 1) {
            stream << ", ";
        }
    }
    stream << kw(")");
    if (op->memory_type != MemoryType::Auto) {
        stream << kw(" in ") << type(op->memory_type);
    }
    if (!is_const_one(op->condition)) {
        stream << kw(" if ");
        print(op->condition);
    }

    stream << " ";
    print_braced_stmt(op->body);
}

void IRPrinter::visit(const Prefetch *op) {
    ScopedValue<int> reset_paren_depth(paren_depth, 0);
    stream << get_indent();
    const bool has_cond = !is_const_one(op->condition);
    if (has_cond) {
        stream << kw("if ");
        openf();
        print_no_parens(op->condition);
        closef();
        stream << " {\n";
        indent++;
        stream << get_indent();
    }
    stream << kw("prefetch ") << buf(op->name) << ", " << op->prefetch.at << ", " << op->prefetch.from << ", (";
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
    if (!is_summary) {
        print(op->body);
    }
}

void IRPrinter::visit(const Block *op) {
    if (!is_summary) {
        print(op->first);
        print(op->rest);
    }
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

    stream << get_indent() << kw("fork ");
    if (is_summary) {
        stream << "[" << stmts.size();
        if (stmts.size() == 1) {
            stream << " child]";
        } else {
            stream << " children]";
        }
    } else {
        for (const Stmt &s : stmts) {
            stream << "{\n";
            indent++;
            print(s);
            indent--;
            stream << get_indent() << "} ";
        }
        stream << "\n";
    }
}

void IRPrinter::visit(const IfThenElse *op) {
    stream << get_indent();
    while (true) {
        stream << paren("if (");
        print_no_parens(op->condition);
        stream << paren(") {\n");
        paren_depth++;
        indent++;
        print(op->then_case);
        indent--;
        paren_depth--;

        if (!op->else_case.defined()) {
            break;
        }

        if (const IfThenElse *nested_if = op->else_case.as<IfThenElse>()) {
            stream << get_indent() << paren("} else ");
            op = nested_if;
        } else {
            stream << get_indent() << paren("}") << (" else {\n");
            paren_depth++;
            indent++;
            print(op->else_case);
            indent--;
            paren_depth--;
            break;
        }
    }

    stream << get_indent() << paren("}\n");
}

void IRPrinter::visit(const Evaluate *op) {
    stream << get_indent();
    print_no_parens(op->value);
    stream << "\n";
}

void IRPrinter::visit(const Shuffle *op) {
    if (op->is_concat()) {
        openf("concat_vectors");
        print_list(op->vectors);
    } else if (op->is_interleave()) {
        openf("interleave_vectors");
        print_list(op->vectors);
    } else if (op->is_extract_element()) {
        openf("extract_element");
        print_list(op->vectors);
        stream << paren(", ") << imm_int(op->indices[0]);
    } else if (op->is_slice()) {
        openf("slice_vectors");
        print_list(op->vectors);
        stream << paren(", ") << imm_int(op->slice_begin())
               << paren(", ") << imm_int(op->slice_stride())
               << paren(", ") << imm_int(op->indices.size());
    } else {
        openf("shuffle");
        print_list(op->vectors);
        stream << paren(", ");
        for (size_t i = 0; i < op->indices.size(); i++) {
            stream << imm_int(op->indices[i]);
            if (i < op->indices.size() - 1) {
                stream << paren(", ");
            }
        }
    }
    closef();
}

void IRPrinter::visit(const VectorReduce *op) {
    stream << typep(op->type);
    paren_depth++;
    stream << paren("vector_reduce_", false) << paren(op->op, false) << paren("(");
    print_no_parens(op->value);
    stream << paren(")");
    paren_depth--;
}

void IRPrinter::visit(const Atomic *op) {
    stream << get_indent();

    if (op->mutex_name.empty()) {
        stream << kw("atomic (") << op->producer_name << kw(") ");
    } else {
        stream << kw("atomic (") << op->producer_name << kw(", ") << op->mutex_name << kw(") ");
    }

    print_braced_stmt(op->body);
}

void IRPrinter::visit(const HoistedStorage *op) {
    if (op->name.empty()) {
        stream << get_indent() << kw("hoisted_storage ");
    } else {
        stream << get_indent() << kw("hoisted_storage (") << buf(op->name) << kw(") ");
    }

    print_braced_stmt(op->body);
}

std::string lldb_string(const Expr &ir) {
    std::stringstream s{};
    IRPrinter p(s);
    p.print_no_parens(ir);
    return s.str();
}

std::string lldb_string(const Internal::BaseExprNode *n) {
    return lldb_string(Expr(n));
}

std::string lldb_string(const Stmt &ir) {
    std::stringstream s{};
    IRPrinter p(s);
    p.print_summary(ir);
    return s.str();
}

}  // namespace Internal
}  // namespace Halide
