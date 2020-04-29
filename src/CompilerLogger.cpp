#include "CompilerLogger.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <vector>

#include "IRMutator.h"
#include "Util.h"

namespace Halide {
namespace Internal {

namespace {

// TODO: for now we are just going to ignore potential issues with
// static-initialization-order-fiasco, as CompilerLogger isn't currently used
// from any static-initialization execution scope.
std::unique_ptr<CompilerLogger> active_compiler_logger;

class ObfuscateNames : public IRMutator {
    using IRMutator::visit;

    std::map<std::string, std::string> remapping;

    Expr visit(const Call *op) override {
        std::vector<Expr> args;
        for (const Expr &e : op->args) {
            args.emplace_back(mutate(e));
        }

        std::string name = op->name;
        if (op->call_type == Call::Extern ||
            op->call_type == Call::ExternCPlusPlus ||
            op->call_type == Call::Halide ||
            op->call_type == Call::Image) {
            name = remap(op->name);
        }
        return Call::make(op->type, name, args, op->call_type, op->func,
                          op->value_index, op->image, op->param);
    }

    Expr visit(const Let *op) override {
        std::string name = remap(op->name);
        Expr value = mutate(op->value);
        Expr body = mutate(op->body);
        return Let::make(name, std::move(value), std::move(body));
    }

    Expr visit(const Load *op) override {
        std::string name = remap(op->name);
        Expr index = mutate(op->index);
        Expr predicate = mutate(op->predicate);
        return Load::make(op->type, name, index, op->image, op->param,
                          predicate, op->alignment);
    }

    Expr visit(const Variable *op) override {
        std::string name = remap(op->name);
        return Variable::make(op->type, name, op->image, op->param, op->reduction_domain);
    }

public:
    ObfuscateNames() = default;
    ObfuscateNames(const std::initializer_list<std::pair<const std::string, std::string>> &values)
        : remapping(values) {
    }

