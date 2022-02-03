#ifndef HALIDE_G2_H_
#define HALIDE_G2_H_

/*
    TODO:

    - How to specify inputs-as-buffers?
        - (Mainly, how to specify buffer-level constraints on an input Func, e.g stride, etc, for e.g. specialize())
        - static declarations aren't flexible enough, alas
        - currently requires declaring those inputs as ImageParam, which is adequate but not ideal (requires
          manual wrappers for some cases)
          - Could we allow some sort of lambda in the registration to allow setting them?
            Probably not, might need info about other inputs that would be hard to pipeline in
        - can't specify Halide::Buffer as input because we need a Parameter
        - can't use func.output_buffer() [sic] because the Parameter won't get found by code
          and is wonky and bad
    - How to specify outputs-as-buffers?
        - func.output_buffer() works, but is ugly and yucky
        - could we surface OutputImageParam as a useful type here?
          Maybe but would be a weird paradigm shift
*/

#include "AbstractGenerator.h"

#include <array>
#include <iostream>
#include <sstream>

// ----------------------------------------------

struct halide_fake_string_type_t {};
struct halide_fake_target_type_t {};
struct halide_fake_type_type_t {};

template<>
struct halide_c_type_to_name<halide_fake_string_type_t> {
    static constexpr bool known_type = true;
    static halide_cplusplus_type_name name() {
        return {halide_cplusplus_type_name::Simple, "std::string"};
    }
};

template<>
struct halide_c_type_to_name<halide_fake_target_type_t> {
    static constexpr bool known_type = true;
    static halide_cplusplus_type_name name() {
        return {halide_cplusplus_type_name::Simple, "Halide::Target"};
    }
};

template<>
struct halide_c_type_to_name<halide_fake_type_type_t> {
    static constexpr bool known_type = true;
    static halide_cplusplus_type_name name() {
        return {halide_cplusplus_type_name::Simple, "Halide::Type"};
    }
};

namespace Halide {

inline std::ostream &operator<<(std::ostream &stream, const std::vector<Type> &v) {
    stream << "{";
    const char *comma = "";
    for (const Type &t : v) {
        stream << comma << t;
        comma = ",";
    }
    stream << "}";
    return stream;
}

namespace Internal {

// ----------------------------------------------

// Strip the class from a method type
template<typename T>
struct remove_class {};
template<typename C, typename R, typename... A>
struct remove_class<R (C::*)(A...)> { typedef R type(A...); };
template<typename C, typename R, typename... A>
struct remove_class<R (C::*)(A...) const> { typedef R type(A...); };

template<typename F>
struct strip_function_object {
    using type = typename remove_class<decltype(&F::operator())>::type;
};

// Extracts the function signature from a function, function pointer or lambda.
template<typename Function, typename F = typename std::remove_reference<Function>::type>
using function_signature = std::conditional<
    std::is_function<F>::value,
    F,
    typename strip_function_object<F>::type>;

template<typename T, typename T0 = typename std::remove_reference<T>::type>
using is_lambda = std::integral_constant<bool, !std::is_function<T0>::value && !std::is_pointer<T0>::value && !std::is_member_pointer<T0>::value>;

// ---------------------------------------

enum class ArgKind {
    Unknown,
    Constant,
    Expression,
    Tuple,
    Function,
    Pipeline,
    ImageParam,
};

struct SingleArg {
    std::string name;
    ArgKind kind = ArgKind::Unknown;
    std::vector<Type> types;
    int dimensions = -1;
    std::string default_value;  // only when kind == Constant
    bool is_output = false;

    explicit SingleArg(const std::string &n, ArgKind k, const std::vector<Type> &t, int d, const std::string &s = "", bool o = false)
        : name(n), kind(k), types(t), dimensions(d), default_value(s), is_output(o) {
    }

