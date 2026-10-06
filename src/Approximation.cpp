#include "Approximation.h"

#include <algorithm>
#include <ostream>
#include <set>
#include <sstream>

#if defined(__GNUC__) || defined(__clang__)
#include <cstdlib>
#endif

#include "Error.h"
#include "FindCalls.h"
#include "Function.h"

namespace Halide {

namespace {

const ApproximationStageOutputs &find_stage(const std::vector<ApproximationStageOutputs> &outputs,
                                            const Approximation &stage, const char *direction) {
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
    return *found;
}

Func find_stage_output(const std::vector<ApproximationStageOutputs> &outputs,
                       const Approximation &stage, size_t port, const char *direction) {
    const ApproximationStageOutputs &found = find_stage(outputs, stage, direction);
    return port < found.ports.size() ? found.ports[port] : Func();
}

Func find_stage_output(const std::vector<ApproximationStageOutputs> &outputs,
                       const Approximation &stage, const std::string &port, const char *direction) {
    const ApproximationStageOutputs &found = find_stage(outputs, stage, direction);
    std::string available;
    size_t match = 0, count = 0;
    for (size_t i = 0; i < found.port_names.size(); i++) {
        available += (i ? ", " : "") + found.port_names[i];
        if (found.port_names[i] == port) {
            match = i;
            count++;
        }
    }
    user_assert(count > 0)
        << "Approximation::" << direction << "_by: stage '" << stage.label() << "' has no "
        << direction << " port named '" << port << "' (available: " << available << ")\n";
    user_assert(count == 1)
        << "Approximation::" << direction << "_by: stage '" << stage.label() << "' has " << count
        << " " << direction << " ports named '" << port << "'\n";
    return found.ports[match];
}

}  // namespace

Func ApproximationResult::encoded_by(const Approximation &stage, size_t port) const {
    return find_stage_output(encoded_stage_outputs, stage, port, "encode");
}

Func ApproximationResult::decoded_by(const Approximation &stage, size_t port) const {
    return find_stage_output(decoded_stage_outputs, stage, port, "decode");
}

Func ApproximationResult::encoded_by(const Approximation &stage, const std::string &port) const {
    return find_stage_output(encoded_stage_outputs, stage, port, "encode");
}

Func ApproximationResult::decoded_by(const Approximation &stage, const std::string &port) const {
    return find_stage_output(decoded_stage_outputs, stage, port, "decode");
}

std::vector<Func> ApproximationResult::stage_ports() const {
    std::vector<Func> result;
    std::set<std::string> seen;
    if (replacement.defined()) {
        seen.insert(replacement.name());
    }
    for (const auto *outputs : {&encoded_stage_outputs, &decoded_stage_outputs}) {
        for (const ApproximationStageOutputs &o : *outputs) {
            for (const Func &p : o.ports) {
                if (p.defined() && seen.insert(p.name()).second) {
                    result.push_back(p);
                }
            }
        }
    }
    return result;
}

bool ApproximationResult::is_stage_port(const Func &f) const {
    if (!f.defined()) {
        return false;
    }
    for (const Func &p : stage_ports()) {
        if (p.name() == f.name()) {
            return true;
        }
    }
    return false;
}

std::vector<Func> ApproximationResult::decode_funcs() const {
    std::vector<Func> result;
    std::vector<Func> candidates = decode_trace.intermediates;
    candidates.push_back(replacement);
    for (const Func &f : candidates) {
        if (f.defined() && Internal::eager_inline_obstacle(f.function()).empty()) {
            result.push_back(f);
        }
    }
    return result;
}

namespace {

void replace_all(std::string &s, const std::string &from, const std::string &to) {
    for (size_t pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + to.size())) {
        s.replace(pos, from.size(), to);
    }
}

void print_names(std::ostream &stream, const std::vector<Func> &funcs,
                 const std::vector<std::string> &port_names = {}) {
    const char *sep = "";
    for (size_t i = 0; i < funcs.size(); i++) {
        stream << sep;
        if (i < port_names.size()) {
            stream << port_names[i] << "=";
        }
        stream << (funcs[i].defined() ? funcs[i].name() : "<undefined>");
        sep = ", ";
    }
}

void print_node(std::ostream &stream, const ApproximationTraceNode &node, int depth) {
    std::string indent(depth * 2, ' ');
    stream << indent << node.label << " -> ";
    print_names(stream, node.ports, node.port_names);
    stream << "\n";
    if (!node.intermediates.empty()) {
        stream << indent << "  intermediates: ";
        print_names(stream, node.intermediates);
        stream << "\n";
    }
    for (const ApproximationTraceNode &child : node.children) {
        print_node(stream, child, depth + 1);
    }
}

void flatten(const ApproximationTraceNode &node, std::vector<ApproximationStageOutputs> &out) {
    for (const ApproximationTraceNode &child : node.children) {
        flatten(child, out);
    }
    out.push_back({node.stage, node.ports, node.port_names, node.intermediates});
}

}  // namespace

std::ostream &operator<<(std::ostream &stream, const ApproximationTraceNode &node) {
    print_node(stream, node, 0);
    return stream;
}

std::ostream &operator<<(std::ostream &stream, const ApproximationResult &result) {
    stream << "encode:\n";
    if (result.encode_trace.stage.defined()) {
        print_node(stream, result.encode_trace, 1);
    }
    stream << "decode:\n";
    if (result.decode_trace.stage.defined()) {
        print_node(stream, result.decode_trace, 1);
    }
    return stream;
}

std::string Approximation::type_label(const char *pretty_function) {
    const std::string pretty = pretty_function;
    std::string name;
    size_t start = std::string::npos;
    if (size_t pos = pretty.find("pretty_type_name<"); pos != std::string::npos) {
        // MSVC: ...pretty_type_name<struct Foo>(void)
        start = pos + std::string("pretty_type_name<").size();
    } else if (pos = pretty.find("T = "); pos != std::string::npos) {
        // clang: [T = Foo]; GCC: [with T = Foo; ...]
        start = pos + 4;
    }
    if (start == std::string::npos) {
        return "unit";
    }
    int depth = 0;
    for (size_t i = start; i < pretty.size(); i++) {
        char c = pretty[i];
        if (c == '<' || c == '(' || c == '[') {
            depth++;
        } else if (c == '>' || c == ')' || c == ']') {
            if (depth == 0) {
                break;
            }
            depth--;
        } else if (c == ';' && depth == 0) {
            break;
        }
        name += c;
    }
    replace_all(name, "struct ", "");
    replace_all(name, "class ", "");
    replace_all(name, "(anonymous namespace)::", "");
    replace_all(name, "{anonymous}::", "");
    replace_all(name, "`anonymous namespace'::", "");
    replace_all(name, "`anonymous-namespace'::", "");
    replace_all(name, "std::__1::", "std::");
    replace_all(name, "std::__cxx11::", "std::");
    replace_all(name, "Halide::", "");
    return name;
}

