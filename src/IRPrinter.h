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

#include <ostream>

#include "IRVisitor.h"
#include "Module.h"
#include "Scope.h"

namespace Halide {

/** Emit an expression on an output stream (such as std::cout) in
 * human-readable form */
std::ostream &operator<<(std::ostream &stream, const Expr &);

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
enum class IRNodeType;

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
    if (a.open) out << a.open;
    out << a.cnt;
    if (a.close) out << a.close;
    return out;
}

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

    static void test();

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
    void visit(const HoistedStorage *) override;
};

/** Debugging helpers for LLDB */
/// @{
std::string lldb_string(const Expr &);
std::string lldb_string(const Internal::BaseExprNode *);
std::string lldb_string(const Stmt &);
/// @}

}  // namespace Internal
}  // namespace Halide

#endif
