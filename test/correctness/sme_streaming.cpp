#include "Halide.h"
#include "parse_llvm_ir.h"
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <set>

using namespace Halide;
using namespace Halide::Internal;
using Attribute = Internal::LoweredFunc::Attribute;

constexpr bool DEBUG = false;
constexpr int WIDTH = 1000;
bool can_run_code = false;

using TaskCall = std::pair<Attribute, Attribute>;

inline std::ostream &operator<<(std::ostream &os, Attribute attr) {
    switch (attr) {
    case Attribute::NO_ATTRIBUTE:
        return os << "Default";
    case Attribute::SME_STREAMING_TASK:
        return os << "SME_STREAMING_TASK";
    case Attribute::SME_NONSTREAMING_TASK:
        return os << "SME_NONSTREAMING_TASK";
    default:
        return os << "Attribute::Unknown";
    }
}

Attribute get_streaming_attr(uint64_t attrs) {
    int num_streaming_attrs = 0;
    Attribute ret = Attribute::NO_ATTRIBUTE;
    for (const auto &attr : {Attribute::SME_STREAMING_TASK, Attribute::SME_NONSTREAMING_TASK}) {
        if (attr & attrs) {
            ret = attr;
            num_streaming_attrs++;
        }
    }
    assert(num_streaming_attrs <= 1);  // Should not have both
    return ret;
}

bool check_calling_tasks(const std::string &name,
                         Module &m,
                         const std::map<std::string, Attribute> &func_attr_map,
                         const std::multiset<TaskCall> &exp_calls) {

    std::multiset<TaskCall> act_calls{};
    for (auto &caller : m.functions()) {
        auto caller_attr = get_streaming_attr(caller.attributes);

        std::vector<std::string> callees;
        visit_with(caller.body, [&](auto *self, const Call *op) {
            callees.emplace_back(op->name);
            self->visit_base(op);
        });

        for (const auto &callee : callees) {
            if (const auto itr = func_attr_map.find(callee); itr != func_attr_map.end()) {
                auto callee_attr = itr->second;
                if (callee_attr == Attribute::SME_STREAMING_TASK ||
                    callee_attr == Attribute::SME_NONSTREAMING_TASK) {
                    act_calls.emplace(caller_attr, callee_attr);
                    if (DEBUG) {
                        std::cout << name << ": " << caller_attr << " -> " << callee_attr << "\n";
                    }
                }
            }
        }
    }

    if (act_calls != exp_calls) {
        std::cerr << name << " failed! Calling tasks does not match.\n";
        auto print_calls = [](const std::string &header, const std::multiset<TaskCall> &calls) {
            std::cout << header << ":\n";
            for (const auto &[caller, callee] : calls) {
                std::cout << "  " << caller << " -> " << callee << "\n";
            }
        };

        print_calls("expected", exp_calls);
        print_calls("actual", act_calls);
        return false;
    }

    return true;
}

bool check_llvm_attribute(const std::string &name,
                          Module &m,
                          const std::map<std::string, Attribute> &func_attr_map) {
    // Verify that streaming task must have LLVM function attribute "aarch64_pstate_sm_body"
    // and vice versa for non-streaming task
    auto llvm_file_name = name + ".ll";
    m.compile({{OutputFileType::llvm_assembly, llvm_file_name}});
    std::unordered_map<std::string, std::string> attributes = parse_llvm_ir_attributes_from_file(llvm_file_name);

    auto check_streaming_attribute = [&](const std::string f_name, bool expect_streaming) -> bool {
        if (auto it = attributes.find(f_name); it != attributes.end()) {
            bool has_streaming_attribute = (it->second.find("aarch64_pstate_sm_body") != std::string::npos);
            if (has_streaming_attribute != expect_streaming) {
                std::cerr << "Streaming attribute does not match in " << f_name << "\n";
                return false;
            }
            return true;
        } else {
            std::cerr << "Cannot find function in llvm-ir: " << f_name << "\n";
            return false;
        }
    };

    for (const auto &[func_name, func_attr] : func_attr_map) {
        if (func_attr == Attribute::SME_STREAMING_TASK ||
            func_attr == Attribute::SME_NONSTREAMING_TASK) {
            bool should_be_streaming = (func_attr == Attribute::SME_STREAMING_TASK);
            if (!check_streaming_attribute(func_name, should_be_streaming)) {
                return false;
            }
        }
    }

    return true;
}

