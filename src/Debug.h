#ifndef HALIDE_DEBUG_H
#define HALIDE_DEBUG_H

/** \file
 * Defines functions for debug logging during code generation.
 */

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace Halide {

struct Expr;
class Tuple;
struct Type;
// Forward declare some things from IRPrinter, which we can't include yet.
std::ostream &operator<<(std::ostream &stream, const Expr &);
std::ostream &operator<<(std::ostream &stream, const Tuple &);
std::ostream &operator<<(std::ostream &stream, const Type &);

class Module;
std::ostream &operator<<(std::ostream &stream, const Module &);

struct Target;
/** Emit a halide Target in a human readable form */
std::ostream &operator<<(std::ostream &stream, const Target &);

namespace Internal {

struct Stmt;
std::ostream &operator<<(std::ostream &stream, const Stmt &);

struct LoweredFunc;
std::ostream &operator<<(std::ostream &, const LoweredFunc &);

bool debug_is_active_impl(int verbosity, const char *file, const char *function, int line);
bool debug_is_active_impl(int verbosity, const char *tag, const char *file, const char *function, int line);

/** Ask an arbitrary std::ostream whether it is a DebugStream and,
 * if so, where the output is being routed to. */
enum class DebugStreamSink {
    None,
    Cout,
    Cerr,
    File,
};
DebugStreamSink debug_stream_sink(std::ostream &os);

/** Backs the debug() macro. Buffers everything written to it in memory, then
 * emits the whole statement's accumulated output as a single write when the
 * temporary is destroyed (i.e. at the end of the debug(n) << ...; statement).
 * This matters when HL_DEBUG_CODEGEN_LOG_FILE names a real file shared by
 * multiple processes: writing statement-by-statement in one shot (rather
 * than incrementally, as each `<<` arrives) keeps one process's debug dump
 * from being interleaved mid-line with another's. Not for direct use --
 * use the debug(n) macro. */
class DebugStream : public std::ostringstream {
public:
    DebugStream();
    ~DebugStream() override;

    /** Exposes this object as a plain std::ostream&, so every `<<` in a
     * debug(n) statement resolves exactly as it would against std::cerr,
     * rather than against DebugStream itself: some standard library
     * implementations' rvalue-stream-insertion operator<< deduces the
     * *exact* runtime type of its left operand, and the SFINAE trait it
     * uses to test "is this ostreamable" can fail to find free-function
     * operator<< overloads (e.g. for Halide's own IR types) once that type
     * is something other than std::ostream itself. */
    std::ostream &stream() {
        return *this;
    }
};

/** For optional debugging during codegen, use the debug macro as
 * follows:
 *
 * \code
 * debug(verbosity) << "The expression is " << expr << "\n";
 * \endcode
 *
 * verbosity of 0 always prints, 1 should print after every major
 * stage, 2 should be used for more detail, and 3 should be used for
 * tracing everything that occurs. The verbosity with which to print
 * is determined by the value of the environment variable
 * HL_DEBUG_CODEGEN. Output goes to stderr by default, but can be
 * redirected via HL_DEBUG_CODEGEN_LOG_FILE (see DebugStream above).
 *
 * A call site can optionally be given a stable tag, independent of its
 * file/line, so it can be selected on its own via
 * HL_DEBUG_CODEGEN=tag:my-tag (or a comma-separated
 * HL_DEBUG_CODEGEN=tag:my-tag,other-tag to select either) regardless of the
 * configured verbosity:
 *
 * \code
 * debug(verbosity, "my-tag") << "The expression is " << expr << "\n";
 * \endcode
 */

#define debug(...)                                   \
    /* NOLINTNEXTLINE(bugprone-macro-parentheses) */ \
    if (::Halide::Internal::debug_is_active_impl(__VA_ARGS__, __FILE__, __FUNCTION__, __LINE__)) ::Halide::Internal::DebugStream().stream()

/** Allow easily printing the contents of containers, or std::vector-like containers,
 *  in debug output. Used like so:
 *        std::vector<Type> arg_types;
 *        debug(4) << "arg_types: " << PrintSpan(arg_types) << "\n";
 * Which results in output like "arg_types: { uint8x8, uint8x8 }" on one line. */
template<typename T>
struct PrintSpan {
    const T &span;
    PrintSpan(const T &span)
        : span(span) {
    }
};
// Class template argument deduction (CTAD) guide to prevent warnings.
template<typename T>
PrintSpan(const T &) -> PrintSpan<T>;

template<typename StreamT, typename T>
inline StreamT &operator<<(StreamT &stream, const PrintSpan<T> &wrapper) {
    stream << "{ ";
    const char *sep = "";
    for (const auto &e : wrapper.span) {
        stream << sep << e;
        sep = ", ";
    }
    stream << " }";
    return stream;
}

/** Allow easily printing the contents of spans, or std::vector-like spans,
 *  in debug output. Used like so:
 *        std::vector<Type> arg_types;
 *        debug(4) << "arg_types: " << PrintSpan(arg_types) << "\n";
 * Which results in output like:
 *     arg_types:
 *     {
 *             uint8x8,
 *             uint8x8,
 *     }
 * Indentation uses a tab character. */
template<typename T>
struct PrintSpanLn {
    const T &span;
    PrintSpanLn(const T &span)
        : span(span) {
    }
};
// Class template argument deduction (CTAD) guide to prevent warnings.
template<typename T>
PrintSpanLn(const T &) -> PrintSpanLn<T>;

template<typename StreamT, typename T>
inline StreamT &operator<<(StreamT &stream, const PrintSpanLn<T> &wrapper) {
    stream << "\n{\n";
    for (const auto &e : wrapper.span) {
        stream << "\t" << e << ",\n";
    }
    stream << "}\n";
    return stream;
}

/** Internal-only accessor for test/correctness/debug_helpers.cpp.
 * debug_is_active_impl() only ever parses HL_DEBUG_CODEGEN once (cached in a
 * function-local static), so it can't be exercised against multiple specs
 * from a single process; this re-parses `spec` fresh on every call using the
 * same grammar, so the parser/matcher can be unit tested directly. */
bool debug_spec_accepts(const std::string &spec, int verbosity, const char *tag,
                        const char *file, const char *function, int line);

}  // namespace Internal
}  // namespace Halide

#endif
