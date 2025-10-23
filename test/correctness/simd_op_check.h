#ifndef SIMD_OP_CHECK_H
#define SIMD_OP_CHECK_H

#include "Halide.h"
#include "halide_test_dirs.h"
#include "halide_thread_pool.h"
#include "test_sharding.h"

#include <fstream>
#include <iostream>

namespace {

using namespace Halide;

// Some exprs of each type to use in checked expressions. These will turn
// into loads to thread-local image params.
Expr input(const Type &t, const Expr &arg) {
    return Internal::Call::make(t, "input", {arg}, Internal::Call::Extern);
}
Expr in_f16(const Expr &arg) {
    return input(Float(16), arg);
}
Expr in_bf16(const Expr &arg) {
    return input(BFloat(16), arg);
}
Expr in_f32(const Expr &arg) {
    return input(Float(32), arg);
}
Expr in_f64(const Expr &arg) {
    return input(Float(64), arg);
}
Expr in_i8(const Expr &arg) {
    return input(Int(8), arg);
}
Expr in_i16(const Expr &arg) {
    return input(Int(16), arg);
}
Expr in_i32(const Expr &arg) {
    return input(Int(32), arg);
}
Expr in_i64(const Expr &arg) {
    return input(Int(64), arg);
}
Expr in_u8(const Expr &arg) {
    return input(UInt(8), arg);
}
Expr in_u16(const Expr &arg) {
    return input(UInt(16), arg);
}
Expr in_u32(const Expr &arg) {
    return input(UInt(32), arg);
}
Expr in_u64(const Expr &arg) {
    return input(UInt(64), arg);
}
}  // namespace

namespace Halide {
struct TestResult {
    std::string op;
    std::string error_msg;
};

struct Task {
    std::string op;
    std::string name;
    int vector_width;
    Expr expr;
};

class SimdOpCheckTest {
public:
    static constexpr int max_i8 = 127;
    static constexpr int max_i16 = 32767;
    static constexpr int max_i32 = 0x7fffffff;
    static constexpr int max_u8 = 255;
    static constexpr int max_u16 = 65535;
    const Expr max_u32 = UInt(32).max();

    std::string filter{"*"};
    std::string output_directory{Internal::get_test_tmp_dir()};
    std::vector<Task> tasks;

    Target target;

    int W;
    int H;

    int rng_seed;

    using Sharder = Halide::Internal::Test::Sharder;

    SimdOpCheckTest(const Target t, int w, int h)
        : target(t), W(w), H(h), rng_seed(0) {
        target = target
                     .with_feature(Target::NoBoundsQuery)
                     .with_feature(Target::NoAsserts)
                     .with_feature(Target::NoRuntime);
    }
    virtual ~SimdOpCheckTest() = default;

    void set_seed(int seed) {
        rng_seed = seed;
    }

    virtual bool can_run_code() const {
        if (target.arch == Target::WebAssembly) {
            return Halide::Internal::WasmModule::can_jit_target(Target("wasm-32-wasmrt"));
        }
        // If we can (target matches host), run the error checking Halide::Func.
        Target host_target = get_host_target();
        bool can_run_the_code =
            (target.arch == host_target.arch &&
             target.bits == host_target.bits &&
             target.os == host_target.os);
        // A bunch of feature flags also need to match between the
        // compiled code and the host in order to run the code.
        for (Target::Feature f : {
                 Target::ARMDotProd,
                 Target::ARMFp16,
                 Target::ARMv7s,
                 Target::ARMv8a,
                 Target::ARMv81a,
                 Target::ARMv82a,
                 Target::ARMv83a,
                 Target::ARMv84a,
                 Target::ARMv85a,
                 Target::ARMv86a,
                 Target::ARMv87a,
                 Target::ARMv88a,
                 Target::ARMv89a,
                 Target::AVX,
                 Target::AVX2,
                 Target::AVX512,
                 Target::AVX512_Cannonlake,
                 Target::AVX512_KNL,
                 Target::AVX512_SapphireRapids,
                 Target::AVX512_Skylake,
                 Target::F16C,
                 Target::FMA,
                 Target::FMA4,
                 Target::NoNEON,
                 Target::POWER_ARCH_2_07,
                 Target::RVV,
                 Target::SSE41,
                 Target::SVE,
                 Target::SVE2,
                 Target::VSX,
             }) {
            if (target.has_feature(f) != host_target.has_feature(f)) {
                can_run_the_code = false;
            }
        }
        return can_run_the_code;
    }

