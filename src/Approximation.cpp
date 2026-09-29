#include "Approximation.h"

#include <set>

#include "Error.h"
#include "FindCalls.h"
#include "Function.h"

namespace Halide {

namespace {

Func find_stage_output(const std::vector<ApproximationStageOutputs> &outputs,
                       const Approximation &stage, size_t port, const char *direction) {
    user_assert(stage.defined()) << "Approximation::" << direction << "_by: undefined stage\n";
    const ApproximationStageOutputs *found = nullptr;
    size_t count = 0;
    for (const ApproximationStageOutputs &o : outputs) {
        if (o.stage.same_as(stage)) {
            found = &o;
            count++;
        }
    }
    user_assert(count > 0)
        << "Approximation::" << direction << "_by: the stage was not invoked in the "
        << direction << " direction (was it converted separately from the handle passed "
        << "into the Approximation?)\n";
    user_assert(count == 1)
        << "Approximation::" << direction << "_by: ambiguous: stage invoked " << count
        << " times in the " << direction << " direction\n";
    return port < found->ports.size() ? found->ports[port] : Func();
}

}  // namespace

Func ApproximationResult::encoded_by(const Approximation &stage, size_t port) const {
    return find_stage_output(encoded_stage_outputs, stage, port, "encode");
}

Func ApproximationResult::decoded_by(const Approximation &stage, size_t port) const {
    return find_stage_output(decoded_stage_outputs, stage, port, "decode");
}

void Approximation::check_single_input(const std::vector<Func> &inputs, const char *direction) {
    user_assert(inputs.size() == 1)
        << "Approximation: a unit with a single-Func " << direction << "() was given "
        << inputs.size() << " inputs, but requires exactly one\n";
}

namespace {

// The trace shared by every handle call nested under the outermost one on this
// thread. Encode and decode share it.
thread_local std::vector<ApproximationStageOutputs> *active_trace = nullptr;

struct TraceScope {
    std::vector<ApproximationStageOutputs> local;
    bool owner;

    TraceScope()
        : owner(active_trace == nullptr) {
        if (owner) {
            active_trace = &local;
        }
    }

    ~TraceScope() {
        if (owner) {
            active_trace = nullptr;
        }
    }