    // Combine the inferred type info with the explicitly-annotated type info
    // to produce an ArgInfo. All information must be specified in at least one
    // of the two. It's ok for info to be specified in both places iff they
    // agree.
    static SingleArg match(const SingleArg &annotated, const SingleArg &inferred, bool skip_default_value = true) {
        user_assert(!annotated.name.empty())
            << "Unable to resolve signature for Generator: all arguments must have an explicit name specified.";

        return SingleArg{
            get_matching_value(annotated.name, inferred.name, annotated.name, "name"),
            get_matching_value(annotated.kind, inferred.kind, annotated.name, "kind"),
            get_matching_value(annotated.types, inferred.types, annotated.name, "types"),
            get_matching_value(annotated.dimensions, inferred.dimensions, annotated.name, "dimensions"),
            skip_default_value ?
                require_both_empty(annotated.default_value, inferred.default_value) :
                get_matching_value(annotated.default_value, inferred.default_value, annotated.name, "default_value"),
            get_matching_value(annotated.is_output, inferred.is_output, annotated.name, "is_output"),
        };
    }

private:
    template<typename T>
    static bool is_specified(const T &t);

    template<typename T>
    static T get_matching_value(const T &annotated, const T &inferred, const std::string &name, const char *field);

    template<typename T>
    static T require_both_empty(const T &annotated, const T &inferred);
};

// ---------------------------------------

inline std::ostream &operator<<(std::ostream &stream, ArgKind k) {
    static const char *const kinds[] = {
        "Unknown",
        "Constant",
        "Expression",
        "Tuple",
        "Function",
        "Pipeline",
        "ImageParam",
    };
    stream << kinds[(int)k];
    return stream;
}

inline std::ostream &operator<<(std::ostream &stream, const SingleArg &a) {
    stream << "SingleArg{" << a.name << "," << a.kind << "," << a.types << "," << a.dimensions << "," << a.default_value << "}";
    return stream;
}

inline std::ostream &operator<<(std::ostream &stream, IOKind k) {
    static const char *const kinds[] = {
        "Scalar",
        "Function",
        "Buffer",
    };
    stream << kinds[(int)k];
    return stream;
}

inline std::ostream &operator<<(std::ostream &stream, const AbstractGenerator::ArgInfo &a) {
    stream << "ArgInfo{" << a.name << "," << a.kind << "," << a.types << "," << a.dimensions << "}";
    return stream;
}

// ---------------------------------------

template<typename T>
/*static*/ T SingleArg::get_matching_value(const T &annotated, const T &inferred, const std::string &name, const char *field) {
    const bool a_spec = is_specified(annotated);
    const bool i_spec = is_specified(inferred);

    user_assert(a_spec || i_spec)
        << "Unable to resolve signature for Generator argument '" << name << "': "
        << "There is no explicitly-specified or inferred value for field '" << field << "'.";

    if (a_spec) {
        if (i_spec) {
            user_assert(annotated == inferred)
                << "Unable to resolve signature for Generator argument '" << name << "': "
                << "The explicitly-specified value for field '" << field
                << "' was '" << annotated
                << "', which does not match the inferred value '" << inferred << "'.";
        }
        return annotated;
    } else {
        return inferred;
    }
}

template<typename T>
/*static*/ T SingleArg::require_both_empty(const T &annotated, const T &inferred) {
    const bool a_spec = is_specified(annotated);
    const bool i_spec = is_specified(inferred);

    internal_assert(!a_spec && !i_spec);
    return annotated;
}

template<>
inline bool SingleArg::is_specified(const std::string &n) {
    return !n.empty();
}

template<>
inline bool SingleArg::is_specified(const ArgKind &k) {
    return k != ArgKind::Unknown;
}

template<>
inline bool SingleArg::is_specified(const std::vector<Type> &t) {
    return !t.empty();
}

template<>
inline bool SingleArg::is_specified(const int &d) {
    return d >= 0;
}

template<>
inline bool SingleArg::is_specified(const bool &d) {
    return true;
}

template<typename T>
struct SingleArgInferrer {
    inline SingleArg operator()() {
        const Type t = type_of<T>();
        if (t.is_scalar() && (t.is_int() || t.is_uint() || t.is_float())) {
            return SingleArg{"", ArgKind::Constant, {t}, 0};
        }
        return SingleArg{"", ArgKind::Unknown, {}, -1};
    }
};

template<>
inline SingleArg SingleArgInferrer<Halide::Type>::operator()() {
    const Type t = type_of<halide_fake_type_type_t *>();
    return SingleArg{"", ArgKind::Constant, {t}, 0};
}

template<>
inline SingleArg SingleArgInferrer<std::string>::operator()() {
    const Type t = type_of<halide_fake_string_type_t *>();
    return SingleArg{"", ArgKind::Constant, {t}, 0};
}

template<>
inline SingleArg SingleArgInferrer<Halide::Func>::operator()() {
    return SingleArg{"", ArgKind::Function, {}, -1};
}

template<>
inline SingleArg SingleArgInferrer<Halide::ImageParam>::operator()() {
    return SingleArg{"", ArgKind::ImageParam, {}, -1};
}

template<>
inline SingleArg SingleArgInferrer<Halide::Pipeline>::operator()() {
    return SingleArg{"", ArgKind::Pipeline, {}, -1};
}

template<>
inline SingleArg SingleArgInferrer<Halide::Expr>::operator()() {
    return SingleArg{"", ArgKind::Expression, {}, 0};
}

template<>
inline SingleArg SingleArgInferrer<Halide::Tuple>::operator()() {
    return SingleArg{"", ArgKind::Tuple, {}, 0};
}

template<>
inline SingleArg SingleArgInferrer<Halide::Target>::operator()() {
    const Type t = type_of<halide_fake_target_type_t *>();
    return SingleArg{"target", ArgKind::Constant, {t}, 0};
}

// ---------------------------------------

struct FnInvoker {
    virtual Pipeline invoke(const std::map<std::string, std::string> &constants) = 0;

