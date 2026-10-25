#ifndef HALIDE_IR_PRINTER_H
#define HALIDE_IR_PRINTER_H

/** \file
 * This header file defines operators that let you dump a Halide
 * expression, statement, or type directly into an output stream
 * in a human readable form.
 * E.g:
 \code
 Expr foo = ...
 std::cout << "Foo is " << foo << "\n";
 \endcode
 *
 * These operators are implemented using \ref Halide::Internal::IRPrinter
 */

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "IRVisitor.h"
#include "Module.h"
#include "Scope.h"

namespace Halide {

/** Emit an expression on an output stream (such as std::cout) in
 * human-readable form */
std::ostream &operator<<(std::ostream &stream, const Expr &);

/** Emit a tuple on an output stream (such as std::cout) in
 * human-readable form */
std::ostream &operator<<(std::ostream &stream, const Tuple &);

/** Emit a halide type on an output stream (such as std::cout) in
 * human-readable form */
std::ostream &operator<<(std::ostream &stream, const Type &);

/** Emit a halide Module on an output stream (such as std::cout) in
 * human-readable form */
std::ostream &operator<<(std::ostream &stream, const Module &);

/** Emit a halide device api type in human-readable form */
std::ostream &operator<<(std::ostream &stream, const DeviceAPI &);

/** Emit a halide memory type in human-readable form */
std::ostream &operator<<(std::ostream &stream, const MemoryType &);

/** Emit a halide tail strategy in human-readable form */
std::ostream &operator<<(std::ostream &stream, const TailStrategy &);

/** Emit a halide loop partitioning policy in human-readable form */
std::ostream &operator<<(std::ostream &stream, const Partition &);

/** Emit a halide LoopLevel in human-readable form */
std::ostream &operator<<(std::ostream &stream, const LoopLevel &);

struct Target;
/** Emit a halide Target in a human readable form */
std::ostream &operator<<(std::ostream &stream, const Target &);

namespace Internal {

struct AssociativePattern;
struct AssociativeOp;
class Closure;
struct Interval;
struct ConstantInterval;
struct ModulusRemainder;
enum class IRNodeType : uint8_t;

/** Emit a halide node type on an output stream (such as std::cout) in
 * human-readable form */
std::ostream &operator<<(std::ostream &stream, IRNodeType);

/** Emit a halide associative pattern on an output stream (such as std::cout)
 * in a human-readable form */
std::ostream &operator<<(std::ostream &stream, const AssociativePattern &);

/** Emit a halide associative op on an output stream (such as std::cout) in a
 * human-readable form */
std::ostream &operator<<(std::ostream &stream, const AssociativeOp &);

/** Emit a halide statement on an output stream (such as std::cout) in
 * a human-readable form */
std::ostream &operator<<(std::ostream &stream, const Stmt &);

/** Emit a halide for loop type (vectorized, serial, etc) in a human
 * readable form */
std::ostream &operator<<(std::ostream &stream, const ForType &);

/** Emit a horizontal vector reduction op in human-readable form. */
std::ostream &operator<<(std::ostream &stream, const VectorReduce::Operator &);

/** Emit a halide name mangling value in a human readable format */
std::ostream &operator<<(std::ostream &stream, const NameMangling &);

/** Emit a halide LoweredFunc in a human readable format */
std::ostream &operator<<(std::ostream &stream, const LoweredFunc &);

/** Emit a halide linkage value in a human readable format */
std::ostream &operator<<(std::ostream &stream, const LinkageType &);

/** Emit a halide dimension type in human-readable format */
std::ostream &operator<<(std::ostream &stream, const DimType &);

/** Emit a Closure in human-readable form */
std::ostream &operator<<(std::ostream &out, const Closure &c);

/** Emit an Interval in human-readable form */
std::ostream &operator<<(std::ostream &out, const Interval &c);

/** Emit a ConstantInterval in human-readable form */
std::ostream &operator<<(std::ostream &out, const ConstantInterval &c);

/** Emit a ModulusRemainder in human-readable form */
std::ostream &operator<<(std::ostream &out, const ModulusRemainder &c);

struct Indentation {
    int indent;
};
std::ostream &operator<<(std::ostream &stream, const Indentation &);

template<typename T>
struct Ansi {
    const T &cnt;
    const char *open, *close;
};

template<typename T>
std::ostream &operator<<(std::ostream &out, const Ansi<T> &a) {
    if (a.open) {
        out << a.open;
    }
    out << a.cnt;
    if (a.close) {
        out << a.close;
    }
    return out;
}

/** Where an IRPrinter printed an IR node, as byte offsets into its stream. */
struct IRPrinterLocation {
    const IRNode *node;
    /** Where printing of the node began. */
    uint32_t start;
    /** The node's most telling character, e.g. the operator of a binary
     * op. Equal to start for most nodes. */
    uint32_t anchor;
    /** Where printing of the node ended. */
    uint32_t end;
};

/** The text of a Module or Stmt exactly as operator<< prints it (minus any
 * ANSI colors), together with where each IR node in it ended up in that text.
 * Debug info and the HTML outputs use this to point into the .stmt files. */
struct StmtSourceMap {
    struct Location {
        const IRNode *node;
        /** Byte offsets at which printing of the node began and ended. A node
         * shared by several parents is printed once per parent, this tells
         * the copies apart. */
        uint32_t start, end;
        /** 1-based line and column of the node's anchor. */
        uint32_t line, column;
        /** 1-based line of the last character of the node, e.g. its closing
         * brace. */
        uint32_t end_line;
    };

