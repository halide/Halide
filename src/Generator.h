#ifndef HALIDE_GENERATOR_H_
#define HALIDE_GENERATOR_H_

// Generator requires C++11
#if __cplusplus > 199711L

#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "Func.h"
#include "ObjectInstanceRegistry.h"
#include "Target.h"

namespace Halide {

namespace Internal {

class GeneratorFactory;

class GeneratorParamBase {
public:
    explicit GeneratorParamBase(const std::string& name);
    virtual ~GeneratorParamBase();
    virtual void set_from_string(const std::string& value_string) = 0;

    const std::string name;
};

}  // namespace Internal

template <typename T>
class GeneratorParam : public Internal::GeneratorParamBase {
 public:
    template <typename T2 = T, typename std::enable_if<std::is_same<T2, Target>::value>::type* = nullptr>
    GeneratorParam(const std::string& name, const T& value)
        : GeneratorParamBase(name),
            value(value),
            min(value),
            max(value) {
    }

    // Note that "is_arithmetic" includes the bool type.
    template <typename T2 = T, typename std::enable_if<std::is_arithmetic<T2>::value>::type* = nullptr>
    GeneratorParam(const std::string& name, const T& value)
        : GeneratorParamBase(name),
            value(value),
            min(std::numeric_limits<T>::min()),
            max(std::numeric_limits<T>::max()) {
    }

    template <typename T2 = T, typename std::enable_if<std::is_arithmetic<T2>::value && !std::is_same<T2, bool>::value>::type* = nullptr>
    GeneratorParam(const std::string& name,
                                 const T& value,
                                 const T& min,
                                 const T& max)
        : GeneratorParamBase(name),
            value(std::min(std::max(value, min), max)),
            min(min),
            max(max) {
        static_assert(std::is_arithmetic<T>::value && !std::is_same<T, bool>::value, "Only arithmetic types may specify min and max");
    }

    template <typename T2 = T, typename std::enable_if<std::is_enum<T2>::value>::type* = nullptr>
    GeneratorParam(const std::string& name,
                                 const T& value,
                                 const std::map<std::string, T>& enum_map)
        : GeneratorParamBase(name),
            value(value),
            min(std::numeric_limits<T>::min()),
            max(std::numeric_limits<T>::max()),
            enum_map(enum_map) {
        static_assert(std::is_enum<T>::value, "Only enum types may specify value maps");
    }

    // TODO(srj): setting an arithmetic value to out-of-range will silently clamp it.
    // Would it be better to fail with user_assert (as would happen if you specified an illegal enum)?
    template <typename T2 = T, typename std::enable_if<std::is_arithmetic<T2>::value>::type* = nullptr>
    void set(const T& new_value) {
        value = std::min(std::max(new_value, min), max);
    }

    template <typename T2 = T, typename std::enable_if<!std::is_arithmetic<T2>::value>::type* = nullptr>
    void set(const T& new_value) {
        value = new_value;
    }
    void set_from_string(const std::string& new_value_string) override {
        // delegate to a function that we can specialize based on the template argument
        set(parse(new_value_string));
    }
    operator const T&() const { return value; }
    operator Expr() const { return value; }

private:
    T value;
    const T min, max;                         // only for arithmetic types
    const std::map<std::string, T> enum_map;  // only for enums

    static T lookup(const std::string& key, const std::map<std::string, T>& value_map) {
      auto it = value_map.find(key);
      user_assert(it != value_map.end()) << "Enumeration value not found: " << key;
      return it->second;
    }

    template <typename T2 = T, typename std::enable_if<std::is_same<T2, Target>::value>::type* = nullptr>
    T parse(const std::string& s) {
        return parse_target_string(s);
    }

    template <typename T2 = T, typename std::enable_if<std::is_same<T2, bool>::value>::type* = nullptr>
    T parse(const std::string& s) {
        if (s == "true") return true;
        if (s == "false") return false;
        user_assert(false) << "Unable to parse bool: " << s;
        return false;
    }

    template <typename T2 = T, typename std::enable_if<std::is_integral<T2>::value && !std::is_same<T2, bool>::value>::type* = nullptr>
    T parse(const std::string& s) {
        std::istringstream iss(s);
        T t;
        iss >> t;
        user_assert(!iss.fail()) << "Unable to parse integer: " << s;
        return t;
    }

