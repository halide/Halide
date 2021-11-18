#include <atomic>
#include <cmath>
#include <condition_variable>
#include <fstream>
#include <memory>
#include <thread>
#include <unordered_map>
#include <utility>

#include "BoundaryConditions.h"
#include "CompilerLogger.h"
#include "Derivative.h"
#include "Generator.h"
#include "IRPrinter.h"
#include "Module.h"
#include "Simplify.h"

namespace Halide {

GeneratorContext::GeneratorContext(const Target &t, bool auto_schedule,
                                   const MachineParams &machine_params)
    : target("target", t),
      auto_schedule("auto_schedule", auto_schedule),
      machine_params("machine_params", machine_params),
      externs_map(std::make_shared<ExternsMap>()),
      value_tracker(std::make_shared<Internal::ValueTracker>()) {
}

void GeneratorContext::init_from_context(const Halide::GeneratorContext &context) {
    target.set(context.get_target());
    auto_schedule.set(context.get_auto_schedule());
    machine_params.set(context.get_machine_params());
    value_tracker = context.get_value_tracker();
    externs_map = context.get_externs_map();
}

namespace Internal {

namespace {

// Return true iff the name is valid for Generators or Params.
// (NOTE: gcc didn't add proper std::regex support until v4.9;
// we don't yet require this, hence the hand-rolled replacement.)

bool is_alpha(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

// Note that this includes '_'
bool is_alnum(char c) {
    return is_alpha(c) || (c == '_') || (c >= '0' && c <= '9');
}

// Basically, a valid C identifier, except:
//
// -- initial _ is forbidden (rather than merely "reserved")
// -- two underscores in a row is also forbidden
bool is_valid_name(const std::string &n) {
    if (n.empty()) {
        return false;
    }
    if (!is_alpha(n[0])) {
        return false;
    }
    for (size_t i = 1; i < n.size(); ++i) {
        if (!is_alnum(n[i])) {
            return false;
        }
        if (n[i] == '_' && n[i - 1] == '_') {
            return false;
        }
    }
    return true;
}

std::string compute_base_path(const std::string &output_dir,
                              const std::string &function_name,
                              const std::string &file_base_name) {
    std::vector<std::string> namespaces;
    std::string simple_name = extract_namespaces(function_name, namespaces);
    std::string base_path = output_dir + "/" + (file_base_name.empty() ? simple_name : file_base_name);
    return base_path;
}

std::map<Output, std::string> compute_output_files(const Target &target,
                                                   const std::string &base_path,
                                                   const std::set<Output> &outputs) {
    std::map<Output, const OutputInfo> output_info = get_output_info(target);

    std::map<Output, std::string> output_files;
    for (auto o : outputs) {
        output_files[o] = base_path + output_info.at(o).extension;
    }
    return output_files;
}

Argument to_argument(const Internal::Parameter &param) {
    return Argument(param.name(),
                    param.is_buffer() ? Argument::InputBuffer : Argument::InputScalar,
                    param.type(),
                    param.dimensions(),
                    param.get_argument_estimates());
}

Func make_param_func(const Parameter &p, const std::string &name) {
    internal_assert(p.is_buffer());
    Func f(name + "_im");
    auto b = p.buffer();
    if (b.defined()) {
        // If the Parameter has an explicit BufferPtr set, bind directly to it
        f(_) = b(_);
    } else {
        std::vector<Var> args;
        std::vector<Expr> args_expr;
        for (int i = 0; i < p.dimensions(); ++i) {
            Var v = Var::implicit(i);
            args.push_back(v);
            args_expr.push_back(v);
        }
        f(args) = Internal::Call::make(p, args_expr);
    }
    return f;
}

}  // namespace

std::vector<Type> parse_halide_type_list(const std::string &types) {
    const auto &e = get_halide_type_enum_map();
    std::vector<Type> result;
    for (const auto &t : split_string(types, ",")) {
        auto it = e.find(t);
        user_assert(it != e.end()) << "Type not found: " << t;
        result.push_back(it->second);
    }
    return result;
}

void ValueTracker::track_values(const std::string &name, const std::vector<Expr> &values) {
    std::vector<std::vector<Expr>> &history = values_history[name];
    if (history.empty()) {
        for (const auto &value : values) {
            history.push_back({value});
        }
        return;
    }

    internal_assert(history.size() == values.size())
        << "Expected values of size " << history.size()
        << " but saw size " << values.size()
        << " for name " << name << "\n";

    // For each item, see if we have a new unique value
    for (size_t i = 0; i < values.size(); ++i) {
        Expr oldval = history[i].back();
        Expr newval = values[i];
        if (oldval.defined() && newval.defined()) {
            if (can_prove(newval == oldval)) {
                continue;
            }
        } else if (!oldval.defined() && !newval.defined()) {
            // Expr::operator== doesn't work with undefined
            // values, but they are equal for our purposes here.
            continue;
        }
        history[i].push_back(newval);
        // If we exceed max_unique_values, fail immediately.
        // TODO: could be useful to log all the entries that
        // overflow max_unique_values before failing.
        // TODO: this could be more helpful about labeling the values
        // that have multiple setttings.
        if (history[i].size() > max_unique_values) {
            std::ostringstream o;
            o << "Saw too many unique values in ValueTracker[" + std::to_string(i) + "]; "
              << "expected a maximum of " << max_unique_values << ":\n";
            for (const auto &e : history[i]) {
                o << "    " << e << "\n";
            }
            user_error << o.str();
        }
    }
}

std::vector<Expr> parameter_constraints(const Parameter &p) {
    internal_assert(p.defined());
    std::vector<Expr> values;
    values.emplace_back(p.host_alignment());
    if (p.is_buffer()) {
        for (int i = 0; i < p.dimensions(); ++i) {
            values.push_back(p.min_constraint(i));
            values.push_back(p.extent_constraint(i));
            values.push_back(p.stride_constraint(i));
        }
    } else {
        values.push_back(p.min_value());
        values.push_back(p.max_value());
    }
    return values;
}

class StubEmitter {
public:
    StubEmitter(std::ostream &dest,
                const std::string &generator_registered_name,
                const std::string &generator_stub_name,
                const std::vector<Internal::GeneratorParamBase *> &generator_params,
                const std::vector<Internal::GeneratorInputBase *> &inputs,
                const std::vector<Internal::GeneratorOutputBase *> &outputs)
        : stream(dest),
          generator_registered_name(generator_registered_name),
          generator_stub_name(generator_stub_name),
          generator_params(select_generator_params(generator_params)),
          inputs(inputs),
          outputs(outputs) {
        namespaces = split_string(generator_stub_name, "::");
        internal_assert(!namespaces.empty());
        if (namespaces[0].empty()) {
            // We have a name like ::foo::bar::baz; omit the first empty ns.
            namespaces.erase(namespaces.begin());
            internal_assert(namespaces.size() >= 2);
        }
        class_name = namespaces.back();
        namespaces.pop_back();
    }

    void emit();

private:
    std::ostream &stream;
    const std::string generator_registered_name;
    const std::string generator_stub_name;
    std::string class_name;
    std::vector<std::string> namespaces;
    const std::vector<Internal::GeneratorParamBase *> generator_params;
    const std::vector<Internal::GeneratorInputBase *> inputs;
    const std::vector<Internal::GeneratorOutputBase *> outputs;
    int indent_level{0};

    std::vector<Internal::GeneratorParamBase *> select_generator_params(const std::vector<Internal::GeneratorParamBase *> &in) {
        std::vector<Internal::GeneratorParamBase *> out;
        for (auto *p : in) {
            // These are always propagated specially.
            if (p->name() == "target" ||
                p->name() == "auto_schedule" ||
                p->name() == "machine_params") {
                continue;
            }
            if (p->is_synthetic_param()) {
                continue;
            }
            out.push_back(p);
        }
        return out;
    }

    /** Emit spaces according to the current indentation level */
    Indentation get_indent() const {
        return Indentation{indent_level};
    }

