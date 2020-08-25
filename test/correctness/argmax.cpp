#include "Halide.h"
#include <array>
#include <iostream>

/*
    Goal:

    - given a C++ fn that returns Func/Pipeline/Buffer, encapsulate
      it in a way that allows for easy AOT compilation via GenGen ("Realizer")

    - C++ fn can have inputs:
        - Func -> ImageParam
        - Pipeline -> multiple ImageParams
        - Buffer -> ImageParam or statically-compiled Buffer
        - Expr -> Param<>
        - scalar C++ type -> Param<scalar> or GeneratorParam<scalar>
        - Target -> GeneratorParam<Target>
        - LoopLevel -> GeneratorParam<LoopLevel>
        - std::string -> GeneratorParam<string>

    - Keep boilerplate terse and minimal, but allow redundancy to avoid creeping errors
        - TBD: require recapitulating type/dim/etc in the Realizer decl, to avoid
          drift if Func sig changes? Yes probably

    - Avoid introducing new concepts (eg use ImageParam, Param<>, etc where sensible)

    - Stretch goal: is there a way to structure it to allow code to be jitted *or* aot'ed
      simply by changing build settings?

    - Open questions:
        - Should ordering of the C++ fn arguments matter, or allow freeform matching?


    - Option 1:
        - require explicit user-written code (lambda, etc) to impedance match
            from Func (etc) to Buffer (etc)
        - easier to implement
        - but lots of boilerplate that could be handled more easily in many cases

    - Option 2:
        - Add ways to automatically generate the boilerplate, keeping it terse and obvious



    Given something like:

        Pipeline Flip(const Func &input, Expr e, uint8_t value);

    syntax:

        // Standalone?

        Realizer flipper(
            Flip,
            "flip",
            RealizerInputBuffer<uint8_t>("input", 2),
            RealizerInputScalar<>("e"),
            RealizerInputScalar<>("value",
            RealizerOutputBuffer<>("output", 2, UInt(16)));

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

enum class CppArgKind {
    Unknown,
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
    ArgType(const Type &t, CppArgKind k)
        : type(t), kind(k) {
    }

    Type type;
    CppArgKind kind{CppArgKind::Unknown};
};

template<typename T>
struct is_halide_buffer : std::false_type {};

template<typename T>
struct is_halide_buffer<Buffer<T>> : std::true_type {};

template<typename T>
struct ArgTypeInferrer {
    ArgType operator()() {
        return helper(T(), typename is_halide_buffer<T>::type());
    }

private:
    template<typename T2>
    ArgType helper(T2, std::true_type) {
        static_assert(!std::is_void<typename T2::ElemType>::value, "Cannot use ArgTypeInferrer on Buffer<void>; please specify a concrete type");
        return ArgType(type_of<typename T2::ElemType>(), CppArgKind::Buffer);  // TODO: can't infer dims()
    }

    template<typename T2>
    ArgType helper(T2, std::false_type) {
        return ArgType(type_of<T2>(), CppArgKind::CppScalar);  // TODO: can't tell RealizerParam vs Expr, must put in arg()
    }
};

template<>
ArgType ArgTypeInferrer<Expr>::operator()() {
    return ArgType(Type(), CppArgKind::Expr);  // TODO: can't tell type, must put in arg()
}

template<>
ArgType ArgTypeInferrer<Func>::operator()() {
    return ArgType(Type(), CppArgKind::Func);  // TODO: can't tell type, must put in arg()
}

template<>
ArgType ArgTypeInferrer<Pipeline>::operator()() {
    return ArgType(Type(), CppArgKind::Pipeline);  // TODO: can't tell type, must put in arg()
}

enum class RealizerArgKind {
    Unknown,
    RealizerParam,
    InputScalar,
    Buffer,
};

std::ostream &operator<<(std::ostream &stream, RealizerArgKind kind) {
    static const char *const kinds[] = {
        "Unknown",
        "RealizerParam",
        "InputScalar",
        "Buffer"};
    stream << kinds[(int)kind];
    return stream;
}

struct RealizerArg {
protected:
    RealizerArg(const std::string &name, Type t, int dim)
        : name(name), type(t), dim(dim), kind(RealizerArgKind::Unknown) {
    }

public:
    std::string name;
    Type type;
    int dim;  // 0 = zero-dim buffer; -1 = scalar
    RealizerArgKind kind;

    void inspect() const {
        if (dim < 0) {
            std::cout << "  out: " << name << " is " << type << " (kind = " << kind << ")";
        } else {
            std::cout << "  out: " << name << " is Buffer<" << type << "> dim=" << dim << " (kind = " << kind << ")";
        }
    }

    // Verify that the information we statically inferred from the C++ fn declaration
    // matches what is declared manually for the Realizer, and that the resulting
    // RealizerArg is complete and unambiguous.
    RealizerArg with_arg_type(const ArgType &arg_type) const {
        RealizerArg r = *this;

        switch (arg_type.kind) {
        case CppArgKind::Unknown:
            _halide_user_assert(0);
            break;
        case CppArgKind::CppScalar:
            _halide_user_assert(arg_type.type == type) << name << ";" << type << ";" << arg_type.type;
            _halide_user_assert(dim == -1);
            r.kind = RealizerArgKind::InputScalar;  // TODO  Scalar can be RealizerParam
            break;
        case CppArgKind::Expr:
            _halide_user_assert(type.bits() != 0 && arg_type.type.bits() == 0) << name << ";" << type << ";" << arg_type.type;
            _halide_user_assert(dim == -1);
            r.kind = RealizerArgKind::InputScalar;
            break;
        case CppArgKind::Func:
            _halide_user_assert(type.bits() != 0 && arg_type.type.bits() == 0) << name << ";" << type << ";" << arg_type.type;
            _halide_user_assert(dim >= 0);
            r.kind = RealizerArgKind::Buffer;
            break;
        case CppArgKind::Pipeline:
            _halide_user_assert(type.bits() != 0 && arg_type.type.bits() == 0) << name << ";" << type << ";" << arg_type.type;
            _halide_user_assert(dim >= 0);
            r.kind = RealizerArgKind::Buffer;
            break;
        case CppArgKind::Buffer:
            _halide_user_assert(type.bits() != 0 && arg_type.type.bits() == 0) << name << ";" << type << ";" << arg_type.type;
            _halide_user_assert(dim >= 0);
            r.kind = RealizerArgKind::Buffer;
            break;
        }
        // Make sure our type is valid
        _halide_user_assert(type.bits() != 0);
        return r;
    }
};

struct RealizerInput : public RealizerArg {
protected:
    RealizerInput(const std::string &name, Type t, int dim)
        : RealizerArg(name, t, dim) {
    }
};

template<typename T = void>
struct RealizerInputBuffer : public RealizerInput {
    explicit RealizerInputBuffer(const std::string &name, int dim, Type t = type_of<T>())
        : RealizerInput(name, t, dim) {
    }
};

template<typename T = void>
struct RealizerInputScalar : public RealizerInput {
    explicit RealizerInputScalar(const std::string &name, Type t = type_of<T>())
        : RealizerInput(name, t, -1) {
    }
};

struct RealizerOutput : public RealizerArg {
protected:
    RealizerOutput(const std::string &name, Type t, int dim)
        : RealizerArg(name, t, dim) {
    }
};

template<typename T = void>
struct RealizerOutputBuffer : public RealizerOutput {
    explicit RealizerOutputBuffer(const std::string &name, int dim, Type t = type_of<T>())
        : RealizerOutput(name, t, dim) {
    }
};

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
    typename std::conditional<
        std::is_pointer<F>::value || std::is_member_pointer<F>::value,
        typename std::remove_pointer<F>::type,
        typename strip_function_object<F>::type>::type>;

template<typename T, typename T0 = typename std::remove_reference<T>::type>
using is_lambda = std::integral_constant<bool, !std::is_function<T0>::value && !std::is_pointer<T0>::value && !std::is_member_pointer<T0>::value>;

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
    Realizer(FnReturnType (*f)(FnArgTypes...), const std::string &name, RealizerArgTypes... realizer_args)
        : name_(name) {
        std::cout << "Realizer-ctor1 " << name << "\n";
        initialize(f, f, realizer_args...);
    }

    // Construct a Realizer from a lambda function (possibly with internal state)
    template<typename Func, typename... RealizerInputs, typename... RealizerArgTypes,
             typename std::enable_if<is_lambda<Func>::value>::type * = nullptr>
    Realizer(Func &&f, const std::string &name, RealizerArgTypes... realizer_args)
        : name_(name) {
        std::cout << "Realizer-ctor2 " << name << "\n";
        initialize(std::forward<Func>(f), (typename function_signature<Func>::type *)nullptr, realizer_args...);
    }

    const std::string &name() const {
        return name_;
    }
    void inspect() const {
        std::cout << "fn: " << name() << "\n";
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
    // std::function<>  TODO
    std::string name_;
    std::vector<RealizerArg> inputs_;
    std::vector<RealizerArg> outputs_;
    // std::vector<Arg> params_;

    void add_realizer_arg(const RealizerInput &input, const ArgType &type) {
        _halide_user_assert(outputs_.empty()) << "All inputs must come before any outputs";
        inputs_.push_back(input.with_arg_type(type));  // {input.name, type.type, type.kind});
    }

    void add_realizer_arg(RealizerOutput output, const ArgType &type) {
        outputs_.push_back(output.with_arg_type(type));
    }

    // void add_realizer_arg(const RealizerParam &param, const ArgType &type) {
    //     params_.push_back({param.name, type.type, type.kind});
    // }

    // TODO: C++17 could use std::apply() here instead
    template<typename... RealizerArgTypes, size_t... I>
    void add_realizer_args(
        const std::array<ArgType, sizeof...(RealizerArgTypes)> fn_arg_types,
        const std::tuple<RealizerArgTypes...> &realizer_args,
        Halide::Internal::index_sequence<I...>) {
        (void)std::initializer_list<int>{((void)add_realizer_arg(std::get<I>(realizer_args), fn_arg_types[I]), 0)...};
    }

    template<typename Func, typename FnReturnType, typename... FnArgTypes, typename... RealizerArgTypes>
    void initialize(Func &&f, FnReturnType (*)(FnArgTypes...), RealizerArgTypes... realizer_args) {
        static_assert(sizeof...(RealizerArgTypes) >= sizeof...(FnArgTypes) + 1, "Insufficient RealizerArgTypes passed");

        const std::array<ArgType, sizeof...(RealizerArgTypes)> fn_arg_types = {
            ArgTypeInferrer<typename std::decay<FnArgTypes>::type>()()...,
            ArgTypeInferrer<typename std::decay<FnReturnType>::type>()(),
            // remainder are default-initialized to ??? for RealizerParam TODO
        };
        add_realizer_args(fn_arg_types,
                      std::forward_as_tuple(realizer_args...),
                      Halide::Internal::make_index_sequence<sizeof...(RealizerArgTypes)>());
    }
};

Pipeline Flip(const Func &input, Expr e, uint8_t value) {
    Func f;
    Var x, y;
    f(x, y) = input(x, y) ^ (e + value);
    return Pipeline(f);
}

int main(int argc, char **argv) {
    Realizer flipper(
        Flip,
        "flip",
        RealizerInputBuffer<uint8_t>("input", 2),
        RealizerInputScalar<uint8_t>("e"),
        RealizerInputScalar<uint8_t>("value"),
        // TODO: validate that outputbuffers matches arity of pipeline?
        RealizerOutputBuffer<>("output", 2, UInt(16)));
    flipper.inspect();

    return 0;
}