    virtual void compile_and_check(Func error,
                                   const std::string &op,
                                   const std::string &name,
                                   int vector_width,
                                   const std::vector<Argument> &arg_types,
                                   std::ostringstream &error_msg) {
        std::string fn_name = "test_" + name;
        std::string file_name = output_directory + fn_name;

        auto ext = Internal::get_output_info(target);
        std::map<OutputFileType, std::string> outputs = {
            {OutputFileType::c_header, file_name + ext.at(OutputFileType::c_header).extension},
            {OutputFileType::object, file_name + ext.at(OutputFileType::object).extension},
            {OutputFileType::assembly, file_name + ".s"},
        };
        error.compile_to(outputs, arg_types, fn_name, target);

        std::ifstream asm_file;
        asm_file.open(file_name + ".s");

        bool found_it = false;

        std::ostringstream msg;
        msg << op << " did not generate for target=" << get_run_target().to_string() << " vector_width=" << vector_width << ". Instead we got:\n";

        std::string line;
        while (getline(asm_file, line)) {
            msg << line << "\n";

            // Check for the op in question
            found_it |= wildcard_search(op, line) && !wildcard_search("_" + op, line);
        }

        if (!found_it) {
            error_msg << "Failed: " << msg.str() << "\n";
        }

        asm_file.close();
    }

    // Check if pattern p matches str, allowing for wildcards (*).
    bool wildcard_match(const char *p, const char *str) const {
        // Match all non-wildcard characters.
        while (*p && *str && *p == *str && *p != '*') {
            str++;
            p++;
        }

        if (!*p) {
            return *str == 0;
        } else if (*p == '*') {
            p++;
            do {
                if (wildcard_match(p, str)) {
                    return true;
                }
            } while (*str++);
        } else if (*p == ' ') {  // ignore whitespace in pattern
            p++;
            if (wildcard_match(p, str)) {
                return true;
            }
        } else if (*str == ' ') {  // ignore whitespace in string
            str++;
            if (wildcard_match(p, str)) {
                return true;
            }
        }
        return !*p;
    }

    bool wildcard_match(const std::string &p, const std::string &str) const {
        return wildcard_match(p.c_str(), str.c_str());
    }

    // Check if a substring of str matches a pattern p.
    bool wildcard_search(const std::string &p, const std::string &str) const {
        return wildcard_match("*" + p + "*", str);
    }

    Target get_run_target() const {
        return target
            .without_feature(Target::NoRuntime)
            .without_feature(Target::NoAsserts)
            .without_feature(Target::NoBoundsQuery);
    }