    virtual std::vector<Parameter> get_parameters_for_input(const std::string &name) = 0;

    FnInvoker() = default;
    virtual ~FnInvoker() = default;

    // Not movable, not copyable
    FnInvoker &operator=(const FnInvoker &) = delete;
    FnInvoker(const FnInvoker &) = delete;
    FnInvoker &operator=(FnInvoker &&) = delete;
    FnInvoker(FnInvoker &&) = delete;
};

// ---------------------------------------

struct CapturedArg {
    std::string name;
    ArgKind kind = ArgKind::Unknown;
    std::vector<Parameter> params;  // Can have > 1 for Tuple-valued inputs
    Func func;
    Expr expr;
    Tuple tuple{Expr()};  // Tuple has no default ctor
    std::string str;

    using StrMap = std::map<std::string, std::string>;

    template<typename T>
    T value(const StrMap &m) const;

    void propagate_estimates() {
        if (kind != ArgKind::Function) {
            return;
        }

        // We may have to copy estimates from input Func to its corresponding Parameter(s).
        const std::vector<Bound> &estimates = func.function().schedule().estimates();
        if (estimates.empty()) {
            return;
        }

        const int d = func.dimensions();
        const std::vector<Var> fargs = func.args();

        const auto dim_of = [&fargs](const std::string &name) -> int {
            for (size_t i = 0; i < fargs.size(); ++i) {
                if (fargs[i].name() == name) {
                    return i;
                }
            }
            return -1;
        };

        internal_assert((int)estimates.size() == d);
        internal_assert((int)fargs.size() == d);

        for (const Bound &b : estimates) {
            const int dim = dim_of(b.var);
            internal_assert(dim >= 0);
            // Could be multiple Params if the Func has Tuple values.
            for (Parameter &p : params) {
                p.set_min_constraint_estimate(dim, b.min);
                p.set_extent_constraint_estimate(dim, b.extent);
            }
        }
    }

private:
    std::string get_string(const StrMap &m) const {
        auto it = m.find(name);
        if (it != m.end()) {
            return it->second;
        } else {
            return str;
        }
    }
};

template<>
inline Expr CapturedArg::value<Expr>(const StrMap &m) const {
    internal_assert(kind == ArgKind::Expression);
    return expr;
}

template<>
inline Tuple CapturedArg::value<Tuple>(const StrMap &m) const {
    internal_assert(kind == ArgKind::Tuple);
    return tuple;
}

template<>
inline Func CapturedArg::value<Func>(const StrMap &m) const {
    internal_assert(kind == ArgKind::Function);
    return func;
}

template<>
inline ImageParam CapturedArg::value<ImageParam>(const StrMap &m) const {
    internal_assert(kind == ArgKind::ImageParam);
    internal_assert(params.size() == 1 && params[0].defined());
    internal_assert(func.defined());
    return Halide::ImageParam(params[0], func);
}

template<>
inline std::string CapturedArg::value<std::string>(const StrMap &m) const {
    internal_assert(kind == ArgKind::Constant);
    const std::string s = get_string(m);
    return s;
}

template<>
inline Type CapturedArg::value<Type>(const StrMap &m) const {
    internal_assert(kind == ArgKind::Constant);
    const std::string s = get_string(m);
    const auto &types = get_halide_type_enum_map();
    const auto it = types.find(s);
    user_assert(it != types.end()) << "The string " << s << " cannot be parsed as a Halide type.";
    return it->second;
}

template<>
inline bool CapturedArg::value<bool>(const StrMap &m) const {
    internal_assert(kind == ArgKind::Constant);
    const std::string s = get_string(m);
    bool b = false;
    if (s == "true") {
        b = true;
    } else if (s == "false") {
        b = false;
    } else {
        user_assert(false) << "Unable to parse bool: " << s;
    }
    return b;
}

template<>
inline Target CapturedArg::value<Target>(const StrMap &m) const {
    internal_assert(kind == ArgKind::Constant);
    const std::string s = get_string(m);
    return Halide::Target(s);
}

template<typename T>
inline T CapturedArg::value(const StrMap &m) const {
    const std::string s = get_string(m);
    static_assert(std::is_arithmetic<T>::value && !std::is_same<T, bool>::value, "Expected only arithmetic types here.");
    std::istringstream iss(s);
    T t;
    // All one-byte ints int8 and uint8 should be parsed as integers, not chars.
    if (sizeof(T) == sizeof(char)) {
        int i;
        iss >> i;
        t = (T)i;
    } else {
        iss >> t;
    }
    user_assert(!iss.fail() && iss.get() == EOF) << "Unable to parse " << type_of<T>() << ": " << s;
    return t;
}

// ---------------------------------------
template<typename ReturnType, typename... Args>
struct CapturedFn : public FnInvoker {
    std::function<ReturnType(Args...)> fn;
    std::array<CapturedArg, sizeof...(Args)> args;