    std::vector<ApproximationStageOutputs> &trace() {
        return *active_trace;
    }
};

// Post-order DFS over direct calls (visited by name order) so producers come
// before consumers. Inputs are never entered.
void discover(const Internal::Function &f, const std::set<std::string> &inputs,
              const std::set<std::string> &outputs, std::set<std::string> &visited,
              std::vector<Func> &result) {
    if (inputs.count(f.name()) || !visited.insert(f.name()).second) {
        return;
    }
    for (const auto &[name, callee] : Internal::find_direct_calls(f)) {
        discover(callee, inputs, outputs, visited, result);
    }
    if (!outputs.count(f.name())) {
        result.push_back(Func(f));
    }
}

std::vector<Func> find_intermediates(const std::vector<Func> &inputs, const std::vector<Func> &outputs) {
    std::set<std::string> input_names, output_names, visited;
    for (const Func &f : inputs) {
        if (f.defined()) {
            input_names.insert(f.name());
        }
    }
    for (const Func &f : outputs) {
        if (f.defined()) {
            output_names.insert(f.name());
        }
    }
    std::vector<Func> result;
    for (const Func &f : outputs) {
        if (f.defined()) {
            discover(f.function(), input_names, output_names, visited, result);
        }
    }
    return result;
}

}  // namespace

EncodeResult Approximation::encode(const std::vector<Func> &inputs) const {
    user_assert(defined()) << "encode called on an undefined Approximation\n";
    TraceScope scope;
    size_t start = scope.trace().size();
    std::vector<Func> encoded = impl_->encode(inputs);
    std::vector<Func> intermediates = find_intermediates(inputs, encoded);
    scope.trace().push_back({*this, encoded, intermediates});
    std::vector<ApproximationStageOutputs> stage_outputs(scope.trace().begin() + start, scope.trace().end());
    return {std::move(encoded), std::move(intermediates), std::move(stage_outputs)};
}

DecodeResult Approximation::decode(const std::vector<Func> &encoded) const {
    user_assert(defined()) << "decode called on an undefined Approximation\n";
    TraceScope scope;
    size_t start = scope.trace().size();
    std::vector<Func> decoded = impl_->decode(encoded);
    std::vector<Func> intermediates = find_intermediates(encoded, decoded);
    scope.trace().push_back({*this, decoded, intermediates});
    std::vector<ApproximationStageOutputs> stage_outputs(scope.trace().begin() + start, scope.trace().end());
    return {std::move(decoded), std::move(intermediates), std::move(stage_outputs)};
}

std::vector<Func> Compose::encode(const std::vector<Func> &inputs) const {
    user_assert(!stages.empty()) << "Compose::encode: no stages\n";
    std::vector<Func> current = inputs;
    for (int i = (int)stages.size() - 1; i >= 0; i--) {
        current = stages[i].encode(current).encoded;
    }
    return current;
}

std::vector<Func> Compose::decode(const std::vector<Func> &encoded) const {
    user_assert(!stages.empty()) << "Compose::decode: no stages\n";
    std::vector<Func> current = encoded;
    for (size_t i = 0; i < stages.size(); i++) {
        current = stages[i].decode(current).decoded;
    }
    return current;
}

std::vector<Func> Apply::encode(const std::vector<Func> &inputs) const {
    user_assert(idx + encode_arity <= (int)inputs.size())
        << "Apply::encode: idx (" << idx << ") + encode_arity (" << encode_arity
        << ") exceeds the input count (" << inputs.size() << ")\n";
    std::vector<Func> target(inputs.begin() + idx, inputs.begin() + idx + encode_arity);
    std::vector<Func> inner_encoded = inner.encode(target).encoded;

    std::vector<Func> encoded(inputs.begin(), inputs.begin() + idx);
    encoded.insert(encoded.end(), inner_encoded.begin(), inner_encoded.end());
    encoded.insert(encoded.end(), inputs.begin() + idx + encode_arity, inputs.end());
    return encoded;
}

std::vector<Func> Apply::decode(const std::vector<Func> &encoded) const {
    user_assert(idx + decode_arity <= (int)encoded.size())
        << "Apply::decode: idx (" << idx << ") + decode_arity (" << decode_arity
        << ") exceeds the input count (" << encoded.size() << ")\n";
    std::vector<Func> target(encoded.begin() + idx, encoded.begin() + idx + decode_arity);
    std::vector<Func> inner_decoded = inner.decode(target).decoded;

    std::vector<Func> decoded(encoded.begin(), encoded.begin() + idx);
    decoded.insert(decoded.end(), inner_decoded.begin(), inner_decoded.end());
    decoded.insert(decoded.end(), encoded.begin() + idx + decode_arity, encoded.end());
    return decoded;
}

std::vector<Func> TrustedInverse::encode(const std::vector<Func> &inputs) const {
    return encoder.encode(inputs).encoded;
}

std::vector<Func> TrustedInverse::decode(const std::vector<Func> &encoded) const {
    return decoder.decode(encoded).decoded;
}

std::vector<Func> Choose::encode(const std::vector<Func> &inputs) const {
    return chosen.encode(inputs).encoded;
}

std::vector<Func> Choose::decode(const std::vector<Func> &encoded) const {
    return chosen.decode(encoded).decoded;
}

namespace {

Pointwise::TupleFn wrap_expr_fn(Pointwise::ExprFn fn) {
    return [fn = std::move(fn)](const std::vector<Expr> &values) {
        user_assert(values.size() == 1)
            << "Pointwise: an Expr-valued function requires a single-output Func, but the input has "
            << values.size() << " outputs (use a std::vector<Expr> function instead)\n";
        return std::vector<Expr>{fn(values[0])};
    };
}

Func apply_pointwise(const Func &input, const Pointwise::TupleFn &fn, const std::string &name,
                     const std::string &var_prefix, const char *direction) {
    user_assert(input.defined()) << "Pointwise::" << direction << ": undefined Func\n";
    std::vector<Var> args;
    std::vector<Expr> call_args;
    for (int i = 0; i < input.dimensions(); i++) {
        args.emplace_back(var_prefix + std::to_string(i));
        call_args.emplace_back(args.back());
    }
    std::vector<Expr> values;
    if (input.outputs() == 1) {
        values.push_back(input(call_args));
    } else {
        for (int i = 0; i < input.outputs(); i++) {
            values.push_back(input(call_args)[i]);
        }
    }
    std::vector<Expr> results = fn(values);
    user_assert(!results.empty()) << "Pointwise::" << direction << ": function returned no values\n";
    Func out(name);
    if (results.size() == 1) {
        out(args) = results[0];
    } else {
        out(args) = Tuple(results);
    }
    return out;
}

}  // namespace

Pointwise::Pointwise(const std::string &name, ExprFn encode_fn, ExprFn decode_fn)
    : Pointwise(name + "_encode", name + "_decode", std::move(encode_fn), std::move(decode_fn)) {
}

Pointwise::Pointwise(const std::string &name, TupleFn encode_fn, TupleFn decode_fn)
    : Pointwise(name + "_encode", name + "_decode", std::move(encode_fn), std::move(decode_fn)) {
}

Pointwise::Pointwise(std::string encode_name, std::string decode_name, ExprFn encode_fn, ExprFn decode_fn,
                     std::string var_prefix)
    : Pointwise(std::move(encode_name), std::move(decode_name),
                wrap_expr_fn(std::move(encode_fn)), wrap_expr_fn(std::move(decode_fn)),
                std::move(var_prefix)) {
}

Pointwise::Pointwise(std::string encode_name, std::string decode_name, TupleFn encode_fn, TupleFn decode_fn,
                     std::string var_prefix)
    : encode_name(std::move(encode_name)), decode_name(std::move(decode_name)),
      var_prefix(std::move(var_prefix)), encode_fn(std::move(encode_fn)), decode_fn(std::move(decode_fn)) {
}

Func Pointwise::encode(const Func &input) const {
    return apply_pointwise(input, encode_fn, encode_name, var_prefix, "encode");
}

Func Pointwise::decode(const Func &encoded) const {
    return apply_pointwise(encoded, decode_fn, decode_name, var_prefix, "decode");
}

std::vector<Func> Identity::encode(const std::vector<Func> &inputs) const {
    return inputs;
}

std::vector<Func> Identity::decode(const std::vector<Func> &encoded) const {
    return encoded;
}

std::vector<Func> Permute::encode(const std::vector<Func> &inputs) const {
    user_assert(inputs.size() == forward.size()) << "Permutation size does not match input size";
    std::vector<Func> result;
    result.reserve(inputs.size());
    for (int i = 0; i < (int)inputs.size(); i++) {
        result.push_back(inputs[forward[i]]);
    }
    return result;
}

std::vector<Func> Permute::decode(const std::vector<Func> &encoded) const {
    user_assert(encoded.size() == forward.size()) << "Permutation size does not match encoded size";
    std::vector<Func> result;
    result.reserve(encoded.size());
    for (int i = 0; i < (int)encoded.size(); i++) {
        result.push_back(encoded[backward[i]]);
    }
    return result;
}

}  // namespace Halide