Approximation Approximation::labelled(std::string label) const {
    user_assert(defined()) << "labelled called on an undefined Approximation\n";
    state_->label = std::move(label);
    return *this;
}

std::string Approximation::label() const {
    if (!defined()) {
        return "";
    }
    return state_->label.empty() ? state_->impl->default_label() : state_->label;
}

void Approximation::check_single_input(const std::vector<Func> &inputs, const char *direction) {
    user_assert(inputs.size() == 1)
        << "Approximation: a unit with a single-Func " << direction << "() was given "
        << inputs.size() << " inputs, but requires exactly one\n";
}

namespace {

// The children collected so far by the innermost handle call in progress on
// this thread (null outside any call). Encode and decode share it.
thread_local std::vector<ApproximationTraceNode> *active_children = nullptr;

struct CallScope {
    std::vector<ApproximationTraceNode> children;
    std::vector<ApproximationTraceNode> *parent;

    CallScope()
        : parent(active_children) {
        active_children = &children;
    }

    ~CallScope() {
        active_children = parent;
    }

    // Finish the call: nest the node under the enclosing call, if any.
    ApproximationTraceNode finish(const Approximation &stage, std::vector<Func> ports,
                                  std::vector<std::string> port_names, std::vector<Func> intermediates,
                                  std::vector<Func> inputs, std::vector<std::string> input_names) {
        ApproximationTraceNode node{stage, stage.label(), std::move(ports), std::move(port_names),
                                    std::move(intermediates), std::move(children), std::move(inputs),
                                    std::move(input_names)};
        if (parent) {
            parent->push_back(node);
        }
        return node;
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
        result.emplace_back(f);
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

namespace {

std::string range_string(const ApproximationRange &range) {
    std::ostringstream stream;
    stream.precision(9);
    stream << "[" << range.lo << ", " << range.hi << "]";
    return stream.str();
}

std::string port_string(const ApproximationPort &port) {
    std::ostringstream stream;
    stream << port.name;
    if (port.type) {
        stream << ": " << *port.type;
    }
    if (port.dimensions) {
        stream << " x" << *port.dimensions;
    }
    if (port.range) {
        stream << " in " << range_string(*port.range);
    }
    return stream.str();
}

// Compare the ranges `context` guarantees with the preconditions `required`
// declares, port by port. Nothing is reported unless the two line up.
std::vector<std::string> input_range_issues(const ApproximationPorts &required,
                                            const ApproximationPorts &context) {
    std::vector<std::string> issues;
    if (required.size() != context.size()) {
        return issues;
    }
    for (size_t i = 0; i < required.size(); i++) {
        if (!required[i].range) {
            continue;
        }
        const std::string prefix = "input '" + required[i].name + "': ";
        if (!context[i].range) {
            issues.push_back(prefix + "requires " + range_string(*required[i].range) +
                             ", but the producer declares no range");
        } else if (!required[i].range->contains(*context[i].range)) {
            issues.push_back(prefix + range_string(*context[i].range) + " not within " +
                             range_string(*required[i].range));
        }
    }
    return issues;
}

std::string type_string(const Type &t) {
    std::ostringstream stream;
    stream << t;
    return stream.str();
}

std::string ports_string(const ApproximationPorts &ports) {
    std::string result;
    for (size_t i = 0; i < ports.size(); i++) {
        result += (i ? ", " : "") + port_string(ports[i]);
    }
    return result;
}

ApproximationPorts positional_ports(size_t count) {
    ApproximationPorts ports;
    for (size_t i = 0; i < count; i++) {
        ports.emplace_back(std::to_string(i));
    }
    return ports;
}

ApproximationPorts names_only(const ApproximationPorts &ports) {
    ApproximationPorts result;
    for (const ApproximationPort &p : ports) {
        result.emplace_back(p.name);
    }
    return result;
}

std::vector<std::string> names_of(const ApproximationPorts &ports) {
    std::vector<std::string> names;
    for (const ApproximationPort &p : ports) {
        names.push_back(p.name);
    }
    return names;
}

// Fill in whatever the ports leave unset from the actual Funcs.
void fill_from_funcs(ApproximationPorts &ports, const std::vector<Func> &funcs) {
    for (size_t i = 0; i < ports.size(); i++) {
        if (!funcs[i].defined()) {
            continue;
        }
        if (!ports[i].type && funcs[i].outputs() == 1) {
            ports[i].type = funcs[i].types()[0];
        }
        if (!ports[i].dimensions) {
            ports[i].dimensions = funcs[i].dimensions();
        }
    }
}

void validate_ports(const Approximation &stage, const char *direction, const char *role,
                    const std::vector<Func> &funcs, const ApproximationPorts &ports) {
    for (size_t i = 0; i < ports.size(); i++) {
        const Func &f = funcs[i];
        if (!f.defined()) {
            continue;
        }
        const ApproximationPort &p = ports[i];
        if (p.type) {
            user_assert(f.outputs() == 1 && f.types()[0] == *p.type)
                << "Approximation '" << stage.label() << "' " << direction << ": " << role
                << " port '" << p.name << "' expects type " << *p.type << " but Func '" << f.name()
                << "' has "
                << (f.outputs() == 1 ? "type " + type_string(f.types()[0]) :
                                       "a Tuple of " + std::to_string(f.outputs()) + " values")
                << "\n";
        }
        if (p.dimensions) {
            user_assert(f.dimensions() == *p.dimensions)
                << "Approximation '" << stage.label() << "' " << direction << ": " << role
                << " port '" << p.name << "' expects " << *p.dimensions << " dimensions but Func '"
                << f.name() << "' has " << f.dimensions() << "\n";
        }
    }
}

}  // namespace

ApproximationPorts Approximation::resolve_inputs(const std::vector<Func> &inputs, const ApproximationPorts &given) const {
    const size_t n = inputs.size();
    if (!given.empty()) {
        user_assert(given.size() == n)
            << "Approximation '" << label() << "' encode: " << given.size()
            << " ports were given for " << n << " Funcs\n";
        validate_ports(*this, "encode", "input", inputs, given);
    }

    // The declared inputs are defaults for the names, and constrain the rest.
    ApproximationSignature sig = signature(given);
    const bool have_declared = sig.known && state_->impl->signature_form() != SignatureForm::None;
    if (have_declared) {
        user_assert(sig.inputs.size() == n)
            << "Approximation '" << label() << "' encode: the declared signature has "
            << sig.inputs.size() << " input ports (" << ports_string(sig.inputs) << ") but " << n
            << " Funcs were given\n";
    }

    ApproximationPorts ports = !given.empty() ? given : have_declared ? sig.inputs :
                                                                        positional_ports(n);
    if (have_declared && !given.empty()) {
        for (size_t i = 0; i < n; i++) {
            ports[i].type = sig.inputs[i].type ? sig.inputs[i].type : ports[i].type;
            ports[i].dimensions = sig.inputs[i].dimensions ? sig.inputs[i].dimensions : ports[i].dimensions;
        }
    }
    validate_ports(*this, "encode", "input", inputs, ports);
    fill_from_funcs(ports, inputs);
    return ports;
}

ApproximationPorts Approximation::resolve_encoded(const std::vector<Func> &encoded, const ApproximationSignature &sig,
                                                  const ApproximationPorts &input_ports) const {
    const size_t n = encoded.size();
    ApproximationPorts ports;
    if (sig.known) {
        user_assert(sig.outputs.size() == n)
            << "Approximation '" << label() << "' decode: the declared signature has "
            << sig.outputs.size() << " output ports (" << ports_string(sig.outputs) << ") but " << n
            << " encoded Funcs were given\n";
        ports = sig.outputs;
    } else {
        ports = n == input_ports.size() ? names_only(input_ports) : positional_ports(n);
    }
    validate_ports(*this, "decode", "encoded", encoded, ports);
    fill_from_funcs(ports, encoded);
    return ports;
}

ApproximationPorts Approximation::output_ports(const std::vector<Func> &outputs, const ApproximationSignature &sig,
                                               const ApproximationPorts &input_ports, bool encode_direction) const {
    const char *direction = encode_direction ? "encode" : "decode";
    ApproximationPorts ports;
    if (sig.known && state_->impl->signature_form() != SignatureForm::None) {
        ports = encode_direction ? sig.outputs : sig.inputs;
        user_assert(ports.size() == outputs.size())
            << "Approximation '" << label() << "' " << direction << ": the declared signature has "
            << ports.size() << (encode_direction ? " output" : " input") << " ports (" << ports_string(ports)
            << ") but the unit returned " << outputs.size() << " Funcs\n";
    } else if (encode_direction) {
        ports = outputs.size() == input_ports.size() ? names_only(input_ports) : positional_ports(outputs.size());
    } else if (sig.known) {
        // Undeclared single-Func unit: its one output is named like its input.
        ports = names_only(sig.inputs);
    } else {
        ports = outputs.size() == input_ports.size() ? names_only(input_ports) : positional_ports(outputs.size());
    }
    validate_ports(*this, direction, "output", outputs, ports);
    fill_from_funcs(ports, outputs);
    return ports;
}

ApproximationSignature Approximation::signature(const ApproximationPorts &inputs) const {
    user_assert(defined()) << "signature called on an undefined Approximation\n";
    ApproximationSignature result;
    switch (state_->impl->signature_form()) {
    case SignatureForm::Static:
        result = state_->impl->declared_signature({});
        break;
    case SignatureForm::Contextual:
        result = state_->impl->declared_signature(inputs);
        break;
    case SignatureForm::None:
        if (!state_->impl->encode_is_single() || inputs.size() > 1) {
            return ApproximationSignature::unknown(inputs);
        }
        result.inputs = inputs.empty() ? positional_ports(1) : inputs;
        result.outputs = {ApproximationPort(result.inputs[0].name)};
        return result;
    }
    // Names that flow in win over declared ones.
    if (result.known && !inputs.empty() && result.inputs.size() == inputs.size()) {
        for (size_t i = 0; i < inputs.size(); i++) {
            result.inputs[i].name = inputs[i].name;
        }
    }
    return result;
}

void Approximation::describe_to(std::string &out, const ApproximationPorts &inputs, int depth) const {
    out += std::string(depth * 2, ' ') + label() + " ";
    ApproximationSignature sig = signature(inputs);
    if (sig.known) {
        out += "(" + ports_string(sig.inputs) + ") -> (" + ports_string(sig.outputs) + ")";
    } else {
        out += "(unknown signature)";
    }
    out += '\n';
    if (sig.known) {
        for (const std::string &issue : input_range_issues(sig.inputs, inputs)) {
            out += std::string(depth * 2 + 2, ' ');
            out += "! " + issue + "\n";
        }
    }
    std::vector<Approximation> children = state_->impl->children();
    std::vector<ApproximationPorts> contexts = state_->impl->child_inputs(inputs);
    for (size_t i = 0; i < children.size(); i++) {
        if (children[i].defined()) {
            children[i].describe_to(out, i < contexts.size() ? contexts[i] : ApproximationPorts{}, depth + 1);
        }
    }
}

void Approximation::range_issues_to(std::vector<std::string> &out, const ApproximationPorts &inputs,
                                    const std::string &path) const {
    std::string here = path;
    if (!here.empty()) {
        here += " > ";
    }
    here += label();
    ApproximationSignature sig = signature(inputs);
    if (sig.known) {
        for (const std::string &issue : input_range_issues(sig.inputs, inputs)) {
            std::string line = here;
            line += ": ";
            line += issue;
            out.push_back(std::move(line));
        }
    }
    std::vector<Approximation> children = state_->impl->children();
    std::vector<ApproximationPorts> contexts = state_->impl->child_inputs(inputs);
    for (size_t i = 0; i < children.size(); i++) {
        if (children[i].defined()) {
            children[i].range_issues_to(out, i < contexts.size() ? contexts[i] : ApproximationPorts{}, here);
        }
    }
}

std::vector<std::string> check_ranges(const Approximation &a, const ApproximationPorts &inputs) {
    std::vector<std::string> out;
    if (a.defined()) {
        a.range_issues_to(out, inputs, "");
    }
    return out;
}

Func Approximation::error_bound(const std::vector<Func> &inputs, const std::vector<Func> &encoded) const {
    user_assert(defined()) << "error_bound called on an undefined Approximation\n";
    Func bound = state_->impl->error_bound(inputs, encoded);
    if (bound.defined()) {
        return bound;
    }
    if (!state_->impl->lossless() || inputs.empty() || !inputs[0].defined()) {
        return Func();
    }
    std::vector<Var> args;
    args.reserve(inputs[0].dimensions());
    for (int i = 0; i < inputs[0].dimensions(); i++) {
        args.emplace_back("zb" + std::to_string(i));
    }
    Func zero("approximation_zero_bound");
    zero(args) = cast<double>(0);
    return zero;
}

bool Approximation::lossless() const {
    user_assert(defined()) << "lossless called on an undefined Approximation\n";
    return state_->impl->lossless();
}

std::string Approximation::describe(const ApproximationPorts &inputs) const {
    if (!defined()) {
        return "<undefined>\n";
    }
    std::string out;
    describe_to(out, inputs, 0);
    return out;
}

std::ostream &operator<<(std::ostream &stream, const Approximation &approximation) {
    return stream << approximation.describe();
}

EncodeResult Approximation::encode(const std::vector<Func> &inputs, const ApproximationPorts &input_ports) const {
    user_assert(defined()) << "encode called on an undefined Approximation\n";
    ApproximationPorts resolved = resolve_inputs(inputs, input_ports);
    CallScope scope;
    std::vector<Func> encoded = state_->impl->encode(inputs, resolved);
    ApproximationPorts encoded_ports = output_ports(encoded, signature(resolved), resolved, true);
    std::vector<Func> intermediates = find_intermediates(inputs, encoded);
    ApproximationTraceNode node = scope.finish(*this, encoded, names_of(encoded_ports), intermediates, inputs, names_of(resolved));
    std::vector<ApproximationStageOutputs> stage_outputs;
    flatten(node, stage_outputs);
    return {std::move(encoded), std::move(encoded_ports), std::move(intermediates), std::move(stage_outputs),
            std::move(node)};
}

DecodeResult Approximation::decode(const std::vector<Func> &encoded, const ApproximationPorts &input_ports) const {
    user_assert(defined()) << "decode called on an undefined Approximation\n";
    // The context is static: the caller's, or else the defaults.
    ApproximationPorts context = input_ports;
    if (context.empty()) {
        ApproximationSignature defaults = signature();
        context = defaults.known ? defaults.inputs : ApproximationPorts{};
    }
    ApproximationSignature sig = signature(context);
    ApproximationPorts resolved = resolve_encoded(encoded, sig, context);
    CallScope scope;
    std::vector<Func> decoded = state_->impl->decode(encoded, context);
    ApproximationPorts decoded_ports = output_ports(decoded, sig, context, false);
    std::vector<Func> intermediates = find_intermediates(encoded, decoded);
    ApproximationTraceNode node = scope.finish(*this, decoded, names_of(decoded_ports), intermediates, encoded, names_of(resolved));
    std::vector<ApproximationStageOutputs> stage_outputs;
    flatten(node, stage_outputs);
    return {std::move(decoded), std::move(decoded_ports), std::move(intermediates), std::move(stage_outputs),
            std::move(node)};
}

std::vector<Func> Compose::encode(const std::vector<Func> &inputs, const ApproximationPorts &input_ports) const {
    user_assert(!stages.empty()) << "Compose::encode: no stages\n";
    std::vector<Func> current = inputs;
    ApproximationPorts current_ports = input_ports;
    for (const Approximation &stage : stages) {
        EncodeResult r = stage.encode(current, current_ports);
        current = std::move(r.encoded);
        current_ports = std::move(r.encoded_ports);
    }
    return current;
}

std::vector<Func> Compose::decode(const std::vector<Func> &encoded, const ApproximationPorts &input_ports) const {
    user_assert(!stages.empty()) << "Compose::decode: no stages\n";
    std::vector<ApproximationPorts> contexts = child_inputs(input_ports);
    std::vector<Func> current = encoded;
    for (int i = (int)stages.size() - 1; i >= 0; i--) {
        current = stages[i].decode(current, contexts[i]).decoded;
    }
    return current;
}

ApproximationSignature Compose::signature(const ApproximationPorts &inputs) const {
    if (stages.empty()) {
        return ApproximationSignature::unknown(inputs);
    }
    ApproximationPorts current = inputs, first_inputs;
    for (size_t i = 0; i < stages.size(); i++) {
        ApproximationSignature s = stages[i].signature(current);
        if (!s.known) {
            return ApproximationSignature::unknown(inputs);
        }
        if (i == 0) {
            first_inputs = s.inputs;
        }
        current = std::move(s.outputs);
    }
    ApproximationSignature result;
    result.inputs = std::move(first_inputs);
    result.outputs = std::move(current);
    return result;
}

std::vector<Approximation> Compose::children() const {
    return stages;
}

std::vector<ApproximationPorts> Compose::child_inputs(const ApproximationPorts &inputs) const {
    std::vector<ApproximationPorts> result;
    ApproximationPorts current = inputs;
    for (const Approximation &stage : stages) {
        result.push_back(current);
        ApproximationSignature s = stage.signature(current);
        current = s.known ? std::move(s.outputs) : ApproximationPorts{};
    }
    return result;
}

bool Compose::lossless() const {
    for (const Approximation &stage : stages) {
        if (!stage.lossless()) {
            return false;
        }
    }
    return !stages.empty();
}

bool Choose::lossless() const {
    return chosen.lossless();
}

namespace {

template<typename T>
std::vector<T> slice(const std::vector<T> &v, size_t begin, size_t count) {
    return std::vector<T>(v.begin() + begin, v.begin() + begin + count);
}

// A port passed through a by-name Parallel is not a precondition of the
// Parallel: keep the interface, drop the range.
ApproximationPort without_range(ApproximationPort port) {
    port.range.reset();
    return port;
}

// The number of inputs `child` takes when it has no context (one if unknown).
size_t input_width(const Approximation &child) {
    ApproximationSignature s = child.signature();
    return s.known && !s.inputs.empty() ? s.inputs.size() : 1;
}

// The number of encoded ports `child` produces from `context` (one if unknown).
size_t output_width(const Approximation &child, const ApproximationPorts &context) {
    ApproximationSignature s = child.signature(context);
    return s.known && !s.outputs.empty() ? s.outputs.size() : 1;
}

}  // namespace

Parallel::Parallel(std::initializer_list<Entry> entries) {
    for (const Entry &e : entries) {
        ports_.push_back(e.port);
        children_.push_back(e.child);
    }
}

bool Parallel::plan(const ApproximationPorts &inputs, std::vector<Segment> &segments, std::string *problem) const {
    auto fail = [&](const std::string &message) {
        if (problem) {
            *problem = message;
        }
        return false;
    };
    segments.clear();
    if (ports_.empty()) {
        size_t begin = 0;
        for (size_t i = 0; i < children_.size(); i++) {
            const size_t width = input_width(children_[i]);
            segments.push_back({(int)i, begin, width, {}, 0});
            begin += width;
        }
        if (!inputs.empty() && begin != inputs.size()) {
            return fail("the children take " + std::to_string(begin) + " Funcs in total, but there are " +
                        std::to_string(inputs.size()) + " (the ports are: " + ports_string(names_only(inputs)) + ")");
        }
        for (Segment &seg : segments) {
            if (!inputs.empty()) {
                seg.context = slice(inputs, seg.begin, seg.width);
            }
            seg.outputs = output_width(children_[seg.child], seg.context);
        }
        return true;
    }

    std::vector<int> owner(inputs.size(), -1);
    for (size_t i = 0; i < ports_.size(); i++) {
        size_t match = 0, count = 0;
        for (size_t j = 0; j < inputs.size(); j++) {
            if (inputs[j].name == ports_[i]) {
                match = j;
                count++;
            }
        }
        if (count != 1) {
            return fail(std::string("there is ") + (count == 0 ? "no" : "more than one") + " input port named '" +
                        ports_[i] + "' (the ports are: " + ports_string(names_only(inputs)) + ")");
        }
        if (owner[match] >= 0) {
            return fail("the port '" + ports_[i] + "' is routed by more than one entry");
        }
        owner[match] = (int)i;
    }
    for (size_t j = 0; j < inputs.size(); j++) {
        Segment seg{owner[j], j, 1, {inputs[j]}, 1};
        if (owner[j] >= 0) {
            seg.outputs = output_width(children_[owner[j]], seg.context);
        }
        segments.push_back(seg);
    }
    return true;
}

std::vector<Func> Parallel::encode(const std::vector<Func> &inputs, const ApproximationPorts &input_ports) const {
    ApproximationPorts ports = input_ports.size() == inputs.size() ? input_ports : positional_ports(inputs.size());
    std::vector<Segment> segments;
    std::string problem;
    user_assert(plan(ports, segments, &problem)) << "Parallel::encode: " << problem << "\n";
    std::vector<Func> result;
    for (const Segment &seg : segments) {
        std::vector<Func> in = slice(inputs, seg.begin, seg.width);
        if (seg.child < 0) {
            result.insert(result.end(), in.begin(), in.end());
        } else {
            std::vector<Func> out = children_[seg.child].encode(in, seg.context).encoded;
            result.insert(result.end(), out.begin(), out.end());
        }
    }
    return result;
}

std::vector<Func> Parallel::decode(const std::vector<Func> &encoded, const ApproximationPorts &input_ports) const {
    std::vector<Segment> segments;
    std::string problem;
    user_assert(plan(input_ports, segments, &problem)) << "Parallel::decode: " << problem << "\n";
    size_t total = 0;
    for (const Segment &seg : segments) {
        total += seg.outputs;
    }
    user_assert(total == encoded.size())
        << "Parallel::decode: the children produce " << total << " Funcs in total, but " << encoded.size()
        << " were given\n";
    std::vector<Func> result;
    size_t begin = 0;
    for (const Segment &seg : segments) {
        std::vector<Func> in = slice(encoded, begin, seg.outputs);
        begin += seg.outputs;
        if (seg.child < 0) {
            result.insert(result.end(), in.begin(), in.end());
            continue;
        }
        std::vector<Func> out = children_[seg.child].decode(in, seg.context).decoded;
        user_assert(ports_.empty() || out.size() == 1)
            << "Parallel::decode: the child for port '" << ports_[seg.child] << "' decoded to " << out.size()
            << " Funcs, but must decode to one\n";
        result.insert(result.end(), out.begin(), out.end());
    }
    return result;
}

ApproximationSignature Parallel::signature(const ApproximationPorts &inputs) const {
    std::vector<Segment> segments;
    if (children_.empty() || (!ports_.empty() && inputs.empty()) || !plan(inputs, segments, nullptr)) {
        return ApproximationSignature::unknown(inputs);
    }
    ApproximationSignature result;
    for (const Segment &seg : segments) {
        if (seg.child < 0) {
            result.inputs.push_back(without_range(seg.context[0]));
            result.outputs.push_back(seg.context[0]);
            continue;
        }
        ApproximationSignature s = children_[seg.child].signature(seg.context);
        if (!s.known || s.inputs.size() != seg.width) {
            return ApproximationSignature::unknown(inputs);
        }
        result.inputs.insert(result.inputs.end(), s.inputs.begin(), s.inputs.end());
        result.outputs.insert(result.outputs.end(), s.outputs.begin(), s.outputs.end());
    }
    return result;
}

std::vector<Approximation> Parallel::children() const {
    return children_;
}

std::vector<ApproximationPorts> Parallel::child_inputs(const ApproximationPorts &inputs) const {
    std::vector<ApproximationPorts> result(children_.size());
    std::vector<Segment> segments;
    if (plan(inputs, segments, nullptr)) {
        for (const Segment &seg : segments) {
            if (seg.child >= 0) {
                result[seg.child] = seg.context;
            }
        }
    }
    return result;
}

bool Parallel::lossless() const {
    for (const Approximation &child : children_) {
        if (!child.lossless()) {
            return false;
        }
    }
    return !children_.empty();
}

std::vector<Func> TrustedInverse::encode(const std::vector<Func> &inputs, const ApproximationPorts &input_ports) const {
    return encoder.encode(inputs, input_ports).encoded;
}

std::vector<Func> TrustedInverse::decode(const std::vector<Func> &encoded, const ApproximationPorts &input_ports) const {
    return decoder.decode(encoded, input_ports).decoded;
}

ApproximationSignature TrustedInverse::signature(const ApproximationPorts &inputs) const {
    return encoder.signature(inputs);
}

std::vector<Approximation> TrustedInverse::children() const {
    return {encoder, decoder};
}

std::vector<ApproximationPorts> TrustedInverse::child_inputs(const ApproximationPorts &inputs) const {
    return {inputs, {}};
}

std::vector<Func> Choose::encode(const std::vector<Func> &inputs, const ApproximationPorts &input_ports) const {
    return chosen.encode(inputs, input_ports).encoded;
}

std::vector<Func> Choose::decode(const std::vector<Func> &encoded, const ApproximationPorts &input_ports) const {
    return chosen.decode(encoded, input_ports).decoded;
}

ApproximationSignature Choose::signature(const ApproximationPorts &inputs) const {
    return chosen.signature(inputs);
}

std::vector<Approximation> Choose::children() const {
    return {chosen};
}

std::vector<ApproximationPorts> Choose::child_inputs(const ApproximationPorts &inputs) const {
    return {inputs};
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

Pointwise Pointwise::with_error_bound(ExprFn bound) const {
    Pointwise copy = *this;
    copy.bound_fn = std::move(bound);
    return copy;
}

Pointwise Pointwise::with_types(Type input_type, Type output_type) const {
    Pointwise copy = *this;
    copy.input_type = input_type;
    copy.output_type = output_type;
    return copy;
}

Pointwise Pointwise::with_ranges(ApproximationRange input_range, ApproximationRange output_range) const {
    Pointwise copy = *this;
    copy.input_range = input_range;
    copy.output_range = output_range;
    return copy;
}

Pointwise Pointwise::with_lossless(bool is_lossless) const {
    Pointwise copy = *this;
    copy.lossless_ = is_lossless;
    return copy;
}

ApproximationSignature Pointwise::signature(const ApproximationPorts &inputs) const {
    if (inputs.size() > 1) {
        return ApproximationSignature::unknown(inputs);
    }
    std::optional<int> dims;
    std::string name = positional_ports(1)[0].name;
    if (inputs.size() == 1) {
        dims = inputs[0].dimensions;
        name = inputs[0].name;
    }
    return {{{name, input_type, dims, input_range}}, {{name, output_type, dims, output_range}}};
}

Func Pointwise::error_bound(const std::vector<Func> &inputs, const std::vector<Func> &) const {
    if (!bound_fn) {
        return Func();
    }
    user_assert(inputs.size() == 1 && inputs[0].outputs() == 1)
        << "Pointwise::error_bound requires a single-valued input Func\n";
    return apply_pointwise(
        inputs[0], wrap_expr_fn([f = bound_fn](const Expr &x) { return f(x); }),
        encode_name + "_bound", var_prefix, "error_bound");
}

std::vector<Func> Identity::encode(const std::vector<Func> &inputs) const {
    return inputs;
}

std::vector<Func> Identity::decode(const std::vector<Func> &encoded) const {
    return encoded;
}

ApproximationSignature Identity::signature(const ApproximationPorts &inputs) const {
    if (inputs.empty()) {
        return ApproximationSignature::unknown();
    }
    return {inputs, inputs};
}

ApproximationSignature Permute::signature(const ApproximationPorts &inputs) const {
    ApproximationPorts ports = inputs.empty() ? positional_ports(forward.size()) : inputs;
    if (ports.size() != forward.size()) {
        return ApproximationSignature::unknown(inputs);
    }
    ApproximationSignature result;
    result.inputs = ports;
    for (int source : forward) {
        result.outputs.push_back(ports[source]);
    }
    return result;
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

namespace {

std::vector<Var> component_vars(int dimensions, const std::string &prefix) {
    std::vector<Var> vars;
    vars.reserve(dimensions);
    for (int i = 0; i < dimensions; ++i) {
        vars.emplace_back(prefix + std::to_string(i));
    }
    return vars;
}

std::vector<Expr> component_exprs(const std::vector<Var> &vars) {
    return std::vector<Expr>(vars.begin(), vars.end());
}

// The trailing dimensions a rank-polymorphic unit passes through unchanged.
std::vector<Var> batch_vars(int dimensions) {
    return component_vars(dimensions, "batch");
}

// A single context port's dimensionality, if it is known.
std::optional<int> context_dimensions(const ApproximationPorts &inputs) {
    return inputs.size() == 1 ? inputs[0].dimensions : std::nullopt;
}

}  // namespace

std::vector<Func> BlockReshape::encode(const std::vector<Func> &inputs) const {
    user_assert(inputs.size() == 1) << "BlockReshape::encode expects one input\n";
    const Func &flat = inputs[0];
    const int flat_dims = block_indexed_ ? 2 : 1;
    user_assert(flat.dimensions() >= flat_dims)
        << "BlockReshape::encode requires at least " << flat_dims << " dimensions, but "
        << flat.name() << " has " << flat.dimensions() << "\n";
    std::vector<Var> rest = batch_vars(flat.dimensions() - flat_dims);
    std::vector<Var> dims = block_vars();
    Var blk("blk");
    Expr within = cast<int>(0);
    int stride = 1;
    for (size_t i = 0; i < dims.size(); ++i) {
        within += dims[i] * stride;
        stride *= extents_[i];
    }
    std::vector<Var> args = dims;
    args.push_back(blk);
    args.insert(args.end(), rest.begin(), rest.end());
    std::vector<Expr> flat_args;
    if (block_indexed_) {
        flat_args = {within, blk};
    } else {
        flat_args = {blk * block_size() + within};
    }
    flat_args.insert(flat_args.end(), rest.begin(), rest.end());
    Func packed("block_reshape_packed");
    packed(args) = flat(flat_args);
    return {packed};
}

std::vector<Func> BlockReshape::decode(const std::vector<Func> &encoded) const {
    user_assert(encoded.size() == 1) << "BlockReshape::decode expects one input\n";
    const Func &packed = encoded[0];
    const int block_dims = (int)extents_.size() + 1;
    user_assert(packed.dimensions() >= block_dims)
        << "BlockReshape::decode requires at least " << block_dims << " dimensions, but "
        << packed.name() << " has " << packed.dimensions() << "\n";
    std::vector<Var> rest = batch_vars(packed.dimensions() - block_dims);
    Var k("k"), kk("kk"), blk("blk");
    Expr within = block_indexed_ ? Expr(kk) : k % block_size();
    Expr block = block_indexed_ ? Expr(blk) : k / block_size();
    std::vector<Expr> args;
    Expr rem = within;
    for (int extent : extents_) {
        args.push_back(rem % extent);
        rem /= extent;
    }
    args.push_back(block);
    args.insert(args.end(), rest.begin(), rest.end());
    std::vector<Var> out_args;
    if (block_indexed_) {
        out_args = {kk, blk};
    } else {
        out_args = {k};
    }
    out_args.insert(out_args.end(), rest.begin(), rest.end());
    Func out("block_reshape_unpacked");
    out(out_args) = packed(args);
    return {out};
}

ApproximationSignature BlockReshape::signature(const ApproximationPorts &inputs) const {
    if (inputs.size() > 1) {
        return ApproximationSignature::unknown(inputs);
    }
    const int flat_dims = block_indexed_ ? 2 : 1;
    const int block_dims = (int)extents_.size() + 1;
    ApproximationPort values("values"), blocks("blocks");
    if (inputs.size() == 1) {
        values.type = blocks.type = inputs[0].type;
        blocks.range = inputs[0].range;
    }
    if (std::optional<int> dims = context_dimensions(inputs)) {
        // Too few dimensions: declare the minimum, so validation reports it.
        const int rest = std::max(*dims - flat_dims, 0);
        values.dimensions = flat_dims + rest;
        blocks.dimensions = block_dims + rest;
    }
    return {{values}, {blocks}};
}

int BlockReshape::block_size() const {
    int size = 1;
    for (int extent : extents_) {
        size *= extent;
    }
    return size;
}

std::vector<Var> BlockReshape::block_vars() const {
    std::vector<Var> vars;
    vars.reserve(extents_.size());
    for (size_t i = 0; i < extents_.size(); ++i) {
        vars.emplace_back(extents_.size() == 1 ? "kk" : "d" + std::to_string(i));
    }
    return vars;
}

StructLayout::StructLayout(Type record_type, std::vector<std::string> logical_fields,
                           std::optional<int> record_dimensions)
    : record_type_(record_type), logical_fields_(std::move(logical_fields)),
      record_dimensions_(record_dimensions) {
    user_assert(record_type_.is_struct()) << "StructLayout requires a struct Type\n";
    user_assert(!record_dimensions_ || *record_dimensions_ > 0)
        << "StructLayout record dimensionality must be positive\n";
    const StructTypeInfo *info = record_type_.struct_type();
    user_assert(logical_fields_.size() == info->fields.size())
        << "StructLayout requires exactly one logical slot per physical field\n";
    for (const std::string &name : logical_fields_) {
        int matches = 0;
        for (const StructField &field : info->fields) {
            matches += field.name == name;
        }
        user_assert(matches == 1) << "StructLayout: no unique field named '" << name << "'\n";
        int logical_matches = 0;
        for (const std::string &logical_name : logical_fields_) {
            logical_matches += logical_name == name;
        }
        user_assert(logical_matches == 1) << "StructLayout: duplicate logical field '" << name << "'\n";
    }
}

std::vector<Func> StructLayout::encode(const std::vector<Func> &inputs) const {
    user_assert(inputs.size() == logical_fields_.size())
        << "StructLayout::encode input count does not match logical field count\n";
    const StructTypeInfo *info = record_type_.struct_type();
    // Without a declared dimensionality, the first slot decides it.
    const int record_dimensions =
        record_dimensions_ ? *record_dimensions_ :
                             inputs[0].dimensions() - (physical_field(logical_fields_[0]).array_extent ? 1 : 0);
    std::vector<Var> records = component_vars(record_dimensions, "record");
    std::vector<Expr> record_args = component_exprs(records);
    std::vector<Expr> values;
    for (const StructField &field : info->fields) {
        size_t slot = logical_slot(field.name);
        const Func &input = inputs[slot];
        user_assert(input.outputs() == 1 && input.types()[0] == field.type)
            << "StructLayout field '" << field.name << "' requires exact type " << field.type
            << " but slot " << slot << " has " << input.types()[0] << "\n";
        int extent = field.array_extent.value_or(1);
        user_assert(input.dimensions() == record_dimensions + (field.array_extent ? 1 : 0))
            << "StructLayout field '" << field.name << "' has the wrong dimensionality\n";
        for (int element = 0; element < extent; ++element) {
            std::vector<Expr> args = record_args;
            if (field.array_extent) {
                args.insert(args.begin(), element);
            }
            values.push_back(input(args));
        }
    }
    Func packed("struct_layout_packed");
    packed(records) = pack_struct(record_type_, values);
    return {packed};
}

std::vector<Func> StructLayout::decode(const std::vector<Func> &encoded) const {
    user_assert(encoded.size() == 1 && encoded[0].outputs() == 1 &&
                encoded[0].types()[0] == record_type_)
        << "StructLayout::decode requires one Func of the exact record type\n";
    const Func &packed = encoded[0];
    user_assert(!record_dimensions_ || packed.dimensions() == *record_dimensions_)
        << "StructLayout::decode requires " << *record_dimensions_ << " record dimensions, but "
        << packed.name() << " has " << packed.dimensions() << "\n";
    std::vector<Var> records = component_vars(packed.dimensions(), "record");
    std::vector<Expr> record_args = component_exprs(records);
    Expr record = packed(record_args);
    std::vector<Func> outputs;
    outputs.reserve(logical_fields_.size());
    for (const std::string &name : logical_fields_) {
        const StructField &physical = physical_field(name);
        Func output("struct_layout_" + name);
        if (physical.array_extent) {
            Var element("element");
            std::vector<Var> args = records;
            args.insert(args.begin(), element);
            output(args) = field(record, name)[element];
        } else {
            output(records) = field(record, name);
        }
        outputs.push_back(output);
    }
    return outputs;
}

ApproximationSignature StructLayout::signature(const ApproximationPorts &inputs) const {
    std::optional<int> record_dimensions = record_dimensions_;
    // Without a declared dimensionality, the first slot with a known one
    // decides it, so validation reports any slot that disagrees.
    for (size_t i = 0; !record_dimensions && i < inputs.size() && i < logical_fields_.size(); i++) {
        if (inputs[i].dimensions) {
            record_dimensions = *inputs[i].dimensions - (physical_field(logical_fields_[i]).array_extent ? 1 : 0);
        }
    }
    ApproximationSignature sig;
    for (const std::string &name : logical_fields_) {
        const StructField &f = physical_field(name);
        std::optional<int> dims;
        if (record_dimensions) {
            dims = *record_dimensions + (f.array_extent ? 1 : 0);
        }
        sig.inputs.emplace_back(name, f.type, dims);
    }
    sig.outputs = {{"record", record_type_, record_dimensions}};
    return sig;
}

size_t StructLayout::logical_slot(const std::string &name) const {
    for (size_t i = 0; i < logical_fields_.size(); ++i) {
        if (logical_fields_[i] == name) {
            return i;
        }
    }
    user_error << "StructLayout internal field mapping failure\n";
    return 0;
}

const StructField &StructLayout::physical_field(const std::string &name) const {
    for (const StructField &field : record_type_.struct_type()->fields) {
        if (field.name == name) {
            return field;
        }
    }
    user_error << "StructLayout internal physical field failure\n";
    return record_type_.struct_type()->fields[0];
}

std::vector<Func> PlanarFieldPack::encode(const std::vector<Func> &inputs) const {
    user_assert(inputs.size() == 1 && inputs[0].dimensions() >= 2)
        << "PlanarFieldPack::encode requires (element, record, ...)\n";
    const Func &fields = inputs[0];
    Var position("position"), record("record");
    std::vector<Var> rest = batch_vars(fields.dimensions() - 2);
    std::vector<Expr> args = {position, record};
    args.insert(args.end(), rest.begin(), rest.end());
    RDom plane(0, planes_, "plane");
    std::vector<Expr> field_args = args;
    field_args[0] = plane * positions_ + position;
    Expr value = cast<uint8_t>(fields(field_args)) & ((1 << field_bits_) - 1);
    Func bytes("planar_field_bytes");
    bytes(args) = cast<uint8_t>(0);
    bytes(args) = bytes(args) | cast<uint8_t>(value << (plane * field_bits_));
    return {bytes};
}

std::vector<Func> PlanarFieldPack::decode(const std::vector<Func> &encoded) const {
    user_assert(encoded.size() == 1 && encoded[0].types() == std::vector<Type>{UInt(8)} &&
                encoded[0].dimensions() >= 2)
        << "PlanarFieldPack::decode requires (position, record, ...) bytes\n";
    const Func &bytes = encoded[0];
    Var element("element"), record("record");
    std::vector<Var> rest = batch_vars(bytes.dimensions() - 2);
    std::vector<Var> args = {element, record};
    args.insert(args.end(), rest.begin(), rest.end());
    std::vector<Expr> byte_args = component_exprs(args);
    Expr plane = element / positions_;
    byte_args[0] = element % positions_;
    Func fields("planar_field_values");
    fields(args) = cast<uint8_t>((bytes(byte_args) >> (plane * field_bits_)) & ((1 << field_bits_) - 1));
    return {fields};
}

ApproximationSignature PlanarFieldPack::signature(const ApproximationPorts &inputs) const {
    if (inputs.size() > 1) {
        return ApproximationSignature::unknown(inputs);
    }
    std::optional<int> dims = context_dimensions(inputs);
    if (dims) {
        // Too few dimensions: declare the minimum, so validation reports it.
        dims = std::max(*dims, 2);
    }
    std::optional<Type> type = inputs.size() == 1 ? inputs[0].type : std::nullopt;
    return {{{"fields", type, dims, ApproximationRange(0, (double)((1 << field_bits_) - 1))}},
            {{"bytes", UInt(8), dims}}};
}

PlanarFieldPack::PlanarFieldPack(int field_bits, int positions)
    : field_bits_(field_bits), positions_(positions), planes_(8 / field_bits) {
    user_assert(field_bits_ > 0 && 8 % field_bits_ == 0 && positions_ > 0)
        << "Invalid PlanarFieldPack shape\n";
}

namespace Internal {

std::vector<Func> little_endian_scalar_encode(Type word_type, const std::vector<Func> &inputs) {
    user_assert(inputs.size() == 1 && inputs[0].types() == std::vector<Type>{word_type})
        << "LittleEndianScalarPack::encode word type mismatch\n";
    const Func &word = inputs[0];
    std::vector<Var> records = component_vars(word.dimensions(), "record");
    std::vector<Expr> record_args(records.begin(), records.end());
    Var byte("byte");
    std::vector<Var> args = records;
    args.insert(args.begin(), byte);
    Func bytes("little_endian_scalar_bytes");
    Expr bits = cast(word_type, word(record_args));
    bytes(args) = cast<uint8_t>(bits >> (byte * 8));
    return {bytes};
}

std::vector<Func> little_endian_scalar_decode(Type word_type, const std::vector<Func> &encoded) {
    user_assert(encoded.size() == 1 && encoded[0].types() == std::vector<Type>{UInt(8)} &&
                encoded[0].dimensions() >= 2)
        << "LittleEndianScalarPack::decode requires byte arrays per record\n";
    const Func &bytes = encoded[0];
    std::vector<Var> records = component_vars(bytes.dimensions() - 1, "record");
    std::vector<Expr> record_args(records.begin(), records.end());
    std::vector<Expr> pieces;
    for (int i = 0; i < word_type.bytes(); ++i) {
        std::vector<Expr> args = record_args;
        args.insert(args.begin(), i);
        pieces.push_back(bytes(args));
    }
    Func word("little_endian_scalar_word");
    word(records) = cast(word_type, concat_bits(pieces));
    return {word};
}

ApproximationSignature little_endian_scalar_signature(Type word_type, const ApproximationPorts &inputs) {
    if (inputs.size() > 1) {
        return ApproximationSignature::unknown(inputs);
    }
    std::optional<int> dims;
    if (inputs.size() == 1) {
        dims = inputs[0].dimensions;
    }
    return {{{inputs.empty() ? "word" : inputs[0].name, word_type, dims}},
            {{"bytes", UInt(8), dims ? std::optional<int>(*dims + 1) : std::nullopt}}};
}

}  // namespace Internal

}  // namespace Halide