    void emit_inputs_struct();
    void emit_generator_params_struct();
};

void StubEmitter::emit_generator_params_struct() {
    const auto &v = generator_params;
    std::string name = "GeneratorParams";
    stream << get_indent() << "struct " << name << " final {\n";
    indent_level++;
    if (!v.empty()) {
        for (auto *p : v) {
            stream << get_indent() << p->get_c_type() << " " << p->name() << "{ " << p->get_default_value() << " };\n";
        }
        stream << "\n";
    }

    stream << get_indent() << name << "() {}\n";
    stream << "\n";

    if (!v.empty()) {
        stream << get_indent() << name << "(\n";
        indent_level++;
        std::string comma = "";
        for (auto *p : v) {
            stream << get_indent() << comma << p->get_c_type() << " " << p->name() << "\n";
            comma = ", ";
        }
        indent_level--;
        stream << get_indent() << ") : \n";
        indent_level++;
        comma = "";
        for (auto *p : v) {
            stream << get_indent() << comma << p->name() << "(" << p->name() << ")\n";
            comma = ", ";
        }
        indent_level--;
        stream << get_indent() << "{\n";
        stream << get_indent() << "}\n";
        stream << "\n";
    }

    indent_level--;
    stream << get_indent() << "};\n";
    stream << "\n";
}

void StubEmitter::emit_inputs_struct() {
    struct InInfo {
        std::string c_type;
        std::string name;
    };
    std::vector<InInfo> in_info;
    for (auto *input : inputs) {
        std::string c_type = input->get_c_type();
        if (input->is_array()) {
            c_type = "std::vector<" + c_type + ">";
        }
        in_info.push_back({c_type, input->name()});
    }

    const std::string name = "Inputs";
    stream << get_indent() << "struct " << name << " final {\n";
    indent_level++;
    for (const auto &in : in_info) {
        stream << get_indent() << in.c_type << " " << in.name << ";\n";
    }
    stream << "\n";

    stream << get_indent() << name << "() {}\n";
    stream << "\n";
    if (!in_info.empty()) {
        stream << get_indent() << name << "(\n";
        indent_level++;
        std::string comma = "";
        for (const auto &in : in_info) {
            stream << get_indent() << comma << "const " << in.c_type << "& " << in.name << "\n";
            comma = ", ";
        }
        indent_level--;
        stream << get_indent() << ") : \n";
        indent_level++;
        comma = "";
        for (const auto &in : in_info) {
            stream << get_indent() << comma << in.name << "(" << in.name << ")\n";
            comma = ", ";
        }
        indent_level--;
        stream << get_indent() << "{\n";
        stream << get_indent() << "}\n";

        indent_level--;
    }
    stream << get_indent() << "};\n";
    stream << "\n";
}

void StubEmitter::emit() {
    if (outputs.empty()) {
        // The generator can't support a real stub. Instead, generate an (essentially)
        // empty .stub.h file, so that build systems like Bazel will still get the output file
        // they expected. Note that we deliberately don't emit an ifndef header guard,
        // since we can't reliably assume that the generator_name will be globally unique;
        // on the other hand, since this file is just a couple of comments, it's
        // really not an issue if it's included multiple times.
        stream << "/* MACHINE-GENERATED - DO NOT EDIT */\n";
        stream << "/* The Generator named " << generator_registered_name << " uses ImageParam or Param, thus cannot have a Stub generated. */\n";
        return;
    }

    struct OutputInfo {
        std::string name;
        std::string ctype;
        std::string getter;
    };
    bool all_outputs_are_func = true;
    std::vector<OutputInfo> out_info;
    for (auto *output : outputs) {
        std::string c_type = output->get_c_type();
        const bool is_func = (c_type == "Func");
        std::string getter = "generator->get_funcs_for_output(\"" + output->name() + "\")";
        if (!is_func) {
            getter = c_type + "::to_output_buffers(" + getter + ", generator)";
        }
        if (!output->is_array()) {
            getter = getter + ".at(0)";
        }

        out_info.push_back({output->name(),
                            output->is_array() ? "std::vector<" + c_type + ">" : c_type,
                            getter});
        if (c_type != "Func") {
            all_outputs_are_func = false;
        }
    }

    std::ostringstream guard;
    guard << "HALIDE_STUB";
    for (const auto &ns : namespaces) {
        guard << "_" << ns;
    }
    guard << "_" << class_name;

    stream << get_indent() << "#ifndef " << guard.str() << "\n";
    stream << get_indent() << "#define " << guard.str() << "\n";
    stream << "\n";

    stream << get_indent() << "/* MACHINE-GENERATED - DO NOT EDIT */\n";
    stream << "\n";

    stream << get_indent() << "#include <cassert>\n";
    stream << get_indent() << "#include <iterator>\n";
    stream << get_indent() << "#include <map>\n";
    stream << get_indent() << "#include <memory>\n";
    stream << get_indent() << "#include <string>\n";
    stream << get_indent() << "#include <utility>\n";
    stream << get_indent() << "#include <vector>\n";
    stream << "\n";
    stream << get_indent() << "#include \"Halide.h\"\n";
    stream << "\n";

    stream << "namespace halide_register_generator {\n";
    stream << "namespace " << generator_registered_name << "_ns {\n";
    stream << "extern std::unique_ptr<Halide::Internal::AbstractGenerator> factory(const Halide::GeneratorContext& context);\n";
    stream << "}  // namespace halide_register_generator\n";
    stream << "}  // namespace " << generator_registered_name << "\n";
    stream << "\n";

    for (const auto &ns : namespaces) {
        stream << get_indent() << "namespace " << ns << " {\n";
    }
    stream << "\n";

    for (auto *p : generator_params) {
        std::string decl = p->get_type_decls();
        if (decl.empty()) {
            continue;
        }
        stream << decl << "\n";
    }

    stream << get_indent() << "class " << class_name << " final : public Halide::NamesInterface {\n";
    stream << get_indent() << "public:\n";
    indent_level++;

    emit_inputs_struct();
    emit_generator_params_struct();

    stream << get_indent() << "struct Outputs final {\n";
    indent_level++;
    stream << get_indent() << "// Outputs\n";
    for (const auto &out : out_info) {
        stream << get_indent() << out.ctype << " " << out.name << ";\n";
    }

    stream << "\n";
    stream << get_indent() << "// The Target used\n";
    stream << get_indent() << "Target target;\n";

    if (out_info.size() == 1) {
        stream << "\n";
        if (all_outputs_are_func) {
            std::string name = out_info.at(0).name;
            auto *output = outputs[0];
            if (output->is_array()) {
                stream << get_indent() << "operator std::vector<Halide::Func>() const {\n";
                indent_level++;
                stream << get_indent() << "return " << name << ";\n";
                indent_level--;
                stream << get_indent() << "}\n";

                stream << get_indent() << "Halide::Func operator[](size_t i) const {\n";
                indent_level++;
                stream << get_indent() << "return " << name << "[i];\n";
                indent_level--;
                stream << get_indent() << "}\n";

                stream << get_indent() << "Halide::Func at(size_t i) const {\n";
                indent_level++;
                stream << get_indent() << "return " << name << ".at(i);\n";
                indent_level--;
                stream << get_indent() << "}\n";

                stream << get_indent() << "// operator operator()() overloads omitted because the sole Output is array-of-Func.\n";
            } else {
                // If there is exactly one output, add overloads
                // for operator Func and operator().
                stream << get_indent() << "operator Halide::Func() const {\n";
                indent_level++;
                stream << get_indent() << "return " << name << ";\n";
                indent_level--;
                stream << get_indent() << "}\n";

                stream << "\n";
                stream << get_indent() << "template <typename... Args>\n";
                stream << get_indent() << "Halide::FuncRef operator()(Args&&... args) const {\n";
                indent_level++;
                stream << get_indent() << "return " << name << "(std::forward<Args>(args)...);\n";
                indent_level--;
                stream << get_indent() << "}\n";

                stream << "\n";
                stream << get_indent() << "template <typename ExprOrVar>\n";
                stream << get_indent() << "Halide::FuncRef operator()(std::vector<ExprOrVar> args) const {\n";
                indent_level++;
                stream << get_indent() << "return " << name << "()(args);\n";
                indent_level--;
                stream << get_indent() << "}\n";
            }
        } else {
            stream << get_indent() << "// operator Func() and operator()() overloads omitted because the sole Output is not Func.\n";
        }
    }

    stream << "\n";
    if (all_outputs_are_func) {
        stream << get_indent() << "Halide::Pipeline get_pipeline() const {\n";
        indent_level++;
        stream << get_indent() << "return Halide::Pipeline(std::vector<Halide::Func>{\n";
        indent_level++;
        int commas = (int)out_info.size() - 1;
        for (const auto &out : out_info) {
            stream << get_indent() << out.name << (commas-- ? "," : "") << "\n";
        }
        indent_level--;
        stream << get_indent() << "});\n";
        indent_level--;
        stream << get_indent() << "}\n";

        stream << "\n";
        stream << get_indent() << "Halide::Realization realize(std::vector<int32_t> sizes) {\n";
        indent_level++;
        stream << get_indent() << "return get_pipeline().realize(sizes, target);\n";
        indent_level--;
        stream << get_indent() << "}\n";

        stream << "\n";
        stream << get_indent() << "template <typename... Args, typename std::enable_if<Halide::Internal::NoRealizations<Args...>::value>::type * = nullptr>\n";
        stream << get_indent() << "Halide::Realization realize(Args&&... args) {\n";
        indent_level++;
        stream << get_indent() << "return get_pipeline().realize(std::forward<Args>(args)..., target);\n";
        indent_level--;
        stream << get_indent() << "}\n";

        stream << "\n";
        stream << get_indent() << "void realize(Halide::Realization r) {\n";
        indent_level++;
        stream << get_indent() << "get_pipeline().realize(r, target);\n";
        indent_level--;
        stream << get_indent() << "}\n";
    } else {
        stream << get_indent() << "// get_pipeline() and realize() overloads omitted because some Outputs are not Func.\n";
    }

    indent_level--;
    stream << get_indent() << "};\n";
    stream << "\n";

    stream << get_indent() << "HALIDE_NO_USER_CODE_INLINE static Outputs generate(\n";
    indent_level++;
    stream << get_indent() << "const GeneratorContext& context,\n";
    stream << get_indent() << "const Inputs& inputs,\n";
    stream << get_indent() << "const GeneratorParams& generator_params = GeneratorParams()\n";
    indent_level--;
    stream << get_indent() << ")\n";
    stream << get_indent() << "{\n";
    indent_level++;
    stream << get_indent() << "std::shared_ptr<Halide::Internal::AbstractGenerator> generator = halide_register_generator::" << generator_registered_name << "_ns::factory(context);\n";
    for (auto *p : generator_params) {
        stream << get_indent();
        if (p->is_looplevel_param()) {
            stream << "generator->set_generatorparam_value(";
        } else {
            stream << "generator->set_generatorparam_value(";
        }
        stream << "\"" << p->name() << "\", ";
        if (p->is_looplevel_param()) {
            stream << "generator_params." << p->name();
        } else {
            stream << p->call_to_string("generator_params." + p->name());
        }
        stream << ");\n";
    }

    for (auto *p : inputs) {
        stream << get_indent() << "generator->bind_input("
               << "\"" << p->name() << "\", ";
        if (p->kind() == IOKind::Buffer) {
            stream << "Halide::Internal::StubInputBuffer<>::to_parameter_vector(inputs." << p->name() << ")";
        } else {
            // Func or Expr
            if (!p->is_array()) {
                stream << "{";
            }
            stream << "inputs." << p->name();
            if (!p->is_array()) {
                stream << "}";
            }
        }
        stream << ");\n";
    }

    stream << get_indent() << "generator->build_pipeline();\n";
    stream << get_indent() << "return {\n";
    indent_level++;
    for (const auto &out : out_info) {
        stream << get_indent() << out.getter << ",\n";
    }
    //    stream << get_indent() << "generator->get_target()\n";
    stream << get_indent() << "generator->get_target_info().target\n";
    indent_level--;
    stream << get_indent() << "};\n";
    indent_level--;
    stream << get_indent() << "}\n";
    stream << "\n";

    stream << get_indent() << "// overload to allow GeneratorContext-pointer\n";
    stream << get_indent() << "inline static Outputs generate(\n";
    indent_level++;
    stream << get_indent() << "const GeneratorContext* context,\n";
    stream << get_indent() << "const Inputs& inputs,\n";
    stream << get_indent() << "const GeneratorParams& generator_params = GeneratorParams()\n";
    indent_level--;
    stream << get_indent() << ")\n";
    stream << get_indent() << "{\n";
    indent_level++;
    stream << get_indent() << "return generate(*context, inputs, generator_params);\n";
    indent_level--;
    stream << get_indent() << "}\n";
    stream << "\n";

    stream << get_indent() << "// overload to allow Target instead of GeneratorContext.\n";
    stream << get_indent() << "inline static Outputs generate(\n";
    indent_level++;
    stream << get_indent() << "const Target& target,\n";
    stream << get_indent() << "const Inputs& inputs,\n";
    stream << get_indent() << "const GeneratorParams& generator_params = GeneratorParams()\n";
    indent_level--;
    stream << get_indent() << ")\n";
    stream << get_indent() << "{\n";
    indent_level++;
    stream << get_indent() << "return generate(Halide::GeneratorContext(target), inputs, generator_params);\n";
    indent_level--;
    stream << get_indent() << "}\n";
    stream << "\n";

    stream << get_indent() << class_name << "() = delete;\n";

    indent_level--;
    stream << get_indent() << "};\n";
    stream << "\n";

    for (int i = (int)namespaces.size() - 1; i >= 0; --i) {
        stream << get_indent() << "}  // namespace " << namespaces[i] << "\n";
    }
    stream << "\n";

    stream << get_indent() << "#endif  // " << guard.str() << "\n";
}

const std::map<std::string, Type> &get_halide_type_enum_map() {
    static const std::map<std::string, Type> halide_type_enum_map{
        {"bool", Bool()},
        {"int8", Int(8)},
        {"int16", Int(16)},
        {"int32", Int(32)},
        {"uint8", UInt(8)},
        {"uint16", UInt(16)},
        {"uint32", UInt(32)},
        {"float16", Float(16)},
        {"float32", Float(32)},
        {"float64", Float(64)}};
    return halide_type_enum_map;
}

std::string halide_type_to_c_source(const Type &t) {
    static const std::map<halide_type_code_t, std::string> m = {
        {halide_type_int, "Int"},
        {halide_type_uint, "UInt"},
        {halide_type_float, "Float"},
        {halide_type_handle, "Handle"},
    };
    std::ostringstream oss;
    oss << "Halide::" << m.at(t.code()) << "(" << t.bits() << +")";
    return oss.str();
}

std::string halide_type_to_c_type(const Type &t) {
    auto encode = [](const Type &t) -> int { return t.code() << 16 | t.bits(); };
    static const std::map<int, std::string> m = {
        {encode(Int(8)), "int8_t"},
        {encode(Int(16)), "int16_t"},
        {encode(Int(32)), "int32_t"},
        {encode(Int(64)), "int64_t"},
        {encode(UInt(1)), "bool"},
        {encode(UInt(8)), "uint8_t"},
        {encode(UInt(16)), "uint16_t"},
        {encode(UInt(32)), "uint32_t"},
        {encode(UInt(64)), "uint64_t"},
        {encode(BFloat(16)), "uint16_t"},  // TODO: see Issues #3709, #3967
        {encode(Float(16)), "uint16_t"},   // TODO: see Issues #3709, #3967
        {encode(Float(32)), "float"},
        {encode(Float(64)), "double"},
        {encode(Handle(64)), "void*"}};
    internal_assert(m.count(encode(t))) << t << " " << encode(t);
    return m.at(encode(t));
}

Module build_module(AbstractGenerator &g, const std::string &function_name) {
    const LinkageType linkage_type = LinkageType::ExternalPlusMetadata;

    Pipeline pipeline = g.build_pipeline();

    AutoSchedulerResults auto_schedule_results;
    const auto target_info = g.get_target_info();
    if (target_info.auto_schedule) {
        auto_schedule_results = pipeline.auto_schedule(target_info.target, target_info.machine_params);
    }

    std::vector<Argument> filter_arguments;
    for (const auto &a : g.get_input_arginfos()) {
        for (const auto &p : g.get_parameters_for_input(a.name)) {
            filter_arguments.push_back(to_argument(p));
        }
    }

    Module result = pipeline.compile_to_module(filter_arguments, function_name, target_info.target, linkage_type);
    for (const auto &map_entry : g.get_external_code_map()) {
        result.append(map_entry.second);
    }

    for (const auto &output_info : g.get_output_arginfos()) {
        const std::vector<Func> output_funcs = g.get_funcs_for_output(output_info.name);
        for (size_t i = 0; i < output_funcs.size(); ++i) {
            const Func &f = output_funcs[i];

            const std::string &from = f.name();
            std::string to = output_info.name;
            if (output_funcs.size() > 1) {
                to += "_" + std::to_string(i);
            }

            const int tuple_size = f.outputs();
            for (int t = 0; t < tuple_size; ++t) {
                const std::string suffix = (tuple_size > 1) ? ("." + std::to_string(t)) : "";
                result.remap_metadata_name(from + suffix, to + suffix);
            }
        }
    }

    result.set_auto_scheduler_results(auto_schedule_results);

    return result;
}

/**
 * Build a module that is suitable for using for gradient descent calculation in TensorFlow or PyTorch.
 *
 * Essentially:
 *   - A new Pipeline is synthesized from the current Generator (according to the rules below)
 *   - The new Pipeline is autoscheduled (if autoscheduling is requested, but it would be odd not to do so)
 *   - The Pipeline is compiled to a Module and returned
 *
 * The new Pipeline is adjoint to the original; it has:
 *   - All the same inputs as the original, in the same order
 *   - Followed by one grad-input for each original output
 *   - Followed by one output for each unique pairing of original-output + original-input.
 *     (For the common case of just one original-output, this amounts to being one output for each original-input.)
 */
Module build_gradient_module(Halide::Internal::AbstractGenerator &g, const std::string &function_name) {
    constexpr int DBG = 1;

    // I doubt these ever need customizing; if they do, we can make them arguments to this function.
    const std::string grad_input_pattern = "_grad_loss_for_$OUT$";
    const std::string grad_output_pattern = "_grad_loss_$OUT$_wrt_$IN$";
    const LinkageType linkage_type = LinkageType::ExternalPlusMetadata;

    user_assert(!function_name.empty()) << "build_gradient_module(): function_name cannot be empty\n";

    Pipeline original_pipeline = g.build_pipeline();

    std::vector<Func> original_outputs = original_pipeline.outputs();

    // Construct the adjoint pipeline, which has:
    // - All the same inputs as the original, in the same order
    // - Followed by one grad-input for each original output
    // - Followed by one output for each unique pairing of original-output + original-input.

    // First: the original inputs. Note that scalar inputs remain scalar,
    // rather being promoted into zero-dimensional buffers.
    std::vector<Argument> gradient_inputs;
    for (const auto &a : g.get_input_arginfos()) {
        for (const auto &p : g.get_parameters_for_input(a.name)) {
            gradient_inputs.push_back(to_argument(p));
            debug(DBG) << "    gradient copied input is: " << gradient_inputs.back().name << "\n";
        }
    }

    // Next: add a grad-input for each *original* output; these will
    // be the same shape as the output (so we should copy estimates from
    // those outputs onto these estimates).
    // - If an output is an Array, we'll have a separate input for each array element.

    std::vector<ImageParam> d_output_imageparams;
    for (const auto &i : g.get_output_arginfos()) {
        for (const auto &f : g.get_funcs_for_output(i.name)) {
            const Parameter &p = f.output_buffer().parameter();
            const std::string &output_name = p.name();
            // output_name is something like "funcname_i"
            const std::string grad_in_name = replace_all(grad_input_pattern, "$OUT$", output_name);
            // TODO(srj): does it make sense for gradient to be a non-float type?
            // For now, assume it's always float32 (unless the output is already some float).
            const Type grad_in_type = p.type().is_float() ? p.type() : Float(32);
            const int grad_in_dimensions = p.dimensions();
            const ArgumentEstimates grad_in_estimates = p.get_argument_estimates();
            internal_assert((int)grad_in_estimates.buffer_estimates.size() == grad_in_dimensions);

            ImageParam d_im(grad_in_type, grad_in_dimensions, grad_in_name);
            for (int d = 0; d < grad_in_dimensions; d++) {
                d_im.parameter().set_min_constraint_estimate(d, grad_in_estimates.buffer_estimates.at(d).min);
                d_im.parameter().set_extent_constraint_estimate(d, grad_in_estimates.buffer_estimates.at(d).extent);
            }
            d_output_imageparams.push_back(d_im);
            gradient_inputs.push_back(to_argument(d_im.parameter()));

            debug(DBG) << "    gradient synthesized input is: " << gradient_inputs.back().name << "\n";
        }
    }

    // Finally: define the output Func(s), one for each unique output/input pair.
    // Note that original_outputs.size() != pi.outputs().size() if any outputs are arrays.
    internal_assert(original_outputs.size() == d_output_imageparams.size());
    internal_assert(original_outputs.size() == d_output_imageparams.size());
    std::vector<Func> gradient_outputs;
    for (size_t i = 0; i < original_outputs.size(); ++i) {
        const Func &original_output = original_outputs.at(i);
        const ImageParam &d_output = d_output_imageparams.at(i);
        Region bounds;
        for (int i = 0; i < d_output.dimensions(); i++) {
            bounds.emplace_back(d_output.dim(i).min(), d_output.dim(i).extent());
        }
        Func adjoint_func = BoundaryConditions::constant_exterior(d_output, make_zero(d_output.type()));
        Derivative d = propagate_adjoints(original_output, adjoint_func, bounds);

        const std::string &output_name = original_output.name();
        for (const auto &a : g.get_input_arginfos()) {
            for (const auto &p : g.get_parameters_for_input(a.name)) {
                const std::string &input_name = p.name();

                if (!p.is_buffer()) {
                    // Not sure if skipping scalar inputs is correct, but that's
                    // what the previous version of this code did, so we'll continue for now.
                    debug(DBG) << "    Skipping scalar input " << output_name << " wrt input " << input_name << "\n";
                    continue;
                }

                // Note that Derivative looks up by name; we don't have the original
                // Func, and we can't create a new one with an identical name (since
                // Func's ctor will uniquify the name for us). Let's just look up
                // by the original string instead.
                Func d_f = d(input_name + "_im");

                std::string grad_out_name = replace_all(replace_all(grad_output_pattern, "$OUT$", output_name), "$IN$", input_name);
                if (!d_f.defined()) {
                    grad_out_name = "_dummy" + grad_out_name;
                }

                Func d_out_wrt_in(grad_out_name);
                if (d_f.defined()) {
                    d_out_wrt_in(Halide::_) = d_f(Halide::_);
                } else {
                    debug(DBG) << "    No Derivative found for output " << output_name << " wrt input " << input_name << "\n";
                    // If there was no Derivative found, don't skip the output;
                    // just replace with a dummy Func that is all zeros. This ensures
                    // that the signature of the Pipeline we produce is always predictable.
                    std::vector<Var> vars;
                    for (int i = 0; i < d_output.dimensions(); i++) {
                        vars.push_back(Var::implicit(i));
                    }
                    d_out_wrt_in(vars) = make_zero(d_output.type());
                }

                d_out_wrt_in.set_estimates(p.get_argument_estimates().buffer_estimates);

                // Useful for debugging; ordinarily better to leave out
                // debug(0) << "\n\n"
                //          << "output:\n" << FuncWithDependencies(original_output) << "\n"
                //          << "d_output:\n" << FuncWithDependencies(adjoint_func) << "\n"
                //          << "input:\n" << FuncWithDependencies(f) << "\n"
                //          << "d_out_wrt_in:\n" << FuncWithDependencies(d_out_wrt_in) << "\n";

                gradient_outputs.push_back(d_out_wrt_in);
                debug(DBG) << "    gradient output is: " << d_out_wrt_in.name() << "\n";
            }
        }
    }

    Pipeline grad_pipeline = Pipeline(gradient_outputs);

    AutoSchedulerResults auto_schedule_results;
    const auto target_info = g.get_target_info();
    if (target_info.auto_schedule) {
        auto_schedule_results = grad_pipeline.auto_schedule(target_info.target, target_info.machine_params);
    } else {
        user_warning << "Autoscheduling is not enabled in build_gradient_module(), so the resulting "
                        "gradient module will be unscheduled; this is very unlikely to be what you want.\n";
    }

    Module result = grad_pipeline.compile_to_module(gradient_inputs, function_name, target_info.target, linkage_type);
    user_assert(g.get_external_code_map().empty())
        << "Building a gradient-descent module for a Generator with ExternalCode is not supported.\n";

    result.set_auto_scheduler_results(auto_schedule_results);
    return result;
}

int generate_filter_main_inner(int argc, char **argv, std::ostream &error_output) {
    const char kUsage[] =
        "gengen\n"
        "  [-g GENERATOR_NAME] [-f FUNCTION_NAME] [-o OUTPUT_DIR] [-r RUNTIME_NAME] [-d 1|0]\n"
        "  [-e EMIT_OPTIONS] [-n FILE_BASE_NAME] [-p PLUGIN_NAME] [-s AUTOSCHEDULER_NAME] [-t TIMEOUT]\n"
        "       target=target-string[,target-string...] [generator_arg=value [...]]\n"
        "\n"
        " -d  Build a module that is suitable for using for gradient descent calculationn\n"
        "     in TensorFlow or PyTorch. See Generator::build_gradient_module() documentation.\n"
        "\n"
        " -e  A comma separated list of files to emit. Accepted values are:\n"
        "     [assembly, bitcode, c_header, c_source, cpp_stub, featurization,\n"
        "      llvm_assembly, object, python_extension, pytorch_wrapper, registration,\n"
        "      schedule, static_library, stmt, stmt_html, compiler_log].\n"
        "     If omitted, default value is [c_header, static_library, registration].\n"
        "\n"
        " -p  A comma-separated list of shared libraries that will be loaded before the\n"
        "     generator is run. Useful for custom auto-schedulers. The generator must\n"
        "     either be linked against a shared libHalide or compiled with -rdynamic\n"
        "     so that references in the shared library to libHalide can resolve.\n"
        "     (Note that this does not change the default autoscheduler; use the -s flag\n"
        "     to set that value.)"
        "\n"
        " -r   The name of a standalone runtime to generate. Only honors EMIT_OPTIONS 'o'\n"
        "     and 'static_library'. When multiple targets are specified, it picks a\n"
        "     runtime that is compatible with all of the targets, or fails if it cannot\n"
        "     find one. Flags across all of the targets that do not affect runtime code\n"
        "     generation, such as `no_asserts` and `no_runtime`, are ignored.\n"
        "\n"
        " -s  The name of an autoscheduler to set as the default.\n"
        " -t  Timeout for the Generator to run, in seconds; mainly useful to ensure that bugs and/or degenerate"
        "     cases don't stall build systems. Defaults to 900 (=15 minutes). Specify 0 to allow ~infinite time.\n";

    std::map<std::string, std::string> flags_info = {
        {"-d", "0"},
        {"-e", ""},
        {"-f", ""},
        {"-g", ""},
        {"-n", ""},
        {"-o", ""},
        {"-p", ""},
        {"-r", ""},
        {"-s", ""},
        {"-t", "900"},  // 15 minutes
    };
    std::map<std::string, std::string> generator_args;

    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') {
            std::vector<std::string> v = split_string(argv[i], "=");
            if (v.size() != 2 || v[0].empty() || v[1].empty()) {
                error_output << kUsage;
                return 1;
            }
            generator_args[v[0]] = v[1];
            continue;
        }
        auto it = flags_info.find(argv[i]);
        if (it != flags_info.end()) {
            if (i + 1 >= argc) {
                error_output << kUsage;
                return 1;
            }
            it->second = argv[i + 1];
            ++i;
            continue;
        }
        error_output << "Unknown flag: " << argv[i] << "\n";
        error_output << kUsage;
        return 1;
    }