    std::string remap(const std::string &var_name) {
        std::string anon_name = "anon" + std::to_string(remapping.size());
        auto p = remapping.insert({var_name, std::move(anon_name)});
        return p.first->second;
    }
};

}  // namespace

std::unique_ptr<CompilerLogger> set_compiler_logger(std::unique_ptr<CompilerLogger> compiler_logger) {
    std::unique_ptr<CompilerLogger> result = std::move(active_compiler_logger);
    active_compiler_logger = std::move(compiler_logger);
    return result;
}

CompilerLogger *get_compiler_logger() {
    return active_compiler_logger.get();
}

JSONCompilerLogger::JSONCompilerLogger(
    const std::string &generator_name,
    const std::string &function_name,
    const std::string &autoscheduler_name,
    const Target &target,
    const std::string &generator_args,
    bool obfuscate_exprs)
    : generator_name(generator_name),
      function_name(function_name),
      autoscheduler_name(autoscheduler_name),
      target(target),
      generator_args(generator_args),
      obfuscate_exprs(obfuscate_exprs) {
}

void JSONCompilerLogger::record_matched_simplifier_rule(const std::string &rulename) {
    matched_simplifier_rules[rulename] += 1;
}

void JSONCompilerLogger::record_non_monotonic_loop_var(const std::string &loop_var, Expr expr) {
    non_monotonic_loop_vars[loop_var].emplace_back(std::move(expr));
}
void JSONCompilerLogger::record_failed_to_prove(Expr failed_to_prove, Expr original_expr) {
    failed_to_prove_exprs.emplace_back(failed_to_prove, original_expr);
}

void JSONCompilerLogger::record_object_code_size(uint64_t bytes) {
    object_code_size += bytes;
}

void JSONCompilerLogger::record_compilation_time(Phase phase, double duration) {
    compilation_time[phase] += duration;
}

void JSONCompilerLogger::obfuscate() {
    {
        std::map<std::string, std::vector<Expr>> n;
        int i = 0;
        for (const auto &it : non_monotonic_loop_vars) {
            std::string loop_name = "loop" + std::to_string(i++);
            for (const auto &e : it.second) {
                // Create a new obfuscater for every Expr, but take pains to ensure
                // that the loop var has a distinct name. (Note that for nested loops,
                // loop vars of enclosing loops will be treated like any other var.)
                ObfuscateNames obfuscater({{it.first, loop_name}});
                n[loop_name].emplace_back(obfuscater.mutate(e));
            }
        }
        non_monotonic_loop_vars = n;
    }
    {
        std::vector<std::pair<Expr, Expr>> n;
        for (const auto &it : failed_to_prove_exprs) {
            // Note that use a separate obfuscater for each pair, so each
            // shares identifiers only with each other; this makes it simpler
            // to post-process output from multiple unrelated Generators
            // and combine Exprs with similar shapes.
            ObfuscateNames obfuscater;
            auto failed_to_prove = obfuscater.mutate(it.first);
            auto original_expr = obfuscater.mutate(it.second);
            n.emplace_back(std::move(failed_to_prove), std::move(original_expr));
        }
        failed_to_prove_exprs = n;
    }
}

namespace {

std::ostream &emit_eol(std::ostream &o, bool comma = true) {
    o << (comma ? ",\n" : "\n");
    return o;
}

std::ostream &emit_key(std::ostream &o, int indent, const std::string &key) {
    std::string spaces(indent, ' ');
    o << spaces << "\"" << key << "\" : ";
    return o;
}

std::ostream &emit_object_key_open(std::ostream &o, int indent, const std::string &key) {
    std::string spaces(indent, ' ');
    o << spaces << "\"" << key << "\" : {\n";
    return o;
}

std::ostream &emit_object_key_close(std::ostream &o, int indent, bool comma = true) {
    std::string spaces(indent, ' ');
    o << spaces << "}";
    emit_eol(o, comma);
    return o;
}

template<typename VALUE>
std::ostream &emit_value(std::ostream &o, const VALUE &value) {
    o << value;
    return o;
}

template<>
std::ostream &emit_value<std::string>(std::ostream &o, const std::string &value) {
    o << "\"" << value << "\"";
    return o;
}

template<typename VALUE>
std::ostream &emit_key_value(std::ostream &o, int indent, const std::string &key, const VALUE &value, bool comma = true) {
    emit_key(o, indent, key);
    emit_value(o, value);
    emit_eol(o, comma);
    return o;
}

std::ostream &emit_optional_key_value(std::ostream &o, int indent, const std::string &key, const std::string &value, bool comma = true) {
    if (!value.empty()) {
        return emit_key_value(o, indent, key, value, comma);
    }
    return o;
}

template<typename T>
std::ostream &emit_pairs(std::ostream &o, int indent, const std::string &key, const T &pairs, bool comma = true) {
    std::string spaces(indent, ' ');

    emit_key(o, indent, key);
    o << "{\n";
    int commas_to_emit = (int)pairs.size() - 1;
    for (const auto &it : pairs) {
        const bool comma = (commas_to_emit > 0);
        emit_key_value(o, indent + 1, it.first, it.second, comma);
        commas_to_emit--;
    }
    o << spaces << "}";
    emit_eol(o, comma);
    return o;
}

template<typename T>
std::ostream &emit_list(std::ostream &o, int indent, const T &list, bool comma = true) {
    std::string spaces(indent, ' ');
    std::string spaces_in(indent + 1, ' ');

    o << spaces << "[\n";
    int commas_to_emit = (int)list.size() - 1;
    for (const auto &it : list) {
        o << spaces_in;
        emit_value(o, it);
        emit_eol(o, commas_to_emit-- > 0);
    }
    o << spaces << "]";
    emit_eol(o, comma);
    return o;
}

std::string expr_to_string(const Expr &e) {
    std::ostringstream s;
    s << e;
    return s.str();
}

std::set<std::string> exprs_to_strings(const std::vector<Expr> &exprs) {
    std::set<std::string> strings;
    for (const auto &e : exprs) {
        strings.insert(expr_to_string(e));
    }
    return strings;
}

}  // namespace

std::ostream &JSONCompilerLogger::emit_to_stream(std::ostream &o) {
    if (obfuscate_exprs) {
        obfuscate();
    }

    // Output in JSON form

    o << "{\n";

    int indent = 1;
    emit_optional_key_value(o, indent, "generator_name", generator_name);
    emit_optional_key_value(o, indent, "function_name", function_name);
    emit_optional_key_value(o, indent, "autoscheduler_name", autoscheduler_name);
    emit_optional_key_value(o, indent, "target", target == Target() ? "" : target.to_string());
    emit_optional_key_value(o, indent, "generator_args", generator_args);

    if (object_code_size) {
        emit_key_value(o, indent, "object_code_size", object_code_size);
    }

    // If these are present, emit them, even if value is zero
    if (compilation_time.count(Phase::HalideLowering)) {
        emit_key_value(o, indent, "compilation_time_halide_lowering", compilation_time[Phase::HalideLowering]);
    }
    if (compilation_time.count(Phase::LLVM)) {
        emit_key_value(o, indent, "compilation_time_llvm", compilation_time[Phase::LLVM]);
    }

    if (!matched_simplifier_rules.empty()) {
        using P = std::pair<std::string, int64_t>;

        // Sort these in descending order by usage,
        // just to make casual reading of the output easier
        struct Compare {
            bool operator()(const P &a, const P &b) const {
                return a.second > b.second;
            }
        };

        std::set<P, Compare> sorted;
        for (const auto &it : matched_simplifier_rules) {
            sorted.emplace(it.first, it.second);
        }
        emit_pairs(o, indent, "matched_simplifier_rules", sorted);
    }

    if (!non_monotonic_loop_vars.empty()) {
        emit_object_key_open(o, indent, "non_monotonic_loop_vars");

        int commas_to_emit = (int)non_monotonic_loop_vars.size() - 1;
        for (const auto &it : non_monotonic_loop_vars) {
            const auto &loop_var = it.first;
            emit_key(o, indent + 1, loop_var);
            emit_eol(o, false);
            emit_list(o, indent + 1, exprs_to_strings(it.second), (commas_to_emit-- > 0));
        }

        emit_object_key_close(o, indent);
    }

    if (!failed_to_prove_exprs.empty()) {
        emit_object_key_open(o, indent, "failed_to_prove");

        // We'll do deduplication here, during stringification.
        std::map<std::string, std::set<std::string>> sorted;
        for (const auto &it : failed_to_prove_exprs) {
            const auto failed_to_prove_str = expr_to_string(it.first);
            const auto original_expr_str = expr_to_string(it.second);
            sorted[failed_to_prove_str].insert(original_expr_str);
        }

        int commas_to_emit = (int)sorted.size() - 1;
        for (const auto &it : sorted) {
            emit_key(o, indent + 1, it.first);
            emit_eol(o, false);
            emit_list(o, indent + 1, it.second, (commas_to_emit-- > 0));
        }

        emit_object_key_close(o, indent);
    }

    // Emit this last as a simple way to dodge the trailing-comma nonsense
    o << " \"version\": \"HalideJSONCompilerLoggerV1\"\n";
    o << "}\n";

    return o;
}

}  // namespace Internal
}  // namespace Halide
