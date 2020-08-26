#include "Halide.h"
#include <array>
#include <iostream>

#if __cplusplus < 201703L
#error "Sorry, requires C++17"
#endif

/*
    Goal:

    - given a C++ fn that returns Func/Pipeline (but NOT Buffer), encapsulate
      it in a way that allows for easy AOT compilation via GenGen ("Realizer")

    - C++ fn can have inputs:
        - Func -> ImageParam
        - Pipeline -> multiple ImageParams
        - Buffer -> ImageParam or statically-compiled Buffer
        - Expr -> Param<>
        - scalar C++ type -> Param<scalar> or GeneratorParam<scalar>
        - Target -> GeneratorParam<Target>
            - LoopLevel -> GeneratorParam<LoopLevel>   NO DO NOT USE
        - std::string -> GeneratorParam<string>

    - Keep boilerplate terse and minimal, but allow redundancy to avoid creeping errors

    - Avoid introducing new concepts (eg use ImageParam, Param<>, etc where sensible)

    - Stretch goal: is there a way to structure it to allow code to be jitted *or* aot'ed
      simply by changing build settings?

    - So the real goal here is to annotate a C++ fn in such a way that we can:
        - AOT-generate it like a Generator
            - including scheduling estimates for all inputs
            - including constraints for input/output args (eg strides, etc)
            - What about autoscheduling? TBD


    TODO:
        - auto-scheduling
        - scheduling estimates
        - constraints for input/output (eg strides)?
        - Tuple-valued inputs or outputs
        - RunGen compatibility
        - check for unique names
        - handle Pipeline inputs and outputs property for size > 1
        - refactor build_gradient_module() to operate on non-Generators

    TODON'T:
        - array/vector inputs/outputs




        HALIDE_GLOBAL_REALIZER(flipper) {
            return Realizer(
                Flip,
                "flip",
                RealizerInputBuffer<uint8_t>("input", 2),
                RealizerInputScalar<>("e"),
                RealizerInputScalar<>("value",
                RealizerOutputBuffer<>("output", 2, UInt(16)));
        };

        HALIDE_GET_REALIZER(flipper).some_method();
*/

using namespace Halide;
using namespace Halide::Internal;

enum class CppArgKind {
    Unknown,
    CppString,
    CppScalar,
    Expr,
    Func,
    Pipeline,
    Buffer,
};

std::ostream &operator<<(std::ostream &stream, CppArgKind kind) {
    static const char *const kinds[] = {
        "Unknown",
        "CppScalar",
        "Expr",
        "Func",
        "Pipeline",
        "Buffer"};
    stream << kinds[(int)kind];
    return stream;
}

struct ArgType {
    ArgType() = default;
    ArgType(const Type &t, CppArgKind k, bool in)
        : type(t), kind(k), is_input(in) {
    }

    Type type;
    CppArgKind kind{CppArgKind::Unknown};
    bool is_input;
};

template<typename T>
struct is_halide_buffer : std::false_type {};

template<typename T>
struct is_halide_buffer<Buffer<T>> : std::true_type {};

template<typename T>
struct ArgTypeInferrer {
    ArgType operator()(bool is_input) {
        return helper(is_input, T(), typename is_halide_buffer<T>::type());
    }

private:
    template<typename T2>
    ArgType helper(bool is_input, T2, std::true_type) {
        static_assert(!std::is_void<typename T2::ElemType>::value, "Cannot use ArgTypeInferrer on Buffer<void>; please specify a concrete type");
        return ArgType(type_of<typename T2::ElemType>(), CppArgKind::Buffer, is_input);
    }

    template<typename T2>
    ArgType helper(bool is_input, T2, std::false_type) {
        return ArgType(type_of<T2>(), CppArgKind::CppScalar, is_input);
    }
};

template<>
ArgType ArgTypeInferrer<Expr>::operator()(bool is_input) {
    return ArgType(Type(), CppArgKind::Expr, is_input);
}

template<>
ArgType ArgTypeInferrer<Func>::operator()(bool is_input) {
    return ArgType(Type(), CppArgKind::Func, is_input);
}

template<>
ArgType ArgTypeInferrer<Pipeline>::operator()(bool is_input) {
    return ArgType(Type(), CppArgKind::Pipeline, is_input);
}