    // It's possible that in the future loaded plugins might change
    // how arguments are parsed, so we handle those first.
    for (const auto &lib : split_string(flags_info["-p"], ",")) {
        if (!lib.empty()) {
            load_plugin(lib);
        }
    }

    if (flags_info["-d"] != "1" && flags_info["-d"] != "0") {
        error_output << "-d must be 0 or 1\n";
        error_output << kUsage;
        return 1;
    }
    const int do_build_gradient_module = flags_info["-d"] == "1";

    std::string autoscheduler_name = flags_info["-s"];
    if (!autoscheduler_name.empty()) {
        Pipeline::set_default_autoscheduler_name(autoscheduler_name);
    }

    std::string runtime_name = flags_info["-r"];

    std::vector<std::string> generator_names = GeneratorRegistry::enumerate();
    if (generator_names.empty() && runtime_name.empty()) {
        error_output << "No generators have been registered and not compiling a standalone runtime\n";
        error_output << kUsage;
        return 1;
    }

    std::string generator_name = flags_info["-g"];
    if (generator_name.empty() && runtime_name.empty()) {
        // Require either -g or -r to be specified:
        // no longer infer the name when only one Generator is registered
        error_output << "Either -g <name> or -r must be specified; available Generators are:\n";
        if (!generator_names.empty()) {
            for (const auto &name : generator_names) {
                error_output << "    " << name << "\n";
            }
        } else {
            error_output << "    <none>\n";
        }
        return 1;
    }

