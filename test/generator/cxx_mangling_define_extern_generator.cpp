#include "Halide.h"

class CPlusPlusNameManglingDefineExternGenerator :
    public Halide::Generator<CPlusPlusNameManglingDefineExternGenerator> {
public:
    // Use all the parameter types to make sure mangling works for each of them.
    ImageParam input{UInt(8), 1, "input"};
    Param<int32_t *> int_ptr{"int_ptr", 0};
    Param<int32_t const *> const_int_ptr{"const_int_ptr", 0};
    Param<void *> void_ptr{"void_ptr", 0};
    Param<void const *> const_void_ptr{"const_void_ptr", 0};
    Param<std::string *> string_ptr{"string_ptr", 0};
    Param<std::string const *> const_string_ptr{"const_string_ptr", 0};

    Pipeline build() {
        assert(get_target().has_feature(Target::CPlusPlusMangling));
        Var x("x");

        Func g("g");
        g(x) = input(x) + 42;

        Func f1("f1"), f2("f2"), f3("f3");

        std::vector<ExternFuncArgument> args;
        args.push_back(Halide::user_context_value());
        args.push_back(g);
        args.push_back(cast<int8_t>(1));
        args.push_back(cast<uint8_t>(2));
        args.push_back(cast<int16_t>(3));
        args.push_back(cast<uint16_t>(4));
        args.push_back(cast<int32_t>(5));
        args.push_back(cast<uint32_t>(6));
        args.push_back(cast<int64_t>(7));
        args.push_back(cast<uint64_t>(8));
        args.push_back(cast<bool>(9 == 9));
        args.push_back(cast<float>(10.0f));
        args.push_back(Expr(11.0));
        args.push_back(int_ptr);
        args.push_back(const_int_ptr);
        args.push_back(void_ptr);
        args.push_back(const_void_ptr);
        args.push_back(string_ptr);
        args.push_back(const_string_ptr);
        f1.define_extern("HalideTest::cxx_mangling_1",
                         args, Float(64), 1, NameMangling::Default);
        f2.define_extern("HalideTest::cxx_mangling_2",
                         args, Float(64), 1, NameMangling::CPlusPlus);
        f3.define_extern("cxx_mangling_3",
                         args, Float(64), 1, NameMangling::C);

        g.compute_root();

        return Pipeline({f1, f2, f3});
    }
};

Halide::RegisterGenerator<CPlusPlusNameManglingDefineExternGenerator>
    register_my_gen{"cxx_mangling_define_extern"};
