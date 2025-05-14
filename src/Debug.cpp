#include "Debug.h"
#include "Error.h"
#include "Util.h"

#include <climits>

namespace Halide::Internal::detail {

namespace {

std::string read_until(const char *&ptr, const char delim) {
    const char *start = ptr;
    while (*ptr != delim && *ptr != '\0') {
        ptr++;
    }
    std::string result{start, ptr};
    // Swallow the delimiter
    if (*ptr == delim) {
        ptr++;
    }
    return result;
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
    std::string function_suffix = "";
    int line_low = -1;
    int line_high = INT_MAX;

public:
    enum Complexity { VerbosityOnly,
                      NeedsMatching } complexity = VerbosityOnly;

    static std::optional<DebugRule> parse(const std::string &spec) {
        DebugRule rule;
        const char *ptr = spec.c_str();

        if (!parse_int(read_until(ptr, ','), rule.verbosity)) {
            return std::nullopt;
        }

        if (!*ptr) {
            return rule;
        }

        rule.file_suffix = read_until(ptr, ':');

        if (*ptr == ':') {
            rule.function_suffix = read_until(++ptr, ':');
        }

        if (*ptr) {
            if (!parse_int(read_until(ptr, '-'), rule.line_low)) {
                return std::nullopt;
            }
            rule.line_high = rule.line_low;
            if (*ptr) {
                if (!parse_int(read_until(ptr, '\0'), rule.line_high)) {
                    return std::nullopt;
                }
            }
        }

        rule.complexity = NeedsMatching;
        return rule;
    }

    bool accepts(const int verbosity, const char *file, const char *function,
                 const int line) const {
        if (verbosity > this->verbosity) {
            return false;
        }
        return complexity == NeedsMatching && ends_with(file, file_suffix) &&
               ends_with(function, function_suffix) && line_low <= line &&
               line <= line_high;
    }
};

std::pair<std::vector<DebugRule>, DebugRule::Complexity>
parse_rules(const std::string &env) {
    std::vector<DebugRule> rules;
    DebugRule::Complexity complexity = DebugRule::VerbosityOnly;

    for (const std::string &spec : split_string(env, ";")) {
        auto rule = DebugRule::parse(spec);
        if (!rule) {
            user_warning
                << "Ignoring malformed HL_DEBUG_CODEGEN entry: [" << env << "]\n"
                << "The expected format is:\n    "
                << "verbosity[,file_suffix[::function_suffix][:line_low[-line_high]]]";
        } else {
            complexity = std::max(complexity, rule->complexity);
            rules.push_back(*rule);
        }
    }

    if (rules.empty()) {
        rules.emplace_back();
    }

    return {rules, complexity};
}

}  // namespace

int debug::should_log(const int verbosity, const char *file, const char *function,
                      const int line) {
    // C++ is silly and doesn't allow `static` with structured bindings
    static const auto rc = parse_rules(get_env_variable("HL_DEBUG_CODEGEN"));
    const auto &[rules, complexity] = rc;

    return std::any_of(rules.begin(), rules.end(), [&](const DebugRule &rule) {
        return rule.accepts(verbosity, file, function, line);
    });
}

}  // namespace Halide::Internal::detail