    std::string function_name = flags_info["-f"];
    if (function_name.empty()) {
        // If -f isn't specified, assume function name = generator name.
        function_name = generator_name;
    }
    std::string output_dir = flags_info["-o"];
    if (output_dir.empty()) {
        error_output << "-o must always be specified.\n";
        error_output << kUsage;
        return 1;
    }

    std::string emit_flags_string = flags_info["-e"];

    // If HL_EXTRA_OUTPUTS is defined, assume it's extra outputs we want to generate
    // (usually for temporary debugging purposes) and just tack it on to the -e contents.
    std::string extra_outputs = get_env_variable("HL_EXTRA_OUTPUTS");
    if (!extra_outputs.empty()) {
        if (!emit_flags_string.empty()) {
            emit_flags_string += ",";
        }
        emit_flags_string += extra_outputs;
    }

    // It's ok to omit "target=" if we are generating *only* a cpp_stub
    const std::vector<std::string> emit_flags = split_string(emit_flags_string, ",");
    const bool stub_only = (emit_flags.size() == 1 && emit_flags[0] == "cpp_stub");
    if (!stub_only) {
        if (generator_args.find("target") == generator_args.end()) {
            error_output << "Target missing\n";
            error_output << kUsage;
            return 1;
        }
    }

    // it's OK for file_base_name to be empty: filename will be based on function name
    std::string file_base_name = flags_info["-n"];

    auto target_strings = split_string(generator_args["target"], ",");
    std::vector<Target> targets;
    for (const auto &s : target_strings) {
        targets.emplace_back(s);
    }

    // extensions won't vary across multitarget output
    std::map<Output, const OutputInfo> output_info = get_output_info(targets[0]);

    std::set<Output> outputs;
    if (emit_flags.empty() || (emit_flags.size() == 1 && emit_flags[0].empty())) {
        // If omitted or empty, assume .a and .h and registration.cpp
        outputs.insert(Output::c_header);
        outputs.insert(Output::registration);
        outputs.insert(Output::static_library);
    } else {
        // Build a reverse lookup table. Allow some legacy aliases on the command line,
        // to allow legacy build systems to work more easily.
        std::map<std::string, Output> output_name_to_enum = {
            {"cpp", Output::c_source},
            {"h", Output::c_header},
            {"html", Output::stmt_html},
            {"o", Output::object},
            {"py.c", Output::python_extension},
        };
        for (const auto &it : output_info) {
            output_name_to_enum[it.second.name] = it.first;
        }

        for (const std::string &opt : emit_flags) {
            auto it = output_name_to_enum.find(opt);
            if (it == output_name_to_enum.end()) {
                error_output << "Unrecognized emit option: " << opt << " is not one of [";
                auto end = output_info.cend();
                auto last = std::prev(end);
                for (auto iter = output_info.cbegin(); iter != end; ++iter) {
                    error_output << iter->second.name;
                    if (iter != last) {
                        error_output << " ";
                    }
                }
                error_output << "], ignoring.\n";
                error_output << kUsage;
                return 1;
            }
            outputs.insert(it->second);
        }
    }

    // Allow quick-n-dirty use of compiler logging via HL_DEBUG_COMPILER_LOGGER env var
    const bool do_compiler_logging = outputs.count(Output::compiler_log) ||
                                     (get_env_variable("HL_DEBUG_COMPILER_LOGGER") == "1");

    const bool obfuscate_compiler_logging = get_env_variable("HL_OBFUSCATE_COMPILER_LOGGER") == "1";

    const CompilerLoggerFactory no_compiler_logger_factory =
        [](const std::string &, const Target &) -> std::unique_ptr<CompilerLogger> {
        return nullptr;
    };

    const CompilerLoggerFactory json_compiler_logger_factory =
        [&](const std::string &function_name, const Target &target) -> std::unique_ptr<CompilerLogger> {
        // rebuild generator_args from the map so that they are always canonical
        std::string generator_args_string;
        std::string sep;
        for (const auto &it : generator_args) {
            if (it.first == "target") {
                continue;
            }
            std::string quote = it.second.find(' ') != std::string::npos ? "\\\"" : "";
            generator_args_string += sep + it.first + "=" + quote + it.second + quote;
            sep = " ";
        }
        std::unique_ptr<JSONCompilerLogger> t(new JSONCompilerLogger(
            obfuscate_compiler_logging ? "" : generator_name,
            obfuscate_compiler_logging ? "" : function_name,
            obfuscate_compiler_logging ? "" : autoscheduler_name,
            obfuscate_compiler_logging ? Target() : target,
            obfuscate_compiler_logging ? "" : generator_args_string,
            obfuscate_compiler_logging));
        return t;
    };

    const CompilerLoggerFactory compiler_logger_factory = do_compiler_logging ?
                                                              json_compiler_logger_factory :
                                                              no_compiler_logger_factory;

    struct TimeoutMonitor {
        std::atomic<bool> generator_finished = false;
        std::thread thread;
        std::condition_variable cond_var;
        std::mutex mutex;

        // Kill the timeout monitor as a destructor to ensure the thread
        // gets joined in the event of an exception
        ~TimeoutMonitor() {
            generator_finished = true;
            cond_var.notify_all();
            thread.join();
        }
    } monitor;

    const int timeout_in_seconds = std::stoi(flags_info["-t"]);
    const auto timeout_time = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_in_seconds);
    monitor.thread = std::thread([timeout_time, timeout_in_seconds, &monitor]() {
        std::unique_lock<std::mutex> lock(monitor.mutex);

        if (timeout_in_seconds <= 0) {
            // No watchdog timer, just let it run as long as it likes.
            return;
        }
        while (!monitor.generator_finished) {
            auto now = std::chrono::steady_clock::now();
            if (now > timeout_time) {
                fprintf(stderr, "Timed out waiting for Generator to complete (%d seconds)!\n", timeout_in_seconds);
                fflush(stdout);
                fflush(stderr);
                exit(1);
            } else {
                monitor.cond_var.wait_for(lock, timeout_time - now);
            }
        }
    });

    if (!runtime_name.empty()) {
        std::string base_path = compute_base_path(output_dir, runtime_name, "");

        Target gcd_target = targets[0];
        for (size_t i = 1; i < targets.size(); i++) {
            if (!gcd_target.get_runtime_compatible_target(targets[i], gcd_target)) {
                error_output << "Failed to find compatible runtime target for "
                             << gcd_target.to_string()
                             << " and "
                             << targets[i].to_string() << "\n";
                return -1;
            }
        }

        if (targets.size() > 1) {
            debug(1) << "Building runtime for computed target: " << gcd_target.to_string() << "\n";
        }

        auto output_files = compute_output_files(gcd_target, base_path, outputs);
        // Runtime doesn't get to participate in the CompilerLogger party
        compile_standalone_runtime(output_files, gcd_target);
    }

    if (!generator_name.empty()) {
        std::string base_path = compute_base_path(output_dir, function_name, file_base_name);
        debug(1) << "Generator " << generator_name << " has base_path " << base_path << "\n";
        if (outputs.count(Output::cpp_stub)) {
            // When generating cpp_stub, we ignore all generator args passed in, and supply a fake Target.
            // (CompilerLogger is never enabled for cpp_stub, for now anyway.)
            auto gen = GeneratorRegistry::create(generator_name, GeneratorContext(Target()));
            auto stub_file_path = base_path + output_info[Output::cpp_stub].extension;
            if (!gen->emit_cpp_stub(stub_file_path)) {
                error_output << "Generator '" << generator_name << "' is not capable of generating Stubs.\n";
                return -1;
            }
        }

        // Don't bother with this if we're just emitting a cpp_stub.
        if (!stub_only) {
            auto output_files = compute_output_files(targets[0], base_path, outputs);
            auto module_factory = [&generator_name, &generator_args, do_build_gradient_module](const std::string &name, const Target &target) -> Module {
                // Must re-create each time since each instance will have a different Target.
                const std::string &auto_schedule_string = generator_args["auto_schedule"];
                const std::string &machine_params_string = generator_args["machine_params"];
                const bool auto_schedule = auto_schedule_string == "true" || auto_schedule_string == "True";
                const MachineParams machine_params = machine_params_string.empty() ? MachineParams::generic() : MachineParams(machine_params_string);
                auto gen = GeneratorRegistry::create(generator_name, GeneratorContext(target, auto_schedule, machine_params));
                for (const auto &kv : generator_args) {
                    if (kv.first == "target" ||
                        kv.first == "auto_schedule" ||
                        kv.first == "machine_params") {
                        continue;
                    }
                    gen->set_generatorparam_value(kv.first, kv.second);
                }
                return do_build_gradient_module ? build_gradient_module(*gen, name) : build_module(*gen, name);
            };
            compile_multitarget(function_name, output_files, targets, target_strings, module_factory, compiler_logger_factory);
        }
    }