template<>
ArgType ArgTypeInferrer<std::string>::operator()(bool is_input) {
    return ArgType(Type(), CppArgKind::CppString, is_input);
}

enum class RealizerArgKind {
    Unknown,
    RealizerParam,
    InputScalar,
    InputBuffer,
    OutputBuffer,
};

std::ostream &operator<<(std::ostream &stream, RealizerArgKind kind) {
    static const char *const kinds[] = {
        "Unknown",
        "RealizerParam",
        "InputScalar",
        "InputBuffer",
        "OutputBuffer",
    };
    stream << kinds[(int)kind];
    return stream;
}

struct RealizerArg {
protected:
    RealizerArg(const std::string &n, Type t, int d, RealizerArgKind k, const std::string &dv = "")
        : p(t, (k == RealizerArgKind::InputBuffer || k == RealizerArgKind::OutputBuffer), d, n),
          default_value(dv), kind(k) {
    }

public:
    Parameter p;
    std::string default_value;
    RealizerArgKind kind;

    Type type() const {
        return p.type();
    }

    int dimensions() const {
        return p.dimensions();
    }

    const std::string &name() const {
        return p.name();
    }

    bool is_buffer() const {
        return p.is_buffer();
    }

    void inspect() const {
        if (!is_buffer()) {
            std::cout << "  out: " << name() << " is " << type() << " (kind = " << kind << ")";
        } else {
            std::cout << "  out: " << name() << " is Buffer<" << type() << "> dim=" << dimensions() << " (kind = " << kind << ")";
        }
        if (kind == RealizerArgKind::RealizerParam) {
            std::cout << " default_value: " << default_value;
        }
    }

    // Verify that the information we statically inferred from the C++ fn declaration
    // matches what is declared manually for the Realizer, and that the resulting
    // RealizerArg is complete and unambiguous.
    const RealizerArg &check_arg_type(const ArgType &arg_type, bool is_input = true) const {
        _halide_user_assert(is_input == arg_type.is_input);
        switch (arg_type.kind) {
        case CppArgKind::Unknown:
            _halide_user_assert(0);
            break;
        case CppArgKind::CppString:
            _halide_user_assert(is_input);
            _halide_user_assert(kind == RealizerArgKind::RealizerParam) << "String inputs must use RealizerParam " << kind << " " << name();
            _halide_user_assert(!is_buffer());
            break;
        case CppArgKind::CppScalar:
            _halide_user_assert(is_input);
            _halide_user_assert(kind == RealizerArgKind::RealizerParam || kind == RealizerArgKind::InputScalar) << "Scalar inputs must use RealizerParam or RealizerInputScalar";
            if (kind == RealizerArgKind::InputScalar) {
                _halide_user_assert(arg_type.type == type()) << name() << ";" << type() << ";" << arg_type.type;
            }
            _halide_user_assert(!is_buffer());
            break;
        case CppArgKind::Expr:
            _halide_user_assert(is_input);
            _halide_user_assert(kind == RealizerArgKind::InputScalar) << "Expr inputs must use InputScalar";
            _halide_user_assert(type().bits() != 0 && arg_type.type.bits() == 0) << name() << ";" << type() << ";" << arg_type.type;
            _halide_user_assert(!is_buffer());
            break;
        case CppArgKind::Func:
        case CppArgKind::Pipeline:
        case CppArgKind::Buffer:
            if (is_input) {
                _halide_user_assert(kind == RealizerArgKind::InputBuffer) << "The input " << name() << " must use RealizerInputBuffer";
            } else {
                _halide_user_assert(arg_type.kind != CppArgKind::Buffer) << "A Realizer cannot use a C++ function that returns Buffer<> (only Func or Pipeline are allowed)";
                _halide_user_assert(kind == RealizerArgKind::OutputBuffer) << "The output " << name() << " must use RealizerOutputBuffer";
            }
            _halide_user_assert(type().bits() != 0 && arg_type.type.bits() == 0) << name() << ";" << type() << ";" << arg_type.type;
            _halide_user_assert(is_buffer());
            break;
        }
        return *this;
    }
};

struct RealizerInput : public RealizerArg {
protected:
    RealizerInput(const std::string &n, Type t, int d, RealizerArgKind k)
        : RealizerArg(n, t, d, k) {
    }
};