    CapturedFn() = default;

    Pipeline invoke(const std::map<std::string, std::string> &constants) override {
        Pipeline result = invoke_impl(constants, std::make_index_sequence<sizeof...(Args)>());
        for (auto &a : args) {
            a.propagate_estimates();
        }
        return result;
    }

    std::vector<Parameter> get_parameters_for_input(const std::string &name) override {
        for (const auto &a : args) {
            if (a.name == name) {
                return a.params;
            }
        }
        user_assert(false) << "Unknown input: " << name;
        return {};
    }

private:
    template<size_t... Is>
    Pipeline invoke_impl(const std::map<std::string, std::string> &constants, std::index_sequence<Is...>) {
        using T = std::tuple<Args...>;
        return fn(std::get<Is>(args).template value<typename std::decay<decltype(std::get<Is>(std::declval<T>()))>::type>(constants)...);
    }

    // Not movable, not copyable
    CapturedFn &operator=(const CapturedFn &) = delete;
    CapturedFn(const CapturedFn &) = delete;
    CapturedFn &operator=(CapturedFn &&) = delete;
    CapturedFn(CapturedFn &&) = delete;
};

// ---------------------------------------

class FnBinder {
public:
    FnBinder() = delete;

    // Movable but not copyable
    FnBinder &operator=(const FnBinder &) = delete;
    FnBinder(const FnBinder &) = delete;
    FnBinder &operator=(FnBinder &&) = default;
    FnBinder(FnBinder &&) = default;

private:
    struct TypeAndString {
        const Type type;
        const std::string str;
    };

    template<typename T>
    inline static TypeAndString get_type_and_string(T value) {
        static_assert(std::is_arithmetic<T>::value && !std::is_same<T, bool>::value, "Expected only arithmetic types here.");
        return {type_of<T>(), std::to_string(value)};
    }

