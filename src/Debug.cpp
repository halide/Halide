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
            user_warning
                << "Ignoring malformed HL_DEBUG_CODEGEN entry: [" << spec << "]\n"
                << "Expected rule format:\n"
                << "    verbosity[,filename[:line_low[-line_high]]][@func]\n"
                << "Rules are separated by ';' and are OR-ed together.\n"
                << "Matching for filename and function uses suffix matching.\n"
                << "Examples:\n"
                << "    HL_DEBUG_CODEGEN=2\n"
                << "    HL_DEBUG_CODEGEN=4,CodeGen_LLVM.cpp\n"
                << "    HL_DEBUG_CODEGEN=3,Simplify.cpp:100-180\n"
                << "    HL_DEBUG_CODEGEN=2@visit\n"
                << "    HL_DEBUG_CODEGEN=1;4,CodeGen_LLVM.cpp@compile\n";
        }
    }
    return rules;
}

}  // namespace

bool debug_is_active_impl(const int verbosity, const char *file, const char *function,
                          const int line) {
    static const std::vector<DebugRule> rules = parse_rules(get_env_variable("HL_DEBUG_CODEGEN"));
    return std::any_of(rules.begin(), rules.end(), [&](const auto &rule) {
        return rule.accepts(verbosity, file, function, line);
    });
}

}  // namespace Halide::Internal