    template <typename T2 = T, typename std::enable_if<std::is_floating_point<T2>::value>::type* = nullptr>
    T parse(const std::string& s) {
        std::istringstream iss(s);
        T t;
        iss >> t;
        user_assert(!iss.fail()) << "Unable to parse float: " << s;
        return t;
    }

    template <typename T2 = T, typename std::enable_if<std::is_enum<T2>::value>::type* = nullptr>
    T parse(const std::string& s) {
        return lookup(s, enum_map);
    }
};

namespace Internal {

using GeneratorParamValues = std::map<std::string, std::string>;

class NamesInterface {
    // Names in this class are only intended for use in derived classes.
protected:
    // Import a consistent list of Halide names that can be used in
    // Halide generators without qualification.
    using Var = Halide::Var;
    using Expr = Halide::Expr;
    using Func = Halide::Func;
    using RDom = Halide::RDom;
    using ImageParam = Halide::ImageParam;
    template <typename T> using GeneratorParam = Halide::GeneratorParam<T>;
    template <typename T> using Param = Halide::Param<T>;
    template <typename T> Expr cast(Expr e) const { return Halide::cast<T>(e); }
};

class GeneratorBase : public NamesInterface {
public:
    struct EmitOptions {
        bool emit_o, emit_h, emit_cpp, emit_assembly, emit_bitcode, emit_stmt, emit_stmt_html;
        EmitOptions() : emit_o(true), emit_h(true), emit_cpp(false), emit_assembly(false),
            emit_bitcode(false), emit_stmt(false), emit_stmt_html(false) {}
    };

    virtual ~GeneratorBase();

    virtual Func build() = 0;

    const Target& get_target() const { return target; }

    void set_generator_param_values(const GeneratorParamValues& params);

    std::vector<Argument> get_filter_arguments() const;

    void emit_filter(const std::string& output_dir,
                               const std::string& function_name,
                               const EmitOptions& options = EmitOptions());
protected:
    GeneratorBase(size_t size);

private:
    const size_t size;

    std::vector<Argument> filter_arguments;
    std::map<std::string, Internal::Parameter*> filter_params;
    std::map<std::string, Internal::GeneratorParamBase*> generator_params;
    bool params_built;

    GeneratorParam<Target> target{"target", Halide::get_jit_target_from_environment()};

    void build_params();

    // Provide private, unimplemented, wrong-result-type methods here
    // so that Generators don't attempt to call the global methods
    // of the same name by accident: use the get_target() method instead.
    void get_host_target();
    void get_jit_target_from_environment();
    void get_target_from_environment();

    GeneratorBase(const GeneratorBase&) = delete;
    void operator=(const GeneratorBase&) = delete;
};

class GeneratorFactory {
public:
    virtual ~GeneratorFactory() {}
    virtual std::unique_ptr<GeneratorBase> create(const GeneratorParamValues& params) const = 0;
};

class GeneratorRegistry {
 public:
    static void register_factory(const std::string& name, std::unique_ptr<GeneratorFactory> factory);
    static void unregister_factory(const std::string& name);
    static std::vector<std::string> enumerate();
    static std::unique_ptr<GeneratorBase> create(const std::string& name, const GeneratorParamValues& params);

 private:
    using GeneratorFactoryMap = std::map<const std::string, std::unique_ptr<GeneratorFactory>>;

    GeneratorFactoryMap factories;
    std::mutex mutex;

    static GeneratorRegistry& get_registry();

    GeneratorRegistry() {}
    GeneratorRegistry(const GeneratorRegistry&) = delete;
    void operator=(const GeneratorRegistry&) = delete;
};

}  // namespace Internal

template<class T>
class Generator : public Internal::GeneratorBase {
 public:
    Generator() : Internal::GeneratorBase(sizeof(T)) {}
};

template <class T>
class RegisterGenerator {
 private:
    class TFactory : public Internal::GeneratorFactory {
     public:
        virtual std::unique_ptr<Internal::GeneratorBase> create(const Internal::GeneratorParamValues& params) const {
            std::unique_ptr<Internal::GeneratorBase> g(new T());
            g->set_generator_param_values(params);
            return g;
        }
    };

 public:
    explicit RegisterGenerator(const std::string& name) {
        std::unique_ptr<Internal::GeneratorFactory> f(new TFactory());
        Internal::GeneratorRegistry::register_factory(name, std::move(f));
    }
};

}  // namespace Halide

#endif  // __cplusplus > 199711L

#endif  // HALIDE_GENERATOR_H_