    template<>
    inline /*static*/ TypeAndString get_type_and_string(Type value) {
        std::ostringstream oss;
        oss << value;
        return {type_of<halide_fake_type_type_t *>(), oss.str()};
    }

    template<>
    inline /*static*/ TypeAndString get_type_and_string(std::string value) {
        return {type_of<halide_fake_string_type_t *>(), value};
    }

    template<>
    inline /*static*/ TypeAndString get_type_and_string(const char *value) {
        return get_type_and_string<std::string>(std::string(value));
    }

    template<>
    inline /*static*/ TypeAndString get_type_and_string(bool value) {
        return {type_of<bool>(), value ? "true" : "false"};
    }

public:
    struct Target : public SingleArg {
        // "illegal-target-string" will get replaced by the target string before invocation.
        Target()
            : SingleArg("target", ArgKind::Constant, {type_of<halide_fake_target_type_t *>()}, 0, "illegal-target-string") {
        }
    };

    struct Constant : public SingleArg {
        template<typename T>
        Constant(const std::string &n, const T &value)
            : Constant(n, get_type_and_string(value)) {
        }

    private:
        Constant(const std::string &n, const TypeAndString &t_and_s)
            : SingleArg(n, ArgKind::Constant, {t_and_s.type}, 0, t_and_s.str) {
        }
    };

    struct Input : public SingleArg {
        explicit Input(const std::string &n, const std::vector<Type> &t, int d)
            : SingleArg(n, ArgKind::Unknown, t, d) {
        }
        explicit Input(const std::string &n, const std::vector<Type> &t)
            : Input(n, t, -1) {
        }
        explicit Input(const std::string &n, const Type &t)
            : Input(n, std::vector<Type>{t}, -1) {
        }
        explicit Input(const std::string &n, const Type &t, int d)
            : Input(n, std::vector<Type>{t}, d) {
        }
    };

    struct Output : public SingleArg {
        explicit Output(const std::string &n, const std::vector<Type> &t, int d)
            : SingleArg(n, ArgKind::Unknown, t, d, "", true) {
        }

        explicit Output(const std::string &n, const std::vector<Type> &t)
            : Output(n, t, -1) {
        }
        explicit Output(const std::string &n, const Type &t)
            : Output(n, std::vector<Type>{t}, -1) {
        }
        explicit Output(const std::string &n, const Type &t, int d)
            : Output(n, std::vector<Type>{t}, d) {
        }
    };

    // Construct an FnBinder from an ordinary function
    template<typename ReturnType, typename... Args>
    FnBinder(ReturnType (*fn)(Args...), const char *registry_name, const std::vector<SingleArg> &inputs_and_outputs)
        : registry_name_(registry_name) {
        initialize(fn, fn, inputs_and_outputs);
    }

    // Construct an FnBinder from a lambda or std::function (possibly with internal state)
    template<typename Fn,  // typename... Inputs,
             typename std::enable_if<is_lambda<Fn>::value>::type * = nullptr>
    FnBinder(Fn &&fn, const char *registry_name, const std::vector<SingleArg> &inputs_and_outputs)
        : registry_name_(registry_name) {
        initialize(std::forward<Fn>(fn), (typename function_signature<Fn>::type *)nullptr, inputs_and_outputs);
    }

    const std::vector<Constant> &constants() const {
        return constants_;
    }
    const std::vector<AbstractGenerator::ArgInfo> &inputs() const {
        return inputs_;
    }
    const std::vector<AbstractGenerator::ArgInfo> &outputs() const {
        return outputs_;
    }

    FnInvoker &invoker() const {
        return *invoker_.get();
    }

protected:
    const char *registry_name_;
    std::vector<Constant> constants_;
    std::vector<AbstractGenerator::ArgInfo> inputs_;
    std::vector<AbstractGenerator::ArgInfo> outputs_;
    std::unique_ptr<FnInvoker> invoker_;

    static IOKind to_iokind(ArgKind k) {
        switch (k) {
        default:
            internal_error << "Unhandled ArgKind: " << k;
        case ArgKind::Expression:
        case ArgKind::Tuple:
            return IOKind::Scalar;
        case ArgKind::Function:
        case ArgKind::Pipeline:
            return IOKind::Function;
        case ArgKind::ImageParam:
            return IOKind::Buffer;
        }
    }