    TestResult check_one(const std::string &op, const std::string &name, int vector_width, Expr e) {
        std::ostringstream error_msg;

        // Map the input calls in the Expr to loads to local
        // imageparams, so that we're not sharing state across threads.
        std::vector<ImageParam> image_params{
            ImageParam{Float(32), 1, "in_f32"},
            ImageParam{Float(64), 1, "in_f64"},
            ImageParam{Float(16), 1, "in_f16"},
            ImageParam{BFloat(16), 1, "in_bf16"},
            ImageParam{Int(8), 1, "in_i8"},
            ImageParam{UInt(8), 1, "in_u8"},
            ImageParam{Int(16), 1, "in_i16"},
            ImageParam{UInt(16), 1, "in_u16"},
            ImageParam{Int(32), 1, "in_i32"},
            ImageParam{UInt(32), 1, "in_u32"},
            ImageParam{Int(64), 1, "in_i64"},
            ImageParam{UInt(64), 1, "in_u64"}};

        for (auto &p : image_params) {
            const int alignment_bytes = image_param_alignment();
            p.set_host_alignment(alignment_bytes);
            const int alignment = alignment_bytes / p.type().bytes();
            p.dim(0).set_min((p.dim(0).min() / alignment) * alignment);
        }

        std::vector<Argument> arg_types(image_params.begin(), image_params.end());

        class HookUpImageParams : public Internal::IRMutator {
            using Internal::IRMutator::visit;

            Expr visit(const Internal::Call *op) override {
                if (op->name == "input") {
                    for (auto &p : image_params) {
                        if (p.type() == op->type) {
                            return p(mutate(op->args[0]));
                        }
                    }
                } else if (op->call_type == Internal::Call::Halide && !op->func.weak) {
                    Internal::Function f(op->func);
                    f.mutate(this);
                }
                return Internal::IRMutator::visit(op);
            }
            const std::vector<ImageParam> &image_params;

        public:
            HookUpImageParams(const std::vector<ImageParam> &image_params)
                : image_params(image_params) {
            }
        } hook_up_image_params(image_params);
        e = hook_up_image_params.mutate(e);

        class HasInlineReduction : public Internal::IRVisitor {
            using Internal::IRVisitor::visit;
            void visit(const Internal::Call *op) override {
                if (op->call_type == Internal::Call::Halide) {
                    Internal::Function f(op->func);
                    if (f.has_update_definition() &&
                        f.update(0).schedule().rvars().size() > 0) {
                        inline_reduction = f;
                        result = true;
                    }
                }
                IRVisitor::visit(op);
            }

        public:
            Internal::Function inline_reduction;
            bool result = false;
        } has_inline_reduction;
        e.accept(&has_inline_reduction);

        // Define a vectorized Halide::Func that uses the pattern.
        Halide::Func f(name);
        f(x, y) = e;
        f.bound(x, 0, W).vectorize(x, vector_width);
        f.compute_root();

        // Include a scalar version
        Halide::Func f_scalar("scalar_" + name);
        f_scalar(x, y) = e;

        if (has_inline_reduction.result) {
            // If there's an inline reduction, we want to vectorize it
            // over the RVar.
            Var xo, xi;
            RVar rxi;
            Func g{has_inline_reduction.inline_reduction};

            // Do the reduction separately in f_scalar
            g.clone_in(f_scalar);

            g.compute_at(f, x)
                .update()
                .split(x, xo, xi, vector_width)
                .atomic(true)
                .vectorize(g.rvars()[0])
                .vectorize(xi);
        }

        // We'll check over H rows, but we won't let the pipeline know H
        // statically, as that can trigger some simplifications that change
        // instruction selection.
        Param<int> rows;
        rows.set(H);
        arg_types.push_back(rows);

        // The output to the pipeline is the maximum absolute difference as a double.
        RDom r_check(0, W, 0, rows);
        Halide::Func error("error_" + name);
        error() = Halide::cast<double>(maximum(absd(f(r_check.x, r_check.y), f_scalar(r_check.x, r_check.y))));

        compile_and_check(error, op, name, vector_width, arg_types, error_msg);

        bool can_run_the_code = can_run_code();
        if (can_run_the_code) {
            Target run_target = get_run_target();

            // Make some unallocated input buffers
            std::vector<Runtime::Buffer<>> inputs(image_params.size());

            std::vector<Argument> args(image_params.size() + 1);
            for (size_t i = 0; i < image_params.size(); i++) {
                args[i] = image_params[i];
                inputs[i] = Runtime::Buffer<>(args[i].type, nullptr, 0);
            }
            args.back() = rows;

            auto callable = error.compile_to_callable(args, run_target);

            Runtime::Buffer<double> output = Runtime::Buffer<double>::make_scalar();
            output(0) = 1;  // To ensure we'll fail if it's never written to

            // Do the bounds query call
            assert(inputs.size() == 12);
            (void)callable(inputs[0], inputs[1], inputs[2], inputs[3],
                           inputs[4], inputs[5], inputs[6], inputs[7],
                           inputs[8], inputs[9], inputs[10], inputs[11],
                           H, output);

            std::mt19937 rng;
            rng.seed(rng_seed);

            // Allocate the input buffers and fill them with noise
            for (size_t i = 0; i < inputs.size(); i++) {
                if (inputs[i].size_in_bytes()) {
                    inputs[i].allocate();

                    Type t = inputs[i].type();
                    // For floats/doubles, we only use values that aren't
                    // subject to rounding error that may differ between
                    // vectorized and non-vectorized versions
                    if (t == Float(32)) {
                        inputs[i].as<float>().for_each_value([&](float &f) { f = (rng() & 0xfff) / 8.0f - 0xff; });
                    } else if (t == Float(64)) {
                        inputs[i].as<double>().for_each_value([&](double &f) { f = (rng() & 0xfff) / 8.0 - 0xff; });
                    } else if (t == Float(16)) {
                        inputs[i].as<float16_t>().for_each_value([&](float16_t &f) { f = float16_t((rng() & 0xff) / 8.0f - 0xf); });
                    } else if (t == BFloat(16)) {
                        inputs[i].as<bfloat16_t>().for_each_value([&](bfloat16_t &f) { f = bfloat16_t((rng() & 0xff) / 8.0f - 0xf); });
                    } else {
                        assert(t.is_int_or_uint());
                        // Random bits is fine, but in the 32-bit int case we
                        // never use the top four bits, to avoid signed integer
                        // overflow.
                        const uint32_t mask = (t == Int(32)) ? 0x0fffffffU : 0xffffffffU;
                        for (uint32_t *ptr = (uint32_t *)inputs[i].data();
                             ptr != (uint32_t *)inputs[i].data() + inputs[i].size_in_bytes() / 4;
                             ptr++) {
                            *ptr = ((uint32_t)rng()) & mask;
                        }
                    }
                }
            }

            // Do the real call
            (void)callable(inputs[0], inputs[1], inputs[2], inputs[3],
                           inputs[4], inputs[5], inputs[6], inputs[7],
                           inputs[8], inputs[9], inputs[10], inputs[11],
                           H, output);

            double e = output(0);
            // Use a very loose tolerance for floating point tests. The
            // kinds of bugs we're looking for are codegen bugs that
            // return the wrong value entirely, not floating point
            // accuracy differences between vectors and scalars.
            if (e > 0.001) {
                error_msg << "The vector and scalar versions of " << name << " disagree. Maximum error: " << e << "\n";

                std::string error_filename = output_directory + "error_" + name + ".s";
                error.compile_to_assembly(error_filename, arg_types, target);

                std::ifstream error_file;
                error_file.open(error_filename);

                error_msg << "Error assembly: \n";
                std::string line;
                while (getline(error_file, line)) {
                    error_msg << line << "\n";
                }

                error_file.close();
            }
        }

        return {op, error_msg.str()};
    }

