#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>

using namespace Halide;

int main(int argc, char **argv) {
    // Make sure it's possible to generate object files (and other outputs)
    // for lots of targets. This provides early warning that you may have
    // broken Halide on some other platform.

    // We test -d3d12compute for 64-bit Windows platforms
    // due to the peculiar required mixture of calling conventions.

    const std::string targets[] = {
        "arm-32-android",
        "arm-32-ios",
        "arm-32-linux",
        "arm-32-noos-semihosting",
        "arm-64-android",
        "arm-64-android-hvx",
        "arm-64-ios",
        "arm-64-linux",
        "arm-64-noos-semihosting",
        "arm-64-windows",
        "arm-64-windows-d3d12compute",
        "wasm-32-wasmrt",
        "x86-32-linux",
        "x86-32-osx",
        "x86-32-windows",
        "x86-64-linux",
        "x86-64-osx",
        "x86-64-windows",
        "x86-64-windows-d3d12compute",
    };

    const auto tmp = Internal::get_test_tmp_dir();

    std::map<OutputFileType, std::string> outputs = {
        {OutputFileType::assembly, ""},
        {OutputFileType::object, ""},
        {OutputFileType::static_library, ""},
        {OutputFileType::stmt, ""},
        {OutputFileType::stmt_html, ""},
    };

    Param<float> p("myParam");
    Var x, y;

    for (const std::string &t : targets) {
        Target target(t);
        if (!target.supported()) continue;

        std::cout << "Test generating: " << target << "\n";

        const auto info = Halide::Internal::get_output_info(Target(t));
        for (auto &it : outputs) {
            const auto &i = info.at(it.first);
            it.second = tmp + "test-" + i.name + "-" + t + i.extension;
            Internal::ensure_no_file_exists(it.second);
            std::cout << "    " << it.second << "\n";
        }

        Func f("f-" + t), g("g-" + t), h("h-" + t), j("j-" + t);
        f(x, y) = x + y;
        g(x, y) = cast<float>(f(x, y) + f(x + 1, y)) * p;
        h(x, y) = f(x, y) + g(x, y);
        j(x, y) = h(x, y) * 2;

        f.compute_root();
        g.compute_root();
        h.compute_root();

        // Ensure that HVX codegen has a submodule, since that is a unique path
        // that isn't exercised otherwise
        if (Target(t).has_feature(Target::HVX)) {
            j.hexagon();
        }

        j.compile_to(outputs, j.infer_arguments(), "", target);

        for (auto &it : outputs) {
            Internal::assert_file_exists(it.second);
        }
    }

    printf("Success!\n");
    return 0;
}
