#include "Debug.h"
#include "Error.h"
#include "Util.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <functional>
#include <iostream>
#include <optional>
#include <variant>

#include <fcntl.h>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace Halide::Internal {

namespace {

std::string read_until(const char *&str, const char *delims) {
    const char *start = str;
    for (; *str; ++str) {
        for (const char *ch = delims; *ch; ++ch) {
            if (*str == *ch) {
                return {start, str};
            }
        }
    }
    return {start, str};
}

bool parse_int(const std::string &number, int &value) {
    const char *start = number.c_str();
    char *end;
    value = static_cast<int>(strtol(start, &end, 10));
    return start < end && *end == '\0';
}

class DebugRule {
    int verbosity = 0;
    std::string file_suffix = "";
    int line_low = -1;
    int line_high = INT_MAX;
    std::string function_suffix = "";
    bool is_tag_rule = false;
    std::vector<std::string> tags;
    enum Complexity { VerbosityOnly,
                      NeedsMatching } complexity = VerbosityOnly;

public:
    static std::optional<DebugRule> parse(const std::string &spec) {
        static constexpr char tag_prefix[] = "tag:";
        if (spec.rfind(tag_prefix, 0) == 0) {
            DebugRule rule;
            rule.is_tag_rule = true;
            for (std::string &tag : split_string(spec.substr(sizeof(tag_prefix) - 1), ",")) {
                if (!tag.empty()) {
                    rule.tags.push_back(std::move(tag));
                }
            }
            if (rule.tags.empty()) {
                return std::nullopt;
            }
            return rule;
        }

        DebugRule rule;
        const char *ptr = spec.c_str();

        if (!parse_int(read_until(ptr, ",@"), rule.verbosity)) {
            return std::nullopt;
        }

        if (*ptr == '\0') {
            return rule;
        }

        if (*ptr == ',') {
            rule.file_suffix = read_until(++ptr, ":@");
            if (*ptr == ':') {
                if (!parse_int(read_until(++ptr, "-@"), rule.line_low)) {
                    return std::nullopt;
                }
                rule.line_high = rule.line_low;
                if (*ptr == '-') {
                    if (!parse_int(read_until(++ptr, "@"), rule.line_high)) {
                        return std::nullopt;
                    }
                }
            }
        }

        if (*ptr == '@') {
            rule.function_suffix = std::string{ptr + 1};
        }

        rule.complexity = NeedsMatching;
        return rule;
    }

