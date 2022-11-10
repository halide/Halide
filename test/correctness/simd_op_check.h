#ifndef SIMD_OP_CHECK_H
#define SIMD_OP_CHECK_H

#include "Halide.h"
#include "halide_test_dirs.h"
#include "test_sharding.h"

#include <fstream>

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
    std::mt19937 rng;

    Target target;

    ImageParam in_f32{Float(32), 1, "in_f32"};
    ImageParam in_f64{Float(64), 1, "in_f64"};
    ImageParam in_f16{Float(16), 1, "in_f16"};
    ImageParam in_bf16{BFloat(16), 1, "in_bf16"};
    ImageParam in_i8{Int(8), 1, "in_i8"};
    ImageParam in_u8{UInt(8), 1, "in_u8"};
    ImageParam in_i16{Int(16), 1, "in_i16"};
    ImageParam in_u16{UInt(16), 1, "in_u16"};
    ImageParam in_i32{Int(32), 1, "in_i32"};
    ImageParam in_u32{UInt(32), 1, "in_u32"};
    ImageParam in_i64{Int(64), 1, "in_i64"};
    ImageParam in_u64{UInt(64), 1, "in_u64"};

    const std::vector<ImageParam> image_params{in_f32, in_f64, in_f16, in_bf16, in_i8, in_u8, in_i16, in_u16, in_i32, in_u32, in_i64, in_u64};
    const std::vector<Argument> arg_types{in_f32, in_f64, in_f16, in_bf16, in_i8, in_u8, in_i16, in_u16, in_i32, in_u32, in_i64, in_u64};
    int W;
    int H;

    using Sharder = Halide::Internal::Test::Sharder;

    SimdOpCheckTest(const Target t, int w, int h)
        : target(t), W(w), H(h) {
        target = target
                     .with_feature(Target::NoBoundsQuery)
                     .with_feature(Target::NoAsserts)
                     .with_feature(Target::NoRuntime);
    }
    virtual ~SimdOpCheckTest() = default;

    void set_seed(int seed) {
        rng.seed(seed);
    }

    virtual bool can_run_code() const {
        // Assume we are configured to run wasm if requested
        // (we'll fail further downstream if not)
        if (target.arch == Target::WebAssembly) {
            return true;
        }
        // If we can (target matches host), run the error checking Halide::Func.
        Target host_target = get_host_target();
        bool can_run_the_code =
            (target.arch == host_target.arch &&
             target.bits == host_target.bits &&
             target.os == host_target.os);
        // A bunch of feature flags also need to match between the
        // compiled code and the host in order to run the code.
        for (Target::Feature f : {Target::SSE41, Target::AVX,
                                  Target::AVX2, Target::AVX512,
                                  Target::FMA, Target::FMA4, Target::F16C,
                                  Target::VSX, Target::POWER_ARCH_2_07,
                                  Target::ARMv7s, Target::NoNEON,
                                  Target::WasmSimd128}) {
            if (target.has_feature(f) != host_target.has_feature(f)) {
                can_run_the_code = false;
            }
        }
        return can_run_the_code;
    }

    virtual void compile_and_check(Func error, const std::string &op, const std::string &name, int vector_width, std::ostringstream &error_msg) {
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
        msg << op << " did not generate for target=" << target.to_string() << " vector_width=" << vector_width << ". Instead we got:\n";

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

    TestResult check_one(const std::string &op, const std::string &name, int vector_width, Expr e) {
        std::ostringstream error_msg;

        class HasInlineReduction : public Internal::IRVisitor {
            using Internal::IRVisitor::visit;
            void visit(const Internal::Call *op) override {
                if (op->call_type == Internal::Call::Halide) {
                    Internal::Function f(op->func);
                    if (f.has_update_definition()) {
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

        // The output to the pipeline is the maximum absolute difference as a double.
        RDom r_check(0, W, 0, H);
        Halide::Func error("error_" + name);
        error() = Halide::cast<double>(maximum(absd(f(r_check.x, r_check.y), f_scalar(r_check.x, r_check.y))));

        setup_images();
        compile_and_check(error, op, name, vector_width, error_msg);

        bool can_run_the_code = can_run_code();
        if (can_run_the_code) {
            Target run_target = target
                                    .without_feature(Target::NoRuntime)
                                    .without_feature(Target::NoAsserts)
                                    .without_feature(Target::NoBoundsQuery);

            error.infer_input_bounds({}, run_target);
            // Fill the inputs with noise
            for (auto p : image_params) {
                Halide::Buffer<> buf = p.get();
                if (!buf.defined()) continue;
                assert(buf.data());
                Type t = buf.type();
                // For floats/doubles, we only use values that aren't
                // subject to rounding error that may differ between
                // vectorized and non-vectorized versions
                if (t == Float(32)) {
                    buf.as<float>().for_each_value([&](float &f) { f = (rng() & 0xfff) / 8.0f - 0xff; });
                } else if (t == Float(64)) {
                    buf.as<double>().for_each_value([&](double &f) { f = (rng() & 0xfff) / 8.0 - 0xff; });
                } else if (t == Float(16)) {
                    buf.as<float16_t>().for_each_value([&](float16_t &f) { f = float16_t((rng() & 0xff) / 8.0f - 0xf); });
                } else {
                    // Random bits is fine
                    for (uint32_t *ptr = (uint32_t *)buf.data();
                         ptr != (uint32_t *)buf.data() + buf.size_in_bytes() / 4;
                         ptr++) {
                        // Never use the top four bits, to avoid
                        // signed integer overflow.
                        *ptr = ((uint32_t)rng()) & 0x0fffffff;
                    }
                }
            }
            Realization r = error.realize();
            double e = Buffer<double>(r[0])();
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
    virtual void setup_images() {
        for (auto p : image_params) {
            p.reset();

            const int alignment_bytes = 16;
            p.set_host_alignment(alignment_bytes);
            const int alignment = alignment_bytes / p.type().bytes();
            p.dim(0).set_min((p.dim(0).min() / alignment) * alignment);
        }
    }
    virtual bool test_all() {
        /* First add some tests based on the target */
        add_tests();

        Sharder sharder;
        bool success = true;
        for (size_t t = 0; t < tasks.size(); t++) {
            if (!sharder.should_run(t)) continue;
            const auto &task = tasks.at(t);
            auto result = check_one(task.op, task.name, task.vector_width, task.expr);
            std::cout << result.op << "\n";
            if (!result.error_msg.empty()) {
                std::cerr << result.error_msg;
                success = false;
            }
        }

        return success;
    }

    template<typename SIMDOpCheckT>
    static int main(int argc, char **argv) {
        Target host = get_host_target();
        Target hl_target = get_target_from_environment();
        printf("host is:      %s\n", host.to_string().c_str());
        printf("HL_TARGET is: %s\n", hl_target.to_string().c_str());

        SIMDOpCheckT test(hl_target);

        if (argc > 1) {
            test.filter = argv[1];
        }

        if (getenv("HL_SIMD_OP_CHECK_FILTER")) {
            test.filter = getenv("HL_SIMD_OP_CHECK_FILTER");
        }

        const int seed = argc > 2 ? atoi(argv[2]) : time(nullptr);
        std::cout << "simd_op_check test seed: " << seed << "\n";
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
            return -1;
        }

        printf("Success!\n");
        return 0;
    }

private:
    const Halide::Var x{"x"}, y{"y"};
};

}  // namespace Halide

#endif  // SIMD_OP_CHECK_H
