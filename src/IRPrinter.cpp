#include <algorithm>
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

ostream &operator<<(ostream &stream, const Module &m) {
    Internal::IRPrinter(stream).print(m);
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
    IRPrinter(stream).print(ir);
    return stream;
}

ostream &operator<<(ostream &stream, const LoweredFunc &function) {
    IRPrinter(stream).print(function);
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

        ansi = use_colors;
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

#define styled_helper(name, style)               \
    template<typename T>                         \
    StyledText<T> IRPrinter::name(const T &t) {  \
        return {t, this, IRPrinterStyle::style}; \
    }

styled_helper(hl, Highlight);
styled_helper(kw, Keyword);
styled_helper(imm_int, ImmInt);
styled_helper(imm_float, ImmFloat);
styled_helper(imm_str, ImmString);
styled_helper(buf, Buffer);
styled_helper(fn, Function);
styled_helper(type, Type);

template<typename T>
StyledText<T> IRPrinter::var(const T &t) {
    // The variables of the loops over GPU threads stand out.
    IRPrinterStyle style = IRPrinterStyle::Var;
    if (t == ".thread_id_x") {
        style = IRPrinterStyle::GPUThreadX;
    } else if (t == ".thread_id_y") {
        style = IRPrinterStyle::GPUThreadY;
    } else if (t == ".thread_id_z") {
        style = IRPrinterStyle::GPUThreadZ;
    }
    return {t, this, style};
}

template<typename T>
StyledText<T> IRPrinter::paren(const T &t, bool bold) {
    return {t, this, bold ? IRPrinterStyle::BoldParen : IRPrinterStyle::Paren};
}

void IRPrinter::begin_style(IRPrinterStyle style) const {
    if (!ansi) {
        return;
    }
    // A simple palette using standard VGA colors, indexed by IRPrinterStyle.
    // clang-format off
    static const char *const styles[] = {
        "\033[4m",     // Highlight
        "\033[2m",     // Dim
        "\033[35;1m",  // Keyword
        "\033[36m",    // ImmInt
        "\033[96m",    // ImmFloat
        "\033[32m",    // ImmString
        "",            // Var
        "\033[33m",    // Buffer
        "\033[31m",    // Function
        "\033[34m",    // Type
        "\033[31m",    // GPUThreadX
        "\033[32m",    // GPUThreadY
        "\033[34m",    // GPUThreadZ
    };
    static const char *const parens[2][6] = {
        {"\033[91m", "\033[92m", "\033[93m", "\033[94m", "\033[95m", "\033[96m"},
        {"\033[91;1m", "\033[92;1m", "\033[93;1m", "\033[94;1m", "\033[95;1m", "\033[96;1m"}};
    // clang-format on
    if (style == IRPrinterStyle::Paren || style == IRPrinterStyle::BoldParen) {
        stream << parens[style == IRPrinterStyle::BoldParen][paren_depth % 6];
    } else {
        stream << styles[(int)style];
    }
}

void IRPrinter::end_style(IRPrinterStyle style) const {
    if (!ansi) {
        return;
    }
    // Immediates and names only have a color, and can be within text that is
    // styled otherwise. The other styles end by resetting all attributes.
    const bool color_only = style >= IRPrinterStyle::ImmInt && style < IRPrinterStyle::Paren;
    stream << (color_only ? "\033[39m" : "\033[0m");
}

void IRPrinter::print_type_in_parens(const Type &t) {
    begin_style(IRPrinterStyle::Type);
    stream << "(" << t << ")";
    end_style(IRPrinterStyle::Type);
}

void IRPrinter::print_node(const IRNode *node) {
    begin_node(node);
    node->accept(this);
    end_node(node);
}

void IRPrinter::print(const Expr &ir) {
    ScopedValue<bool> old(implicit_parens, false);
    print_node(ir.get());
}

void IRPrinter::print_no_parens(const Expr &ir) {
    ScopedValue<bool> old(implicit_parens, true);
    print_node(ir.get());
}

void IRPrinter::print(const Stmt &ir) {
    if (!ir.defined()) {
        stream << "(undefined)\n";
        return;
    }
    print_node(ir.get());
}

void IRPrinter::print_summary(const Stmt &ir) {
    ScopedValue<bool> old(is_summary, true);
    print_node(ir.get());
}

void IRPrinter::print(const LoweredFunc &function) {
    if (record) {
        record->function_starts.push_back((uint32_t)stream.tellp());
    }
    stream << function.linkage << " func " << function.name << " (";
    for (size_t i = 0; i < function.args.size(); i++) {
        stream << function.args[i].name;
        if (i + 1 < function.args.size()) {
            stream << ", ";
        }
    }
    stream << ") {\n";
    print(function.body);
    stream << "}\n\n";
}

void IRPrinter::print(const Module &m) {
    for (const auto &s : m.submodules()) {
        print(s);
        stream << "\n";
    }

    // The module retains implied features, but print it in minimal form.
    stream << "module name=" << m.name() << ", target=" << m.target().without_implied_features().to_string() << "\n";
    for (const auto &b : m.buffers()) {
        stream << b << "\n";
    }
    for (const auto &f : m.functions()) {
        print(f);
        stream << "\n";
    }
}

void IRPrinter::begin_node(const IRNode *node) {
    if (record) {
        const uint32_t at = (uint32_t)stream.tellp();
        record->locations.push_back({node, at, at, at, current_location});
        current_location = (int)record->locations.size() - 1;
    }
}

void IRPrinter::end_node(const IRNode *) {
    if (record) {
        IRPrinterRecord::Location &l = record->locations[current_location];
        l.end = (uint32_t)stream.tellp();
        current_location = l.parent;
    }
}

void IRPrinter::anchor() {
    if (record && current_location >= 0) {
        record->locations[current_location].anchor = (uint32_t)stream.tellp();
    }
}

namespace {

// Turn the locations recorded while printing text into a source map of it.
// If the text is that of a Module, m is set.
StmtSourceMap make_source_map(string text, const IRPrinterRecord &record, const Module *m) {
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

    result.locations.resize(record.locations.size());
    for (size_t i = 0; i < record.locations.size(); i++) {
        const IRPrinterRecord::Location &r = record.locations[i];
        StmtSourceMap::Location &l = result.locations[i];
        l.node = r.node;
        l.start = r.start;
        l.end = r.end;
        l.order = (uint32_t)i + 1;
        line_and_column(r.anchor, l.line, l.column);
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

    if (m) {
        // The functions of submodules are printed first, so those of the
        // Module itself are the last ones.
        const size_t n = m->functions().size();
        internal_assert(record.function_starts.size() >= n);
        result.functions.resize(n);
        for (size_t i = 0; i < n; i++) {
            StmtSourceMap::Function &f = result.functions[i];
            f.name = m->functions()[i].name;
            f.start = record.function_starts[record.function_starts.size() - n + i];
            line_and_column(f.start, f.line, f.column);
        }
    }
    return result;
}

}  // namespace

StmtSourceMap print_with_source_map(const Module &m) {
    std::ostringstream stream;
    IRPrinterRecord record;
    IRPrinter printer(stream);
    printer.record = &record;
    printer.print(m);
    return make_source_map(std::move(stream).str(), record, &m);
}

StmtSourceMap print_with_source_map(const Stmt &s) {
    std::ostringstream stream;
    IRPrinterRecord record;
    IRPrinter printer(stream);
    printer.record = &record;
    printer.print(s);
    return make_source_map(std::move(stream).str(), record, nullptr);
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
        begin_style(IRPrinterStyle::ImmInt);
        stream << op->value << "_i" << op->type.bits();
        end_style(IRPrinterStyle::ImmInt);
    }
}

void IRPrinter::visit(const UIntImm *op) {
    if (op->type.bits() == 1) {
        begin_style(IRPrinterStyle::ImmInt);
        stream << (op->value ? "true" : "false");
        end_style(IRPrinterStyle::ImmInt);
    } else {
        begin_style(IRPrinterStyle::ImmInt);
        stream << op->value << "_u" << op->type.bits();
        end_style(IRPrinterStyle::ImmInt);
    }
}

void IRPrinter::visit(const FloatImm *op) {
    const bool use_scientific_format = (op->value != 0.0) && (std::log10(std::abs(op->value)) < -6);
    if (use_scientific_format) {
        stream << std::scientific;
    }

    begin_style(IRPrinterStyle::ImmFloat);

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

    end_style(IRPrinterStyle::ImmFloat);

    if (use_scientific_format) {
        stream << std::fixed;
    }
}

void IRPrinter::visit(const StringImm *op) {
    begin_style(IRPrinterStyle::ImmString);
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
    end_style(IRPrinterStyle::ImmString);
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
            print_type_in_parens(op->type);
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

void IRPrinter::print_binary(const Expr &a, const char *op, const Expr &b) {
    open();
    print(a);
    anchor();
    stream << paren(op);
    print(b);
    close();
}

void IRPrinter::visit(const Add *op) {
    print_binary(op->a, " + ", op->b);
}

void IRPrinter::visit(const Sub *op) {
    print_binary(op->a, " - ", op->b);
}

void IRPrinter::visit(const Mul *op) {
    print_binary(op->a, "*", op->b);
}

void IRPrinter::visit(const Div *op) {
    print_binary(op->a, "/", op->b);
}

void IRPrinter::visit(const Mod *op) {
    print_binary(op->a, " % ", op->b);
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
    print_binary(op->a, " == ", op->b);
}

void IRPrinter::visit(const NE *op) {
    print_binary(op->a, " != ", op->b);
}

void IRPrinter::visit(const LT *op) {
    print_binary(op->a, " < ", op->b);
}

void IRPrinter::visit(const LE *op) {
    print_binary(op->a, " <= ", op->b);
}

void IRPrinter::visit(const GT *op) {
    print_binary(op->a, " > ", op->b);
}

void IRPrinter::visit(const GE *op) {
    print_binary(op->a, " >= ", op->b);
}

void IRPrinter::visit(const And *op) {
    print_binary(op->a, " && ", op->b);
}

void IRPrinter::visit(const Or *op) {
    print_binary(op->a, " || ", op->b);
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
        print_type_in_parens(op->type);
    }
    paren_depth++;
    anchor();
    stream << buf(op->name) << paren("[");
    print_no_parens(op->index);
    if (show_alignment) {
        begin_style(IRPrinterStyle::Dim);
        stream << " aligned("
               << imm_int(op->alignment.modulus)
               << ", "
               << imm_int(op->alignment.remainder) << ")";
        end_style(IRPrinterStyle::Dim);
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
    begin_style(IRPrinterStyle::Keyword);
    stream << "x" << op->lanes;
    stream << "(";
    end_style(IRPrinterStyle::Keyword);
    paren_depth++;
    print_no_parens(op->value);
    paren_depth--;
    stream << kw(")");
}

void IRPrinter::visit(const Call *op) {

    if (op->is_intrinsic(Call::bitwise_or)) {
        print_binary(op->args[0], " | ", op->args[1]);
    } else if (op->is_intrinsic(Call::bitwise_and)) {
        print_binary(op->args[0], " & ", op->args[1]);
    } else if (op->is_intrinsic(Call::bitwise_xor)) {
        print_binary(op->args[0], " ^ ", op->args[1]);
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
                print_type_in_parens(op->type);
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
        begin_style(IRPrinterStyle::Dim);
        stream << " aligned("
               << imm_int(op->alignment.modulus)
               << ", "
               << imm_int(op->alignment.remainder) << ")";
        end_style(IRPrinterStyle::Dim);
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
    print_if_chain(op);
}

void IRPrinter::print_if_chain(const IfThenElse *op) {
    stream << paren("if (");
    print_no_parens(op->condition);
    stream << paren(") {\n");
    paren_depth++;
    indent++;
    print(op->then_case);
    indent--;
    paren_depth--;

    if (const IfThenElse *nested_if = op->else_case.as<IfThenElse>()) {
        stream << get_indent() << paren("} else ");
        begin_node(nested_if);
        print_if_chain(nested_if);
        end_node(nested_if);
        return;
    }
    if (op->else_case.defined()) {
        stream << get_indent() << paren("}") << (" else {\n");
        paren_depth++;
        indent++;
        print(op->else_case);
        indent--;
        paren_depth--;
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
    print_type_in_parens(op->type);
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