template<typename T = void>
struct RealizerInputBuffer : public RealizerInput {
    explicit RealizerInputBuffer(const std::string &n, int d)
        : RealizerInput(n, type_of<T>(), d, RealizerArgKind::InputBuffer) {
    }
    explicit RealizerInputBuffer(const std::string &n, Type t, int d)
        : RealizerInput(n, t, d, RealizerArgKind::InputBuffer) {
        static_assert(std::is_void<T>(), "Must use <void> when passing an explicit Type");
    }
};

template<typename T = void>
struct RealizerInputScalar : public RealizerInput {
    explicit RealizerInputScalar(const std::string &n)
        : RealizerInput(n, type_of<T>(), 0, RealizerArgKind::InputScalar) {
    }
    explicit RealizerInputScalar(const std::string &n, Type t)
        : RealizerInput(n, t, 0, RealizerArgKind::InputScalar) {
        static_assert(std::is_void<T>(), "Must use <void> when passing an explicit Type");
    }
};

struct RealizerOutput : public RealizerArg {
protected:
    RealizerOutput(const std::string &n, Type t, int d, RealizerArgKind k)
        : RealizerArg(n, t, d, k) {
    }
};

template<typename T = void>
struct RealizerOutputBuffer : public RealizerOutput {
    explicit RealizerOutputBuffer(const std::string &n, int d)
        : RealizerOutput(n, type_of<T>(), d, RealizerArgKind::OutputBuffer) {
    }
    explicit RealizerOutputBuffer(const std::string &n, Type t, int d)
        : RealizerOutput(n, t, d, RealizerArgKind::OutputBuffer) {
        static_assert(std::is_void<T>(), "Must use <void> when passing an explicit Type");
    }
};

struct RealizerParam : public RealizerArg {
    template<typename T>
    explicit RealizerParam(const std::string &n, T dv)
        : RealizerArg(n, Type(), 0, RealizerArgKind::RealizerParam, std::to_string(dv)) {
    }
};

template<>
RealizerParam::RealizerParam(const std::string &n, const char *dv)
    : RealizerArg(n, Type(), 0, RealizerArgKind::RealizerParam, dv) {
}

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
using function_signature =
    std::conditional<
        std::is_function<F>::value,
        F,
        typename std::conditional<
            std::is_pointer<F>::value || std::is_member_pointer<F>::value,
            typename std::remove_pointer<F>::type,
            typename strip_function_object<F>::type>::type>;

template<typename T, typename T0 = typename std::remove_reference<T>::type>
using is_lambda = std::integral_constant<bool, !std::is_function<T0>::value && !std::is_pointer<T0>::value && !std::is_member_pointer<T0>::value>;

template<typename Fn>
class Realizer final {
public:
    // Movable but not copyable
    Realizer() = delete;
    Realizer(const Realizer &) = delete;
    Realizer &operator=(const Realizer &) = delete;
    Realizer(Realizer &&) = default;
    Realizer &operator=(Realizer &&) = default;

    // Construct a Realizer from an ordinary function
    template<typename FnReturnType, typename... FnArgTypes, typename... RealizerArgTypes>
    Realizer(FnReturnType (&fn)(FnArgTypes...), const std::string &name, RealizerArgTypes... realizer_args)
        : fn_(fn), name_(name) {
        std::cout << "Realizer-ctor1 " << name << "\n";
        initialize(fn, realizer_args...);
    }

    // Construct a Realizer from a lambda function (possibly with internal state)
    template<typename F = Fn, typename... RealizerArgTypes,
             typename std::enable_if<is_lambda<F>::value>::type * = nullptr>
    Realizer(F &&fn, const std::string &name, RealizerArgTypes... realizer_args)
        : fn_(std::move(fn)), name_(name) {
        std::cout << "Realizer-ctor2 " << name << "\n";
        using FnSignature = typename function_signature<F>::type;
        initialize((FnSignature *)nullptr, realizer_args...);
    }

    const std::string &name() const {
        return name_;
    }

    void inspect() const {
        std::cout << "fn: " << name() << "\n";
        for (const auto &a : params_) {
            std::cout << "  gp: ";
            a.inspect();
            std::cout << "\n";
        }
        for (const auto &a : inputs_) {
            std::cout << "  in: ";
            a.inspect();
            std::cout << "\n";
        }
        for (const auto &a : outputs_) {
            std::cout << "  out: ";
            a.inspect();
            std::cout << "\n";
        }
    }