Target target_without_sme2() {
    auto target = get_jit_target_from_environment();
    return target.without_feature(Target::SME2)
        .without_feature(Target::SME_SVL128)
        .without_feature(Target::SME_SVL256)
        .without_feature(Target::SME_SVL512)
        .without_feature(Target::SME_SVL1024)
        .without_feature(Target::SME_SVL2048);
}

std::optional<Buffer<float>> realize_ref(Func &f) {
    if (!can_run_code) {
        return std::nullopt;
    }

    return f.realize({WIDTH}, target_without_sme2());
}

bool check_correctness(const std::string &name, Func &f, const Buffer<float> &ref_output) {
    Buffer<float> with_streaming = f.realize({WIDTH}, get_jit_target_from_environment());
    constexpr float REL_TOLERANCE = 1e-6;
    for (int x = 0; x < WIDTH; x++) {
        float diff = std::fabs(with_streaming(x) - ref_output(x));
        float tolerance = REL_TOLERANCE * std::max(std::fabs(ref_output(x)), std::fabs(with_streaming(x)));
        if (diff > tolerance) {
            std::cerr << std::setprecision(std::numeric_limits<double>::max_digits10)
                      << "im(" << x << ") = " << with_streaming(x)
                      << " instead of " << ref_output(x)
                      << " in " << name << "\n";
            return false;
        }
    }
    return true;
}

bool check(Func &f, const std::string &name,
           const std::multiset<TaskCall> &exp_calls,
           const std::optional<Buffer<float>> &ref_output) {

    Target target("arm-64-linux-sme2-sme_svl512-no_asserts-no_runtime-no_bounds_query");
    Module m = f.compile_to_module(f.infer_arguments(), "", target);

    if (DEBUG) {
        f.print_loop_nest();
        m.compile({{OutputFileType::stmt, "/dev/stdout"}});
    }

    std::map<std::string, Attribute> func_attr_map;
    for (auto &func : m.functions()) {
        func_attr_map.emplace(func.name, get_streaming_attr(func.attributes));
    }

    if (!check_calling_tasks(name, m, func_attr_map, exp_calls)) {
        return false;
    };

    if (!check_llvm_attribute(name, m, func_attr_map)) {
        return false;
    };

    if (can_run_code) {
        assert(ref_output.has_value());
        if (!check_correctness(name, f, *ref_output)) {
            return false;
        }
    }

    return true;
}

Var x("x"), xo("xo"), xi("xi");

bool test_1_stage_non_streaming() {
    const std::string name("test_1_stage_non_streaming");
    Func f("f");

    f(x) = x * 0.1f;

    auto ref_output = realize_ref(f);

    f.compute_root();

    std::multiset<TaskCall> expected_calls{};
    return check(f, name, expected_calls, ref_output);
}

bool test_1_stage_streaming_outermost() {
    const std::string name("test_1_stage_streaming_outermost");
    Func f("f");

    f(x) = x * 0.1f;

    auto ref_output = realize_ref(f);

    f.compute_root().sme_streaming(true);

    std::multiset<TaskCall> expected_calls{
        {Attribute::NO_ATTRIBUTE, Attribute::SME_STREAMING_TASK}};
    return check(f, name, expected_calls, ref_output);
}

bool test_1_stage_streaming_inner() {
    const std::string name("test_1_stage_streaming_inner");
    Func f("f");

    f(x) = x * 0.1f;

    auto ref_output = realize_ref(f);

    f.compute_root()
        .split(x, xo, xi, 256)
        .sme_streaming(true, xi);

    std::multiset<TaskCall> expected_calls{
        {Attribute::NO_ATTRIBUTE, Attribute::SME_STREAMING_TASK}};
    return check(f, name, expected_calls, ref_output);
}

bool test_2_stages_both_streaming() {
    const std::string name("test_2_stages_both_streaming");
    Func f("f");
    Func g("g");

    f(x) = x * 0.1f;
    g(x) = f(x) * f(x);

    auto ref_output = realize_ref(g);

    g.compute_root().sme_streaming(true, x);
    f.compute_root().sme_streaming(true, x);

    std::multiset<TaskCall> expected_calls{
        {Attribute::NO_ATTRIBUTE, Attribute::SME_STREAMING_TASK},
        {Attribute::NO_ATTRIBUTE, Attribute::SME_STREAMING_TASK}};
    return check(g, name, expected_calls, ref_output);
}

