#include "Approximation.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <numeric>
#include <ostream>
#include <set>
#include <sstream>
#include <tuple>

#if defined(__GNUC__) || defined(__clang__)
#include <cstdlib>
#endif

#include "Error.h"
#include "FindCalls.h"
#include "Function.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "IRVisitor.h"
#include "Simplify.h"
#include "StrictifyFloat.h"
#include "Substitute.h"
#include "Util.h"

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

BlockReshape BlockReshape::tiles(std::vector<int> blocks) {
    user_assert(!blocks.empty()) << "BlockReshape::tiles requires at least one block extent\n";
    for (int b : blocks) {
        user_assert(b > 0) << "BlockReshape::tiles block extents must be positive, but got " << b << "\n";
    }
    BlockReshape result(1);
    result.tiles_ = std::move(blocks);
    return result;
}

std::vector<Func> BlockReshape::encode(const std::vector<Func> &inputs) const {
    user_assert(inputs.size() == 1) << "BlockReshape::encode expects one input\n";
    const Func &flat = inputs[0];
    if (!tiles_.empty()) {
        const int k = (int)tiles_.size();
        user_assert(flat.dimensions() >= k)
            << "BlockReshape::tiles encode requires at least " << k << " dimensions, but "
            << flat.name() << " has " << flat.dimensions() << "\n";
        std::vector<Var> within = component_vars(k, "within");
        std::vector<Var> tile = component_vars(k, "tile");
        std::vector<Var> rest = batch_vars(flat.dimensions() - k);
        std::vector<Var> args = within;
        args.insert(args.end(), tile.begin(), tile.end());
        args.insert(args.end(), rest.begin(), rest.end());
        std::vector<Expr> flat_args;
        flat_args.reserve(k + rest.size());
        for (int i = 0; i < k; i++) {
            flat_args.push_back(tile[i] * tiles_[i] + within[i]);
        }
        flat_args.insert(flat_args.end(), rest.begin(), rest.end());
        Func packed("block_reshape_packed");
        packed(args) = flat(flat_args);
        return {packed};
    }
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
    if (!tiles_.empty()) {
        const int k = (int)tiles_.size();
        user_assert(packed.dimensions() >= 2 * k)
            << "BlockReshape::tiles decode requires at least " << 2 * k << " dimensions, but "
            << packed.name() << " has " << packed.dimensions() << "\n";
        std::vector<Var> xs = component_vars(k, "x");
        std::vector<Var> rest = batch_vars(packed.dimensions() - 2 * k);
        std::vector<Expr> args;
        args.reserve(2 * k + rest.size());
        for (int i = 0; i < k; i++) {
            args.push_back(xs[i] % tiles_[i]);
        }
        for (int i = 0; i < k; i++) {
            args.push_back(xs[i] / tiles_[i]);
        }
        args.insert(args.end(), rest.begin(), rest.end());
        std::vector<Var> out_args = xs;
        out_args.insert(out_args.end(), rest.begin(), rest.end());
        Func out("block_reshape_unpacked");
        out(out_args) = packed(args);
        return {out};
    }
    const int block_dims = (int)extents_.size() + 1;
    user_assert(packed.dimensions() >= block_dims)
        << "BlockReshape::decode requires at least " << block_dims << " dimensions, but "
        << packed.name() << " has " << packed.dimensions() << "\n";
    std::vector<Var> rest = batch_vars(packed.dimensions() - block_dims);
    Var k("k"), kk("kk"), blk("blk");
    Expr within = block_indexed_ ? Expr(kk) : k % block_size();
    Expr block = block_indexed_ ? Expr(blk) : k / block_size();
    std::vector<Expr> args;
    // `within` is in [0, block_size()), so what's left of it for the last
    // extent is already in range. Leaving off its % keeps bounds inference
    // exact on pieces of a record (as at a loop over a split of `within`).
    Expr rem = within;
    for (size_t i = 0; i + 1 < extents_.size(); ++i) {
        args.push_back(rem % extents_[i]);
        rem /= extents_[i];
    }
    args.push_back(rem);
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
    const int flat_dims = !tiles_.empty() ? (int)tiles_.size() : block_indexed_ ? 2 :
                                                                                  1;
    const int block_dims = !tiles_.empty() ? 2 * (int)tiles_.size() : (int)extents_.size() + 1;
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
    user_assert(record_dimensions > 0)
        << "StructLayout record dimensionality must be positive, but slot 0 ("
        << inputs[0].name() << ") implies " << record_dimensions << "\n";
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
    user_assert(packed.dimensions() > 0)
        << "StructLayout record dimensionality must be positive, but "
        << packed.name() << " has 0 dimensions\n";
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
            user_assert(*record_dimensions > 0)
                << "StructLayout record dimensionality must be positive, but slot " << i
                << " implies " << *record_dimensions << "\n";
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

const Derive *Approximation::as_derive() const {
    return defined() ? state_->impl->derive() : nullptr;
}

namespace {

int index_of(const std::vector<std::string> &names, const std::string &name) {
    for (size_t i = 0; i < names.size(); i++) {
        if (names[i] == name) {
            return (int)i;
        }
    }
    return -1;
}

std::string joined(const std::vector<std::string> &names) {
    std::string out;
    for (const std::string &n : names) {
        out += (out.empty() ? "" : ", ") + n;
    }
    return out;
}

}  // namespace

Derive::Derive(std::string port, std::vector<std::string> from, Definition psi)
    : port_(std::move(port)), from_(std::move(from)), psi_(std::move(psi)) {
    user_assert(!port_.empty() && psi_) << "Derive requires a port name and a definition\n";
    user_assert(!from_.empty()) << "Derive(" << port_ << ") must read at least one port\n";
    for (size_t i = 0; i < from_.size(); i++) {
        user_assert(from_[i] != port_ && index_of(std::vector<std::string>(from_.begin(), from_.begin() + i), from_[i]) < 0)
            << "Derive(" << port_ << "): the ports it reads must be distinct and differ from the port it derives\n";
    }
}

std::string Derive::name() const {
    return "Derive(" + port_ + ")";
}

Func Derive::define(const std::vector<Func> &from) const {
    user_assert(from.size() == from_.size())
        << name() << ": the definition reads " << from_.size() << " ports, but was given " << from.size() << "\n";
    Func f = psi_(from);
    user_assert(f.defined()) << name() << ": the definition returned an undefined Func\n";
    return f;
}

std::vector<Func> Derive::encode(const std::vector<Func> &inputs, const ApproximationPorts &input_ports) const {
    std::vector<std::string> names = names_of(input_ports);
    user_assert(index_of(names, port_) < 0)
        << name() << ": there is already a port named '" << port_ << "' (ports: " << joined(names) << ")\n";
    std::vector<Func> from;
    for (const std::string &n : from_) {
        int i = index_of(names, n);
        user_assert(i >= 0) << name() << ": no input port '" << n << "' (ports: " << joined(names) << ")\n";
        from.push_back(inputs[i]);
    }
    std::vector<Func> out = inputs;
    out.push_back(define(from));
    return out;
}

std::vector<Func> Derive::decode(const std::vector<Func> &encoded, const ApproximationPorts &input_ports) const {
    user_assert(!encoded.empty() && (input_ports.empty() || encoded.size() == input_ports.size() + 1))
        << name() << "::decode expects its inputs' ports followed by the derived port\n";
    return std::vector<Func>(encoded.begin(), encoded.end() - 1);
}

ApproximationSignature Derive::signature(const ApproximationPorts &inputs) const {
    std::vector<std::string> names = names_of(inputs);
    for (const std::string &n : from_) {
        if (index_of(names, n) < 0) {
            return ApproximationSignature::unknown(inputs);
        }
    }
    ApproximationSignature s{inputs, inputs};
    s.outputs.emplace_back(port_);
    return s;
}

namespace {

void flatten_nodes(const ApproximationTraceNode &node, std::vector<const ApproximationTraceNode *> &out) {
    for (const ApproximationTraceNode &c : node.children) {
        flatten_nodes(c, out);
    }
    out.push_back(&node);
}

std::set<std::string> func_names(const std::vector<Func> &fs) {
    std::set<std::string> names;
    for (const Func &f : fs) {
        if (f.defined()) {
            names.insert(f.name());
        }
    }
    return names;
}

// Read the derived port's relation off the stages that follow Derive in
// encode order: lossless stages pass; at most one lossy stage may follow, on
// the port alone, and it must declare a bound. Sets exact, error_bound,
// lossy_stages, or bound_problem.
void declare_bound(DerivedPort &p, const Derive &d, const std::vector<const ApproximationTraceNode *> &enc_nodes,
                   const std::vector<const ApproximationTraceNode *> &dec_nodes) {
    size_t at = enc_nodes.size(), count = 0;
    for (size_t i = 0; i < enc_nodes.size(); i++) {
        if (enc_nodes[i]->stage.same_as(p.stage)) {
            at = i;
            count++;
        }
    }
    if (count != 1) {
        p.bound_problem = count ? "the stage is encoded more than once" : "the stage is not in the encode trace";
        return;
    }
    const ApproximationTraceNode &node = *enc_nodes[at];
    int port_index = index_of(node.port_names, d.port());
    internal_assert(port_index >= 0);
    std::string derived_func = node.ports[port_index].name();
    std::set<std::string> derived = {derived_func}, from;
    for (const std::string &n : d.from()) {
        int i = index_of(node.port_names, n);
        internal_assert(i >= 0);
        from.insert(node.ports[i].name());
    }
    std::vector<const ApproximationTraceNode *> lossy;
    for (size_t i = at + 1; i < enc_nodes.size(); i++) {
        const ApproximationTraceNode &n = *enc_nodes[i];
        if (!n.children.empty()) {
            continue;
        }
        std::set<std::string> inputs = func_names(n.inputs);
        for (size_t k = 0; k < n.ports.size(); k++) {
            const Func &out = n.ports[k];
            if (inputs.count(out.name())) {
                continue;  // passed through: it keeps its own taint
            }
            // The stage's inputs this output is computed from.
            std::set<std::string> reached, visited;
            std::vector<Internal::Function> stack = {out.function()};
            while (!stack.empty()) {
                Internal::Function f = stack.back();
                stack.pop_back();
                if (inputs.count(f.name())) {
                    reached.insert(f.name());
                } else if (visited.insert(f.name()).second) {
                    for (const auto &[name, callee] : Internal::find_direct_calls(f)) {
                        stack.push_back(callee);
                    }
                }
            }
            bool reads_derived = false, reads_from = false;
            for (const std::string &r : reached) {
                reads_derived |= derived.count(r) > 0;
                reads_from |= from.count(r) > 0;
            }
            if (!n.stage.lossless() && (reads_derived || reads_from)) {
                if (reads_from) {
                    p.bound_problem = "the lossy stage " + n.label + " follows " + d.name() +
                                      " on a port its definition reads (" + joined(d.from()) +
                                      "), so the stored port is not within a declared bound of the definition " +
                                      "of the decoded ports (bounds do not compose)";
                    return;
                }
                bool alone = reached.size() == 1 && derived.count(*reached.begin());
                if (!alone || (lossy.empty() && *reached.begin() != derived_func)) {
                    p.bound_problem = "the lossy stage " + n.label + " follows " + d.name() +
                                      ", but not on the derived port alone, as psi produced it";
                    return;
                }
                if (lossy.empty() || lossy.back() != &n) {
                    lossy.push_back(&n);
                }
            }
            if (reads_derived) {
                derived.insert(out.name());
            }
            if (reads_from) {
                from.insert(out.name());
            }
        }
    }
    for (const ApproximationTraceNode *n : lossy) {
        p.lossy_stages.push_back(n->stage);
    }
    if (lossy.empty()) {
        p.exact = true;
        return;
    }
    if (lossy.size() > 1) {
        std::vector<std::string> labels;
        for (const ApproximationTraceNode *n : lossy) {
            labels.push_back(n->label);
        }
        p.bound_problem = "more than one lossy stage follows " + d.name() + " (" + joined(labels) +
                          "): bounds do not compose";
        return;
    }
    const ApproximationTraceNode &s = *lossy[0];
    std::vector<Func> encoded;
    size_t dec_count = 0;
    for (const ApproximationTraceNode *n : dec_nodes) {
        if (n->stage.same_as(s.stage)) {
            encoded = n->inputs;
            dec_count++;
        }
    }
    if (dec_count != 1) {
        encoded.clear();
    }
    p.error_bound = s.stage.error_bound({p.definition}, encoded);
    if (!p.error_bound.defined()) {
        p.bound_problem = "the lossy stage " + s.label + " that follows " + d.name() + " declares no error bound";
    }
}

}  // namespace

std::vector<DerivedPort> derived_ports(const ApproximationTraceNode &encode_trace,
                                       const ApproximationTraceNode &decode_trace) {
    std::vector<const ApproximationTraceNode *> enc_nodes, dec_nodes;
    flatten_nodes(encode_trace, enc_nodes);
    flatten_nodes(decode_trace, dec_nodes);
    std::vector<DerivedPort> result;
    for (const ApproximationTraceNode *node : dec_nodes) {
        const Derive *d = node->stage.defined() ? node->stage.as_derive() : nullptr;
        if (!d) {
            continue;
        }
        DerivedPort p;
        p.stage = node->stage;
        p.name = d->port();
        p.from_names = d->from();
        int stored = index_of(node->input_names, d->port());
        if (stored < 0 && !node->inputs.empty()) {
            stored = (int)node->inputs.size() - 1;
        }
        user_assert(stored >= 0) << d->name() << ": its decode received no derived port\n";
        p.stored = node->inputs[stored];
        for (const std::string &n : d->from()) {
            int i = index_of(node->input_names, n);
            user_assert(i >= 0) << d->name() << ": its decode received no port '" << n
                                << "' (ports: " << joined(node->input_names) << ")\n";
            p.from.push_back(node->inputs[i]);
        }
        p.definition = d->define(p.from);
        user_assert(p.definition.dimensions() == p.stored.dimensions())
            << d->name() << ": the definition has " << p.definition.dimensions() << " dimensions, but the port has "
            << p.stored.dimensions() << "\n";
        declare_bound(p, *d, enc_nodes, dec_nodes);
        result.push_back(std::move(p));
    }
    return result;
}

std::vector<DerivedPort> ApproximationResult::derived_ports() const {
    return Halide::derived_ports(encode_trace, decode_trace);
}

DerivedPort ApproximationResult::derived_port(const std::string &name) const {
    std::vector<DerivedPort> all = derived_ports(), found;
    std::vector<std::string> names;
    for (const DerivedPort &p : all) {
        names.push_back(p.name);
        if (p.name == name) {
            found.push_back(p);
        }
    }
    user_assert(found.size() == 1)
        << "derived_port: " << (found.empty() ? "no" : "more than one") << " derived port named '" << name
        << "' (derived ports: " << joined(names) << ")\n";
    return found[0];
}

// The real-valued normalizer behind match_derived_port().
namespace {

using Internal::Call;
using Internal::Variable;

// An exact rational, num/den with den > 0, in lowest terms.
struct Rational {
    int64_t num = 0, den = 1;
};

struct NNode;
using NNodePtr = std::shared_ptr<const NNode>;

struct Mono {
    Rational coeff;
    std::vector<NNodePtr> factors;
};

struct Poly {
    std::vector<Mono> monos;
};

struct Interval {
    double lo = -INFINITY, hi = INFINITY;
};

// A factor: an atom (a call to a port or other unexpanded Func, an image, a
// variable), an opaque operation over normalized children, or a sum of one
// monomial over a summation variable.
struct NNode {
    enum Kind { Atom,
                Opaque,
                Sum } kind;
    std::string name;            // Atom: the callee or variable; Opaque: the operation
    std::vector<Expr> args;      // Atom: index arguments
    std::vector<Poly> children;  // Opaque
    std::string var;             // Sum: the summation variable (a placeholder)
    Expr min, extent;            // Sum
    Mono body;                   // Sum
    std::set<std::string> free;  // placeholders that occur free
    Interval range;
    bool port = false;  // Atom: a call to a port
};

const char *const placeholder_prefix = "$dp";

class FindPlaceholders : public Internal::IRGraphVisitor {
    using IRGraphVisitor::visit;
    void visit(const Variable *op) override {
        if (Internal::starts_with(op->name, placeholder_prefix)) {
            found.insert(op->name);
        }
    }

public:
    std::set<std::string> found;
};

std::set<std::string> placeholders_in(const Expr &e) {
    FindPlaceholders f;
    if (e.defined()) {
        e.accept(&f);
    }
    return f.found;
}

std::string type_name(const Type &t) {
    std::ostringstream s;
    s << t;
    return s.str();
}

Interval type_range(const Type &t) {
    if (t.is_int()) {
        return {-std::ldexp(1.0, t.bits() - 1), std::ldexp(1.0, t.bits() - 1) - 1};
    }
    if (t.is_uint()) {
        return {0, std::ldexp(1.0, t.bits()) - 1};
    }
    if (t.is_bool()) {
        return {0, 1};
    }
    return {};
}

Interval interval_mul(Interval a, Interval b) {
    double c[4] = {a.lo * b.lo, a.lo * b.hi, a.hi * b.lo, a.hi * b.hi};
    Interval r{INFINITY, -INFINITY};
    for (double x : c) {
        if (std::isnan(x)) {
            x = 0;  // 0 * inf
        }
        r.lo = std::min(r.lo, x);
        r.hi = std::max(r.hi, x);
    }
    return r;
}

class Normalizer {
public:
    std::set<std::string> stops;
    std::string failure;
    int budget = 200000;
    int counter = 0;

    Poly constant(Rational r) {
        Poly p;
        if (r.num != 0) {
            p.monos.push_back({r, {}});
        }
        return p;
    }

    Poly factor(NNodePtr n) {
        Poly p;
        p.monos.push_back({{1, 1}, {std::move(n)}});
        return p;
    }

    Rational rat(int64_t n, int64_t d) {
        if (d < 0) {
            n = -n;
            d = -d;
        }
        int64_t g = std::gcd(n < 0 ? -n : n, d);
        if (g > 1) {
            n /= g;
            d /= g;
        }
        return {n, d};
    }

    bool fits(__int128 v) {
        return v >= INT64_MIN / 4 && v <= INT64_MAX / 4;
    }

    Rational reduce(__int128 n, __int128 d) {
        if (d < 0) {
            n = -n;
            d = -d;
        }
        __int128 a = n < 0 ? -n : n, b = d;
        while (b) {
            __int128 t = a % b;
            a = b;
            b = t;
        }
        if (a > 1) {
            n /= a;
            d /= a;
        }
        if (!fits(n) || !fits(d)) {
            fail("a coefficient overflowed");
            return {0, 1};
        }
        return {(int64_t)n, (int64_t)d};
    }

    Rational mul(Rational a, Rational b) {
        return reduce((__int128)a.num * b.num, (__int128)a.den * b.den);
    }

    Rational add(Rational a, Rational b) {
        return reduce((__int128)a.num * b.den + (__int128)b.num * a.den, (__int128)a.den * b.den);
    }

    void fail(const std::string &why) {
        if (failure.empty()) {
            failure = why;
        }
    }

    // Keys. env maps placeholders to canonical binder names.
    std::string expr_key(const Expr &e, const std::map<std::string, Expr> &env) {
        std::ostringstream s;
        s << Internal::simplify(Internal::substitute(env, e));
        return s.str();
    }

    std::string rat_key(Rational r) {
        return r.den == 1 ? std::to_string(r.num) : std::to_string(r.num) + "/" + std::to_string(r.den);
    }

    std::string node_key(const NNode &n, const std::map<std::string, Expr> &env, int depth) {
        switch (n.kind) {
        case NNode::Atom: {
            if (n.args.empty()) {
                return n.name;
            }
            std::string s = n.name + "(";
            for (size_t i = 0; i < n.args.size(); i++) {
                s += (i ? ", " : "") + expr_key(n.args[i], env);
            }
            return s + ")";
        }
        case NNode::Opaque: {
            std::string s = n.name + "(";
            for (size_t i = 0; i < n.children.size(); i++) {
                s += (i ? ", " : "") + poly_key(n.children[i], env, depth);
            }
            return s + ")";
        }
        case NNode::Sum: {
            std::string binder = "#" + std::to_string(depth);
            std::map<std::string, Expr> inner = env;
            inner[n.var] = Variable::make(Int(32), binder);
            return "sum[" + binder + " in " + expr_key(n.min, env) + " .. +" + expr_key(n.extent, env) + "](" +
                   factors_key(n.body.factors, inner, depth + 1) + ")";
        }
        }
        return "";
    }

    std::string factors_key(const std::vector<NNodePtr> &factors, const std::map<std::string, Expr> &env, int depth) {
        std::vector<std::string> keys;
        for (const NNodePtr &f : factors) {
            const std::string k = node_key(*f, env, depth);
            keys.push_back(f->kind == NNode::Atom ? k : "(" + k + ")");
        }
        std::sort(keys.begin(), keys.end());
        std::string s;
        for (const std::string &k : keys) {
            s += (s.empty() ? "" : " * ") + k;
        }
        return s;
    }

    std::string mono_key(const Mono &m, const std::map<std::string, Expr> &env, int depth) {
        if (m.factors.empty()) {
            return rat_key(m.coeff);
        }
        std::string f = factors_key(m.factors, env, depth);
        if (m.coeff.num == 1 && m.coeff.den == 1) {
            return f;
        }
        return rat_key(m.coeff) + " * " + f;
    }

    std::vector<std::string> mono_keys(const Poly &p, const std::map<std::string, Expr> &env, int depth) {
        std::vector<std::string> keys;
        for (const Mono &m : p.monos) {
            keys.push_back(mono_key(m, env, depth));
        }
        std::sort(keys.begin(), keys.end());
        return keys;
    }

    std::string poly_key(const Poly &p, const std::map<std::string, Expr> &env = {}, int depth = 0) {
        std::vector<std::string> keys = mono_keys(p, env, depth);
        if (keys.empty()) {
            return "0";
        }
        std::string s;
        for (const std::string &k : keys) {
            s += (s.empty() ? "" : " + ") + k;
        }
        return s;
    }

    // Arithmetic.
    Poly plus(const Poly &a, const Poly &b) {
        Poly r;
        std::map<std::string, size_t> where;
        auto add_mono = [&](const Mono &m) {
            std::string k = factors_key(m.factors, {}, 0);
            auto it = where.find(k);
            if (it == where.end()) {
                where[k] = r.monos.size();
                r.monos.push_back(m);
            } else {
                r.monos[it->second].coeff = add(r.monos[it->second].coeff, m.coeff);
            }
        };
        for (const Mono &m : a.monos) {
            add_mono(m);
        }
        for (const Mono &m : b.monos) {
            add_mono(m);
        }
        Poly out;
        for (Mono &m : r.monos) {
            if (m.coeff.num != 0) {
                out.monos.push_back(std::move(m));
            }
        }
        return out;
    }

    Poly scale(const Poly &a, Rational c) {
        Poly r;
        if (c.num == 0) {
            return r;
        }
        for (const Mono &m : a.monos) {
            r.monos.push_back({mul(m.coeff, c), m.factors});
        }
        return r;
    }

    Poly times(const Poly &a, const Poly &b) {
        Poly r;
        if ((double)a.monos.size() * (double)b.monos.size() > budget) {
            fail("the expansion exceeds the size budget");
            return r;
        }
        for (const Mono &x : a.monos) {
            for (const Mono &y : b.monos) {
                Mono m{mul(x.coeff, y.coeff), x.factors};
                m.factors.insert(m.factors.end(), y.factors.begin(), y.factors.end());
                r = plus(r, Poly{{m}});
            }
        }
        budget -= (int)r.monos.size();
        if (budget < 0) {
            fail("the expansion exceeds the size budget");
        }
        return r;
    }

    bool as_constant(const Poly &p, Rational &c) {
        if (p.monos.empty()) {
            c = {0, 1};
            return true;
        }
        if (p.monos.size() == 1 && p.monos[0].factors.empty()) {
            c = p.monos[0].coeff;
            return true;
        }
        return false;
    }

    Interval range_of(const Poly &p) {
        Interval r{0, 0};
        for (const Mono &m : p.monos) {
            Interval t{(double)m.coeff.num / (double)m.coeff.den, (double)m.coeff.num / (double)m.coeff.den};
            for (const NNodePtr &f : m.factors) {
                t = interval_mul(t, f->range);
            }
            r.lo += t.lo;
            r.hi += t.hi;
        }
        return r;
    }

    NNodePtr atom(const std::string &name, std::vector<Expr> args, Interval range) {
        auto n = std::make_shared<NNode>();
        n->kind = NNode::Atom;
        n->name = name;
        for (Expr &a : args) {
            // likely() and promise_clamped() are value-preserving tags that
            // schedules and layouts put on indices.
            a = Internal::simplify(Internal::remove_promises(Internal::remove_likelies(a)));
            std::set<std::string> f = placeholders_in(a);
            n->free.insert(f.begin(), f.end());
        }
        n->args = std::move(args);
        n->range = range;
        return n;
    }

    Poly opaque(const std::string &name, const Type &t, std::vector<Poly> children) {
        auto n = std::make_shared<NNode>();
        n->kind = NNode::Opaque;
        n->name = name;
        for (const Poly &c : children) {
            for (const Mono &m : c.monos) {
                for (const NNodePtr &f : m.factors) {
                    n->free.insert(f->free.begin(), f->free.end());
                }
            }
        }
        n->children = std::move(children);
        n->range = type_range(t);
        return factor(n);
    }

    // Sum `body` over var in [min, min + extent), one monomial at a time,
    // pulling out the factors that do not depend on var.
    Poly sum(const Poly &body, const std::string &var, const Expr &min, const Expr &extent) {
        Poly result;
        std::set<std::string> bounds_free = placeholders_in(min);
        std::set<std::string> extent_free = placeholders_in(extent);
        bounds_free.insert(extent_free.begin(), extent_free.end());
        std::optional<int64_t> n_iter = Internal::as_const_int(Internal::simplify(extent));
        for (const Mono &m : body.monos) {
            Mono inv{m.coeff, {}}, dep{{1, 1}, {}};
            for (const NNodePtr &f : m.factors) {
                (f->free.count(var) ? dep : inv).factors.push_back(f);
            }
            Poly term{{inv}};
            if (dep.factors.empty()) {
                term = times(term, normalize(extent));
            } else {
                auto n = std::make_shared<NNode>();
                n->kind = NNode::Sum;
                n->var = var;
                n->min = Internal::simplify(min);
                n->extent = Internal::simplify(extent);
                n->body = dep;
                for (const NNodePtr &f : dep.factors) {
                    n->free.insert(f->free.begin(), f->free.end());
                }
                n->free.erase(var);
                n->free.insert(bounds_free.begin(), bounds_free.end());
                Interval r{1, 1};
                for (const NNodePtr &f : dep.factors) {
                    r = interval_mul(r, f->range);
                }
                if (n_iter && *n_iter >= 0) {
                    n->range = interval_mul(r, {(double)*n_iter, (double)*n_iter});
                } else {
                    n->range = {};
                }
                term = times(term, factor(n));
            }
            result = plus(result, term);
        }
        return result;
    }

    std::string fresh() {
        return placeholder_prefix + std::to_string(counter++);
    }

    Poly call(const Call *op) {
        std::vector<Poly> args;
        if (op->call_type == Call::Halide && op->func.defined()) {
            Internal::Function f(op->func);
            if (stops.count(f.name())) {
                NNodePtr n = atom(f.name() + (f.outputs() > 1 ? "." + std::to_string(op->value_index) : ""),
                                  op->args, type_range(op->type));
                const_cast<NNode &>(*n).port = true;
                return factor(n);
            }
            if (f.has_pure_definition() && !f.has_extern_definition() && f.outputs() == 1 &&
                f.updates().size() <= 1) {
                std::map<std::string, Expr> pure;
                for (size_t i = 0; i < f.args().size(); i++) {
                    pure[f.args()[i]] = op->args[i];
                }
                if (f.updates().empty()) {
                    return normalize(Internal::substitute(pure, f.values()[0]));
                }
                if (Poly p; expand_sum(f, pure, p)) {
                    return p;
                }
            }
            return factor(atom(f.name() + (f.outputs() > 1 ? "." + std::to_string(op->value_index) : ""),
                               op->args, type_range(op->type)));
        }
        if (op->call_type == Call::Image) {
            return factor(atom(op->name, op->args, type_range(op->type)));
        }
        if (op->is_strict_float_intrinsic()) {
            // Real-valued, strict and fast arithmetic agree.
            return normalize(Internal::unstrictify_float(op));
        }
        for (const Expr &a : op->args) {
            args.push_back(normalize(a));
        }
        return opaque(op->name, op->type, std::move(args));
    }

    // A sum reduction: f(pure) = init; f(pure) = f(pure) + body over its RDom.
    bool expand_sum(const Internal::Function &f, const std::map<std::string, Expr> &pure, Poly &out) {
        const Internal::Definition &u = f.update(0);
        if (u.args().size() != f.args().size() || u.values().size() != 1) {
            return false;
        }
        for (size_t i = 0; i < f.args().size(); i++) {
            const Variable *v = u.args()[i].as<Variable>();
            if (!v || v->name != f.args()[i]) {
                return false;
            }
        }
        Expr pred = Internal::simplify(u.predicate());
        if (pred.defined() && !Internal::is_const_one(pred)) {
            return false;
        }
        const Internal::Add *add = u.values()[0].as<Internal::Add>();
        if (!add) {
            return false;
        }
        auto is_self = [&](const Expr &e) {
            const Call *c = e.as<Call>();
            if (!c || c->call_type != Call::Halide || c->name != f.name() || c->value_index != 0) {
                return false;
            }
            for (size_t i = 0; i < c->args.size(); i++) {
                const Variable *v = c->args[i].as<Variable>();
                if (!v || v->name != f.args()[i]) {
                    return false;
                }
            }
            return true;
        };
        Expr body;
        if (is_self(add->a)) {
            body = add->b;
        } else if (is_self(add->b)) {
            body = add->a;
        } else {
            return false;
        }
        // The body must not read f itself.
        bool reads_self = false;
        Internal::visit_with(body, [&](auto *self, const Call *c) {
            reads_self |= c->call_type == Call::Halide && c->name == f.name();
            self->visit_base(c);
        });
        if (reads_self) {
            return false;
        }
        std::map<std::string, Expr> rename = pure;
        std::vector<std::tuple<std::string, Expr, Expr>> rvars;
        for (const Internal::ReductionVariable &rv : u.schedule().rvars()) {
            std::string p = fresh();
            rename[rv.var] = Variable::make(Int(32), p);
            rvars.emplace_back(p, rv.min, rv.extent);
        }
        Poly result = normalize(Internal::substitute(rename, body));
        for (auto &[p, min, extent] : rvars) {
            result = sum(result, p, Internal::substitute(pure, min), Internal::substitute(pure, extent));
        }
        out = plus(normalize(Internal::substitute(pure, f.values()[0])), result);
        return true;
    }

    bool exact_float(double v, Rational &r) {
        if (!std::isfinite(v)) {
            return false;
        }
        int e = 0;
        double m = std::frexp(v, &e);
        // v = m * 2^e, |m| in [0.5, 1): scale m to an integer.
        int64_t n = (int64_t)std::ldexp(m, 53);
        e -= 53;
        while (n != 0 && (n % 2) == 0 && e < 0) {
            n /= 2;
            e++;
        }
        if (n == 0) {
            r = {0, 1};
            return true;
        }
        if (e >= 0) {
            if (e > 8) {
                return false;
            }
            r = {n << e, 1};
            return true;
        }
        if (-e > 60) {
            return false;
        }
        r = rat(n, (int64_t)1 << (-e));
        return true;
    }

    static int mantissa_bits(const Type &t) {
        if (t.is_bfloat()) {
            return 8;
        }
        return t.bits() == 16 ? 11 : t.bits() == 32 ? 24 :
                                                      53;
    }

    Poly cast(const Type &to, const Expr &value) {
        const Type &from = value.type();
        Poly p = normalize(value);
        std::string round = "round<" + type_name(to) + ">";
        if (from.is_float() && to.is_float()) {
            if (from.is_bfloat() == to.is_bfloat() && to.bits() >= from.bits()) {
                return p;
            }
            if (from.is_bfloat() && to.bits() >= 32) {
                return p;
            }
            return opaque(round, to, {p});
        }
        if ((from.is_int() || from.is_uint()) && (to.is_int() || to.is_uint())) {
            Interval r = range_of(p), t = type_range(to);
            if (r.lo >= t.lo && r.hi <= t.hi) {
                return p;
            }
            return opaque("cast<" + type_name(to) + ">", to, {p});
        }
        if ((from.is_int() || from.is_uint()) && to.is_float()) {
            Interval r = range_of(p);
            double limit = std::ldexp(1.0, mantissa_bits(to));
            if (r.lo >= -limit && r.hi <= limit) {
                return p;
            }
            return opaque(round, to, {p});
        }
        return opaque(round, to, {p});
    }

    template<typename Op>
    bool opaque_binary(const Expr &e, const std::string &name, Poly &out) {
        if (const Op *o = e.as<Op>()) {
            out = opaque(name, e.type(), {normalize(o->a), normalize(o->b)});
            return true;
        }
        return false;
    }

    bool ring(const Type &t) {
        return t.is_float() || (t.is_int() && t.bits() >= 32);
    }

    Poly normalize(const Expr &e) {
        using namespace Internal;
        if (!failure.empty()) {
            return {};
        }
        if (--budget < 0) {
            fail("the expansion exceeds the size budget");
            return {};
        }
        if (const IntImm *i = e.as<IntImm>()) {
            return constant({i->value, 1});
        }
        if (const UIntImm *u = e.as<UIntImm>()) {
            if (u->value <= (uint64_t)INT64_MAX / 4) {
                return constant({(int64_t)u->value, 1});
            }
            return factor(atom(std::to_string(u->value), {}, {(double)u->value, (double)u->value}));
        }
        if (const FloatImm *f = e.as<FloatImm>()) {
            Rational r;
            if (exact_float(f->value, r)) {
                return constant(r);
            }
            std::ostringstream s;
            s << e;
            return factor(atom(s.str(), {}, {f->value, f->value}));
        }
        if (const Variable *v = e.as<Variable>()) {
            if (starts_with(v->name, placeholder_prefix)) {
                return factor(atom("rvar", {v}, type_range(v->type)));
            }
            return factor(atom(v->name, {}, type_range(v->type)));
        }
        if (const Cast *c = e.as<Cast>()) {
            return cast(c->type, c->value);
        }
        if (const Let *l = e.as<Let>()) {
            return normalize(substitute(l->name, l->value, l->body));
        }
        if (const Call *c = e.as<Call>()) {
            return call(c);
        }
        if (ring(e.type())) {
            if (const Add *a = e.as<Add>()) {
                return plus(normalize(a->a), normalize(a->b));
            }
            if (const Sub *s = e.as<Sub>()) {
                return plus(normalize(s->a), scale(normalize(s->b), {-1, 1}));
            }
            if (const Mul *m = e.as<Mul>()) {
                return times(normalize(m->a), normalize(m->b));
            }
            if (const Div *d = e.as<Div>(); d && e.type().is_float()) {
                Poly b = normalize(d->b);
                Rational c;
                if (as_constant(b, c) && c.num != 0) {
                    return scale(normalize(d->a), rat(c.den, c.num));
                }
                return opaque("div", e.type(), {normalize(d->a), b});
            }
        }
        // Everything else is opaque: an operation over normalized children.
        std::string t = "<" + type_name(e.type()) + ">";
        Poly out;
        if (opaque_binary<Add>(e, "add" + t, out) || opaque_binary<Sub>(e, "sub" + t, out) ||
            opaque_binary<Mul>(e, "mul" + t, out) || opaque_binary<Div>(e, "div" + t, out) ||
            opaque_binary<Mod>(e, "mod" + t, out) || opaque_binary<Min>(e, "min", out) ||
            opaque_binary<Max>(e, "max", out) || opaque_binary<EQ>(e, "eq", out) ||
            opaque_binary<NE>(e, "ne", out) || opaque_binary<LT>(e, "lt", out) ||
            opaque_binary<LE>(e, "le", out) || opaque_binary<GT>(e, "gt", out) ||
            opaque_binary<GE>(e, "ge", out) || opaque_binary<And>(e, "and", out) ||
            opaque_binary<Or>(e, "or", out)) {
            return out;
        }
        if (const Not *n = e.as<Not>()) {
            return opaque("not", e.type(), {normalize(n->a)});
        }
        if (const Select *sel = e.as<Select>()) {
            return opaque("select", e.type(),
                          {normalize(sel->condition), normalize(sel->true_value), normalize(sel->false_value)});
        }
        if (const Reinterpret *r = e.as<Reinterpret>()) {
            return opaque("reinterpret" + t, e.type(), {normalize(r->value)});
        }
        return factor(atom("expr", {e}, type_range(e.type())));
    }
};

}  // namespace

namespace {

void port_atoms(const Poly &p, std::vector<const NNode *> &out) {
    for (const Mono &m : p.monos) {
        for (const NNodePtr &f : m.factors) {
            if (f->kind == NNode::Atom && f->port) {
                out.push_back(f.get());
            } else if (f->kind == NNode::Opaque) {
                for (const Poly &c : f->children) {
                    port_atoms(c, out);
                }
            } else if (f->kind == NNode::Sum) {
                port_atoms(Poly{{f->body}}, out);
            }
        }
    }
}

std::string difference(Normalizer &n, const Poly &a, const Poly &b) {
    std::vector<std::string> ka = n.mono_keys(a, {}, 0), kb = n.mono_keys(b, {}, 0), only_a, only_b;
    std::set_difference(ka.begin(), ka.end(), kb.begin(), kb.end(), std::back_inserter(only_a));
    std::set_difference(kb.begin(), kb.end(), ka.begin(), ka.end(), std::back_inserter(only_b));
    std::string s = "the normal forms differ";
    if (!only_a.empty()) {
        s += "; only in the term: " + joined(only_a);
    }
    if (!only_b.empty()) {
        s += "; only in the definition: " + joined(only_b);
    }
    return s;
}

}  // namespace

DerivedPortMatch match_derived_port(const Func &term, const DerivedPort &port) {
    user_assert(term.defined() && port.definition.defined())
        << "match_derived_port: the term and the port must be defined\n";
    DerivedPortMatch result;
    std::set<std::string> stops = func_names(port.from);
    stops.insert(port.stored.name());

    std::vector<Expr> term_args;
    for (const Var &v : term.args()) {
        term_args.push_back(v);
    }
    Normalizer term_norm;
    term_norm.stops = stops;
    Poly t = term_norm.normalize(term(term_args));
    result.term_form = term_norm.poly_key(t);
    if (!term_norm.failure.empty()) {
        result.reason = "normalizing the term: " + term_norm.failure;
        return result;
    }

    std::vector<std::string> def_vars = port.definition.function().args();
    std::vector<Expr> def_args;
    for (const std::string &v : def_vars) {
        def_args.push_back(Variable::make(Int(32), v));
    }
    Normalizer def_norm;
    def_norm.stops = stops;
    Poly d = def_norm.normalize(port.definition(def_args));
    result.definition_form = def_norm.poly_key(d);
    if (!def_norm.failure.empty()) {
        result.reason = "normalizing the definition: " + def_norm.failure;
        return result;
    }

    // Unify the definition's port calls, where an argument is one of its
    // Vars, with the term's calls to the same port.
    std::vector<const NNode *> d_atoms, t_atoms;
    port_atoms(d, d_atoms);
    port_atoms(t, t_atoms);
    if (t_atoms.empty()) {
        result.reason = "the term reads none of the ports the definition reads (" + joined(port.from_names) + ")";
        return result;
    }
    std::vector<std::vector<Expr>> candidates(def_vars.size());
    for (size_t v = 0; v < def_vars.size(); v++) {
        std::set<std::string> seen;
        for (const NNode *da : d_atoms) {
            for (size_t k = 0; k < da->args.size(); k++) {
                const Variable *var = da->args[k].as<Variable>();
                if (!var || var->name != def_vars[v]) {
                    continue;
                }
                for (const NNode *ta : t_atoms) {
                    if (ta->name != da->name || ta->args.size() != da->args.size() || !ta->args[k].defined() ||
                        !placeholders_in(ta->args[k]).empty()) {
                        continue;
                    }
                    std::ostringstream key;
                    key << ta->args[k];
                    if (seen.insert(key.str()).second) {
                        candidates[v].push_back(ta->args[k]);
                    }
                }
            }
        }
        if (candidates[v].empty()) {
            result.reason = "no call to a port in the term determines the definition's argument " +
                            std::to_string(v) + " (" + def_vars[v] + ")";
            return result;
        }
    }

    const int max_attempts = 256;
    std::vector<size_t> pick(def_vars.size(), 0);
    std::string first_difference;
    for (int attempt = 0; attempt < max_attempts; attempt++) {
        std::vector<Expr> args;
        for (size_t v = 0; v < def_vars.size(); v++) {
            args.push_back(candidates[v][pick[v]]);
        }
        Normalizer n;
        n.stops = stops;
        Poly dd = n.normalize(port.definition(args));
        std::string form = n.poly_key(dd);
        if (n.failure.empty() && form == result.term_form) {
            result.matched = true;
            result.port_args = args;
            result.definition_form = form;
            result.reason.clear();
            return result;
        }
        if (attempt == 0) {
            result.definition_form = form;
            first_difference = n.failure.empty() ? difference(n, t, dd) : "normalizing the definition: " + n.failure;
        }
        // Next combination.
        size_t v = 0;
        while (v < pick.size() && ++pick[v] == candidates[v].size()) {
            pick[v++] = 0;
        }
        if (v == pick.size()) {
            break;
        }
    }
    result.reason = first_difference;
    return result;
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
