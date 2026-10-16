#include "Halide.h"
#include "expect_user_error.h"
#include "halide_test_dirs.h"

#include <cstdio>
#include <fstream>
#include <sstream>

using namespace Halide;

namespace {

std::string read_file(const std::string &path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool check(const std::string &what, const std::string &text, const std::string &needle, bool expected) {
    bool found = text.find(needle) != std::string::npos;
    if (found != expected) {
        printf("%s: expected \"%s\" to be %s\n", what.c_str(), needle.c_str(), expected ? "present" : "absent");
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    ImageParam secret_input(UInt(8), 1, "secret_input");
    Param<uint8_t> secret_offset("secret_offset");
    Var x("x");
    Func f("f");
    f(x) = secret_input(x) + secret_offset;

    const std::vector<Argument> args = {secret_input, secret_offset};
    const std::string dir = Internal::get_test_tmp_dir();

    // Asserts mention buffer names too, so leave them out to isolate the metadata.
    const Target base = get_host_target().with_feature(Target::NoAsserts).with_feature(Target::NoRuntime);

    bool ok = true;
    for (bool no_metadata : {false, true}) {
        const Target t = no_metadata ? base.with_feature(Target::NoMetadata) : base;
        const std::string prefix = dir + (no_metadata ? "without_metadata" : "with_metadata");

        Module m = f.compile_to_module(args, "secret_pipeline", t);
        LinkageType expected_linkage = no_metadata ? LinkageType::External : LinkageType::ExternalPlusMetadata;
        if (m.functions().front().linkage != expected_linkage) {
            printf("Unexpected linkage for secret_pipeline (no_metadata=%d)\n", no_metadata);
            ok = false;
        }

        const std::string h = prefix + ".h", c = prefix + ".c", ll = prefix + ".ll";
        m.compile({{OutputFileType::c_header, h},
                   {OutputFileType::c_source, c},
                   {OutputFileType::llvm_assembly, ll}});

        for (const std::string &path : {h, c, ll}) {
            std::string text = read_file(path);
            ok &= check(path, text, "secret_pipeline_argv", !no_metadata);
            ok &= check(path, text, "secret_pipeline_metadata", !no_metadata);
        }
        // The names in the metadata end up as string constants in the binary.
        ok &= check(ll, read_file(ll), "c\"secret_offset\\00\"", !no_metadata);
        ok &= check(c, read_file(c), "\"secret_offset\"", !no_metadata);
    }

    // A registration needs the _argv() and _metadata() entry points.
    ok &= expect_user_error("no_metadata_registration", "requires the _argv() and _metadata() entry points", [&]() {
        Module m = f.compile_to_module(args, "secret_pipeline", base.with_feature(Target::NoMetadata));
        m.compile({{OutputFileType::registration, dir + "no_metadata.registration.cpp"}});
    });

    if (!ok) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}
