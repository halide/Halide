#include "Halide.h"

#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace Halide::Internal;

namespace Halide {
namespace {

// Note to reader: this test is meant as a simple way to verify that arbitrary
// implementations of AbstractGenerator work properly. That said, we recommend
// that you don't imitate this code; AbstractGenerator is an *internal*
// abtraction, intended for Halide to build on internally. If you use AbstractGenerator
// directly, you'll almost certainly have more work maintaining your code
// on your own.

const char *const AbstractGeneratorTestName = "abstractgeneratortest";

// We could use std::stoi() here, but we explicitly want to assert-fail
// if we can't parse the string as a valid int.
int string_to_int(const std::string &s) {
    std::istringstream iss(s);
    int i;
    iss >> i;
    _halide_user_assert(!iss.fail() && iss.get() == EOF) << "Unable to parse: " << s;
    return i;
}

class AbstractGeneratorTest : public AbstractGenerator {
    // Boilerplate
    const TargetInfo target_info_;

    // Constants (aka GeneratorParams)
    std::map<std::string, std::string> constants_ = {
        {"scaling", "2"},
    };

    // Inputs
    ImageParam input_{Int(32), 2, "input"};
    Param<int32_t> offset_{"offset"};

    // Outputs
    Func output_{"output"};

    // Misc
    Pipeline pipeline_;

public:
    explicit AbstractGeneratorTest(const GeneratorContext &context)
        : target_info_{context.get_target(),
                       context.get_auto_schedule(),
                       context.get_machine_params()} {
    }

    std::string get_name() override {
        return AbstractGeneratorTestName;
    }

    TargetInfo get_target_info() override {
        return target_info_;
    }

    std::vector<ArgInfo> get_input_arginfos() override {
        return {
            {"input", IOKind::Buffer, {Int(32)}, 2},
            {"offset", IOKind::Scalar, {Int(32)}, 0},
        };
    }

    std::vector<ArgInfo> get_output_arginfos() override {
        return {
            {"output", IOKind::Buffer, {Int(32)}, 2},
        };
    }

    std::vector<std::string> get_generatorparam_names() override {
        std::vector<std::string> v;
        for (const auto &c : constants_) {
            v.push_back(c.first);
        }
        return v;
    }

    void set_generatorparam_value(const std::string &name, const std::string &value) override {
        _halide_user_assert(!pipeline_.defined());
        _halide_user_assert(constants_.count(name) == 1) << "Unknown Constant: " << name;
        constants_[name] = value;
    }

    void set_generatorparam_value(const std::string &name, const LoopLevel &value) override {
        _halide_user_assert(!pipeline_.defined());
        _halide_user_assert(constants_.count(name) == 1) << "Unknown Constant: " << name;
        _halide_user_assert(false) << "This Generator has no LoopLevel constants.";
    }

    Pipeline build_pipeline() override {
        _halide_user_assert(!pipeline_.defined());

        const int scaling = string_to_int(constants_.at("scaling"));

        Var x, y;
        output_(x, y) = input_(x, y) * scaling + offset_;
        output_.compute_root();

        pipeline_ = output_;
        return pipeline_;
    }

    std::vector<Parameter> get_parameters_for_input(const std::string &name) override {
        _halide_user_assert(pipeline_.defined());
        if (name == "input") {
            return {input_.parameter()};
        }
        if (name == "offset") {
            return {offset_.parameter()};
        }
        _halide_user_assert(false) << "Unknown input: " << name;
        return {};
    }

    std::vector<Func> get_funcs_for_output(const std::string &name) override {
        _halide_user_assert(pipeline_.defined());
        if (name == "output") {
            return {output_};
        }
        _halide_user_assert(false) << "Unknown output: " << name;
        return {};
    }

    ExternsMap get_external_code_map() override {
        // none
        return {};
    }

    void bind_input(const std::string &name, const std::vector<Parameter> &v) override {
        _halide_user_assert(false) << "OOPS";
    }

    // void bind_input(const std::string &name, const std::vector<Buffer<void>> &v) override {
    //     _halide_user_assert(false) << "OOPS";
    // }

    void bind_input(const std::string &name, const std::vector<Func> &v) override {
        _halide_user_assert(false) << "OOPS";
    }

    void bind_input(const std::string &name, const std::vector<Expr> &v) override {
        _halide_user_assert(false) << "OOPS";
    }

    bool emit_cpp_stub(const std::string & /*stub_file_path*/) override {
        // not supported
        return false;
    }
};

RegisterGenerator register_something(AbstractGeneratorTestName,
                                     [](const GeneratorContext &context) -> AbstractGeneratorPtr {
                                         return std::unique_ptr<AbstractGeneratorTest>(new AbstractGeneratorTest(context));
                                     });

}  // namespace
}  // namespace Halide
