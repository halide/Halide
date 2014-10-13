// Generator requires C++11
#if __cplusplus > 199711L

#include "Generator.h"
#include <regex>

namespace {

// Return true iff the name is valid for Generators or Params.
bool IsValidName(const std::string& n) {
    static std::regex valid_name_pattern("^[A-Za-z_][A-Za-z0-9_]*$");
    return std::regex_match(n, valid_name_pattern);
}

}  // namespace

namespace Halide {
namespace Internal {

GeneratorParamBase::GeneratorParamBase(const std::string &name) : name(name) {
    ObjectInstanceRegistry::register_instance(this, 0, ObjectInstanceRegistry::GeneratorParam,
                                              this);
}

GeneratorParamBase::~GeneratorParamBase() { ObjectInstanceRegistry::unregister_instance(this); }

/* static */
GeneratorRegistry &GeneratorRegistry::get_registry() {
    static GeneratorRegistry *registry = new GeneratorRegistry;
    return *registry;
}

/* static */
void GeneratorRegistry::register_factory(const std::string &name,
                                         std::unique_ptr<GeneratorFactory> factory) {
    user_assert(IsValidName(name)) << "Invalid Generator name: " << name;
    GeneratorRegistry &registry = get_registry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    internal_assert(registry.factories.find(name) == registry.factories.end())
        << "Duplicate Generator name: " << name;
    registry.factories[name] = std::move(factory);
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
std::unique_ptr<GeneratorBase> GeneratorRegistry::create(const std::string &name,
                                                         const GeneratorParamValues &params) {
    GeneratorRegistry &registry = get_registry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    auto it = registry.factories.find(name);
    user_assert(it != registry.factories.end()) << "Generator not found: " << name;
    return it->second->create(params);
}

/* static */
std::vector<std::string> GeneratorRegistry::enumerate() {
    GeneratorRegistry &registry = get_registry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    std::vector<std::string> result;
    for (auto it = registry.factories.begin(); it != registry.factories.end(); ++it) {
        result.push_back(it->first);
    }
    return result;
}

GeneratorBase::GeneratorBase(size_t size) : size(size), params_built(false) {
    ObjectInstanceRegistry::register_instance(this, size, ObjectInstanceRegistry::Generator, this);
}

GeneratorBase::~GeneratorBase() { ObjectInstanceRegistry::unregister_instance(this); }

void GeneratorBase::build_params() {
    if (!params_built) {
        std::vector<void *> vf = ObjectInstanceRegistry::instances_in_range(
            this, size, ObjectInstanceRegistry::FilterParam);
        for (size_t i = 0; i < vf.size(); ++i) {
            Parameter *param = static_cast<Parameter *>(vf[i]);
            internal_assert(param != nullptr);
            user_assert(IsValidName(param->name())) << "Invalid Param name: " << param->name();
            user_assert(filter_params.find(param->name()) == filter_params.end())
                << "Duplicate Param name: " << param->name();
            filter_params[param->name()] = param;
            filter_arguments.push_back(Argument(param->name(), param->is_buffer(), param->type()));
        }

        std::vector<void *> vg = ObjectInstanceRegistry::instances_in_range(
            this, size, ObjectInstanceRegistry::GeneratorParam);
        for (size_t i = 0; i < vg.size(); ++i) {
            GeneratorParamBase *param = static_cast<GeneratorParamBase *>(vg[i]);
            internal_assert(param != nullptr);
            user_assert(IsValidName(param->name)) << "Invalid GeneratorParam name: " << param->name;
            user_assert(generator_params.find(param->name) == generator_params.end())
                << "Duplicate GeneratorParam name: " << param->name;
            generator_params[param->name] = param;
        }
        params_built = true;
    }
}

void GeneratorBase::set_generator_param_values(const GeneratorParamValues &params) {
    build_params();
    for (auto key_value : params) {
        const std::string &key = key_value.first;
        const std::string &value = key_value.second;
        auto param = generator_params.find(key);
        user_assert(param != generator_params.end())
            << "Generator has no GeneratorParam named: " << key;
        param->second->set_from_string(value);
    }
}

void GeneratorBase::emit_filter(const std::string &output_dir, const std::string &function_name,
                                const EmitOptions &options) {
    Func func = build();

    build_params();

    std::string base_path = output_dir + "/" + function_name;
    if (options.emit_o) {
        func.compile_to_object(base_path + ".o", filter_arguments, function_name, target);
    }
    if (options.emit_h) {
        func.compile_to_header(base_path + ".h", filter_arguments, function_name);
    }
    if (options.emit_cpp) {
        func.compile_to_c(base_path + ".cpp", filter_arguments, function_name, target);
    }
    if (options.emit_assembly) {
        func.compile_to_assembly(base_path + ".s", filter_arguments, function_name, target);
    }
    if (options.emit_bitcode) {
        func.compile_to_bitcode(base_path + ".bc", filter_arguments, function_name, target);
    }
    if (options.emit_stmt) {
        func.compile_to_lowered_stmt(base_path + ".stmt", Halide::Text, target);
    }
    if (options.emit_stmt_html) {
        func.compile_to_lowered_stmt(base_path + ".html", Halide::HTML, target);
    }
}

}  // namespace Internal
}  // namespace Halide

#endif  // __cplusplus > 199711L
