#include "Halide.h"
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

enum class ArgKind {
    Unknown,
    FrobParam,
    InputScalar,
    InputFunc,
    InputBuffer,
    OutputBuffer
};

std::ostream &operator<<(std::ostream &stream, ArgKind kind) {
    static const char * const kinds[] = { "Unknown", "FrobParam", "InputScalar", "InputFunc", "InputBuffer", "OutputBuffer"};
    stream << kinds[(int)kind];
    return stream;
}

struct ArgType {
    ArgType() = default;
    ArgType(const Type &t, ArgKind k) : type(t), kind(k) {}

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
        return ArgType(type_of<typename T2::ElemType>(), ArgKind::InputBuffer);  // TODO: can't infer dims()
    }

    template<typename T2>
    ArgType helper(T2, std::false_type) {
        return ArgType(type_of<T2>(), ArgKind::Unknown);  // TODO: can't tell FrobParam vs InputScalar, must put in arg()
    }
};

template<>
ArgType ArgTypeInferrer<Func>::operator()() {
    return ArgType(Type(), ArgKind::InputFunc);  // TODO: can't tell type, must put in arg()
}

struct FrobInput {
    explicit FrobInput(const std::string &name)
        : name(name) {};

    std::string name;
};

struct FrobOutput {
    explicit FrobOutput(const std::string &name)
        : name(name) {};

    std::string name;
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
    template<typename ReturnType, typename... ArgTypes, typename... FrobInputs, typename... FrobOutputs>
    Frob(const std::string &name, ReturnType (*f)(ArgTypes...), FrobInputs ...inputs, FrobOutputs ...outputs)
        : name_(name) {
        std::cout << "Frob-ctor1 " << name << "\n";
        initialize(f, f, inputs..., outputs...);
    }

    // Construct a Frob from a lambda function (possibly with internal state)
    // template<typename Func, typename... FrobInputs,
    //          typename std::enable_if<is_lambda<Func>::value>::type * = nullptr>
    // Frob(const std::string &name, Func &&f, const FrobOutput &ret, const FrobInputs &...args)
    //     : name_(name) {
    //     std::cout << "Frob-ctor2 " << name << "\n";
    //     initialize(std::forward<Func>(f), (typename function_signature<Func>::type *)nullptr, ret, args...);
    // }

    struct Arg {
        std::string name;
        Type type;
        ArgKind kind;
    };

    const std::string &name() const {
        return name_;
    }
    const std::vector<Arg> &inputs() const {
        return inputs_;
    }

    const std::vector<Arg> &outputs() const {
        return outputs_;
    }

    void inspect() const {
        std::cout << "fn: " << name() << "\n";
        for (const Arg &a : inputs()) {
            std::cout << "  in: " << a.name << " is " << a.type << " " << a.kind << "\n";
            // assert(a.kind != ArgKind::Unknown);
        }
        for (const Arg &a : outputs()) {
            std::cout << "  out: " << a.name << " is " << a.type << " " << a.kind << "\n";
            // assert(a.kind != ArgKind::Unknown);
        }
    }

protected:
    std::string name_;
    std::vector<Arg> inputs_;
    std::vector<Arg> outputs_;

    void add_input(const FrobInput &name, const ArgType &type) {
        inputs_.push_back({name.name, type.type, type.kind});
    }

    void add_output(const FrobOutput &name, const ArgType &type) {
        outputs_.push_back({name.name, type.type, type.kind});
    }

    template<typename Func, typename ReturnType, typename... ArgTypes, typename... FrobInputs, typename... FrobOutputs>
    void initialize(Func &&f, ReturnType (*)(ArgTypes...), FrobInputs ...inputs, FrobOutputs ...outputs) {
        static_assert(sizeof...(ArgTypes) == sizeof...(FrobInputs),
                      "The number of argument annotations does not match the number of function arguments");

        // This is awkward, but effective for C++11.
        int unused1[] = {0, (add_input(inputs, ArgTypeInferrer<typename std::decay<ArgTypes>::type>()()), 0)...};
        (void)unused1;
        int unused2[] = {0, (add_output(outputs, ArgTypeInferrer<typename std::decay<ReturnType>::type>()()), 0)...};
        (void)unused2;

        // const ArgType ret_type = ArgTypeInferrer<typename std::decay<ReturnType>::type>()();
        // ret_ = Arg{ret.name, ret_type.type, ret_type.kind};
    }
};

Func Flip(const Func &input, uint8_t value) {
    Func f;
    Var x, y;
    f(x, y) = input(x, y) ^ value;
    return f;
}


int main(int argc, char **argv) {
    Frob flipper(
        "flip",
        Flip,
        FrobInput("input"), FrobInput("value"),
        FrobOutput("output")
    );
    flipper.inspect();

    return 0;
}