    /** A piece of text that IRPrinter styles, e.g. a keyword. */
    struct Token {
        uint32_t start, end;
        /** Which of IRPrinter's styles: 'k'eyword, 'v'ariable, 'b'uffer,
         * 'n'ame of a function, 't'ype, 'i'nteger, 'f'loat, 's'tring,
         * 'h'ighlight, 'd'immed, or 'p'unctuation and operators, which
         * are 'P' when bold. */
        char style;
        /** For punctuation and operators: the nesting depth, modulo 6. */
        uint8_t depth;
    };

    struct Function {
        std::string name;
        /** Byte offset of the function header. */
        uint32_t start;
        /** 1-based lines of the function header and its closing brace. */
        uint32_t line, end_line;
    };

    std::string text;

    /** The file debug info refers to. The text is expected to be written
     * there by whoever sets it. */
    std::string path;

    /** Sorted by node, then by start. */
    std::vector<Location> locations;

    /** In Module::functions() order. Empty when printing a Stmt. */
    std::vector<Function> functions;

    /** 1-based line of each buffer, in Module::buffers() order. */
    std::vector<uint32_t> buffer_lines;

    /** Sorted by start. Tokens can contain one another, but never contain
     * the start or end of a node they are not part of. */
    std::vector<Token> tokens;

    /** Find the printed copy of node that sits under a parent which began
     * printing at parent_start: the first copy at or after parent_start, or
     * the last copy before it if there is none. Returns nullptr if the node
     * was never printed. */
    const Location *find(const IRNode *node, uint32_t parent_start) const;

    /** The location with the given anchor. Of nested nodes that share one,
     * the innermost. Returns nullptr if there is none. */
    const Location *at(uint32_t line, uint32_t column) const;

    /** Indices into locations, sorted by line and column, for at(). */
    std::vector<uint32_t> by_anchor;
};

/** Print a Module the same way operator<< does, without colors, and record
 * where all IR nodes of its functions went. */
StmtSourceMap print_with_source_map(const Module &m);

/** Print a Stmt the same way operator<< does, without colors, and record
 * where all of its IR nodes went. */
StmtSourceMap print_with_source_map(const Stmt &s);

/** For each location of from, in from.locations order: the location in to of
 * the same IR node, or else that of its nearest enclosing node that to has, or
 * else nullptr. Nodes are printed in both when to is a printing of IR that
 * from's IR was lowered from, e.g. the conceptual Stmt of a Module. */
std::vector<const StmtSourceMap::Location *> map_locations(const StmtSourceMap &from, const StmtSourceMap &to);

/** An IRVisitor that emits IR to the given output stream in a human
 * readable form. Can be subclassed if you want to modify the way in
 * which it prints.
 */
class IRPrinter : public IRVisitor {
public:
    /** Construct an IRPrinter pointed at a given output stream
     * (e.g. std::cout, or a std::ofstream) */
    explicit IRPrinter(std::ostream &);

    /** emit an expression on the output stream */
    void print(const Expr &);

    /** Emit an expression on the output stream without enclosing parens */
    void print_no_parens(const Expr &);

    /** emit a statement on the output stream */
    void print(const Stmt &);

    /** emit a statement summary on the output stream */
    void print_summary(const Stmt &);

    /** emit a comma delimited list of exprs, without any leading or
     * trailing punctuation. */
    void print_list(const std::vector<Expr> &exprs);

protected:
    Indentation get_indent() const {
        return Indentation{indent};
    }

    /** The stream on which we're outputting */
    std::ostream &stream;

    /** The current indentation level, useful for pretty-printing
     * statements */
    int indent = 0;

    /** Certain expressions do not need parens around them, e.g. the
     * args to a call are already separated by commas and a
     * surrounding set of parens. */
    bool implicit_parens = false;

    /** Print only a summary of a statement, with sub-statements replaced by
     * ellipses (...). */
    bool is_summary = false;

    bool ansi = false;
    int paren_depth = 0;

    /** Instead of ANSI escape codes, mark styled text with '\x01', a style
     * character (see StmtSourceMap::Token), and for punctuation a depth
     * digit, up to a closing '\x02'. */
    bool markup = false;

    /** If set, every node printed through print() or print_lets() gets an
     * entry here. */
    std::vector<IRPrinterLocation> *recorded_locations = nullptr;

    /** The index in recorded_locations of the node being printed, or -1. */
    int current_location = -1;

