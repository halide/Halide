#include <algorithm>
#include <functional>
#include <iostream>
#include <sstream>
#include <tuple>

#include "IRPrinter.h"

#include "AssociativeOpsTable.h"
#include "Associativity.h"
#include "Closure.h"
#include "ConstantInterval.h"
#include "Debug.h"
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
    case Type::StructKind:
        out << "struct{";
        if (const StructTypeInfo *info = type.struct_type()) {
            const char *sep = "";
            for (const auto &f : info->fields) {
                out << sep << f.name << ": " << f.type;
                if (f.array_extent) {
                    out << "[" << *f.array_extent << "]";
                }
                sep = ", ";
            }
        }
        out << "}";
        break;
    }
    if (!type.is_handle() && !type.is_struct()) {
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

ostream &operator<<(ostream &stream, const Tuple &ir) {
    stream << "(";
    for (const Expr &e : ir) {
        stream << e << ", ";  // keep the trailing comma
    }
    return stream << ")";
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

namespace Internal {
namespace {

// Print a LoweredFunc, with its body printed by print_body.
void print_lowered_func(ostream &stream, const LoweredFunc &function,
                        const std::function<void(const Stmt &)> &print_body) {
    stream << function.linkage << " func " << function.name << " (";
    for (size_t i = 0; i < function.args.size(); i++) {
        stream << function.args[i].name;
        if (i + 1 < function.args.size()) {
            stream << ", ";
        }
    }
    stream << ") {\n";
    print_body(function.body);
    stream << "}\n\n";
}

// Where the functions and buffers of a Module were printed, as byte offsets
// into the stream.
struct ModuleRecord {
    vector<uint32_t> function_starts, function_ends, buffer_starts;
};

// Print a Module, with the bodies of its functions printed by print_body. If
// record is set, note where its own functions and buffers went.
void print_module(ostream &stream, const Module &m,
                  const std::function<void(const Stmt &)> &print_body,
                  ModuleRecord *record = nullptr) {
    for (const auto &s : m.submodules()) {
        print_module(stream, s, print_body);
        stream << "\n";
    }

    // The module retains implied features, but print it in minimal form.
    stream << "module name=" << m.name() << ", target=" << m.target().without_implied_features().to_string() << "\n";
    for (const auto &b : m.buffers()) {
        if (record) {
            record->buffer_starts.push_back((uint32_t)stream.tellp());
        }
        stream << b << "\n";
    }
    for (const auto &f : m.functions()) {
        if (record) {
            record->function_starts.push_back((uint32_t)stream.tellp());
        }
        print_lowered_func(stream, f, print_body);
        if (record) {
            record->function_ends.push_back((uint32_t)stream.tellp());
        }
        stream << "\n";
    }
}

// Turn the locations recorded while printing text into a source map of it.
StmtSourceMap make_source_map(string text, const vector<IRPrinterLocation> &recorded,
                              const Module *m, const ModuleRecord *record) {
    StmtSourceMap result;
    result.text = std::move(text);
    const string &t = result.text;

    vector<uint32_t> line_starts{0};
    for (uint32_t i = 0; i < (uint32_t)t.size(); i++) {
        if (t[i] == '\n') {
            line_starts.push_back(i + 1);
        }
    }
    auto line_of = [&](uint32_t offset) {
        return (uint32_t)(std::upper_bound(line_starts.begin(), line_starts.end(), offset) - line_starts.begin());
    };

    // Offsets usually sit on indentation or on the space in front of an
    // operator. Point at the first thing actually printed instead.
    auto line_and_column = [&](uint32_t offset, uint32_t &line, uint32_t &column) {
        while (offset < t.size() && (t[offset] == ' ' || t[offset] == '\n')) {
            offset++;
        }
        line = line_of(offset);
        column = offset - line_starts[line - 1] + 1;
    };

    // The end is just past the last thing printed, which is usually a
    // newline.
    auto end_line = [&](uint32_t start, uint32_t end) {
        while (end > start && (t[end - 1] == ' ' || t[end - 1] == '\n')) {
            end--;
        }
        return line_of(end > start ? end - 1 : start);
    };

    result.locations.resize(recorded.size());
    for (size_t i = 0; i < recorded.size(); i++) {
        StmtSourceMap::Location &l = result.locations[i];
        l.node = recorded[i].node;
        l.start = recorded[i].start;
        l.end = recorded[i].end;
        line_and_column(recorded[i].anchor, l.line, l.column);
        l.end_line = end_line(l.start, l.end);
    }
    std::sort(result.locations.begin(), result.locations.end(),
              [](const StmtSourceMap::Location &a, const StmtSourceMap::Location &b) {
                  return a.node != b.node ? std::less<>()(a.node, b.node) : a.start < b.start;
              });

    // Nodes nested in one another can share an anchor, e.g. a Block and its
    // first Stmt. The innermost one began printing last, or ended first.
    result.by_anchor.resize(result.locations.size());
    for (uint32_t i = 0; i < (uint32_t)result.by_anchor.size(); i++) {
        result.by_anchor[i] = i;
    }
    std::sort(result.by_anchor.begin(), result.by_anchor.end(), [&](uint32_t i, uint32_t j) {
        const StmtSourceMap::Location &a = result.locations[i], &b = result.locations[j];
        return std::tie(a.line, a.column, b.start, a.end) < std::tie(b.line, b.column, a.start, b.end);
    });

    if (m && record) {
        result.functions.resize(record->function_starts.size());
        for (size_t i = 0; i < record->function_starts.size(); i++) {
            StmtSourceMap::Function &f = result.functions[i];
            f.name = m->functions()[i].name;
            f.start = record->function_starts[i];
            line_and_column(f.start, f.line, f.column);
            f.end_line = end_line(f.start, record->function_ends[i]);
        }
        for (uint32_t start : record->buffer_starts) {
            uint32_t line, column;
            line_and_column(start, line, column);
            result.buffer_lines.push_back(line);
        }
    }
    return result;
}

}  // namespace
}  // namespace Internal

ostream &operator<<(ostream &stream, const Module &m) {
    Internal::print_module(stream, m, [&](const Internal::Stmt &s) { stream << s; });
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
    case DeviceAPI::SMEStreaming:
        out << "<SMEStreaming>";
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
    case MemoryType::GPUSharedAsync:
        out << "GPUSharedAsync";
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
    case MemoryType::Tile:
        out << "Tile";
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

std::ostream &operator<<(std::ostream &stream, const IRHandle &ir) {
    if (ir.defined() && ir->node_type > StrongestExprNodeType) {
        return stream << Stmt((const BaseStmtNode *)ir.get());
    } else {
        return stream << Expr((const BaseExprNode *)ir.get());
    }
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
        CASE(StreamingStore)
        CASE(StreamingLoads)
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
    print_lowered_func(stream, function, [&](const Stmt &s) { stream << s; });
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
bool supports_ansi(const std::ostream *os) {
    if (!os) {
        return false;
    }
    if (const char *term = getenv("TERM")) {
        // Check if the terminal supports colors
        if (!(strstr(term, "color") || strstr(term, "xterm"))) {
            return false;
        }
    }
#if _WIN32
    HANDLE h;
    if (os == &std::cout) {
        h = GetStdHandle(STD_OUTPUT_HANDLE);
    } else if (os == &std::cerr) {
        h = GetStdHandle(STD_ERROR_HANDLE);
    } else {
        return false;
    }

    DWORD mode;
    return GetConsoleMode(h, &mode) &&
           SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#else
    if (os == &std::cout) {
        return isatty(fileno(stdout));
    } else if (os == &std::cerr) {
        return isatty(fileno(stderr));
    }
    return false;
#endif
}
}  // namespace

IRPrinter::IRPrinter(ostream &s)
    : stream(s) {
    s.setf(std::ios::fixed, std::ios::floatfield);

    auto detect_color = [&](const std::ostream *terminal) {
        std::string opt = get_env_variable("HL_COLORS");
        bool use_colors;
        if (!opt.empty()) {
            use_colors = opt == "1";
        } else {
            // Respect NO_COLOR in addition to whether we're writing to a
            // terminal, matching the profiler report's color gate.
            use_colors = get_env_variable("NO_COLOR").empty() && supports_ansi(terminal);
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
    };

    switch (debug_stream_sink(stream)) {
    case DebugStreamSink::Cout:
        detect_color(&std::cout);
        break;
    case DebugStreamSink::Cerr:
        detect_color(&std::cerr);
        break;
    case DebugStreamSink::File:
        // A shared log file: never auto-detect colors, but still honor an
        // explicit HL_COLORS override.
        detect_color(nullptr);
        break;
    case DebugStreamSink::None:
        // It's not a DebugStream. Is it cout or cerr identically?
        if (&stream == &std::cout || &stream == &std::cerr) {
            detect_color(&stream);
        }
        break;
    }
}

#define styled_helper(name, style, reset)                  \
    template<typename T>                                   \
    StyledText<T> IRPrinter::name(const T &t) {            \
        return {t, this, IRPrinterStyle::style, 0, reset}; \
    }

styled_helper(hl, Highlight, true);
styled_helper(kw, Keyword, true);
styled_helper(imm_int, ImmInt, false);
styled_helper(imm_float, ImmFloat, false);
styled_helper(imm_str, ImmString, false);
styled_helper(var, Var, false);
styled_helper(buf, Buffer, false);
styled_helper(fn, Function, false);
styled_helper(type, Type, false);
styled_helper(typep, TypeParens, true);

template<typename T>
StyledText<T> IRPrinter::paren(const T &t, bool bold, int depth) {
    return {t, this, bold ? IRPrinterStyle::BoldParen : IRPrinterStyle::Paren,
            depth == -1 ? paren_depth : depth, true};
}

void IRPrinter::begin_style(IRPrinterStyle style, int depth) const {
    if (!ansi) {
        if (style == IRPrinterStyle::TypeParens) {
            stream << "(";
        }
        return;
    }
    // clang-format off
    static const char *const parens[2][6] = {
        {"\033[91m", "\033[92m", "\033[93m", "\033[94m", "\033[95m", "\033[96m"},
        {"\033[91;1m", "\033[92;1m", "\033[93;1m", "\033[94;1m", "\033[95;1m", "\033[96;1m"}};
    // clang-format on
    switch (style) {
    case IRPrinterStyle::Highlight:
        stream << ansi_hl;
        break;
    case IRPrinterStyle::Dim:
        stream << ansi_dim;
        break;
    case IRPrinterStyle::Keyword:
        stream << ansi_kw;
        break;
    case IRPrinterStyle::ImmInt:
        stream << ansi_imm_int;
        break;
    case IRPrinterStyle::ImmFloat:
        stream << ansi_imm_float;
        break;
    case IRPrinterStyle::ImmString:
        stream << ansi_imm_str;
        break;
    case IRPrinterStyle::Var:
        stream << ansi_var;
        break;
    case IRPrinterStyle::Buffer:
        stream << ansi_buf;
        break;
    case IRPrinterStyle::Function:
        stream << ansi_fn;
        break;
    case IRPrinterStyle::Type:
        stream << ansi_type;
        break;
    case IRPrinterStyle::TypeParens:
        stream << "\033[34m(";
        break;
    case IRPrinterStyle::Paren:
    case IRPrinterStyle::BoldParen:
        stream << parens[style == IRPrinterStyle::BoldParen][depth % 6];
        break;
    }
}

void IRPrinter::end_style(IRPrinterStyle style, bool reset) const {
    if (style == IRPrinterStyle::TypeParens) {
        stream << ")";
    }
    if (ansi) {
        stream << (reset ? ansi_reset : ansi_reset_col);
    }
}

void IRPrinter::print(const Expr &ir) {
    ScopedValue<bool> old(implicit_parens, false);
    begin_node(ir.get());
    ir.accept(this);
    end_node(ir.get());
}

void IRPrinter::print_no_parens(const Expr &ir) {
    ScopedValue<bool> old(implicit_parens, true);
    begin_node(ir.get());
    ir.accept(this);
    end_node(ir.get());
}

void IRPrinter::print(const Stmt &ir) {
    begin_node(ir.get());
    ir.accept(this);
    end_node(ir.get());
}

void IRPrinter::print_summary(const Stmt &ir) {
    ScopedValue<bool> old(is_summary, true);
    begin_node(ir.get());
    ir.accept(this);
    end_node(ir.get());
}

void IRPrinter::print(const Module &m) {
    print_module(stream, m, [&](const Stmt &s) {
        if (s.defined()) {
            print(s);
        } else {
            stream << s;
        }
    });
}

void IRPrinter::begin_node(const IRNode *node) {
    enclosing_locations.push_back(current_location);
    current_location = record_location(node);
}

void IRPrinter::end_node(const IRNode *) {
    end_location(current_location);
    current_location = enclosing_locations.back();
    enclosing_locations.pop_back();
}

int IRPrinter::record_location(const IRNode *node) {
    if (!recorded_locations) {
        return -1;
    }
    uint32_t at = (uint32_t)stream.tellp();
    recorded_locations->push_back({node, at, at, at});
    return (int)recorded_locations->size() - 1;
}

void IRPrinter::anchor() {
    if (recorded_locations && current_location >= 0) {
        (*recorded_locations)[current_location].anchor = (uint32_t)stream.tellp();
    }
}

void IRPrinter::end_location(int index) {
    if (recorded_locations && index >= 0) {
        (*recorded_locations)[index].end = (uint32_t)stream.tellp();
    }
}

StmtSourceMap print_with_source_map(const Module &m) {
    std::ostringstream stream;
    vector<IRPrinterLocation> locations;
    IRPrinter printer(stream);
    printer.recorded_locations = &locations;
    ModuleRecord record;
    print_module(stream, m, [&](const Stmt &s) {
        if (s.defined()) {
            printer.print(s);
        } else {
            stream << s;
        } }, &record);
    return make_source_map(std::move(stream).str(), locations, &m, &record);
}

StmtSourceMap print_with_source_map(const Stmt &s) {
    std::ostringstream stream;
    vector<IRPrinterLocation> locations;
    if (s.defined()) {
        IRPrinter printer(stream);
        printer.recorded_locations = &locations;
        printer.print(s);
    } else {
        stream << s;
    }
    return make_source_map(std::move(stream).str(), locations, nullptr, nullptr);
}

StmtSourceMap source_map_of(std::string text, const std::vector<IRPrinterLocation> &locations) {
    return make_source_map(std::move(text), locations, nullptr, nullptr);
}

const StmtSourceMap::Location *StmtSourceMap::find(const IRNode *node, uint32_t parent_start) const {
    auto it = std::lower_bound(locations.begin(), locations.end(), parent_start,
                               [&](const Location &l, uint32_t start) {
                                   return l.node != node ? std::less<>()(l.node, node) : l.start < start;
                               });
    if (it != locations.end() && it->node == node) {
        return &*it;
    }
    if (it != locations.begin() && (it - 1)->node == node) {
        return &*(it - 1);
    }
    return nullptr;
}

const StmtSourceMap::Location *StmtSourceMap::at(uint32_t line, uint32_t column) const {
    auto it = std::lower_bound(by_anchor.begin(), by_anchor.end(), std::make_pair(line, column),
                               [&](uint32_t i, const std::pair<uint32_t, uint32_t> &lc) {
                                   return std::make_pair(locations[i].line, locations[i].column) < lc;
                               });
    if (it != by_anchor.end() && locations[*it].line == line && locations[*it].column == column) {
        return &locations[*it];
    }
    return nullptr;
}

std::vector<const StmtSourceMap::Location *> map_locations(const StmtSourceMap &from, const StmtSourceMap &to) {
    // Walk the locations of from in the order they were printed, which visits
    // each node before the nodes nested in it.
    std::vector<uint32_t> order(from.locations.size());
    for (uint32_t i = 0; i < (uint32_t)order.size(); i++) {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(), [&](uint32_t i, uint32_t j) {
        const StmtSourceMap::Location &a = from.locations[i], &b = from.locations[j];
        return a.start != b.start ? a.start < b.start : a.end > b.end;
    });

    std::vector<const StmtSourceMap::Location *> result(from.locations.size(), nullptr);
    // The enclosing nodes of the current one.
    std::vector<uint32_t> stack;
    for (uint32_t i : order) {
        const StmtSourceMap::Location &l = from.locations[i];
        while (!stack.empty() && from.locations[stack.back()].end < l.end) {
            stack.pop_back();
        }
        const StmtSourceMap::Location *parent = stack.empty() ? nullptr : result[stack.back()];
        const StmtSourceMap::Location *found = to.find(l.node, parent ? parent->start : 0);
        result[i] = found ? found : parent;
        stack.push_back(i);
    }
    return result;
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
        begin_style(IRPrinterStyle::ImmInt, 0);
        stream << op->value << "_i" << op->type.bits();
        end_style(IRPrinterStyle::ImmInt, true);
    }
}

void IRPrinter::visit(const UIntImm *op) {
    if (op->type.bits() == 1) {
        begin_style(IRPrinterStyle::ImmInt, 0);
        stream << (op->value ? "true" : "false");
        end_style(IRPrinterStyle::ImmInt, true);
    } else {
        begin_style(IRPrinterStyle::ImmInt, 0);
        stream << op->value << "_u" << op->type.bits();
        end_style(IRPrinterStyle::ImmInt, true);
    }
}

void IRPrinter::visit(const FloatImm *op) {
    const bool use_scientific_format = (op->value != 0.0) && (std::log10(std::abs(op->value)) < -6);
    if (use_scientific_format) {
        stream << std::scientific;
    }

    begin_style(IRPrinterStyle::ImmFloat, 0);

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

    end_style(IRPrinterStyle::ImmFloat, true);

    if (use_scientific_format) {
        stream << std::fixed;
    }
}

void IRPrinter::visit(const StringImm *op) {
    begin_style(IRPrinterStyle::ImmString, 0);
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
    end_style(IRPrinterStyle::ImmString, true);
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
    anchor();
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
    anchor();
    stream << paren(" + ");
    print(op->b);
    close();
}

void IRPrinter::visit(const Sub *op) {
    open();
    print(op->a);
    anchor();
    stream << paren(" - ");
    print(op->b);
    close();
}

void IRPrinter::visit(const Mul *op) {
    open();
    print(op->a);
    anchor();
    stream << paren("*");
    print(op->b);
    close();
}

void IRPrinter::visit(const Div *op) {
    open();
    print(op->a);
    anchor();
    stream << paren("/");
    print(op->b);
    close();
}

void IRPrinter::visit(const Mod *op) {
    open();
    print(op->a);
    anchor();
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
    anchor();
    stream << paren(" == ");
    print(op->b);
    close();
}

void IRPrinter::visit(const NE *op) {
    open();
    print(op->a);
    anchor();
    stream << paren(" != ");
    print(op->b);
    close();
}

void IRPrinter::visit(const LT *op) {
    open();
    print(op->a);
    anchor();
    stream << paren(" < ");
    print(op->b);
    close();
}

void IRPrinter::visit(const LE *op) {
    open();
    print(op->a);
    anchor();
    stream << paren(" <= ");
    print(op->b);
    close();
}

void IRPrinter::visit(const GT *op) {
    open();
    print(op->a);
    anchor();
    stream << paren(" > ");
    print(op->b);
    close();
}

void IRPrinter::visit(const GE *op) {
    open();
    print(op->a);
    anchor();
    stream << paren(" >= ");
    print(op->b);
    close();
}

void IRPrinter::visit(const And *op) {
    open();
    print(op->a);
    anchor();
    stream << paren(" && ");
    print(op->b);
    close();
}

void IRPrinter::visit(const Or *op) {
    open();
    print(op->a);
    anchor();
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
    if (op->is_streaming) {
        stream << kw("streaming ");
    }
    if (!known_type.contains(op->name)) {
        stream << typep(op->type);
    }
    paren_depth++;
    anchor();
    stream << buf(op->name) << paren("[");
    print_no_parens(op->index);
    if (show_alignment) {
        begin_style(IRPrinterStyle::Dim, 0);
        stream << " aligned("
               << imm_int(op->alignment.modulus)
               << ", "
               << imm_int(op->alignment.remainder) << ")";
        end_style(IRPrinterStyle::Dim, true);
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
    begin_style(IRPrinterStyle::Keyword, 0);
    stream << "x" << op->lanes;
    stream << "(";
    end_style(IRPrinterStyle::Keyword, true);
    paren_depth++;
    print_no_parens(op->value);
    paren_depth--;
    stream << kw(")");
}

void IRPrinter::visit(const Call *op) {

    if (op->is_intrinsic(Call::bitwise_or)) {
        open();
        print(op->args[0]);
        anchor();
        stream << paren(" | ");
        print(op->args[1]);
        close();
    } else if (op->is_intrinsic(Call::bitwise_and)) {
        open();
        print(op->args[0]);
        anchor();
        stream << paren(" & ");
        print(op->args[1]);
        close();
    } else if (op->is_intrinsic(Call::bitwise_xor)) {
        open();
        print(op->args[0]);
        anchor();
        stream << paren(" ^ ");
        print(op->args[1]);
        close();
    } else if (op->is_intrinsic(Call::bitwise_not)) {
        stream << kw("~(");
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
        anchor();
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
    print_no_parens(op->max);
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
    begin_node(let);
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
    end_node(let);
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
    if (op->is_streaming) {
        stream << kw("streaming ");
    }
    stream << buf(op->name) << paren("[");
    print_no_parens(op->index);
    if (show_alignment) {
        begin_style(IRPrinterStyle::Dim, 0);
        stream << " aligned("
               << imm_int(op->alignment.modulus)
               << ", "
               << imm_int(op->alignment.remainder) << ")";
        end_style(IRPrinterStyle::Dim, true);
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
    // The ifs chained in as else cases, which end with the chain.
    std::vector<const IfThenElse *> chained_ifs;
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
            begin_node(nested_if);
            chained_ifs.push_back(nested_if);
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
    for (auto it = chained_ifs.rbegin(); it != chained_ifs.rend(); it++) {
        end_node(*it);
    }
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
    } else if (auto slice = op->is_slice()) {
        openf("slice_vectors");
        print_list(op->vectors);
        stream << paren(", ") << imm_int(slice->begin)
               << paren(", ") << imm_int(slice->stride)
               << paren(", ") << imm_int(op->indices.size());
    } else if (auto cols = op->is_transpose()) {
        openf("transpose_vector");
        print_list(op->vectors);
        stream << paren(", ") << imm_int(*cols);
    } else if (auto factor = op->is_repeat_each_lane()) {
        openf("repeat_each_lane");
        print_list(op->vectors);
        stream << paren(", ") << imm_int(*factor);
    } else if (auto factor = op->is_broadcast()) {
        openf("broadcast");
        print_list(op->vectors);
        stream << paren(", ") << imm_int(*factor);
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

void IRPrinter::visit(const StreamingStore *op) {
    stream << get_indent();
    stream << kw("streaming_store (") << op->producer_name << kw(") ");
    print_braced_stmt(op->body);
}

void IRPrinter::visit(const StreamingLoads *op) {
    stream << get_indent();
    if (!op->names) {
        stream << kw("streaming_loads (") << kw("all") << kw(") ");
    } else {
        stream << kw("streaming_loads (");
        for (size_t i = 0; i < op->names->size(); i++) {
            if (i > 0) {
                stream << kw(", ");
            }
            stream << (*op->names)[i];
        }
        stream << kw(") ");
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

namespace {
// Render any type that has an operator<< into a std::string. The public
// debug_string overloads below are deliberately concrete so their symbols are
// emitted into libHalide and stay callable from a debugger.
template<typename T>
std::string stream_to_string(const T &value) {
    std::stringstream s{};
    s << value;
    return s.str();
}
}  // namespace

std::string debug_string(const Expr &ir) {
    std::stringstream s{};
    IRPrinter p(s);
    p.print_no_parens(ir);
    return s.str();
}

std::string debug_string(const BaseExprNode *n) {
    return debug_string(Expr(n));
}

std::string debug_string(const Stmt &ir) {
    std::stringstream s{};
    IRPrinter p(s);
    p.print_summary(ir);
    return s.str();
}

std::string debug_string(const BaseStmtNode *n) {
    return debug_string(Stmt(n));
}

std::string debug_string(const Type &t) {
    return stream_to_string(t);
}

std::string debug_string(const Target &t) {
    return stream_to_string(t);
}

std::string debug_string(const Module &m) {
    return stream_to_string(m);
}

std::string debug_string(const Tuple &t) {
    return stream_to_string(t);
}

std::string debug_string(const Interval &i) {
    return stream_to_string(i);
}

std::string debug_string(const ConstantInterval &i) {
    return stream_to_string(i);
}

std::string debug_string(const ModulusRemainder &m) {
    return stream_to_string(m);
}

std::string debug_string(const LoweredFunc &f) {
    return stream_to_string(f);
}

}  // namespace Internal
}  // namespace Halide