bool test_2_stages_producer_streaming() {
    const std::string name("test_2_stages_producer_streaming");
    Func f("f");
    Func g("g");

    f(x) = x * 0.1f;
    g(x) = f(x) * f(x);

    auto ref_output = realize_ref(g);

    g.compute_root();
    f.compute_root().sme_streaming(true, x);

    std::multiset<TaskCall> expected_calls{
        {Attribute::NO_ATTRIBUTE, Attribute::SME_STREAMING_TASK}};
    return check(g, name, expected_calls, ref_output);
}

bool test_2_stages_consumer_streaming() {
    const std::string name("test_2_stages_consumer_streaming");
    Func f("f");
    Func g("g");

    f(x) = x * 0.1f;
    g(x) = f(x) * f(x);

    auto ref_output = realize_ref(g);

    g.compute_root().sme_streaming(true, x);
    f.compute_root();

    std::multiset<TaskCall> expected_calls{
        {Attribute::NO_ATTRIBUTE, Attribute::SME_STREAMING_TASK}};
    return check(g, name, expected_calls, ref_output);
}

bool test_2_stages_both_streaming_at() {
    const std::string name("test_2_stages_both_streaming_at");
    Func f("f");
    Func g("g");

    f(x) = x * 0.1f;
    g(x) = f(x) * f(x);

    auto ref_output = realize_ref(g);

    g.compute_root().sme_streaming(true, x).split(x, xo, xi, 256);
    f.compute_at(g, xo);  // Computed in streaming mode implicitly

    std::multiset<TaskCall> expected_calls{
        {Attribute::NO_ATTRIBUTE, Attribute::SME_STREAMING_TASK}};
    return check(g, name, expected_calls, ref_output);
}

bool test_2_stages_producer_streaming_at() {
    const std::string name("test_2_stages_producer_streaming_at");
    Func f("f");
    Func g("g");

    f(x) = x * 0.1f;
    g(x) = f(x) * f(x);

    auto ref_output = realize_ref(g);

    g.compute_root().split(x, xo, xi, 256);
    f.compute_at(g, xo).sme_streaming(true, x);

    std::multiset<TaskCall> expected_calls{
        {Attribute::NO_ATTRIBUTE, Attribute::SME_STREAMING_TASK}};
    return check(g, name, expected_calls, ref_output);
}

bool test_2_stages_consumer_streaming_at() {
    const std::string name("test_2_stages_consumer_streaming_at");
    Func f("f");
    Func g("g");

    f(x) = x * 0.1f;
    g(x) = f(x) * f(x);

    auto ref_output = realize_ref(g);

    g.compute_root().sme_streaming(true, x).split(x, xo, xi, 256);
    // explicitly set false, otherwise streaming is enabled
    f.compute_at(g, xo).sme_streaming(false);

    std::multiset<TaskCall> expected_calls{
        {Attribute::NO_ATTRIBUTE, Attribute::SME_STREAMING_TASK},
        {Attribute::SME_STREAMING_TASK, Attribute::SME_NONSTREAMING_TASK},
    };
    return check(g, name, expected_calls, ref_output);
}

bool test_2_stages_consumer_streaming_at_2() {
    const std::string name("test_2_stages_consumer_streaming_at_2");
    Func f("f");
    Func g("g");
    Func h("h");

    f(x) = x * 0.1f;
    g(x) = f(x) * f(x);
    h(x) = g(x) + g(x);

    auto ref_output = realize_ref(h);

    // Nested twice
    h.compute_root().sme_streaming(true, x).split(x, xo, xi, 256);
    g.compute_at(h, xo).sme_streaming(false, x).split(x, xo, xi, 64);
    f.compute_at(g, xo).sme_streaming(true, x);

    std::multiset<TaskCall> expected_calls{
        {Attribute::NO_ATTRIBUTE, Attribute::SME_STREAMING_TASK},
        {Attribute::SME_STREAMING_TASK, Attribute::SME_NONSTREAMING_TASK},
        {Attribute::SME_NONSTREAMING_TASK, Attribute::SME_STREAMING_TASK},
    };
    return check(h, name, expected_calls, ref_output);
}

