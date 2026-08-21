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
                          const std::map<RuntimeLinkage, std::string> &prefixes,
                          const std::vector<Target::Feature> &extra_features = {}) {
    Pipeline p = make_pipeline();

    // Always an AOT (non-JIT) target: runtime namespacing is unsupported for JIT.
    Target target = get_host_target();
    for (auto f : extra_features) {
        target = target.with_feature(f);
    }

    if (!prefixes.empty()) {
        p.apply_runtime_prefixes(target, RuntimePrefixParams(prefixes));
    }

    const std::string path =
        Internal::get_test_tmp_dir() + "runtime_prefixes_" + tag + ".ll";
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
        const std::string exp = "my_prefix_";
        const std::string ll = compile_to_ll("export", {{RuntimeLinkage::Export, exp}});
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
        const std::string imp = "my_prefix_";
        const std::string ll = compile_to_ll("import",
                                             {{RuntimeLinkage::Import, imp}},
                                             {Target::NoRuntime});
        check(contains(ll, "@" + imp + "malloc"),
              "import: expected @" + imp + "malloc");
        check(!contains(ll, "@halide_malloc"),
              "import: @halide_malloc should have been renamed");
    }

    // ------------------------------------------------------------------
    // (3) Internal scope. Two facets:
    //
    //  (a) The runtime's internal C++ state -- the Halide::Runtime::Internal
    //      namespace symbols (custom-handler pointers, memoization cache, thread
    //      pool work queue, ...). These are what let two separately-namespaced
    //      runtimes keep independent state, so with the runtime linked in the
    //      internal prefix must rename all of them.
    //
    //  (b) The halide_-prefixed C ABI: an external declaration is "internal"
    //      only when called from other runtime methods, not the kernel. In a
    //      NoRuntime kernel the halide_ declarations are kernel-imported, so an
    //      internal-only prefix must leave them unchanged.
    // ------------------------------------------------------------------
    {
        // (a) With the runtime linked in, the internal namespace state must be
        // renamed and no un-prefixed Halide::Runtime::Internal symbol remains.
        const std::string internal = "my_prefix_internal_";
        const std::string ll = compile_to_ll("internal", {{RuntimeLinkage::Internal, internal}});
        // Itanium mangling of the Halide::Runtime::Internal namespace.
        const std::string ns = "6Halide7Runtime8Internal";
        check(contains(ll, "@" + internal + "_ZN" + ns),
              "internal: runtime state/helpers should be namespaced with the internal prefix");
        check(!contains(ll, "@_ZN" + ns),
              "internal: no un-prefixed Halide::Runtime::Internal symbol should remain");
    }
    {
        // (b) NoRuntime kernel: an internal-only prefix must not touch the
        // kernel's halide_ imports.
        const std::string ll = compile_to_ll("internal_only",
                                             {{RuntimeLinkage::Internal, "my_prefix_internal_"}},
                                             {Target::NoRuntime});
        check(!contains(ll, "@my_prefix_internal_malloc"),
              "internal: kernel-called declarations must NOT get the internal prefix");
        check(contains(ll, "@halide_malloc"),
              "internal: kernel-called halide_malloc should be unchanged by an internal-only prefix");
    }

    // ------------------------------------------------------------------
    // Each prefix is optional: exercise all 2^3 combinations, both with the
    // runtime linked in and with NoRuntime, and confirm they all compile.
    // ------------------------------------------------------------------
    for (int mask = 0; mask < 8; mask++) {
        std::map<RuntimeLinkage, std::string> prefixes;
        if (mask & 1) prefixes[RuntimeLinkage::Import] = "imp_";
        if (mask & 2) prefixes[RuntimeLinkage::Export] = "exp_";
        if (mask & 4) prefixes[RuntimeLinkage::Internal] = "int_";

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
            const RuntimeLinkage relevant =
                no_runtime ? RuntimeLinkage::Import : RuntimeLinkage::Export;
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
        RuntimePrefixParams ns({{RuntimeLinkage::Export, "my_prefix_"}});

        bool error = false;
        try {
            p.apply_runtime_prefixes(jit_target, ns);
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
