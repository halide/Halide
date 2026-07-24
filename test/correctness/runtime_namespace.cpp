#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace Halide;

namespace {

// Read an entire file into a string.
std::string read_file(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Build a small pipeline that reliably forces a heap allocation (and therefore
// a call to the halide_malloc / halide_free runtime API): a compute_root
// producer whose extent depends on the (runtime) output extent cannot be
// promoted to the stack.
Pipeline make_pipeline() {
    Var x("x");
    Func producer("producer"), consumer("consumer");
    producer(x) = x * 2;
    consumer(x) = producer(x) + producer(x + 1);
    producer.compute_root();
    return Pipeline(consumer);
}

// Compile make_pipeline() to LLVM assembly (.ll) text with the given runtime
// namespace prefixes and return the text. `extra_features` lets callers add
// e.g. Target::NoRuntime.
std::string compile_to_ll(const std::string &tag,
                          const std::map<RuntimeVisibility, std::string> &prefixes,
                          const std::vector<Target::Feature> &extra_features = {}) {
    Pipeline p = make_pipeline();

    // Always an AOT (non-JIT) target: runtime namespacing is unsupported for JIT.
    Target target = get_host_target();
    for (auto f : extra_features) {
        target = target.with_feature(f);
    }

    if (!prefixes.empty()) {
        p.apply_runtime_namespace(target, RuntimeNamespaceParams(prefixes));
    }

    const std::string path =
        Internal::get_test_tmp_dir() + "runtime_namespace_" + tag + ".ll";
    Internal::ensure_no_file_exists(path);

    Module m = p.compile_to_module({}, "consumer_" + tag, target);
    m.compile({{OutputFileType::llvm_assembly, path}});

    Internal::assert_file_exists(path);
    return read_file(path);
}

bool contains(const std::string &haystack, const std::string &needle) {
    return haystack.find(needle) != std::string::npos;
}

void check(bool cond, const std::string &msg) {
    if (!cond) {
        std::cerr << "FAILED: " << msg << "\n";
        exit(1);
    }
}

}  // namespace

int main(int argc, char **argv) {
    // ------------------------------------------------------------------
    // Baseline: no prefixes -> stock halide_ names, and the pipeline must
    // still compile normally.
    // ------------------------------------------------------------------
    const std::string base = compile_to_ll("baseline", {});
    check(contains(base, "@halide_malloc"),
          "baseline should reference @halide_malloc");
    check(contains(base, "@halide_free"),
          "baseline should reference @halide_free");

    // ------------------------------------------------------------------
    // (2) Export scope: with the runtime linked in, halide_ definitions are
    // renamed with the export prefix.
    // ------------------------------------------------------------------
    {
        const std::string exp = "myexport_";
        const std::string ll = compile_to_ll("export", {{RuntimeVisibility::Export, exp}});
        check(contains(ll, "@" + exp + "malloc"),
              "export: expected @" + exp + "malloc");
        check(contains(ll, "@" + exp + "free"),
              "export: expected @" + exp + "free");
        // Every reference to the halide_ names should have been rewritten.
        check(!contains(ll, "@halide_malloc"),
              "export: @halide_malloc should have been renamed");
        check(!contains(ll, "@halide_free"),
              "export: @halide_free should have been renamed");
    }

    // ------------------------------------------------------------------
    // (1) Import scope: with NoRuntime, halide_ calls are external
    // declarations, renamed with the import prefix.
    // ------------------------------------------------------------------
    {
        const std::string imp = "myimport_";
        const std::string ll = compile_to_ll("import",
                                             {{RuntimeVisibility::Import, imp}},
                                             {Target::NoRuntime});
        check(contains(ll, "@" + imp + "malloc"),
              "import: expected @" + imp + "malloc");
        check(!contains(ll, "@halide_malloc"),
              "import: @halide_malloc should have been renamed");
    }

    // ------------------------------------------------------------------
    // (3) Internal scope: an external halide_ declaration whose call sites are
    // inside *other runtime methods* (defined in a different compilation unit).
    // The standard host runtime is self-contained, so this scope does not fire
    // for host builds; the positive case only arises for split/partial runtimes.
    //
    // What we *can* verify on host is that Import and Internal are correctly
    // distinguished: in a NoRuntime kernel the halide_ declarations are called
    // by the generated kernel (import scope), so setting *only* the internal
    // prefix must leave them unchanged.
    // ------------------------------------------------------------------
    {
        const std::string ll = compile_to_ll("internal_only",
                                             {{RuntimeVisibility::Internal, "myinternal_"}},
                                             {Target::NoRuntime});
        check(!contains(ll, "@myinternal_malloc"),
              "internal: kernel-called declarations must NOT get the internal prefix");
        check(contains(ll, "@halide_malloc"),
              "internal: kernel-called halide_malloc should be unchanged by an internal-only prefix");
    }

    // ------------------------------------------------------------------
    // Each prefix is optional: exercise all 2^3 combinations, both with the
    // runtime linked in and with NoRuntime, and confirm they all compile.
    // ------------------------------------------------------------------
    for (int mask = 0; mask < 8; mask++) {
        std::map<RuntimeVisibility, std::string> prefixes;
        if (mask & 1) prefixes[RuntimeVisibility::Import] = "imp_";
        if (mask & 2) prefixes[RuntimeVisibility::Export] = "exp_";
        if (mask & 4) prefixes[RuntimeVisibility::Internal] = "int_";

        for (bool no_runtime : {false, true}) {
            std::vector<Target::Feature> features;
            if (no_runtime) {
                features.push_back(Target::NoRuntime);
            }
            const std::string tag =
                "combo_" + std::to_string(mask) + (no_runtime ? "_nr" : "_rt");
            const std::string ll = compile_to_ll(tag, prefixes, features);  // must not throw

            // Whatever prefixes were chosen, the corresponding halide_ names
            // must be gone. With the runtime linked in, halide_malloc is a
            // definition (export); with NoRuntime it is a kernel-called
            // declaration (import).
            const RuntimeVisibility relevant =
                no_runtime ? RuntimeVisibility::Import : RuntimeVisibility::Export;
            if (prefixes.count(relevant)) {
                check(contains(ll, "@" + prefixes[relevant] + "malloc"),
                      tag + ": expected renamed malloc");
                check(!contains(ll, "@halide_malloc"),
                      tag + ": halide_malloc should have been renamed");
            }
        }
    }

    // ------------------------------------------------------------------
    // JIT test case: runtime namespacing is unsupported for JIT (the JIT
    // resolves runtime calls against a non-namespaced shared runtime), so
    // requesting it on a JIT target must raise a clean error.
    // ------------------------------------------------------------------
#if HALIDE_WITH_EXCEPTIONS
    if (Halide::exceptions_enabled()) {
        Pipeline p = make_pipeline();
        Target jit_target = get_host_target().with_feature(Target::JIT);
        RuntimeNamespaceParams ns({{RuntimeVisibility::Export, "myexport_"}});

        bool error = false;
        try {
            p.apply_runtime_namespace(jit_target, ns);
        } catch (const Halide::CompileError &e) {
            error = true;
            printf("Expected compile error:\n%s\n", e.what());
        }
        check(error, "JIT + runtime namespace prefixes should raise an error");
    }
#endif

    printf("Success!\n");
    return 0;
}
