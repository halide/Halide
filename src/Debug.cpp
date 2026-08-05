#include "Debug.h"
#include "Error.h"
#include "Util.h"

#include <algorithm>
#include <climits>
#include <optional>

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
    enum Complexity { VerbosityOnly,
                      NeedsMatching } complexity = VerbosityOnly;

public:
    static std::optional<DebugRule> parse(const std::string &spec) {
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

    bool accepts(const int verbosity, const char *file, const char *function,
                 const int line) const {
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
                "Rules are separated by ';' and are OR-ed together.\n"
                "Matching for filename and function uses suffix matching.\n"
                "Examples:\n"
                "    HL_DEBUG_CODEGEN=2\n"
                "    HL_DEBUG_CODEGEN=4,CodeGen_LLVM.cpp\n"
                "    HL_DEBUG_CODEGEN=3,Simplify.cpp:100-180\n"
                "    HL_DEBUG_CODEGEN=2@visit\n"
                "    HL_DEBUG_CODEGEN=1;4,CodeGen_LLVM.cpp@compile\n";
            issue_warning(warning.c_str());
        }
    }
    return rules;
}

bool rules_accept(const std::vector<DebugRule> &rules, const int verbosity,
                  const char *file, const char *function, const int line) {
    return std::any_of(rules.begin(), rules.end(), [&](const auto &rule) {
        return rule.accepts(verbosity, file, function, line);
    });
}

}  // namespace

bool debug_is_active_impl(const int verbosity, const char *file, const char *function,
                          const int line) {
    static const std::vector<DebugRule> rules = parse_rules(get_env_variable("HL_DEBUG_CODEGEN"));
    return rules_accept(rules, verbosity, file, function, line);
}

bool debug_spec_accepts(const std::string &spec, const int verbosity,
                        const char *file, const char *function, const int line) {
    return rules_accept(parse_rules(spec), verbosity, file, function, line);
}

void debug_filter_test() {
    // A bare verbosity matches purely on level, at any location.
    internal_assert(debug_spec_accepts("2", 0, "any.cpp", "any", 1));
    internal_assert(debug_spec_accepts("2", 2, "any.cpp", "any", 1));
    internal_assert(!debug_spec_accepts("2", 3, "any.cpp", "any", 1));

    // Filenames are matched as suffixes, subject to the verbosity bound.
    internal_assert(debug_spec_accepts("4,CodeGen_LLVM.cpp", 4, "src/CodeGen_LLVM.cpp", "f", 10));
    internal_assert(!debug_spec_accepts("4,CodeGen_LLVM.cpp", 4, "src/Simplify.cpp", "f", 10));
    internal_assert(!debug_spec_accepts("4,CodeGen_LLVM.cpp", 5, "src/CodeGen_LLVM.cpp", "f", 10));

    // Line ranges are inclusive on both ends.
    internal_assert(debug_spec_accepts("3,Simplify.cpp:100-180", 3, "src/Simplify.cpp", "f", 100));
    internal_assert(debug_spec_accepts("3,Simplify.cpp:100-180", 3, "src/Simplify.cpp", "f", 180));
    internal_assert(!debug_spec_accepts("3,Simplify.cpp:100-180", 3, "src/Simplify.cpp", "f", 99));
    internal_assert(!debug_spec_accepts("3,Simplify.cpp:100-180", 3, "src/Simplify.cpp", "f", 181));

    // A single line means low == high.
    internal_assert(debug_spec_accepts("3,Simplify.cpp:100", 3, "src/Simplify.cpp", "f", 100));
    internal_assert(!debug_spec_accepts("3,Simplify.cpp:100", 3, "src/Simplify.cpp", "f", 101));

    // Functions are also matched as suffixes.
    internal_assert(debug_spec_accepts("2@visit", 2, "any.cpp", "visit", 1));
    internal_assert(debug_spec_accepts("2@visit", 2, "any.cpp", "IRVisitor::visit", 1));
    internal_assert(!debug_spec_accepts("2@visit", 2, "any.cpp", "mutate", 1));

    // File, line, and function qualifiers combine (all must hold).
    internal_assert(debug_spec_accepts("3,Simplify.cpp:100-180@visit", 3, "Simplify.cpp", "visit", 150));
    internal_assert(!debug_spec_accepts("3,Simplify.cpp:100-180@visit", 3, "Simplify.cpp", "mutate", 150));

    // Rules separated by ';' are OR-ed together.
    internal_assert(debug_spec_accepts("1;4,CodeGen_LLVM.cpp@compile", 1, "whatever.cpp", "g", 5));
    internal_assert(debug_spec_accepts("1;4,CodeGen_LLVM.cpp@compile", 4, "CodeGen_LLVM.cpp", "compile", 5));
    internal_assert(!debug_spec_accepts("1;4,CodeGen_LLVM.cpp@compile", 4, "CodeGen_LLVM.cpp", "other", 5));

    // An empty spec behaves like verbosity 0: only debug(0) prints.
    internal_assert(debug_spec_accepts("", 0, "any.cpp", "f", 1));
    internal_assert(!debug_spec_accepts("", 1, "any.cpp", "f", 1));

    // A malformed rule is skipped (and warns on stderr); with no valid rules,
    // nothing matches. A valid rule alongside it still takes effect.
    internal_assert(!debug_spec_accepts("garbage", 0, "any.cpp", "f", 1));
    internal_assert(debug_spec_accepts("2;garbage", 2, "any.cpp", "f", 1));

    debug(0) << "debug_filter_test passed\n";
}

}  // namespace Halide::Internal