    static AbstractGenerator::ArgInfo to_arginfo(const SingleArg &a) {
        return AbstractGenerator::ArgInfo{
            a.name,
            to_iokind(a.kind),
            a.types,
            a.dimensions,
        };
    }

    static Func make_param_func(const Parameter &p, const std::string &name) {
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

    template<typename Fn, typename ReturnType, typename... Args>
    void initialize(Fn &&fn, ReturnType (*)(Args...), const std::vector<SingleArg> &inputs_and_outputs) {
        const std::array<SingleArg, sizeof...(Args)> inferred_input_arg_types = {SingleArgInferrer<typename std::decay<Args>::type>()()...};
        // TODO: it's not clear if this is useful or not; I think it's redundant and the error can be misleading
        // user_assert(inferred_input_arg_types.size() < inputs_and_outputs.size())
        //     << "There must be at least one Output() specification for HALIDE_REGISTER_G2(" << registry_name_ << ").";

        {
            std::set<std::string> names;
            for (const auto &it : inputs_and_outputs) {
                user_assert(names.count(it.name) == 0) << "The name '" << it.name << "' is used more than once"
                                                       << " for HALIDE_REGISTER_G2(" << registry_name_ << ").";
                names.insert(it.name);
            }
        }

        using CapFn = CapturedFn<ReturnType, Args...>;
        auto captured = std::make_unique<CapFn>();
        captured->fn = std::move(fn);

        internal_assert(inputs_and_outputs.size() > 0);
        size_t first_output = inputs_and_outputs.size() - 1;
        user_assert(inputs_and_outputs[first_output].is_output)
            << "Expected an Output as the final argument, but saw " << inputs_and_outputs[first_output].kind
            << " '" << inputs_and_outputs[first_output].name << "'"
            << " for HALIDE_REGISTER_G2(" << registry_name_ << ").";
        while (first_output > 0 && inputs_and_outputs[first_output - 1].is_output) {
            first_output--;
        }

        user_assert(sizeof...(Args) == first_output) << "The number of Input and Constant annotations does not match the number of function arguments"
                                                     << " for HALIDE_REGISTER_G2(" << registry_name_ << ").";
        for (size_t i = 0; i < first_output; ++i) {
            user_assert(!inputs_and_outputs[i].is_output) << "Outputs must be listed after all Inputs and Constants, but saw '"
                                                          << inputs_and_outputs[i].name << "' out of place"
                                                          << " for HALIDE_REGISTER_G2(" << registry_name_ << ").";
            const bool is_constant = (inferred_input_arg_types[i].kind == ArgKind::Constant);
            const bool skip_default_value = !is_constant;
            const SingleArg matched = SingleArg::match(inputs_and_outputs[i], inferred_input_arg_types[i], skip_default_value);

            CapturedArg &carg = captured->args[i];
            carg.name = matched.name;
            const auto k = inferred_input_arg_types[i].kind;
            user_assert(k != ArgKind::Pipeline)
                << "Pipeline is only allowed for Outputs, not Inputs"
                << " for HALIDE_REGISTER_G2(" << registry_name_ << ").";
            internal_assert(carg.kind == ArgKind::Unknown);
            carg.kind = k;
            if (k == ArgKind::Constant) {
                constants_.emplace_back(matched.name, matched.default_value);
                constants_.back().types = matched.types;

                carg.str = matched.default_value;
            } else {
                inputs_.push_back(to_arginfo(matched));

                const bool is_buffer = (k == ArgKind::Function || k == ArgKind::ImageParam);
                std::vector<Func> funcs;
                std::vector<Expr> exprs;
                for (size_t idx = 0; idx < matched.types.size(); idx++) {
                    std::string param_name = carg.name;
                    if (matched.types.size() > 1)
                        param_name += "_" + std::to_string(idx);
                    const Type t = matched.types[idx];
                    carg.params.emplace_back(t, is_buffer, matched.dimensions, param_name);
                    Parameter &p = carg.params.back();
                    if (is_buffer) {
                        funcs.push_back(make_param_func(p, param_name));
                    } else {
                        exprs.push_back(Internal::Variable::make(t, param_name, p));
                    }
                }
                if (funcs.size() > 1) {
                    std::vector<Expr> wrap;
                    for (const auto &f : funcs) {
                        wrap.push_back(f(Halide::_));
                    }
                    carg.func = Func(carg.name);
                    carg.func(Halide::_) = Tuple(wrap);
                } else if (funcs.size() == 1) {
                    carg.func = funcs[0];
                }

                if (exprs.size() > 1) {
                    internal_assert(k == ArgKind::Tuple);
                    carg.tuple = Tuple(exprs);
                } else if (exprs.size() == 1) {
                    if (k == ArgKind::Tuple) {
                        // Tuple of size 1
                        carg.tuple = Tuple(exprs);
                    } else {
                        carg.expr = exprs[0];
                    }
                }
            }
        }

        invoker_ = std::move(captured);

        SingleArg inferred_ret_type = SingleArgInferrer<typename std::decay<ReturnType>::type>()();
        inferred_ret_type.is_output = true;
        user_assert(inferred_ret_type.kind == ArgKind::Function || inferred_ret_type.kind == ArgKind::Pipeline)
            << "Outputs must be Func or Pipeline, but the type seen was " << inferred_ret_type.types
            << " for HALIDE_REGISTER_G2(" << registry_name_ << ").";
        for (size_t i = first_output; i < inputs_and_outputs.size(); ++i) {
            user_assert(inputs_and_outputs[i].is_output) << "All Inputs and Constants must come before any Outputs"
                                                         << " for HALIDE_REGISTER_G2(" << registry_name_ << ").";
            outputs_.push_back(to_arginfo(SingleArg::match(inputs_and_outputs[i], inferred_ret_type)));
        }
    }
};

class G2Generator : public AbstractGenerator {
    const TargetInfo target_info_;
    const std::string name_;
    FnBinder binder_;
    std::map<std::string, std::string> generatorparams_;

