#include "Approximation.h"

#include <ostream>
#include <set>
#include <sstream>

#if defined(__GNUC__) || defined(__clang__)
#include <cstdlib>
#include <cxxabi.h>
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

std::string Approximation::type_label(const std::type_info &type) {
    std::string name = type.name();
#if defined(__GNUC__) || defined(__clang__)
    int status = 0;
    char *demangled = abi::__cxa_demangle(name.c_str(), nullptr, nullptr, &status);
    if (status == 0 && demangled) {
        name = demangled;
    }
    std::free(demangled);
#else
    replace_all(name, "struct ", "");
    replace_all(name, "class ", "");
#endif
    replace_all(name, "(anonymous namespace)::", "");
    replace_all(name, "`anonymous namespace'::", "");
    replace_all(name, "std::__1::", "std::");
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

ApproximationPorts Approximation::resolve_ports(const std::vector<Func> &funcs, const ApproximationPorts &given,
                                                bool encode_direction) const {
    const char *direction = encode_direction ? "encode" : "decode";
    const size_t n = funcs.size();
    if (!given.empty()) {
        user_assert(given.size() == n)
            << "Approximation '" << label() << "' " << direction << ": " << given.size()
            << " ports were given for " << n << " Funcs\n";
    }

    const SignatureForm form = state_->impl->signature_form();
    ApproximationPorts declared;
    bool have_declared = false;
    if (form != SignatureForm::None) {
        ApproximationSignature sig = signature(encode_direction ? given : ApproximationPorts{});
        if (sig.known) {
            declared = encode_direction ? sig.inputs : sig.outputs;
            have_declared = declared.size() == n;
        }
    }

    ApproximationPorts ports;
    if (given.empty()) {
        ports = have_declared ? declared : positional_ports(n);
    } else {
        validate_ports(*this, direction, encode_direction ? "input" : "encoded", funcs, given);
        ports = given;
        if (have_declared) {
            for (size_t i = 0; i < n; i++) {
                if (form == SignatureForm::Static) {
                    ports[i].name = declared[i].name;
                }
                ports[i].type = declared[i].type ? declared[i].type : ports[i].type;
                ports[i].dimensions = declared[i].dimensions ? declared[i].dimensions : ports[i].dimensions;
            }
        }
    }
    validate_ports(*this, direction, encode_direction ? "input" : "encoded", funcs, ports);
    fill_from_funcs(ports, funcs);
    return ports;
}

ApproximationPorts Approximation::output_ports(const std::vector<Func> &outputs, const ApproximationPorts &input_ports,
                                               bool encode_direction) const {
    const char *direction = encode_direction ? "encode" : "decode";
    ApproximationPorts ports;
    bool resolved = false;
    if (state_->impl->signature_form() != SignatureForm::None) {
        ApproximationSignature sig = signature(encode_direction ? input_ports : ApproximationPorts{});
        if (sig.known) {
            ports = encode_direction ? sig.outputs : sig.inputs;
            user_assert(ports.size() == outputs.size())
                << "Approximation '" << label() << "' " << direction << ": the declared signature has "
                << ports.size() << " output ports (" << ports_string(ports) << ") but the unit returned "
                << outputs.size() << " Funcs\n";
            resolved = true;
        }
    }
    if (!resolved) {
        ports = outputs.size() == input_ports.size() ? names_only(input_ports) : positional_ports(outputs.size());
    }
    validate_ports(*this, direction, "output", outputs, ports);
    fill_from_funcs(ports, outputs);
    return ports;
}

ApproximationSignature Approximation::signature(const ApproximationPorts &inputs) const {
    user_assert(defined()) << "signature called on an undefined Approximation\n";
    switch (state_->impl->signature_form()) {
    case SignatureForm::Static:
        return state_->impl->declared_signature({});
    case SignatureForm::Contextual:
        return state_->impl->declared_signature(inputs);
    case SignatureForm::None:
        break;
    }
    if (!state_->impl->encode_is_single() || inputs.size() > 1) {
        return ApproximationSignature::unknown(inputs);
    }
    ApproximationSignature result;
    result.inputs = inputs.empty() ? positional_ports(1) : inputs;
    result.outputs = {ApproximationPort(result.inputs[0].name)};
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
    out += "\n";
    if (sig.known) {
        for (const std::string &issue : input_range_issues(sig.inputs, inputs)) {
            out += std::string(depth * 2 + 2, ' ') + "! " + issue + "\n";
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
    const std::string here = path.empty() ? label() : path + " > " + label();
    ApproximationSignature sig = signature(inputs);
    if (sig.known) {
        for (const std::string &issue : input_range_issues(sig.inputs, inputs)) {
            out.push_back(here + ": " + issue);
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
    ApproximationPorts resolved = resolve_ports(inputs, input_ports, true);
    CallScope scope;
    std::vector<Func> encoded = state_->impl->encode(inputs, resolved);
    ApproximationPorts encoded_ports = output_ports(encoded, resolved, true);
    std::vector<Func> intermediates = find_intermediates(inputs, encoded);
    ApproximationTraceNode node = scope.finish(*this, encoded, names_of(encoded_ports), intermediates, inputs, names_of(resolved));
    std::vector<ApproximationStageOutputs> stage_outputs;
    flatten(node, stage_outputs);
    return {std::move(encoded), std::move(encoded_ports), std::move(intermediates), std::move(stage_outputs),
            std::move(node)};
}

DecodeResult Approximation::decode(const std::vector<Func> &encoded, const ApproximationPorts &encoded_ports) const {
    user_assert(defined()) << "decode called on an undefined Approximation\n";
    ApproximationPorts resolved = resolve_ports(encoded, encoded_ports, false);
    CallScope scope;
    std::vector<Func> decoded = state_->impl->decode(encoded, resolved);
    ApproximationPorts decoded_ports = output_ports(decoded, resolved, false);
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
    for (int i = (int)stages.size() - 1; i >= 0; i--) {
        EncodeResult r = stages[i].encode(current, current_ports);
        current = std::move(r.encoded);
        current_ports = std::move(r.encoded_ports);
    }
    return current;
}

std::vector<Func> Compose::decode(const std::vector<Func> &encoded, const ApproximationPorts &encoded_ports) const {
    user_assert(!stages.empty()) << "Compose::decode: no stages\n";
    std::vector<Func> current = encoded;
    ApproximationPorts current_ports = encoded_ports;
    for (size_t i = 0; i < stages.size(); i++) {
        DecodeResult r = stages[i].decode(current, current_ports);
        current = std::move(r.decoded);
        current_ports = std::move(r.decoded_ports);
    }
    return current;
}

ApproximationSignature Compose::signature(const ApproximationPorts &inputs) const {
    if (stages.empty()) {
        return ApproximationSignature::unknown(inputs);
    }
    ApproximationPorts current = inputs, first_inputs;
    for (int i = (int)stages.size() - 1; i >= 0; i--) {
        ApproximationSignature s = stages[i].signature(current);
        if (!s.known) {
            return ApproximationSignature::unknown(inputs);
        }
        if (i == (int)stages.size() - 1) {
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
    return std::vector<Approximation>(stages.rbegin(), stages.rend());
}

std::vector<ApproximationPorts> Compose::child_inputs(const ApproximationPorts &inputs) const {
    std::vector<ApproximationPorts> result;
    ApproximationPorts current = inputs;
    for (int i = (int)stages.size() - 1; i >= 0; i--) {
        result.push_back(current);
        ApproximationSignature s = stages[i].signature(current);
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

bool Apply::lossless() const {
    return inner.lossless();
}

bool Choose::lossless() const {
    return chosen.lossless();
}

bool Apply::locate(const ApproximationPorts &inputs, size_t &begin, size_t &arity, std::string *problem) const {
    auto fail = [&](const std::string &message) {
        if (problem) {
            *problem = message;
        }
        return false;
    };
    if (port.empty()) {
        begin = idx;
        arity = encode_arity;
    } else {
        size_t count = 0;
        for (size_t i = 0; i < inputs.size(); i++) {
            if (inputs[i].name == port) {
                begin = i;
                count++;
            }
        }
        if (count != 1) {
            return fail("there is " + std::string(count == 0 ? "no" : "more than one") + " input port named '" + port +
                        "' (available: " + ports_string(names_only(inputs)) + ")");
        }
        ApproximationSignature s = inner.signature({inputs[begin]});
        arity = s.known && !s.inputs.empty() ? s.inputs.size() : 1;
    }
    if (begin + arity > inputs.size()) {
        return fail("the range [" + std::to_string(begin) + ", " + std::to_string(begin + arity) +
                    ") exceeds the input count (" + std::to_string(inputs.size()) + ")");
    }
    return true;
}

namespace {

template<typename T>
std::vector<T> slice(const std::vector<T> &v, size_t begin, size_t count) {
    return std::vector<T>(v.begin() + begin, v.begin() + begin + count);
}

template<typename T>
std::vector<T> splice(const std::vector<T> &v, size_t begin, size_t count, const std::vector<T> &replacement) {
    std::vector<T> result(v.begin(), v.begin() + begin);
    result.insert(result.end(), replacement.begin(), replacement.end());
    result.insert(result.end(), v.begin() + begin + count, v.end());
    return result;
}

}  // namespace

std::vector<Func> Apply::encode(const std::vector<Func> &inputs, const ApproximationPorts &input_ports) const {
    ApproximationPorts ports = input_ports.size() == inputs.size() ? input_ports : positional_ports(inputs.size());
    size_t begin = 0, arity = 0;
    std::string problem;
    user_assert(locate(ports, begin, arity, &problem)) << "Apply::encode: " << problem << "\n";
    return splice(inputs, begin, arity,
                  inner.encode(slice(inputs, begin, arity), slice(ports, begin, arity)).encoded);
}

std::vector<Func> Apply::decode(const std::vector<Func> &encoded, const ApproximationPorts &encoded_ports) const {
    ApproximationPorts ports = encoded_ports.size() == encoded.size() ? encoded_ports : positional_ports(encoded.size());
    size_t begin = 0, arity = 0;
    if (port.empty()) {
        begin = idx;
        arity = decode_arity;
    } else {
        // The inner's encoded outputs sit where its inputs were.
        ApproximationSignature s = inner.signature({ApproximationPort(port)});
        ApproximationPorts outs = s.known && !s.outputs.empty() ? s.outputs : ApproximationPorts{ApproximationPort(port)};
        size_t count = 0;
        for (size_t i = 0; i < ports.size(); i++) {
            if (ports[i].name == outs[0].name) {
                begin = i;
                count++;
            }
        }
        user_assert(count == 1)
            << "Apply::decode: there is " << (count == 0 ? "no" : "more than one") << " encoded port named '"
            << outs[0].name << "' (available: " << ports_string(names_only(ports)) << ")\n";
        arity = outs.size();
    }
    user_assert(begin + arity <= encoded.size())
        << "Apply::decode: the range [" << begin << ", " << begin + arity
        << ") exceeds the input count (" << encoded.size() << ")\n";
    return splice(encoded, begin, arity,
                  inner.decode(slice(encoded, begin, arity), slice(ports, begin, arity)).decoded);
}

ApproximationSignature Apply::signature(const ApproximationPorts &inputs) const {
    size_t begin = 0, arity = 0;
    if (!locate(inputs, begin, arity, nullptr)) {
        return ApproximationSignature::unknown(inputs);
    }
    ApproximationSignature s = inner.signature(slice(inputs, begin, arity));
    if (!s.known || s.inputs.size() != arity) {
        return ApproximationSignature::unknown(inputs);
    }
    ApproximationSignature result;
    result.inputs = inputs;
    for (size_t j = 0; j < arity; j++) {
        ApproximationPort &p = result.inputs[begin + j];
        p.type = s.inputs[j].type ? s.inputs[j].type : p.type;
        p.dimensions = s.inputs[j].dimensions ? s.inputs[j].dimensions : p.dimensions;
    }
    result.outputs = splice(inputs, begin, arity, s.outputs);
    return result;
}

std::vector<Approximation> Apply::children() const {
    return {inner};
}

std::vector<ApproximationPorts> Apply::child_inputs(const ApproximationPorts &inputs) const {
    size_t begin = 0, arity = 0;
    if (!locate(inputs, begin, arity, nullptr)) {
        return {{}};
    }
    return {slice(inputs, begin, arity)};
}

std::string Apply::name() const {
    return "Apply[" + (port.empty() ? std::to_string(idx) : port) + "]";
}

std::vector<Func> TrustedInverse::encode(const std::vector<Func> &inputs, const ApproximationPorts &input_ports) const {
    return encoder.encode(inputs, input_ports).encoded;
}

std::vector<Func> TrustedInverse::decode(const std::vector<Func> &encoded, const ApproximationPorts &encoded_ports) const {
    return decoder.decode(encoded, encoded_ports).decoded;
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

std::vector<Func> Choose::decode(const std::vector<Func> &encoded, const ApproximationPorts &encoded_ports) const {
    return chosen.decode(encoded, encoded_ports).decoded;
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

Func Pointwise::error_bound(const std::vector<Func> &inputs, const std::vector<Func> &) const {
    if (!bound_fn) {
        return Func();
    }
    user_assert(inputs.size() == 1 && inputs[0].outputs() == 1)
        << "Pointwise::error_bound requires a single-valued input Func\n";
    return apply_pointwise(
        inputs[0], wrap_expr_fn([f = bound_fn](Expr x) { return f(x); }),
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

}  // namespace Halide