    return 0;
}

#ifdef HALIDE_WITH_EXCEPTIONS
int generate_filter_main(int argc, char **argv, std::ostream &error_output) {
    try {
        return generate_filter_main_inner(argc, argv, error_output);
    } catch (std::runtime_error &err) {
        error_output << "Unhandled exception: " << err.what() << "\n";
        return -1;
    }
}
#else
int generate_filter_main(int argc, char **argv, std::ostream &error_output) {
    return generate_filter_main_inner(argc, argv, error_output);
}
#endif

GeneratorParamBase::GeneratorParamBase(const std::string &name)
    : name_(name) {
    ObjectInstanceRegistry::register_instance(this, 0, ObjectInstanceRegistry::GeneratorParam,
                                              this, nullptr);
}

GeneratorParamBase::~GeneratorParamBase() {
    ObjectInstanceRegistry::unregister_instance(this);
}

void GeneratorParamBase::check_value_readable() const {
    // GeneratorParams are now always readable.
}

void GeneratorParamBase::check_value_writable() const {
    // Allow writing when no Generator is set, to avoid having to special-case ctor initing code
    if (!generator) {
        return;
    }
    user_assert(generator->phase < GeneratorBase::GenerateCalled) << "The GeneratorParam \"" << name() << "\" cannot be written after build() or generate() is called.\n";
}

void GeneratorParamBase::fail_wrong_type(const char *type) {
    user_error << "The GeneratorParam \"" << name() << "\" cannot be set with a value of type " << type << ".\n";
}

/* static */
GeneratorRegistry &GeneratorRegistry::get_registry() {
    static GeneratorRegistry *registry = new GeneratorRegistry;
    return *registry;
}

/* static */
void GeneratorRegistry::register_factory(const std::string &name,
                                         GeneratorFactory generator_factory) {
    user_assert(is_valid_name(name)) << "Invalid Generator name: " << name;
    GeneratorRegistry &registry = get_registry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    internal_assert(registry.factories.find(name) == registry.factories.end())
        << "Duplicate Generator name: " << name;
    registry.factories[name] = std::move(generator_factory);
}

/* static */
void GeneratorRegistry::unregister_factory(const std::string &name) {
    GeneratorRegistry &registry = get_registry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    internal_assert(registry.factories.find(name) != registry.factories.end())
        << "Generator not found: " << name;
    registry.factories.erase(name);
}

/* static */
AbstractGeneratorPtr GeneratorRegistry::create(const std::string &name,
                                               const GeneratorContext &context) {
    GeneratorRegistry &registry = get_registry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    auto it = registry.factories.find(name);
    if (it == registry.factories.end()) {
        std::ostringstream o;
        o << "Generator not found: " << name << "\n";
        o << "Did you mean:\n";
        for (const auto &n : registry.factories) {
            o << "    " << n.first << "\n";
        }
        user_error << o.str();
    }
    GeneratorFactory f = it->second;
    AbstractGeneratorPtr g = f(context);
    internal_assert(g != nullptr);
    return g;
}

/* static */
std::vector<std::string> GeneratorRegistry::enumerate() {
    GeneratorRegistry &registry = get_registry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    std::vector<std::string> result;
    result.reserve(registry.factories.size());
    for (const auto &i : registry.factories) {
        result.push_back(i.first);
    }
    return result;
}

GeneratorBase::GeneratorBase(size_t size, const void *introspection_helper)
    : size(size) {
    ObjectInstanceRegistry::register_instance(this, size, ObjectInstanceRegistry::Generator, this, introspection_helper);
}

GeneratorBase::~GeneratorBase() {
    ObjectInstanceRegistry::unregister_instance(this);
}

GeneratorParamInfo::GeneratorParamInfo(GeneratorBase *generator, const size_t size) {
    std::vector<void *> vf = ObjectInstanceRegistry::instances_in_range(
        generator, size, ObjectInstanceRegistry::FilterParam);
    user_assert(vf.empty()) << "ImageParam and Param<> are no longer allowed in Generators; use Input<> instead.";

    const auto add_synthetic_params = [this, generator](GIOBase *gio) {
        const std::string &n = gio->name();
        const std::string &gn = generator->generator_registered_name;

        owned_synthetic_params.push_back(GeneratorParam_Synthetic<Type>::make(generator, gn, n + ".type", *gio, SyntheticParamType::Type, gio->types_defined()));
        filter_generator_params.push_back(owned_synthetic_params.back().get());

        if (gio->kind() != IOKind::Scalar) {
            owned_synthetic_params.push_back(GeneratorParam_Synthetic<int>::make(generator, gn, n + ".dim", *gio, SyntheticParamType::Dim, gio->dims_defined()));
            filter_generator_params.push_back(owned_synthetic_params.back().get());
        }
        if (gio->is_array()) {
            owned_synthetic_params.push_back(GeneratorParam_Synthetic<size_t>::make(generator, gn, n + ".size", *gio, SyntheticParamType::ArraySize, gio->array_size_defined()));
            filter_generator_params.push_back(owned_synthetic_params.back().get());
        }
    };

    std::vector<void *> vi = ObjectInstanceRegistry::instances_in_range(
        generator, size, ObjectInstanceRegistry::GeneratorInput);
    for (auto *v : vi) {
        auto *input = static_cast<Internal::GeneratorInputBase *>(v);
        internal_assert(input != nullptr);
        user_assert(is_valid_name(input->name())) << "Invalid Input name: (" << input->name() << ")\n";
        user_assert(!names.count(input->name())) << "Duplicate Input name: " << input->name();
        names.insert(input->name());
        internal_assert(input->generator == nullptr || input->generator == generator);
        input->generator = generator;
        filter_inputs.push_back(input);
        add_synthetic_params(input);
    }

    std::vector<void *> vo = ObjectInstanceRegistry::instances_in_range(
        generator, size, ObjectInstanceRegistry::GeneratorOutput);
    for (auto *v : vo) {
        auto *output = static_cast<Internal::GeneratorOutputBase *>(v);
        internal_assert(output != nullptr);
        user_assert(is_valid_name(output->name())) << "Invalid Output name: (" << output->name() << ")\n";
        user_assert(!names.count(output->name())) << "Duplicate Output name: " << output->name();
        names.insert(output->name());
        internal_assert(output->generator == nullptr || output->generator == generator);
        output->generator = generator;
        filter_outputs.push_back(output);
        add_synthetic_params(output);
    }

    std::vector<void *> vg = ObjectInstanceRegistry::instances_in_range(
        generator, size, ObjectInstanceRegistry::GeneratorParam);
    for (auto *v : vg) {
        auto *param = static_cast<GeneratorParamBase *>(v);
        internal_assert(param != nullptr);
        user_assert(is_valid_name(param->name())) << "Invalid GeneratorParam name: " << param->name();
        user_assert(!names.count(param->name())) << "Duplicate GeneratorParam name: " << param->name();
        names.insert(param->name());
        internal_assert(param->generator == nullptr || param->generator == generator);
        param->generator = generator;
        filter_generator_params.push_back(param);
    }

    for (auto &g : owned_synthetic_params) {
        g->generator = generator;
    }
}

GeneratorParamInfo &GeneratorBase::param_info() {
    internal_assert(param_info_ptr != nullptr);
    return *param_info_ptr;
}

namespace {

template<typename T>
T *find_by_name(const std::string &name, const std::vector<T *> &v) {
    for (T *t : v) {
        if (t->name() == name) {
            return t;
        }
    }
    return nullptr;
}

}  // namespace

GeneratorInputBase *GeneratorBase::find_input_by_name(const std::string &name) {
    auto *t = find_by_name(name, param_info().inputs());
    internal_assert(t != nullptr) << "Input " << name << " not found.";
    return t;
}

GeneratorOutputBase *GeneratorBase::find_output_by_name(const std::string &name) {
    auto *t = find_by_name(name, param_info().outputs());
    internal_assert(t != nullptr) << "Output " << name << " not found.";
    return t;
}

void GeneratorBase::set_generator_param_values(const GeneratorParamsMap &params) {
    GeneratorParamInfo &pi = param_info();

    std::unordered_map<std::string, Internal::GeneratorParamBase *> generator_params_by_name;
    for (auto *g : pi.generator_params()) {
        generator_params_by_name[g->name()] = g;
    }

    for (const auto &key_value : params) {
        auto gp = generator_params_by_name.find(key_value.first);
        user_assert(gp != generator_params_by_name.end())
            << "Generator " << generator_registered_name << " has no GeneratorParam named: " << key_value.first << "\n";
        if (gp->second->is_looplevel_param()) {
            if (!key_value.second.string_value.empty()) {
                gp->second->set_from_string(key_value.second.string_value);
            } else {
                gp->second->set(key_value.second.loop_level);
            }
        } else {
            gp->second->set_from_string(key_value.second.string_value);
        }
    }
}

void GeneratorBase::init_from_context(const Halide::GeneratorContext &context) {
    Halide::GeneratorContext::init_from_context(context);
    internal_assert(param_info_ptr == nullptr);
    // pre-emptively build our param_info now
    param_info_ptr = std::make_unique<GeneratorParamInfo>(this, size);
}

void GeneratorBase::set_generator_names(const std::string &registered_name, const std::string &stub_name) {
    user_assert(is_valid_name(registered_name)) << "Invalid Generator name: " << registered_name;
    internal_assert(!registered_name.empty() && !stub_name.empty());
    internal_assert(generator_registered_name.empty() && generator_stub_name.empty());
    generator_registered_name = registered_name;
    generator_stub_name = stub_name;
}

void GeneratorBase::set_inputs_vector(const std::vector<std::vector<StubInput>> &inputs) {
    ensure_configure_has_been_called();
    advance_phase(InputsSet);
    GeneratorParamInfo &pi = param_info();
    user_assert(inputs.size() == pi.inputs().size())
        << "Expected exactly " << pi.inputs().size()
        << " inputs but got " << inputs.size() << "\n";
    for (size_t i = 0; i < pi.inputs().size(); ++i) {
        pi.inputs()[i]->set_inputs(inputs[i]);
    }
}

void GeneratorBase::track_parameter_values(bool include_outputs) {
    GeneratorParamInfo &pi = param_info();
    for (auto *input : pi.inputs()) {
        if (input->kind() == IOKind::Buffer) {
            internal_assert(!input->parameters_.empty());
            for (auto &p : input->parameters_) {
                // This must use p.name(), *not* input->name()
                get_value_tracker()->track_values(p.name(), parameter_constraints(p));
            }
        }
    }
    if (include_outputs) {
        for (auto *output : pi.outputs()) {
            if (output->kind() == IOKind::Buffer) {
                internal_assert(!output->funcs().empty());
                for (const auto &f : output->funcs()) {
                    user_assert(f.defined()) << "Output " << output->name() << " is not fully defined.";
                    auto output_buffers = f.output_buffers();
                    for (auto &o : output_buffers) {
                        Parameter p = o.parameter();
                        // This must use p.name(), *not* output->name()
                        get_value_tracker()->track_values(p.name(), parameter_constraints(p));
                    }
                }
            }
        }
    }
}

void GeneratorBase::check_min_phase(Phase expected_phase) const {
    user_assert(phase >= expected_phase) << "You may not do this operation at this phase.";
}

void GeneratorBase::check_exact_phase(Phase expected_phase) const {
    user_assert(phase == expected_phase) << "You may not do this operation at this phase.";
}