    Pipeline pipeline_;

    static std::map<std::string, std::string> init_generatorparams(const TargetInfo &target_info,
                                                                   const std::vector<FnBinder::Constant> &constants) {
        std::map<std::string, std::string> result;
        for (const auto &c : constants) {
            result[c.name] = c.default_value;
        }
        // Always set this last, to override the placeholder value we may have stuffed there
        result["target"] = target_info.target.to_string();
        return result;
    }

public:
    explicit G2Generator(const GeneratorContext &context, const std::string &name, FnBinder binder)
        : target_info_{context.get_target(), context.get_auto_schedule(), context.get_machine_params()},
          name_(name),
          binder_(std::move(binder)),
          generatorparams_(init_generatorparams(target_info_, binder_.constants())) {
    }

    std::string get_name() override {
        return name_;
    }

    TargetInfo get_target_info() override {
        return target_info_;
    }

    std::vector<AbstractGenerator::ArgInfo> get_input_arginfos() override {
        return binder_.inputs();
    }

    std::vector<AbstractGenerator::ArgInfo> get_output_arginfos() override {
        return binder_.outputs();
    }

    std::vector<std::string> get_generatorparam_names() override {
        std::vector<std::string> v;
        for (const auto &c : generatorparams_) {
            v.push_back(c.first);
        }
        return v;
    }

    void set_generatorparam_value(const std::string &name, const std::string &value) override {
        internal_assert(!pipeline_.defined())
            << "set_generatorparam_value() must be called before build_pipeline().";
        internal_assert(generatorparams_.count(name) == 1) << "Unknown Constant: " << name;
        generatorparams_[name] = value;
    }

    void set_generatorparam_value(const std::string &name, const LoopLevel &value) override {
        internal_assert(!pipeline_.defined())
            << "set_generatorparam_value() must be called before build_pipeline().";
        internal_assert(generatorparams_.count(name) == 1) << "Unknown Constant: " << name;
        internal_assert(false) << "This Generator has no LoopLevel constants.";
    }

    void bind_input(const std::string &name, const std::vector<Parameter> &v) override {
        internal_assert(!pipeline_.defined())
            << "bind_input() must be called before build_pipeline().";
        internal_error << "Unimplemented: " << __func__;
    }