    /** Add an entry for the given node to recorded_locations, starting at the
     * current stream position. Returns its index, or -1 if not recording. */
    int record_location(const IRNode *node);

    /** Mark the current stream position as the anchor of the node being
     * printed. */
    void anchor();

    /** Mark the current stream position as the end of the node with the
     * given index in recorded_locations. */
    void end_location(int index);

    const char *ansi_hl = "";
    const char *ansi_dim = "";
    const char *ansi_kw = "";
    const char *ansi_imm_int = "";
    const char *ansi_imm_float = "";
    const char *ansi_imm_str = "";
    const char *ansi_var = "";
    const char *ansi_buf = "";
    const char *ansi_fn = "";
    const char *ansi_type = "";
    const char *ansi_reset_col = "";
    const char *ansi_reset = "";

    // clang-format off
    template<typename T> Ansi<T> hl(const T &t);
    template<typename T> Ansi<T> kw(const T &t);
    template<typename T> Ansi<T> imm_int(const T &t);
    template<typename T> Ansi<T> imm_float(const T &t);
    template<typename T> Ansi<T> imm_str(const T &t);
    template<typename T> Ansi<T> var(const T &t);
    template<typename T> Ansi<T> buf(const T &t);
    template<typename T> Ansi<T> fn(const T &t);
    template<typename T> Ansi<T> type(const T &t);
    template<typename T> Ansi<T> typep(const T &t);
    template<typename T> Ansi<T> paren(const T &t, bool bold = true, int d = -1);
    // clang-format on

    /** Either emits "(" or "", depending on the value of implicit_parens */
    void open();

    /** Either emits ")" or "", depending on the value of implicit_parens */
    void close();

    /** Emits "(" always */
    void openf();

    /** Emits "name(" always */
    void openf(const char *name);

    /** Emits ")" always */
    void closef();

    /** The symbols whose types can be inferred from values printed
     * already. */
    Scope<> known_type;

    /** A helper for printing a chain of lets with line breaks */
    void print_lets(const Let *let);

    /** A helper for printing a braced statement */
    void print_braced_stmt(const Stmt &, int extra_indent = 2);

    void visit(const IntImm *) override;
    void visit(const UIntImm *) override;
    void visit(const FloatImm *) override;
    void visit(const StringImm *) override;
    void visit(const Cast *) override;
    void visit(const Reinterpret *) override;
    void visit(const Variable *) override;
    void visit(const Add *) override;
    void visit(const Sub *) override;
    void visit(const Mul *) override;
    void visit(const Div *) override;
    void visit(const Mod *) override;
    void visit(const Min *) override;
    void visit(const Max *) override;
    void visit(const EQ *) override;
    void visit(const NE *) override;
    void visit(const LT *) override;
    void visit(const LE *) override;
    void visit(const GT *) override;
    void visit(const GE *) override;
    void visit(const And *) override;
    void visit(const Or *) override;
    void visit(const Not *) override;
    void visit(const Select *) override;
    void visit(const Load *) override;
    void visit(const Ramp *) override;
    void visit(const Broadcast *) override;
    void visit(const Call *) override;
    void visit(const Let *) override;
    void visit(const LetStmt *) override;
    void visit(const AssertStmt *) override;
    void visit(const ProducerConsumer *) override;
    void visit(const For *) override;
    void visit(const Acquire *) override;
    void visit(const Store *) override;
    void visit(const Provide *) override;
    void visit(const Allocate *) override;
    void visit(const Free *) override;
    void visit(const Realize *) override;
    void visit(const Block *) override;
    void visit(const Fork *) override;
    void visit(const IfThenElse *) override;
    void visit(const Evaluate *) override;
    void visit(const Shuffle *) override;
    void visit(const VectorReduce *) override;
    void visit(const Prefetch *) override;
    void visit(const Atomic *) override;
    void visit(const StreamingStore *) override;
    void visit(const StreamingLoads *) override;
    void visit(const HoistedStorage *) override;
};

/** Debugging helpers for interactive debuggers (LLDB/GDB).
 *
 * These render a value with the IR printer and return the resulting string, so
 * a debugger's pretty-printer can obtain a human-readable summary by evaluating
 * e.g. `Halide::Internal::debug_string(*this)` in the inferior. Keep these as
 * concrete (non-template) overloads so the symbols are emitted into libHalide
 * and remain callable from a debugger even in a build with no other caller. */
/// @{
std::string debug_string(const Expr &);
std::string debug_string(const BaseExprNode *);
std::string debug_string(const Stmt &);
std::string debug_string(const BaseStmtNode *);
std::string debug_string(const Type &);
std::string debug_string(const Target &);
std::string debug_string(const Module &);
std::string debug_string(const Tuple &);
std::string debug_string(const Interval &);
std::string debug_string(const ConstantInterval &);
std::string debug_string(const ModulusRemainder &);
std::string debug_string(const LoweredFunc &);
/// @}

}  // namespace Internal
}  // namespace Halide

#endif
