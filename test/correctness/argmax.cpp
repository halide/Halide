#include "Halide.h"
#include <array>
#include <iostream>

using namespace Halide;

/*
inputs:
    ImageParam
    Param

    Buffer
    Scalar
    Arithmetic
    LoopLevel?
    Enum?
    String?

    Func  -- no, require explicit wrapping?

outputs:
    OutputImageParam

    Pipeline  -- no, require explicit wrapping?
    Func   -- no, require explicit wrapping?
*/

#if __cplusplus >= 201402L
//#error nope
#endif

enum class ArgKind {
    Unknown,
    FrobParam,
    Expr,
    Func,
    Pipeline,
    Buffer,
};

std::ostream &operator<<(std::ostream &stream, ArgKind kind) {
    static const char *const kinds[] = {
        "Unknown",
        "FrobParam",
        "Expr",
        "Func",
        "Pipeline",
        "Buffer"
    };
    stream << kinds[(int)kind];
    return stream;
}

struct ArgType {
    ArgType() = default;
    ArgType(const Type &t, ArgKind k)
        : type(t), kind(k) {
    }

    Type type;
    ArgKind kind{ArgKind::Unknown};
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
        return ArgType(type_of<typename T2::ElemType>(), ArgKind::Buffer);  // TODO: can't infer dims()
    }

    template<typename T2>
    ArgType helper(T2, std::false_type) {
        return ArgType(type_of<T2>(), ArgKind::Unknown);  // TODO: can't tell FrobParam vs Scalar, must put in arg()
    }
};

template<>
ArgType ArgTypeInferrer<Expr>::operator()() {
    return ArgType(Type(), ArgKind::Expr);  // TODO: can't tell type, must put in arg()
}

template<>
ArgType ArgTypeInferrer<Func>::operator()() {
    return ArgType(Type(), ArgKind::Func);  // TODO: can't tell type, must put in arg()
}

template<>
ArgType ArgTypeInferrer<Pipeline>::operator()() {
    return ArgType(Type(), ArgKind::Pipeline);  // TODO: can't tell type, must put in arg()
}

struct FrobArg {
protected:
    FrobArg(const std::string &name, Type t, int dim) : name(name), type(t), dim(dim), kind(ArgKind::Unknown) {}

public:
    std::string name;
    Type type;
    int dim;  // 0 = zero-dim buffer; -1 = scalar
    ArgKind kind;

    void inspect() const {
        if (dim < 0) {
            std::cout << "  out: " << name << " is " << type << " (kind = " << kind << ")";
        } else {
            std::cout << "  out: " << name << " is Buffer<" << type << "> dim=" << dim << " (kind = " << kind << ")";
        }
    }

    // Verify that the information we statically inferred from the C++ fn declaration
    // matches what is declared manually for the Frob, and that the resulting
    // FrobArg is complete and unambiguous.
    FrobArg with_arg_type(const ArgType &arg_type) const {
        // _halide_user_assert(type.kind == ArgKind::Func ||
        //                     type.kind == ArgKind::Pipeline ||
        //                     type.kind == ArgKind::Buffer)
        //     << "Frob outputs must be Func or Buffer";
        FrobArg r = *this;
        r.kind = arg_type.kind;
        return r;
    }
};

struct FrobInput : public FrobArg {
protected:
    FrobInput(const std::string &name, Type t, int dim) : FrobArg(name, t, dim) {}
};

template<typename T = void>
struct FrobInputBuffer : public FrobInput {
    explicit FrobInputBuffer(const std::string &name, int dim, Type t = type_of<T>())
        : FrobInput(name, t, dim) {
    }
};

template<typename T = void>
struct FrobInputScalar : public FrobInput {
    explicit FrobInputScalar(const std::string &name, Type t = type_of<T>())
        : FrobInput(name, t, -1) {
    }
};

struct FrobOutput : public FrobArg {
protected:
    FrobOutput(const std::string &name, Type t, int dim) : FrobArg(name, t, dim) {}
};