    void validate() const {
        _halide_user_assert(!name_.empty());
        for (const auto &a : inputs_) {
            assert(a.kind != RealizerArgKind::Unknown);
        }
        for (const auto &a : outputs_) {
            assert(a.kind != RealizerArgKind::Unknown);
        }
    }

protected:
    Fn fn_;
    std::string name_;
    std::vector<RealizerArg> inputs_;
    std::vector<RealizerArg> outputs_;
    std::vector<RealizerArg> params_;

    void add_realizer_arg(const RealizerInput &input, const ArgType &type) {
        // std::cout << "ara input " << input.name << "\n";
        inputs_.push_back(input.check_arg_type(type));  // {input.name, type.type, type.kind});
    }

    void add_realizer_arg(const RealizerOutput &output, const ArgType &type) {
        // std::cout << "ara output " << output.name << "\n";
        outputs_.push_back(output.check_arg_type(type, false));
    }

    void add_realizer_arg(const RealizerParam &param, const ArgType &type) {
        // std::cout << "ara param " << param.name << "\n";
        params_.push_back(param.check_arg_type(type));
    }

    template<typename... RealizerArgTypes, size_t... I>
    void add_realizer_args(
        const std::array<ArgType, sizeof...(RealizerArgTypes)> fn_arg_types,
        const std::tuple<RealizerArgTypes...> &realizer_args,
        Halide::Internal::index_sequence<I...>) {
#if __cplusplus < 201703L
        (void)std::initializer_list<int>{((void)add_realizer_arg(std::get<I>(realizer_args), fn_arg_types[I]), 0)...};
#else
        (add_realizer_arg(std::get<I>(realizer_args), fn_arg_types[I]), ...);
#endif
    }

    template<typename FnReturnType, typename... FnArgTypes, typename... RealizerArgTypes>
    void initialize(FnReturnType (*)(FnArgTypes...), RealizerArgTypes... realizer_args) {
        static_assert(sizeof...(RealizerArgTypes) >= sizeof...(FnArgTypes) + 1, "Insufficient RealizerArgTypes passed");

        const std::array<ArgType, sizeof...(RealizerArgTypes)> fn_arg_types = {
            ArgTypeInferrer<typename std::decay<FnReturnType>::type>()(false),
            ArgTypeInferrer<typename std::decay<FnArgTypes>::type>()(true)...,
        };
        add_realizer_args(fn_arg_types,
                          std::forward_as_tuple(realizer_args...),
                          Halide::Internal::make_index_sequence<sizeof...(RealizerArgTypes)>());
    }

    // TODO:
    // - build a tuple of all the args (but not ret) and use std::apply
    // - tuple vals:
    //    - for GPs, coerce the string into the right type and pass it
    //         - maybe avoid the string to/from conversion via cleverness
    //    - for inputs, pass the Parameter with a Func or Var wrapped around it (see make_param_func)
    //    - get return value, coerce to Pipeline, and use output_buffer() to get the OutputImageParam
    // - Just use C++17 for now (add backports later as needed)
    //
    // THIS IS TANTAMOUNT TO GENERATOR::build_pipeline
    // - remotely useful to expose externally? dubious, simpler to just call the original Fn
    //
    // template<typename FnReturnType, typename... FnArgTypes>
    // FnReturnType call(FnArgTypes... fn_args) {
    //     // set_inputs_vector() checks this too, but checking it here allows build_inputs() to avoid out-of-range checks.
    //     GeneratorParamInfo &pi = this->param_info();
    //     user_assert(sizeof...(args) == pi.inputs().size())
    //         << "Expected exactly " << pi.inputs().size()
    //         << " inputs but got " << sizeof...(args) << "\n";
    //     set_inputs_vector(build_inputs(std::forward_as_tuple<const Args &...>(args...), make_index_sequence<sizeof...(Args)>{}));
    // }

#if 0
void set_generator_param_values(const std::map<std::string, std::string> &params) {
    const std::map<std::string, RealizerArg*> params_by_name;
    for (auto &it : params_) {
        params_by_name[it.name] = &it;
    }

    for (auto &it : params) {
        auto gp = params_by_name.find(it.first);
        _halide_user_assert(gp != params_by_name.end())
            << "Generator " << generator_registered_name << " has no GeneratorParam named: " << it.first << "\n";
        gp->second->set_from_string(it.second.string_value);
    }
}

Argument to_argument(const Internal::Parameter &param, const Expr &default_value) {
    ArgumentEstimates argument_estimates = param.get_argument_estimates();
    argument_estimates.scalar_def = default_value;
    return Argument(param.name(),
                    param.is_buffer() ? Argument::InputBuffer : Argument::InputScalar,
                    param.type(), param.dimensions(), argument_estimates);
}

Module build_module(const std::string &function_name) {
    // AutoSchedulerResults auto_schedule_results;
    // call_configure();
    Pipeline pipeline = build_pipeline();
    // if (get_auto_schedule()) {
    //     auto_schedule_results = pipeline.auto_schedule(get_target(), get_machine_params());
    // }

    std::vector<Argument> filter_arguments;
    for (const auto &input : inputs_) {
        filter_arguments.push_back(to_argument(p, p.is_buffer() ? Expr() : input->get_def_expr()));
    }

    Module result = pipeline.compile_to_module(filter_arguments, function_name, get_target());

    // for (const auto &output : outputs_) {
    //     auto from = output->funcs()[i].name();
    //     auto to = output->array_name(i);
    //     size_t tuple_size = output->types_defined() ? output->types().size() : 1;
    //     for (size_t t = 0; t < tuple_size; ++t) {
    //         std::string suffix = (tuple_size > 1) ? ("." + std::to_string(t)) : "";
    //         result.remap_metadata_name(from + suffix, to + suffix);
    //     }
    // }

    // result.set_auto_scheduler_results(auto_schedule_results);

    return result;
}
#endif
    // std::unique_ptr<GeneratorBase> make_gen(const Halide::GeneratorContext &context) {
    //     auto g = RealizerGenerator::create();
    //     return g;
    // }
};