    bool accepts(const int verbosity, const char *tag, const char *file,
                 const char *function, const int line) const {
        if (is_tag_rule) {
            return std::any_of(tags.begin(), tags.end(), [&](const std::string &t) { return t == tag; });
        }
        switch (complexity) {
        case VerbosityOnly:
            return verbosity <= this->verbosity;
        case NeedsMatching:
            return verbosity <= this->verbosity &&
                   ends_with(file, file_suffix) &&
                   ends_with(function, function_suffix) &&
                   line_low <= line && line <= line_high;
        }
        return false;
    }
};

std::vector<DebugRule> parse_rules(const std::string &env) {
    std::vector<DebugRule> rules;
    if (env.empty()) {
        // Treat an unset env var as HL_DEBUG_CODEGEN=0
        rules.resize(1);
        return rules;
    }
    for (const std::string &spec : split_string(env, ";")) {
        if (auto rule = DebugRule::parse(spec)) {
            rules.push_back(*rule);
        } else if (!spec.empty()) {
            // Don't use user_warning here: it consults debug_is_active_impl(),
            // which would recurse while this function-local static is initializing.
            const std::string warning =
                "Warning: Ignoring malformed HL_DEBUG_CODEGEN entry: [" + spec + "]\n" +
                "Expected rule format:\n"
                "    verbosity[,filename[:line_low[-line_high]]][@func]\n"
                "    tag:name[,name...]\n"
                "Rules are separated by ';' and are OR-ed together.\n"
                "Matching for filename and function uses suffix matching. A\n"
                "tag:name rule matches a debug(verbosity, \"name\") call site's\n"
                "tag exactly, regardless of verbosity; a comma-separated list\n"
                "of names matches any one of them.\n"
                "Examples:\n"
                "    HL_DEBUG_CODEGEN=2\n"
                "    HL_DEBUG_CODEGEN=4,CodeGen_LLVM.cpp\n"
                "    HL_DEBUG_CODEGEN=3,Simplify.cpp:100-180\n"
                "    HL_DEBUG_CODEGEN=2@visit\n"
                "    HL_DEBUG_CODEGEN=tag:counterexample\n"
                "    HL_DEBUG_CODEGEN=tag:counterexample,non-monotonic\n"
                "    HL_DEBUG_CODEGEN=1;4,CodeGen_LLVM.cpp@compile\n";
            issue_warning(warning.c_str());
        }
    }
    return rules;
}

bool rules_accept(const std::vector<DebugRule> &rules, const int verbosity, const char *tag,
                  const char *file, const char *function, const int line) {
    return std::any_of(rules.begin(), rules.end(), [&](const auto &rule) {
        return rule.accepts(verbosity, tag, file, function, line);
    });
}

// Either a std::ostream to write to (cerr/cout) or a raw fd to write to (an
// HL_DEBUG_CODEGEN_LOG_FILE opened via open_append_only).
using DebugSink = std::variant<std::reference_wrapper<std::ostream>, int>;

// Opened directly at the OS level (rather than via std::ofstream) so that
// DebugStream's destructor can write each debug() statement's output with a
// single raw write() call: on a file opened O_APPEND, the kernel appends the
// bytes of a single write() atomically, so concurrent processes/threads
// sharing this log file can't tear each other's output mid-statement.
int open_append_only(const std::string &path) {
#ifdef _WIN32
    // _O_BINARY avoids CRLF translation, which would corrupt byte counts.
    return _open(path.c_str(), _O_WRONLY | _O_CREAT | _O_APPEND | _O_BINARY, _S_IWRITE);
#else
    return ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
#endif
}

DebugSink make_debug_sink() {
    const std::string log_file = get_env_variable("HL_DEBUG_CODEGEN_LOG_FILE");
    // /dev/stdout and /dev/stderr are handled explicitly both for compatibility
    // with Windows and for consistency with interleaved std::cout and std::cerr.
    if (log_file.empty() || log_file == "/dev/stderr") {
        return std::ref(std::cerr);
    }
    if (log_file == "/dev/stdout") {
        return std::ref(std::cout);
    }
    const int fd = open_append_only(log_file);
    if (fd < 0) {
        issue_warning(("Warning: Could not open HL_DEBUG_CODEGEN_LOG_FILE: " + log_file +
                       "; falling back to stderr\n")
                          .c_str());
        return std::ref(std::cerr);
    }
    return fd;
}

const DebugSink &debug_sink() {
    static const DebugSink sink = make_debug_sink();
    return sink;
}

// Writes as much of [data, data + size) as the OS accepts in one call. A
// single write() to a regular file normally consumes the whole request; the
// loop only guards against the rare partial write (e.g. an EINTR-interrupted
// call), at the cost of the atomicity guarantee above in that rare case.
void write_all(int fd, const char *data, size_t size) {
#ifndef _WIN32
    int n_retries = 16;
#endif
    while (size > 0) {
#ifdef _WIN32
        const int n = _write(fd, data, (unsigned int)std::min<size_t>(size, INT_MAX));
#else
        const ssize_t n = ::write(fd, data, size);
        if (n < 0 && errno == EINTR) {
            internal_assert(n_retries-- > 0) << "write_all() failed with EINTR too many times";
            continue;
        }
#endif
        if (n <= 0) {
            break;
        }
        data += n;
        size -= (size_t)n;
    }
}

}  // namespace

DebugStream::~DebugStream() {
    if (std::string s = str(); !s.empty()) {
        std::visit(LambdaOverloads{
                       [&](int fd) { write_all(fd, s.data(), s.size()); },
                       [&](auto osr) { osr.get() << s << std::flush; },
                   },
                   debug_sink());
    }
}

bool debug_is_active_impl(const int verbosity, const char *file, const char *function,
                          const int line) {
    return debug_is_active_impl(verbosity, "", file, function, line);
}

bool debug_is_active_impl(const int verbosity, const char *tag, const char *file,
                          const char *function, const int line) {
    static const std::vector<DebugRule> rules = parse_rules(get_env_variable("HL_DEBUG_CODEGEN"));
    return rules_accept(rules, verbosity, tag, file, function, line);
}

bool debug_spec_accepts(const std::string &spec, const int verbosity, const char *tag,
                        const char *file, const char *function, const int line) {
    return rules_accept(parse_rules(spec), verbosity, tag, file, function, line);
}

}  // namespace Halide::Internal
