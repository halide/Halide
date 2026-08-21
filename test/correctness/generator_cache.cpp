// Exercises the generator compile cache (HL_CACHE_DIR) that execute_generator
// uses to avoid recompiling unchanged pipelines. Only meaningful when Halide
// was built with serialization support (the cache keys pipelines by their
// serialized form); TEST_WITH_SERIALIZATION is defined by CMake in that case.
//
// The cache is verified behaviorally, using only the public API. Each
// generation runs in a *separate child process* (by re-exec'ing this test with
// --gen), matching how a real build invokes one generator process per library.
// This matters: Halide's global unique-name counter advances within a process,
// so two build_pipeline() calls in the same process serialize differently and
// would never share a cache key -- but two fresh processes start from the same
// counter and produce identical pipelines, keys, and artifacts. We tamper with
// a cached artifact and check whether a later run reproduces the tampered bytes
// (a hit) or freshly compiled bytes (a miss).

#include "Halide.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

using namespace Halide;

namespace {

// A trivial generator with a GeneratorParam that changes the pipeline, so we
// can verify that changing it produces a different cache key.
class CacheAdd : public Generator<CacheAdd> {
public:
    GeneratorParam<int> offset{"offset", 0};
    Input<Buffer<int32_t, 1>> input{"input"};
    Output<Buffer<int32_t, 1>> output{"output"};

    void generate() {
        Var x;
        output(x) = input(x) + offset;
    }
};

// Run one generation into `outdir` with the given offset. Used by the --gen
// child process. HL_CACHE_DIR is inherited from the parent's environment.
int generate_one(const std::string &outdir, const std::string &offset) {
    Internal::ExecuteGeneratorArgs args;
    args.output_dir = outdir;
    args.output_types = {OutputFileType::object, OutputFileType::c_header};
    args.targets = {get_host_target()};
    args.generator_name = "cache_add";
    args.generator_params = {{"offset", offset}};
    Internal::execute_generator(args);
    return 0;
}

}  // namespace

HALIDE_REGISTER_GENERATOR(CacheAdd, cache_add)

#ifdef TEST_WITH_SERIALIZATION

namespace {

namespace fs = std::filesystem;

// Return 1 from main() on failure; the test harness treats that as a failure.
// (assert() is compiled out in release builds, so we can't rely on it.)
#define check(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::cerr << "FAILED: " #cond " (line " << __LINE__ << ")\n"; \
            return 1;                                                     \
        }                                                                 \
    } while (0)

std::vector<uint8_t> read_all(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

void set_cache_dir(const std::string &dir) {
#ifdef _WIN32
    _putenv_s("HL_CACHE_DIR", dir.c_str());
#else
    setenv("HL_CACHE_DIR", dir.c_str(), /*overwrite*/ 1);
#endif
}

// Find the single cached blob for a given output type (blobs are named "f<int>"
// where <int> is the OutputFileType value). Returns an empty path if not found.
fs::path find_blob(const fs::path &entries, OutputFileType type) {
    const std::string want = "f" + std::to_string((int)type);
    if (!fs::exists(entries)) {
        return {};
    }
    for (const auto &e : fs::recursive_directory_iterator(entries)) {
        if (e.is_regular_file() && e.path().filename() == want) {
            return e.path();
        }
    }
    return {};
}

}  // namespace

int main(int argc, char **argv) {
    // Child mode: perform exactly one generation and exit.
    if (argc == 4 && std::string(argv[1]) == "--gen") {
        return generate_one(argv[2], argv[3]);
    }

    const std::string self = fs::absolute(argv[0]).string();
    const fs::path tmp =
        fs::temp_directory_path() / ("hlgc_" + std::to_string(std::random_device{}()));
    fs::remove_all(tmp);
    const fs::path cache = tmp / "cache";
    const fs::path entries = cache / "entries";
    fs::create_directories(cache);

    // Children inherit HL_CACHE_DIR from our environment.
    set_cache_dir(cache.string());

    // The generator's object output uses the platform-appropriate extension
    // (.obj on Windows, .o elsewhere); see Module.cpp's get_output_info.
    const std::string obj_ext =
        Internal::get_output_info(get_host_target()).at(OutputFileType::object).extension;

    // Run one generation in a fresh child process; returns the object path.
    const auto run = [&](const std::string &sub, const std::string &offset) -> fs::path {
        const fs::path outdir = tmp / sub;
        fs::create_directories(outdir);
        (void)Internal::run_process({self, "--gen", outdir.string(), offset});
        return outdir / ("cache_add" + obj_ext);
    };

    // 1. First compile of offset=1: this populates the cache.
    const fs::path obj_a = run("a", "1");
    check(fs::exists(obj_a));
    const std::vector<uint8_t> real_obj = read_all(obj_a);

    // Tamper with the cached object blob so a genuine cache hit is observable:
    // a restored object will contain these sentinel bytes, a recompiled one
    // will not.
    const fs::path blob = find_blob(entries, OutputFileType::object);
    check(!blob.empty());
    const std::vector<uint8_t> sentinel = {'S', 'E', 'N', 'T', 'I', 'N', 'E', 'L'};
    {
        std::ofstream f(blob, std::ios::binary | std::ios::trunc);
        f.write((const char *)sentinel.data(), sentinel.size());
    }

    // 2. Same inputs, fresh output dir: a cache hit that restores the (tampered)
    //    bytes, proving the object came from the cache rather than a recompile.
    const fs::path obj_b = run("b", "1");
    check(read_all(obj_b) == sentinel);

    // 3. A different GeneratorParam changes the pipeline, so the key differs and
    //    the run recompiles rather than restoring the sentinel.
    const fs::path obj_c = run("c", "2");
    check(read_all(obj_c) != sentinel);
    check(read_all(obj_c) != real_obj);

    // 4. Corrupt every cache entry (drop manifests): the next offset=1 run must
    //    treat the entry as a miss and recompile the real object.
    for (const auto &e : fs::recursive_directory_iterator(entries)) {
        if (e.path().filename() == "manifest.txt") {
            fs::remove(e.path());
        }
    }
    const fs::path obj_d = run("d", "1");
    check(read_all(obj_d) == real_obj);

    fs::remove_all(tmp);
    std::cout << "Success!\n";
    return 0;
}

#else  // TEST_WITH_SERIALIZATION

int main() {
    std::cout << "[SKIP] generator_cache requires WITH_SERIALIZATION.\n";
    return 0;
}

#endif  // TEST_WITH_SERIALIZATION