void GeneratorBase::advance_phase(Phase new_phase) {
    switch (new_phase) {
    case Created:
        internal_error;
        break;
    case ConfigureCalled:
        internal_assert(phase == Created);
        break;
    case InputsSet:
        internal_assert(phase == Created || phase == ConfigureCalled || phase == InputsSet);
        break;
    case GenerateCalled:
        // It's OK to advance directly to GenerateCalled.
        internal_assert(phase == Created || phase == ConfigureCalled || phase == InputsSet);
        break;
    case ScheduleCalled:
        internal_assert(phase == GenerateCalled);
        break;
    }
    phase = new_phase;
}

void GeneratorBase::ensure_configure_has_been_called() {
    if (phase < ConfigureCalled) {
        call_configure();
    }
    check_min_phase(ConfigureCalled);
}

void GeneratorBase::pre_configure() {
    advance_phase(ConfigureCalled);
}

void GeneratorBase::post_configure() {
}

void GeneratorBase::pre_generate() {
    advance_phase(GenerateCalled);
    GeneratorParamInfo &pi = param_info();
    user_assert(!pi.outputs().empty()) << "Must use Output<> with generate() method.";
    user_assert(get_target() != Target()) << "The Generator target has not been set.";

    for (auto *input : pi.inputs()) {
        input->init_internals();
    }
    for (auto *output : pi.outputs()) {
        output->init_internals();
    }
    track_parameter_values(false);
}

void GeneratorBase::post_generate() {
    track_parameter_values(true);
}

void GeneratorBase::pre_schedule() {
    advance_phase(ScheduleCalled);
    track_parameter_values(true);
}

void GeneratorBase::post_schedule() {
    track_parameter_values(true);
}

void GeneratorBase::pre_build() {
    advance_phase(GenerateCalled);
    advance_phase(ScheduleCalled);
    GeneratorParamInfo &pi = param_info();
    user_assert(pi.outputs().empty()) << "May not use build() method with Output<>.";
    for (auto *input : pi.inputs()) {
        input->init_internals();
    }
    track_parameter_values(false);
}

void GeneratorBase::post_build() {
    track_parameter_values(true);
}

Pipeline GeneratorBase::get_pipeline() {
    check_min_phase(GenerateCalled);
    if (!pipeline.defined()) {
        GeneratorParamInfo &pi = param_info();
        user_assert(!pi.outputs().empty()) << "Must use get_pipeline<> with Output<>.";
        std::vector<Func> funcs;
        for (auto *output : pi.outputs()) {
            for (const auto &f : output->funcs()) {
                user_assert(f.defined()) << "Output \"" << f.name() << "\" was not defined.\n";
                if (output->dims_defined()) {
                    user_assert(f.dimensions() == output->dims()) << "Output \"" << f.name()
                                                                  << "\" requires dimensions=" << output->dims()
                                                                  << " but was defined as dimensions=" << f.dimensions() << ".\n";
                }
                if (output->types_defined()) {
                    user_assert((int)f.outputs() == (int)output->types().size()) << "Output \"" << f.name()
                                                                                 << "\" requires a Tuple of size " << output->types().size()
                                                                                 << " but was defined as Tuple of size " << f.outputs() << ".\n";
                    for (size_t i = 0; i < f.output_types().size(); ++i) {
                        Type expected = output->types().at(i);
                        Type actual = f.output_types()[i];
                        user_assert(expected == actual) << "Output \"" << f.name()
                                                        << "\" requires type " << expected
                                                        << " but was defined as type " << actual << ".\n";
                    }
                }
                funcs.push_back(f);
            }
        }
        pipeline = Pipeline(funcs);
    }
    return pipeline;
}

void GeneratorBase::check_scheduled(const char *m) const {
    check_min_phase(ScheduleCalled);
}

void GeneratorBase::check_input_is_singular(Internal::GeneratorInputBase *in) {
    user_assert(!in->is_array())
        << "Input " << in->name() << " is an array, and must be set with a vector type.";
}

void GeneratorBase::check_input_is_array(Internal::GeneratorInputBase *in) {
    user_assert(in->is_array())
        << "Input " << in->name() << " is not an array, and must not be set with a vector type.";
}

void GeneratorBase::check_input_kind(Internal::GeneratorInputBase *in, Internal::IOKind kind) {
    user_assert(in->kind() == kind)
        << "Input " << in->name() << " cannot be set with the type specified.";
}

std::vector<std::string> GeneratorBase::get_generatorparam_names() {
    std::vector<std::string> v;
    for (auto *g : param_info().generator_params()) {
        if (g->is_synthetic_param()) {
            continue;
        }
        const auto &n = g->name();
        if (n == "target" ||
            n == "auto_schedule" ||
            n == "machine_params") {
            continue;
        }
        v.push_back(n);
    }

    return v;
}

void GeneratorBase::set_generatorparam_value(const std::string &name, const std::string &value) {
    if (name == "target" ||
        name == "auto_schedule" ||
        name == "machine_params") {
        user_error
            << "The GeneratorParam named " << name << " cannot be set by set_generatorparam_value().\n";
    }

    GeneratorParamInfo &pi = param_info();

    for (auto *g : pi.generator_params()) {
        if (g->name() != name) {
            continue;
        }
        g->set_from_string(value);
        return;
    }
    user_error
        << "Generator " << generator_registered_name << " has no GeneratorParam named: " << name << "\n";
}

void GeneratorBase::set_generatorparam_value(const std::string &name, const LoopLevel &value) {
    GeneratorParamInfo &pi = param_info();
    for (auto *g : pi.generator_params()) {
        if (g->name() != name) {
            continue;
        }
        user_assert(g->is_looplevel_param()) << "GeneratorParam " << name << " is not a LoopLevel and cannot be set this way.";
        g->set(value);
        return;
    }
    user_error
        << "Generator " << generator_registered_name << " has no GeneratorParam named: " << name << "\n";
}

std::string GeneratorBase::get_name() {
    return generator_registered_name;
}

AbstractGenerator::TargetInfo GeneratorBase::get_target_info() {
    return {
        get_target(),
        get_auto_schedule(),
        get_machine_params(),
    };
}

namespace {

// Note that this deliberately ignores inputs/outputs with multiple array values
// (ie, one name per input or output, regardless of array_size())
template<typename T>
std::vector<AbstractGenerator::ArgInfo> get_arguments(const T &t) {
    std::vector<AbstractGenerator::ArgInfo> args;
    args.reserve(t.size());
    for (auto *e : t) {
        args.push_back({e->name(),
                        e->kind(),
                        e->types_defined() ? e->types() : std::vector<Type>{},
                        e->dims_defined() ? e->dims() : 0});
    }
    return args;
}

}  // namespace

std::vector<AbstractGenerator::ArgInfo> GeneratorBase::get_input_arginfos() {
    ensure_configure_has_been_called();
    return get_arguments(param_info().inputs());
}

std::vector<AbstractGenerator::ArgInfo> GeneratorBase::get_output_arginfos() {
    ensure_configure_has_been_called();
    return get_arguments(param_info().outputs());
}

std::vector<Parameter> GeneratorBase::get_parameters_for_input(const std::string &name) {
    auto *input = find_input_by_name(name);

    std::vector<Parameter> params;
    // TODO: replicated code, yuck
    if (input->kind() == IOKind::Scalar) {
        internal_assert(input->funcs_.empty() && input->exprs_.size() == input->parameters_.size());
        for (size_t i = 0; i < input->exprs_.size(); ++i) {
            const auto &p = input->parameters_[i];
            internal_assert(!p.is_buffer());
            internal_assert(p.name() == input->array_name(i)) << "input name was " << p.name() << " expected " << input->array_name(i);
            internal_assert(p.dimensions() == 0) << "input dimensions was " << p.dimensions() << " expected " << 0;
            internal_assert(p.type() == input->type()) << "input type was " << p.name() << " expected " << input->type();
            params.push_back(p);
        }
    } else {
        internal_assert(input->exprs_.empty() && input->funcs_.size() == input->parameters_.size());
        for (size_t i = 0; i < input->funcs_.size(); ++i) {
            const auto &f = input->funcs_[i];
            const auto &p = input->parameters_[i];
            internal_assert(p.is_buffer());
            internal_assert(p.name() == input->array_name(i)) << "input name was " << p.name() << " expected " << input->array_name(i);
            internal_assert(p.dimensions() == f.dimensions()) << "input dimensions was " << p.dimensions() << " expected " << f.dimensions();
            internal_assert(p.type() == input->type()) << "input type was " << p.name() << " expected " << input->type();
            params.push_back(p);
        }
    }
    return params;
}

std::vector<Func> GeneratorBase::get_funcs_for_output(const std::string &n) {
    check_min_phase(GenerateCalled);
    auto *output = find_output_by_name(n);
    // Call for the side-effect of asserting if the value isn't defined.
    (void)output->array_size();
    for (const auto &f : output->funcs()) {
        user_assert(f.defined()) << "Output " << n << " was not fully defined.\n";
    }
    return output->funcs();
}

ExternsMap GeneratorBase::get_external_code_map() {
    // get_externs_map() returns a std::shared_ptr<ExternsMap>
    return *get_externs_map();
}

void GeneratorBase::bind_input(const std::string &name, const std::vector<Parameter> &v) {
    ensure_configure_has_been_called();
    advance_phase(InputsSet);
    std::vector<StubInput> si;
    std::copy(v.begin(), v.end(), std::back_inserter(si));
    find_input_by_name(name)->set_inputs(si);
}

void GeneratorBase::bind_input(const std::string &name, const std::vector<Func> &v) {
    ensure_configure_has_been_called();
    advance_phase(InputsSet);
    std::vector<StubInput> si;
    std::copy(v.begin(), v.end(), std::back_inserter(si));
    find_input_by_name(name)->set_inputs(si);
}

void GeneratorBase::bind_input(const std::string &name, const std::vector<Expr> &v) {
    ensure_configure_has_been_called();
    advance_phase(InputsSet);
    std::vector<StubInput> si;
    std::copy(v.begin(), v.end(), std::back_inserter(si));
    find_input_by_name(name)->set_inputs(si);
}

bool GeneratorBase::emit_cpp_stub(const std::string &stub_file_path) {
    user_assert(!generator_registered_name.empty() && !generator_stub_name.empty()) << "Generator has no name.\n";
    // Make sure we call configure() so that extra inputs/outputs are added as necessary.
    ensure_configure_has_been_called();
    // StubEmitter will want to access the GP/SP values, so advance the phase to avoid assert-fails.
    advance_phase(GenerateCalled);
    advance_phase(ScheduleCalled);
    GeneratorParamInfo &pi = param_info();
    std::ofstream file(stub_file_path);
    StubEmitter emit(file, generator_registered_name, generator_stub_name, pi.generator_params(), pi.inputs(), pi.outputs());
    emit.emit();
    return true;
}

GIOBase::GIOBase(size_t array_size,
                 const std::string &name,
                 IOKind kind,
                 const std::vector<Type> &types,
                 int dims)
    : array_size_(array_size), name_(name), kind_(kind), types_(types), dims_(dims) {
}

bool GIOBase::array_size_defined() const {
    return array_size_ != -1;
}

size_t GIOBase::array_size() const {
    user_assert(array_size_defined()) << "ArraySize is unspecified for " << input_or_output() << "'" << name() << "'; you need to explicitly set it via the resize() method or by setting '"
                                      << name() << ".size' in your build rules.";
    return (size_t)array_size_;
}

bool GIOBase::is_array() const {
    internal_error << "Unimplemented";
    return false;
}