    void check(std::string op, int vector_width, Expr e) {
        // Make a name for the test by uniquing then sanitizing the op name
        std::string name = "op_" + op;
        for (size_t i = 0; i < name.size(); i++) {
            if (!isalnum(name[i])) name[i] = '_';
        }

        name += "_" + std::to_string(tasks.size());

        // Bail out after generating the unique_name, so that names are
        // unique across different processes and don't depend on filter
        // settings.
        if (!wildcard_match(filter, op)) return;

        tasks.emplace_back(Task{op, name, vector_width, e});
    }
    virtual void add_tests() = 0;
    virtual int image_param_alignment() {
        return 16;
    }

    virtual bool use_multiple_threads() const {
        return true;
    }

    virtual bool test_all() {
        /* First add some tests based on the target */
        add_tests();

        // Remove irrelevant noise from output
        const Target run_target = get_run_target();
        const std::string run_target_str = run_target.to_string();

        Sharder sharder;

        Halide::Tools::ThreadPool<TestResult> pool(
            use_multiple_threads() ?
                Halide::Tools::ThreadPool<TestResult>::num_processors_online() :
                1);
        std::vector<std::future<TestResult>> futures;

        for (size_t t = 0; t < tasks.size(); t++) {
            if (!sharder.should_run(t)) continue;
            const auto &task = tasks.at(t);
            futures.push_back(pool.async([&]() {
                return check_one(task.op, task.name, task.vector_width, task.expr);
            }));
        }

        for (auto &f : futures) {
            auto result = f.get();
            constexpr int tabstop = 32;
            const int spaces = std::max(1, tabstop - (int)result.op.size());
            std::cout << result.op << std::string(spaces, ' ') << "(" << run_target_str << ")\n";
            if (!result.error_msg.empty()) {
                std::cerr << result.error_msg;
                // The thread-pool destructor will block until in-progress tasks
                // are done, and then will discard any tasks that haven't been
                // launched yet.
                return false;
            }
        }

        return true;
    }

    template<typename SIMDOpCheckT>
    static int main(int argc, char **argv, const std::vector<Target> &targets_to_test) {
        Target host = get_host_target();
        std::cout << "host is:      " << host << "\n";

        const int seed = argc > 2 ? atoi(argv[2]) : time(nullptr);
        std::cout << "simd_op_check test seed: " << seed << "\n";

        for (const auto &t : targets_to_test) {
            if (!t.supported()) {
                std::cout << "[SKIP] Unsupported target: " << t << "\n";
                return 0;
            }
            SIMDOpCheckT test(t);

            if (!t.supported()) {
                std::cout << "Halide was compiled without support for " << t.to_string() << ". Skipping.\n";
                continue;
            }

            if (argc > 1) {
                test.filter = argv[1];
            }

            if (getenv("HL_SIMD_OP_CHECK_FILTER")) {
                test.filter = getenv("HL_SIMD_OP_CHECK_FILTER");
            }

            test.set_seed(seed);

            if (argc > 2) {
                // Don't forget: if you want to run the standard tests to a specific output
                // directory, you'll need to invoke with the first arg enclosed
                // in quotes (to avoid it being wildcard-expanded by the shell):
                //
                //    correctness_simd_op_check "*" /path/to/output
                //
                test.output_directory = argv[2];
            }

            bool success = test.test_all();

            // Compile a runtime for this target, for use in the static test.
            compile_standalone_runtime(test.output_directory + "simd_op_check_runtime.o", test.target);

            if (!success) {
                return 1;
            }
        }

        std::cout << "Success!\n";
        return 0;
    }

private:
    const Halide::Var x{"x"}, y{"y"};
};

}  // namespace Halide

#endif  // SIMD_OP_CHECK_H
