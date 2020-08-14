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
    G2Param,
    InputScalar,
    InputFunc_DONOTUSE,
    InputBuffer,
    OutputBuffer
};

std::ostream &operator<<(std::ostream &stream, ArgKind kind) {
    static const char * const kinds[] = { "Unknown", "G2Param", "InputScalar", "InputFunc", "InputBuffer", "OutputBuffer"};
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
        return ArgType(type_of<T2>(), ArgKind::Unknown);  // TODO: can't G2Param vs InputScalar, must put in argname()
    }
};

struct argname {
    explicit argname(const std::string &name)
        : name(name){};

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

class G2 {
public:
    // Construct a G2 from an ordinary function
    template<typename ReturnType, typename... ArgTypes, typename... ArgNames>
    G2(const std::string &name, ReturnType (*f)(ArgTypes...), const ArgNames &...arg_names)
        : name_(name) {
        std::cout << "G2-ctor1 " << name << "\n";
        initialize(f, f, arg_names...);
    }

    // Construct a G2 from a lambda function (possibly with internal state)
    template<typename Func, typename... ArgNames,
             typename std::enable_if<is_lambda<Func>::value>::type * = nullptr>
    G2(const std::string &name, Func &&f, const ArgNames &...arg_names)
        : name_(name) {
        std::cout << "G2-ctor2 " << name << "\n";
        initialize(std::forward<Func>(f), (typename function_signature<Func>::type *)nullptr, arg_names...);
    }

    struct Arg {
        std::string name;
        Type type;
        ArgKind kind;
    };

    const std::string &name() const {
        return name_;
    }
    const std::vector<Arg> &args() const {
        return args_;
    }

    void inspect() const {
        std::cout << "fn: " << name() << "\n";
        for (auto a : args()) {
            std::cout << "  " << a.name << " is " << a.type << " " << a.kind << "\n";
        }
    }

protected:
    std::string name_;
    std::vector<Arg> args_;

    void add_arg(const argname &name, const ArgType &type) {
        args_.push_back({name.name, type.type, type.kind});
    }

    template<typename... ArgTypes, typename... ArgNames>
    void zip_args(ArgNames &&...arg_names) {
        static_assert(sizeof...(ArgTypes) == sizeof...(ArgNames),
                      "The number of argument annotations does not match the number of function arguments");
        // This is awkward, but effective for C++11.
        int unused[] = {0, (add_arg(arg_names, ArgTypeInferrer<typename std::remove_cv<typename std::remove_reference<ArgTypes>::type>::type>()()), 0)...};
        (void)unused;
    }

    template<typename Func, typename ReturnType, typename... ArgTypes, typename... ArgNames>
    void initialize(Func &&f, ReturnType (*)(ArgTypes...), const ArgNames &...arg_names) {
        static_assert(sizeof...(ArgTypes) == sizeof...(ArgNames),
                      "The number of argument annotations does not match the number of function arguments");
        zip_args<ArgTypes...>(arg_names...);
    }
};

Func Flip(Func input, uint8_t value) {
    Func f;
    Var x, y;
    f(x, y) = input(x, y) ^ value;
    return f;
}


int main(int argc, char **argv) {
    G2 gzero(
        "gzero",
        [](int value) -> Func {
            Func f;
            Var x, y;
            f(x, y) = 0;
            return f;
        },
        argname("value"));
    gzero.inspect();

    // G2 gbuf(
    //     "gbuf",
    //     [](const Buffer<uint8_t> &input) -> Func {
    //         Func f;
    //         Var x, y;
    //         f(x, y) = input(x, y);
    //         return f;
    //     },
    //     argname("input"));
    // gbuf.inspect();

    // G2 gfunc(
    //     "gfunc",
    //     [](Func &infunc) -> Func {
    //         Func f;
    //         Var x, y;
    //         f(x, y) = infunc(x, y);
    //         return f;
    //     },
    //     argname("infunc"));
    // inspect(gfunc);

    // G2 g("unpack_raw",
    //      UnpackRaw,
    //      argname("layout"), argname("input"));
    // g.inspect();

    return 0;
}