const std::string &GIOBase::name() const {
    return name_;
}

IOKind GIOBase::kind() const {
    return kind_;
}

bool GIOBase::types_defined() const {
    return !types_.empty();
}

const std::vector<Type> &GIOBase::types() const {
    // If types aren't defined, but we have one Func that is,
    // we probably just set an Output<Func> and should propagate the types.
    if (!types_defined()) {
        // use funcs_, not funcs(): the latter could give a much-less-helpful error message
        // in this case.
        const auto &f = funcs_;
        if (f.size() == 1 && f.at(0).defined()) {
            check_matching_types(f.at(0).output_types());
        }
    }
    user_assert(types_defined()) << "Type is not defined for " << input_or_output() << " '" << name() << "'; you may need to specify '" << name() << ".type' as a GeneratorParam, or call set_type() from the configure() method.\n";
    return types_;
}

Type GIOBase::type() const {
    const auto &t = types();
    internal_assert(t.size() == 1) << "Expected types_.size() == 1, saw " << t.size() << " for " << name() << "\n";
    return t.at(0);
}

void GIOBase::set_type(const Type &type) {
    generator->check_exact_phase(GeneratorBase::ConfigureCalled);
    user_assert(!types_defined()) << "set_type() may only be called on an Input or Output that has no type specified.";
    types_ = {type};
}

void GIOBase::set_dimensions(int dims) {
    generator->check_exact_phase(GeneratorBase::ConfigureCalled);
    user_assert(!dims_defined()) << "set_dimensions() may only be called on an Input or Output that has no dimensionality specified.";
    dims_ = dims;
}

void GIOBase::set_array_size(int size) {
    generator->check_exact_phase(GeneratorBase::ConfigureCalled);
    user_assert(!array_size_defined()) << "set_array_size() may only be called on an Input or Output that has no array size specified.";
    array_size_ = size;
}

bool GIOBase::dims_defined() const {
    return dims_ != -1;
}

int GIOBase::dims() const {
    // If types aren't defined, but we have one Func that is,
    // we probably just set an Output<Func> and should propagate the types.
    if (!dims_defined()) {
        // use funcs_, not funcs(): the latter could give a much-less-helpful error message
        // in this case.
        const auto &f = funcs_;
        if (f.size() == 1 && f.at(0).defined()) {
            check_matching_dims(funcs().at(0).dimensions());
        }
    }
    user_assert(dims_defined()) << "Dimensions are not defined for " << input_or_output() << " '" << name() << "'; you may need to specify '" << name() << ".dim' as a GeneratorParam.\n";
    return dims_;
}

const std::vector<Func> &GIOBase::funcs() const {
    internal_assert(funcs_.size() == array_size() && exprs_.empty());
    return funcs_;
}

const std::vector<Expr> &GIOBase::exprs() const {
    internal_assert(exprs_.size() == array_size() && funcs_.empty());
    return exprs_;
}

void GIOBase::verify_internals() {
    user_assert(dims_ >= 0) << "Generator Input/Output Dimensions must have positive values";

    if (kind() != IOKind::Scalar) {
        for (const Func &f : funcs()) {
            user_assert(f.defined()) << "Input/Output " << name() << " is not defined.\n";
            user_assert(f.dimensions() == dims())
                << "Expected dimensions " << dims()
                << " but got " << f.dimensions()
                << " for " << name() << "\n";
            user_assert(f.outputs() == 1)
                << "Expected outputs() == " << 1
                << " but got " << f.outputs()
                << " for " << name() << "\n";
            user_assert(f.output_types().size() == 1)
                << "Expected output_types().size() == " << 1
                << " but got " << f.outputs()
                << " for " << name() << "\n";
            user_assert(f.output_types()[0] == type())
                << "Expected type " << type()
                << " but got " << f.output_types()[0]
                << " for " << name() << "\n";
        }
    } else {
        for (const Expr &e : exprs()) {
            user_assert(e.defined()) << "Input/Ouput " << name() << " is not defined.\n";
            user_assert(e.type() == type())
                << "Expected type " << type()
                << " but got " << e.type()
                << " for " << name() << "\n";
        }
    }
}

std::string GIOBase::array_name(size_t i) const {
    std::string n = name();
    if (is_array()) {
        n += "_" + std::to_string(i);
    }
    return n;
}

// If our type(s) are defined, ensure it matches the ones passed in, asserting if not.
// If our type(s) are not defined, just set to the ones passed in.
void GIOBase::check_matching_types(const std::vector<Type> &t) const {
    if (types_defined()) {
        user_assert(types().size() == t.size()) << "Type mismatch for " << name() << ": expected " << types().size() << " types but saw " << t.size();
        for (size_t i = 0; i < t.size(); ++i) {
            user_assert(types().at(i) == t.at(i)) << "Type mismatch for " << name() << ": expected " << types().at(i) << " saw " << t.at(i);
        }
    } else {
        types_ = t;
    }
}

void GIOBase::check_gio_access() const {
    // // Allow reading when no Generator is set, to avoid having to special-case ctor initing code
    if (!generator) {
        return;
    }
    user_assert(generator->phase > GeneratorBase::InputsSet)
        << "The " << input_or_output() << " \"" << name() << "\" cannot be examined before build() or generate() is called.\n";
}

// If our dims are defined, ensure it matches the one passed in, asserting if not.
// If our dims are not defined, just set to the one passed in.
void GIOBase::check_matching_dims(int d) const {
    internal_assert(d >= 0);
    if (dims_defined()) {
        user_assert(dims() == d) << "Dimensions mismatch for " << name() << ": expected " << dims() << " saw " << d;
    } else {
        dims_ = d;
    }
}

void GIOBase::check_matching_array_size(size_t size) const {
    if (array_size_defined()) {
        user_assert(array_size() == size) << "ArraySize mismatch for " << name() << ": expected " << array_size() << " saw " << size;
    } else {
        array_size_ = size;
    }
}

GeneratorInputBase::GeneratorInputBase(size_t array_size,
                                       const std::string &name,
                                       IOKind kind,
                                       const std::vector<Type> &t,
                                       int d)
    : GIOBase(array_size, name, kind, t, d) {
    ObjectInstanceRegistry::register_instance(this, 0, ObjectInstanceRegistry::GeneratorInput, this, nullptr);
}

GeneratorInputBase::GeneratorInputBase(const std::string &name, IOKind kind, const std::vector<Type> &t, int d)
    : GeneratorInputBase(1, name, kind, t, d) {
    // nothing
}

GeneratorInputBase::~GeneratorInputBase() {
    ObjectInstanceRegistry::unregister_instance(this);
}

void GeneratorInputBase::check_value_writable() const {
    user_assert(generator && generator->phase == GeneratorBase::InputsSet)
        << "The Input " << name() << " cannot be set at this point.\n";
}

void GeneratorInputBase::set_def_min_max() {
    // nothing
}

Parameter GeneratorInputBase::parameter() const {
    user_assert(!this->is_array()) << "Cannot call the parameter() method on Input<[]> " << name() << "; use an explicit subscript operator instead.";
    return parameters_.at(0);
}

void GeneratorInputBase::verify_internals() {
    GIOBase::verify_internals();

    const size_t expected = (kind() != IOKind::Scalar) ? funcs().size() : exprs().size();
    user_assert(parameters_.size() == expected) << "Expected parameters_.size() == "
                                                << expected << ", saw " << parameters_.size() << " for " << name() << "\n";
}

void GeneratorInputBase::init_internals() {
    if (inputs_set) {
        return;
    }

    // Call these for the side-effect of asserting if the values aren't defined.
    (void)array_size();
    (void)types();
    (void)dims();

    parameters_.clear();
    exprs_.clear();
    funcs_.clear();
    for (size_t i = 0; i < array_size(); ++i) {
        auto name = array_name(i);
        parameters_.emplace_back(type(), kind() != IOKind::Scalar, dims(), name);
        auto &p = parameters_[i];
        if (kind() != IOKind::Scalar) {
            internal_assert(dims() == p.dimensions());
            funcs_.push_back(make_param_func(p, name));
        } else {
            Expr e = Internal::Variable::make(type(), name, p);
            exprs_.push_back(e);
        }
    }

    set_def_min_max();
    verify_internals();
}

void GeneratorInputBase::set_inputs(const std::vector<StubInput> &inputs) {
    generator->check_exact_phase(GeneratorBase::InputsSet);
    parameters_.clear();
    exprs_.clear();
    funcs_.clear();
    check_matching_array_size(inputs.size());
    for (size_t i = 0; i < inputs.size(); ++i) {
        const StubInput &in = inputs.at(i);
        user_assert(in.kind() == kind()) << "An input for " << name() << " is not of the expected kind.\n";
        if (kind() == IOKind::Function) {
            auto f = in.func();
            user_assert(f.defined()) << "The input for " << name() << " is an undefined Func. Please define it.\n";
            check_matching_types(f.output_types());
            check_matching_dims(f.dimensions());
            funcs_.push_back(f);
            parameters_.emplace_back(f.output_types().at(0), true, f.dimensions(), array_name(i));
        } else if (kind() == IOKind::Buffer) {
            auto p = in.parameter();
            user_assert(p.defined()) << "The input for " << name() << " is an undefined Buffer. Please define it.\n";
            check_matching_types({p.type()});
            check_matching_dims(p.dimensions());
            funcs_.push_back(make_param_func(p, name()));
            parameters_.push_back(p);
        } else {
            auto e = in.expr();
            user_assert(e.defined()) << "The input for " << name() << " is an undefined Expr. Please define it.\n";
            check_matching_types({e.type()});
            check_matching_dims(0);
            exprs_.push_back(e);
            parameters_.emplace_back(e.type(), false, 0, array_name(i));
        }
    }

    set_def_min_max();
    verify_internals();
    inputs_set = true;
}

void GeneratorInputBase::set_estimate_impl(const Var &var, const Expr &min, const Expr &extent) {
    internal_assert(exprs_.empty() && !funcs_.empty() && parameters_.size() == funcs_.size());
    for (size_t i = 0; i < funcs_.size(); ++i) {
        Func &f = funcs_[i];
        f.set_estimate(var, min, extent);
        // Propagate the estimate into the Parameter as well, just in case
        // we end up compiling this for toplevel.
        std::vector<Var> args = f.args();
        int dim = -1;
        for (size_t a = 0; a < args.size(); ++a) {
            if (args[a].same_as(var)) {
                dim = a;
                break;
            }
        }
        internal_assert(dim >= 0);
        Parameter &p = parameters_[i];
        p.set_min_constraint_estimate(dim, min);
        p.set_extent_constraint_estimate(dim, extent);
    }
}

void GeneratorInputBase::set_estimates_impl(const Region &estimates) {
    internal_assert(exprs_.empty() && !funcs_.empty() && parameters_.size() == funcs_.size());
    for (size_t i = 0; i < funcs_.size(); ++i) {
        Func &f = funcs_[i];
        f.set_estimates(estimates);
        // Propagate the estimate into the Parameter as well, just in case
        // we end up compiling this for toplevel.
        for (size_t dim = 0; dim < estimates.size(); ++dim) {
            Parameter &p = parameters_[i];
            const Range &r = estimates[dim];
            p.set_min_constraint_estimate(dim, r.min);
            p.set_extent_constraint_estimate(dim, r.extent);
        }
    }
}

GeneratorOutputBase::GeneratorOutputBase(size_t array_size, const std::string &name, IOKind kind, const std::vector<Type> &t, int d)
    : GIOBase(array_size, name, kind, t, d) {
    internal_assert(kind != IOKind::Scalar);
    ObjectInstanceRegistry::register_instance(this, 0, ObjectInstanceRegistry::GeneratorOutput,
                                              this, nullptr);
}