template<typename T = void>
struct FrobOutputBuffer : public FrobOutput {
    explicit FrobOutputBuffer(const std::string &name, int dim, Type t = type_of<T>())
        : FrobOutput(name, t, dim) {
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

class Frob {
public:
    Frob() = delete;
    Frob(const Frob &) = delete;
    Frob(Frob &&) = delete;
    void operator=(const Frob &) = delete;
    void operator=(Frob &&) = delete;

    // Construct a Frob from an ordinary function
    template<typename FnReturnType, typename... FnArgTypes, typename... FrobArgTypes>
    Frob(FnReturnType (*f)(FnArgTypes...), const std::string &name, FrobArgTypes... frob_args)
        : name_(name) {
        std::cout << "Frob-ctor1 " << name << "\n";
        initialize(f, f, frob_args...);
    }

    // Construct a Frob from a lambda function (possibly with internal state)
    template<typename Func, typename... FrobInputs, typename... FrobArgTypes,
             typename std::enable_if<is_lambda<Func>::value>::type * = nullptr>
    Frob(Func &&f, const std::string &name, FrobArgTypes... frob_args)
        : name_(name) {
        std::cout << "Frob-ctor2 " << name << "\n";
        initialize(std::forward<Func>(f), (typename function_signature<Func>::type *)nullptr, frob_args...);
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
            assert(a.kind != ArgKind::Unknown);
        }
        for (const auto &a : outputs_) {
            assert(a.kind != ArgKind::Unknown);
        }
    }

protected:
    // std::function<>  TODO
    std::string name_;
    std::vector<FrobArg> inputs_;
    std::vector<FrobArg> outputs_;
    // std::vector<Arg> params_;

    void add_frob_arg(const FrobInput &input, const ArgType &type) {
        _halide_user_assert(outputs_.empty()) << "All inputs must come before any outputs";
        inputs_.push_back(input.with_arg_type(type)); // {input.name, type.type, type.kind});
    }

    void add_frob_arg(FrobOutput output, const ArgType &type) {
        outputs_.push_back(output.with_arg_type(type));
    }

    // void add_frob_arg(const FrobParam &param, const ArgType &type) {
    //     params_.push_back({param.name, type.type, type.kind});
    // }

    // TODO: C++17 could use std::apply() here instead
    template<typename... FrobArgTypes, size_t... I>
    void add_frob_args(
            const std::array<ArgType, sizeof...(FrobArgTypes)> fn_arg_types,
            const std::tuple<FrobArgTypes...> &frob_args,
            Halide::Internal::index_sequence<I...>
        ) {
        (void) std::initializer_list<int>{((void)add_frob_arg(std::get<I>(frob_args), fn_arg_types[I]), 0)... };
    }

    template<typename Func, typename FnReturnType, typename... FnArgTypes, typename... FrobArgTypes>
    void initialize(Func &&f, FnReturnType (*)(FnArgTypes...), FrobArgTypes... frob_args) {
        static_assert(sizeof...(FrobArgTypes) >= sizeof...(FnArgTypes) + 1, "Insufficient FrobArgTypes passed");

        const std::array<ArgType, sizeof...(FrobArgTypes)> fn_arg_types = {
            ArgTypeInferrer<typename std::decay<FnArgTypes>::type>()()...,
            ArgTypeInferrer<typename std::decay<FnReturnType>::type>()(),
            // remainder are default-initialized to ??? for FrobParam TODO
        };
        add_frob_args(fn_arg_types,
            std::forward_as_tuple(frob_args...),
            Halide::Internal::make_index_sequence<sizeof...(FrobArgTypes)>());
    }
};

Pipeline Flip(const Func &input, Expr e, uint8_t value) {
    Func f;
    Var x, y;
    f(x, y) = input(x, y) ^ (e + value);
    return Pipeline(f);
}

int main(int argc, char **argv) {
    Frob flipper(
        Flip,
        "flip",
        FrobInputBuffer<uint8_t>("input", 2),
        FrobInputScalar<uint8_t>("e"),
        FrobInputScalar<>("value", UInt(8)),
        // TODO: validate that outputbuffers matches arity of pipeline?
        FrobOutputBuffer<>("output", 2, UInt(16))
    );
    flipper.inspect();

    return 0;
}