template<typename Fn, typename... Args>
inline auto make_realizer(Fn &&fn, Args... args) -> Realizer<decltype(fn)> {
    return Realizer<decltype(fn)>(std::forward<Fn>(fn), std::forward<Args>(args)...);
}





Pipeline Flip(const Func &input, Expr e, uint8_t value, std::string some_param, int another_param) {
    Func f;
    Var x, y;
    f(x, y) = cast<uint16_t>(input(x, y) ^ (e + value + another_param));
    return Pipeline(f);
}

struct FlipClass {
    static auto flip1(const Func &input, Expr e, uint8_t value, int another_param, const std::string &some_param) -> Pipeline {
        return Flip(input, e, value, some_param, another_param);
    }

    auto flip2(const Func &input, Expr e, uint8_t value, int another_param, const std::string &some_param) -> Pipeline {
        return Flip(input, e, value, some_param, another_param);
    }
};


int main(int argc, char **argv) {

    auto flipper = make_realizer(
        Flip,
        "flip",
        RealizerOutputBuffer<uint16_t>("output", 2),
        RealizerInputBuffer<uint8_t>("input", 2),
        RealizerInputScalar<uint8_t>("e"),
        RealizerInputScalar<uint8_t>("value"),
        RealizerParam("some_param", "my def val"),
        RealizerParam("another_param", 1)
    );
    // TODO: validate that outputbuffers matches arity of pipeline?
    flipper.inspect();

    auto flipper_static = make_realizer(
        std::function<decltype(FlipClass::flip1)>(FlipClass::flip1),
        "flip_static",
        RealizerOutputBuffer<>("output_static", UInt(16), 2),
        RealizerInputBuffer<uint8_t>("input_static", 2),
        RealizerInputScalar<uint8_t>("e_static"),
        RealizerInputScalar<uint8_t>("value_static"),
        RealizerParam("some_param_static", "my def val"),
        RealizerParam("another_param_static", 2)
    );
    flipper_static.inspect();

    std::string some_param = "foo";
    auto flipper_lambda = make_realizer(
        [&](const Func &input, Expr e, uint8_t value, int another_param) -> Pipeline {
            return Flip(input, e, value, some_param, another_param);
        },
        "flip_lambda",
        RealizerOutputBuffer<>("output_lambda", UInt(16), 2),
        RealizerInputBuffer<uint8_t>("input_lambda", 2),
        RealizerInputScalar<uint8_t>("e_lambda"),
        RealizerInputScalar<uint8_t>("value_lambda"),
        RealizerParam("another_param", 4)
    );
    flipper_lambda.inspect();

    return 0;
}