bool test_update_rdom() {
    const std::string name("test_update_rdom");
    Func f("f");
    Func g("g");
    RDom r(0, 3);

    f(x) = sin(x);
    g(x) = 0.f;
    g(x) += f(x + r - 1);

    auto ref_output = realize_ref(g);

    g.compute_root().sme_streaming(true, x);
    g.update().sme_streaming(true, x);
    f.compute_root().sme_streaming(true, x);

    std::multiset<TaskCall> expected_calls{
        {Attribute::NO_ATTRIBUTE, Attribute::SME_STREAMING_TASK},
        {Attribute::NO_ATTRIBUTE, Attribute::SME_STREAMING_TASK},
        {Attribute::NO_ATTRIBUTE, Attribute::SME_STREAMING_TASK},
    };
    return check(g, name, expected_calls, ref_output);
}

bool test_update_rdom_2() {
    const std::string name("test_update_rdom_2");
    Func f("f");
    Func g("g");
    RDom r(0, 3);

    f(x) = sin(x);
    g(x) = 0.f;
    g(x) += f(x + r - 1);

    auto ref_output = realize_ref(g);

    g.compute_at(g.in(), x);
    g.in().compute_root().sme_streaming(true, x);
    g = g.in();
    f.compute_root();

    std::multiset<TaskCall> expected_calls{
        {Attribute::NO_ATTRIBUTE, Attribute::SME_STREAMING_TASK},
    };
    return check(g, name, expected_calls, ref_output);
}

bool test_update_rdom_rvar() {
    const std::string name("test_update_rdom_rvar");
    Func f("f");
    Func g("g");
    RDom r(0, 256);

    f(x) = sin(x);
    g(x) = 0.f;
    g(x) += f(x + r);

    auto ref_output = realize_ref(g);

    g.compute_root().update().sme_streaming(true, r);

    std::multiset<TaskCall> expected_calls{
        {Attribute::NO_ATTRIBUTE, Attribute::SME_STREAMING_TASK},
    };
    return check(g, name, expected_calls, ref_output);
}

bool test_compute_with() {
    const std::string name("test_compute_with");
    Func f("f");
    Func g("g");
    Func h("h");

    f(x) = sin(x);
    g(x) = x * 0.1f;
    h(x) = f(x) + g(x);

    auto ref_output = realize_ref(h);

    h.compute_root();
    // DeviceAPI of g and f must match to compute with
    g.compute_root().compute_with(f, x).sme_streaming(true, x);
    f.compute_root().sme_streaming(true, x);

    std::multiset<TaskCall> expected_calls{
        {Attribute::NO_ATTRIBUTE, Attribute::SME_STREAMING_TASK},
    };
    return check(h, name, expected_calls, ref_output);
}

bool test_parallel() {
    const std::string name("test_parallel");
    Func f("f");
    Var xso("xso"), xsi("xsi");

    f(x) = x * 0.1f;

    auto ref_output = realize_ref(f);

    // Streaming task is called for each thread spawned by parallel(),
    // rather than one streaming task spawning threads in it.
    f.compute_root()
        .split(x, xo, xi, 256)
        .parallel(xo)
        .sme_streaming(true, xi);

    std::multiset<TaskCall> expected_calls{
        {Attribute::NO_ATTRIBUTE, Attribute::SME_STREAMING_TASK},
    };
    return check(f, name, expected_calls, ref_output);
}

int main(int argc, char **argv) {
    can_run_code = get_jit_target_from_environment().has_feature(Target::SME2);
    if (!can_run_code) {
        std::cout << "(skip) Cannot run correctness check of sme_streaming on this target\n";
    }

    bool ok = true;
    ok &= test_1_stage_non_streaming();
    ok &= test_1_stage_streaming_outermost();
    ok &= test_1_stage_streaming_inner();
    ok &= test_2_stages_both_streaming();
    ok &= test_2_stages_producer_streaming();
    ok &= test_2_stages_consumer_streaming();
    ok &= test_2_stages_both_streaming_at();
    ok &= test_2_stages_producer_streaming_at();
    ok &= test_2_stages_consumer_streaming_at();
    ok &= test_2_stages_consumer_streaming_at_2();
    ok &= test_update_rdom();
    ok &= test_update_rdom_2();
    ok &= test_update_rdom_rvar();
    ok &= test_compute_with();
    ok &= test_parallel();

    if (!ok) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}