    void bind_input(const std::string &name, const std::vector<Func> &v) override {
        internal_assert(!pipeline_.defined())
            << "bind_input() must be called before build_pipeline().";
        internal_error << "Unimplemented: " << __func__;
    }

    void bind_input(const std::string &name, const std::vector<Expr> &v) override {
        internal_assert(!pipeline_.defined())
            << "bind_input() must be called before build_pipeline().";
        internal_error << "Unimplemented: " << __func__;
    }

    Pipeline build_pipeline() override {
        internal_assert(!pipeline_.defined())
            << "build_pipeline() may not be called twice.";

        pipeline_ = binder_.invoker().invoke(generatorparams_);

        internal_assert(binder_.outputs().size() == pipeline_.outputs().size())
            << "Expected exactly " << binder_.outputs().size() << " output(s) but the function returned a Pipeline containing "
            << pipeline_.outputs().size() << ".";

        internal_assert(pipeline_.defined())
            << "build_pipeline() did not build a Pipeline!";
        return pipeline_;
    }

    std::vector<Parameter> get_parameters_for_input(const std::string &name) override {
        internal_assert(pipeline_.defined())
            << "get_parameters_for_input() must be called after build_pipeline().";
        return binder_.invoker().get_parameters_for_input(name);
    }

    std::vector<Func> get_funcs_for_output(const std::string &name) override {
        internal_assert(pipeline_.defined())
            << "get_funcs_for_output() must be called after build_pipeline().";
        auto outputs = pipeline_.outputs();

        internal_assert(binder_.outputs().size() == outputs.size());
        for (size_t i = 0; i < binder_.outputs().size(); i++) {
            if (binder_.outputs()[i].name == name) {
                return {outputs[i]};
            }
        }
        internal_error << "Unknown output: " << name;
        return {};
    }

    ExternsMap get_external_code_map() override {
        internal_assert(pipeline_.defined())
            << "get_external_code_map() must be called after build_pipeline().";
        // TODO: not supported now; how necessary and/or doable is this?
        return {};
    }

    bool emit_cpp_stub(const std::string & /*stub_file_path*/) override {
        // not supported
        return false;
    }
};

}  // namespace Internal
}  // namespace Halide

#define HALIDE_REGISTER_G2(GEN_FUNC, GEN_REGISTRY_NAME, ...)                                                   \
    namespace halide_register_generator {                                                                      \
    struct halide_global_ns;                                                                                   \
    namespace GEN_REGISTRY_NAME##_ns {                                                                         \
        Halide::Internal::AbstractGeneratorPtr factory(const Halide::GeneratorContext &context) {              \
            using Input [[maybe_unused]] = Halide::Internal::FnBinder::Input;                                  \
            using Output [[maybe_unused]] = Halide::Internal::FnBinder::Output;                                \
            using Constant [[maybe_unused]] = Halide::Internal::FnBinder::Constant;                            \
            using Target [[maybe_unused]] = Halide::Internal::FnBinder::Target;                                \
            using Halide::Bool;                                                                                \
            using Halide::Float;                                                                               \
            using Halide::Int;                                                                                 \
            using Halide::UInt;                                                                                \
            using Halide::Handle;                                                                              \
            Halide::Internal::FnBinder d(GEN_FUNC, #GEN_REGISTRY_NAME, {__VA_ARGS__});                         \
            return std::make_unique<Halide::Internal::G2Generator>(context, #GEN_REGISTRY_NAME, std::move(d)); \
        }                                                                                                      \
    }                                                                                                          \
    static auto reg_##GEN_REGISTRY_NAME =                                                                      \
        Halide::Internal::RegisterGenerator(#GEN_REGISTRY_NAME, GEN_REGISTRY_NAME##_ns::factory);              \
    }                                                                                                          \
    static_assert(std::is_same<::halide_register_generator::halide_global_ns,                                  \
                               halide_register_generator::halide_global_ns>::value,                            \
                  "HALIDE_REGISTER_G2 must be used at global scope");

#endif  // HALIDE_G2_H_