GeneratorOutputBase::GeneratorOutputBase(const std::string &name, IOKind kind, const std::vector<Type> &t, int d)
    : GeneratorOutputBase(1, name, kind, t, d) {
    // nothing
}

GeneratorOutputBase::~GeneratorOutputBase() {
    ObjectInstanceRegistry::unregister_instance(this);
}

void GeneratorOutputBase::check_value_writable() const {
    user_assert(generator && generator->phase == GeneratorBase::GenerateCalled)
        << "The Output " << name() << " can only be set inside generate().\n";
}

void GeneratorOutputBase::init_internals() {
    exprs_.clear();
    funcs_.clear();
    if (array_size_defined()) {
        for (size_t i = 0; i < array_size(); ++i) {
            funcs_.emplace_back(array_name(i));
        }
    }
}

void GeneratorOutputBase::resize(size_t size) {
    internal_assert(is_array());
    internal_assert(!array_size_defined()) << "You may only call " << name()
                                           << ".resize() when then size is undefined\n";
    array_size_ = (int)size;
    init_internals();
}

StubOutputBufferBase::StubOutputBufferBase() = default;

StubOutputBufferBase::StubOutputBufferBase(const Func &f, const std::shared_ptr<AbstractGenerator> &generator)
    : f(f), generator(generator) {
}

Realization StubOutputBufferBase::realize(std::vector<int32_t> sizes) {
    return f.realize(std::move(sizes), get_target());
}

Target StubOutputBufferBase::get_target() const {
    return generator->get_target_info().target;
}

RegisterGenerator::RegisterGenerator(const char *registered_name, GeneratorFactory generator_factory) {
    Internal::GeneratorRegistry::register_factory(registered_name, std::move(generator_factory));
}

void generator_test() {
    GeneratorContext context(get_host_target());

    // Verify that the Generator's internal phase actually prevents unsupported
    // order of operations.
    {
        class Tester : public Generator<Tester> {
        public:
            GeneratorParam<int> gp0{"gp0", 0};
            GeneratorParam<float> gp1{"gp1", 1.f};
            GeneratorParam<uint64_t> gp2{"gp2", 2};

            Input<int> input{"input"};
            Output<Func> output{"output", Int(32), 1};

            void generate() {
                internal_assert(gp0 == 1);
                internal_assert(gp1 == 2.f);
                internal_assert(gp2 == (uint64_t)2);  // unchanged
                Var x;
                output(x) = input + gp0;
            }
            void schedule() {
                // empty
            }
        };

        Tester tester;
        tester.init_from_context(context);
        internal_assert(tester.phase == GeneratorBase::Created);

        // Verify that calling GeneratorParam::set() works.
        tester.gp0.set(1);

        tester.set_inputs_vector({{StubInput(42)}});
        internal_assert(tester.phase == GeneratorBase::InputsSet);

        // tester.set_inputs_vector({{StubInput(43)}});  // This will assert-fail.

        // Also ok to call in this phase.
        tester.gp1.set(2.f);

        tester.call_generate();
        internal_assert(tester.phase == GeneratorBase::GenerateCalled);

        // tester.set_inputs_vector({{StubInput(44)}});  // This will assert-fail.
        // tester.gp2.set(2);  // This will assert-fail.

        tester.call_schedule();
        internal_assert(tester.phase == GeneratorBase::ScheduleCalled);

        // tester.set_inputs_vector({{StubInput(45)}});  // This will assert-fail.
        // tester.gp2.set(2);  // This will assert-fail.
        // tester.sp2.set(202);  // This will assert-fail.
    }

    // Verify that the Generator's internal phase actually prevents unsupported
    // order of operations (with old-style Generator)
    {
        class Tester : public Generator<Tester> {
        public:
            GeneratorParam<int> gp0{"gp0", 0};
            GeneratorParam<float> gp1{"gp1", 1.f};
            GeneratorParam<uint64_t> gp2{"gp2", 2};
            GeneratorParam<uint8_t> gp_uint8{"gp_uint8", 65};
            GeneratorParam<int8_t> gp_int8{"gp_int8", 66};
            GeneratorParam<char> gp_char{"gp_char", 97};
            GeneratorParam<signed char> gp_schar{"gp_schar", 98};
            GeneratorParam<unsigned char> gp_uchar{"gp_uchar", 99};
            GeneratorParam<bool> gp_bool{"gp_bool", true};

            Input<int> input{"input"};

            Func build() {
                internal_assert(gp0 == 1);
                internal_assert(gp1 == 2.f);
                internal_assert(gp2 == (uint64_t)2);  // unchanged
                internal_assert(gp_uint8 == 67);
                internal_assert(gp_int8 == 68);
                internal_assert(gp_bool == false);
                internal_assert(gp_char == 107);
                internal_assert(gp_schar == 108);
                internal_assert(gp_uchar == 109);
                Var x;
                Func output;
                output(x) = input + gp0;
                return output;
            }
        };

        Tester tester;
        tester.init_from_context(context);
        internal_assert(tester.phase == GeneratorBase::Created);

        // Verify that calling GeneratorParam::set() works.
        tester.gp0.set(1);

        // set_inputs_vector() can't be called on an old-style Generator;
        // that's OK, since we can skip from Created -> GenerateCalled anyway
        // tester.set_inputs_vector({{StubInput(42)}});
        // internal_assert(tester.phase == GeneratorBase::InputsSet);

        // tester.set_inputs_vector({{StubInput(43)}});  // This will assert-fail.

        // Also ok to call in this phase.
        tester.gp1.set(2.f);

        // Verify that 8-bit non-boolean GP values are parsed as integers, not chars.
        tester.gp_int8.set_from_string("68");
        tester.gp_uint8.set_from_string("67");
        tester.gp_char.set_from_string("107");
        tester.gp_schar.set_from_string("108");
        tester.gp_uchar.set_from_string("109");
        tester.gp_bool.set_from_string("false");

        tester.build_pipeline();
        internal_assert(tester.phase == GeneratorBase::ScheduleCalled);

        // tester.set_inputs_vector({{StubInput(45)}});  // This will assert-fail.
        // tester.gp2.set(2);  // This will assert-fail.
        // tester.sp2.set(202);  // This will assert-fail.
    }

    // Verify that set_inputs() works properly, even if the specific subtype of Generator is not known.
    {
        class Tester : public Generator<Tester> {
        public:
            Input<int> input_int{"input_int"};
            Input<float> input_float{"input_float"};
            Input<uint8_t> input_byte{"input_byte"};
            Input<uint64_t[4]> input_scalar_array{"input_scalar_array"};
            Input<Func> input_func_typed{"input_func_typed", Int(16), 1};
            Input<Func> input_func_untyped{"input_func_untyped", 1};
            Input<Func[]> input_func_array{"input_func_array", 1};
            Input<Buffer<uint8_t>> input_buffer_typed{"input_buffer_typed", 3};
            Input<Buffer<>> input_buffer_untyped{"input_buffer_untyped"};
            Output<Func> output{"output", Float(32), 1};

            void generate() {
                Var x;
                output(x) = input_int +
                            input_float +
                            input_byte +
                            input_scalar_array[3] +
                            input_func_untyped(x) +
                            input_func_typed(x) +
                            input_func_array[0](x) +
                            input_buffer_typed(x, 0, 0) +
                            input_buffer_untyped(x, Halide::_);
            }
            void schedule() {
                // nothing
            }
        };

        Tester tester_instance;
        tester_instance.init_from_context(context);
        // Use a base-typed reference to verify the code below doesn't know about subtype
        GeneratorBase &tester = tester_instance;

        const int i = 1234;
        const float f = 2.25f;
        const uint8_t b = 0x42;
        const std::vector<uint64_t> a = {1, 2, 3, 4};
        Var x;
        Func fn_typed, fn_untyped;
        fn_typed(x) = cast<int16_t>(38);
        fn_untyped(x) = 32.f;
        const std::vector<Func> fn_array = {fn_untyped, fn_untyped};

        Buffer<uint8_t> buf_typed(1, 1, 1);
        Buffer<float> buf_untyped(1);

        buf_typed.fill(33);
        buf_untyped.fill(34);

        // set_inputs() requires inputs in Input<>-decl-order,
        // and all inputs match type exactly.
        tester.set_inputs(i, f, b, a, fn_typed, fn_untyped, fn_array, buf_typed, buf_untyped);
        tester.call_generate();
        tester.call_schedule();

        Buffer<float> im = tester_instance.realize({1});
        internal_assert(im.dimensions() == 1);
        internal_assert(im.dim(0).extent() == 1);
        internal_assert(im(0) == 1475.25f) << "Expected 1475.25 but saw " << im(0);
    }

    // Verify that array inputs and outputs are typed correctly.
    {
        class Tester : public Generator<Tester> {
        public:
            Input<int[]> expr_array_input{"expr_array_input"};
            Input<Func[]> func_array_input{"input_func_array"};
            Input<Buffer<>[]> buffer_array_input { "buffer_array_input" };

            Input<int[]> expr_array_output{"expr_array_output"};
            Output<Func[]> func_array_output{"func_array_output"};
            Output<Buffer<>[]> buffer_array_output { "buffer_array_output" };

            void generate() {
            }
        };

        Tester tester_instance;

        static_assert(std::is_same<decltype(tester_instance.expr_array_input[0]), const Expr &>::value, "type mismatch");
        static_assert(std::is_same<decltype(tester_instance.expr_array_output[0]), const Expr &>::value, "type mismatch");

        static_assert(std::is_same<decltype(tester_instance.func_array_input[0]), const Func &>::value, "type mismatch");
        static_assert(std::is_same<decltype(tester_instance.func_array_output[0]), Func &>::value, "type mismatch");

        static_assert(std::is_same<decltype(tester_instance.buffer_array_input[0]), ImageParam>::value, "type mismatch");
        static_assert(std::is_same<decltype(tester_instance.buffer_array_output[0]), const Func &>::value, "type mismatch");
    }

    class GPTester : public Generator<GPTester> {
    public:
        GeneratorParam<int> gp{"gp", 0};
        Output<Func> output{"output", Int(32), 0};
        void generate() {
            output() = 0;
        }
        void schedule() {
        }
    };
    GPTester gp_tester;
    gp_tester.init_from_context(context);
    // Accessing the GeneratorParam will assert-fail if we
    // don't do some minimal setup here.
    gp_tester.set_inputs_vector({});
    gp_tester.call_generate();
    gp_tester.call_schedule();
    auto &gp = gp_tester.gp;

    // Verify that RDom parameter-pack variants can convert GeneratorParam to Expr
    RDom rdom(0, gp, 0, gp);

    // Verify that Func parameter-pack variants can convert GeneratorParam to Expr
    Var x, y;
    Func f, g;
    f(x, y) = x + y;
    g(x, y) = f(gp, gp);  // check Func::operator() overloads
    g(rdom.x, rdom.y) += f(rdom.x, rdom.y);
    g.update(0).reorder(rdom.y, rdom.x);  // check Func::reorder() overloads for RDom::operator RVar()

    // Verify that print() parameter-pack variants can convert GeneratorParam to Expr
    print(f(0, 0), g(1, 1), gp);
    print_when(true, f(0, 0), g(1, 1), gp);

    // Verify that Tuple parameter-pack variants can convert GeneratorParam to Expr
    Tuple t(gp, gp, gp);

    std::cout << "Generator test passed" << std::endl;
}

}  // namespace Internal
}  // namespace Halide
