#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>

using namespace Halide;

extern "C" HALIDE_EXPORT_SYMBOL int dump_to_file(halide_buffer_t *input, const char *filename,
                                                 int desired_min, int desired_extent,
                                                 halide_buffer_t *) {
    // Note the final output buffer argument is unused.
    if (input->is_bounds_query()) {
        // Request some range of the input buffer
        input->dim[0].min = desired_min;
        input->dim[0].extent = desired_extent;
    } else {
        FILE *f = fopen(filename, "w");
        // Depending on the schedule, other consumers, etc, Halide may
        // have evaluated more than we asked for, so don't assume that
        // the min and extents match what we requested.
        int *base = ((int *)input->host) - input->dim[0].min;
        for (int i = desired_min; i < desired_min + desired_extent; i++) {
            fprintf(f, "%d\n", base[i]);
        }
        fclose(f);
    }

    return 0;
}

bool check_result() {
    // Check the right thing happened
    const char *correct =
        "0\n"
        "1\n"
        "4\n"
        "9\n"
        "16\n"
        "25\n"
        "36\n"
        "49\n"
        "64\n"
        "81\n";

    std::string path = Internal::get_test_tmp_dir() + "halide_test_extern_consumer.txt";
    Internal::assert_file_exists(path);
    FILE *f = fopen(path.c_str(), "r");
    char result[1024];
    size_t bytes_read = fread(&result[0], 1, 1023, f);
    result[bytes_read] = 0;
    fclose(f);

    if (strncmp(result, correct, 1023)) {
        printf("Incorrect output: %s\n", result);
        return false;
    }

    return true;
}

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly JIT does not support passing arbitrary pointers to/from HalideExtern code.\n");
        return 0;
    }

    // Define a pipeline that dumps some squares to a file using an
    // external consumer stage.
    Func source;
    Var x;
    source(x) = x * x;

    Param<int> min, extent;
    Param<const char *> filename;

    Func sink;
    std::vector<ExternFuncArgument> args;
    args.push_back(source);
    args.push_back(filename);
    args.push_back(min);
    args.push_back(extent);
    sink.define_extern("dump_to_file", args, Int(32), 0);

    // Extern stages still have an outermost var.
    source.compute_at(sink, Var::outermost());

    sink.compile_jit();

    // Dump the first 10 squares to a file
    std::string path = Internal::get_test_tmp_dir() + "halide_test_extern_consumer.txt";
    Internal::ensure_no_file_exists(path);

    filename.set(path.c_str());
    min.set(0);
    extent.set(10);
    sink.realize();

    if (!check_result())
        return -1;

    // Test ImageParam ExternFuncArgument via passed in image.
    Buffer<int32_t> buf = source.realize({10});
    ImageParam passed_in(Int(32), 1);
    passed_in.set(buf);

    Func sink2;
    std::vector<ExternFuncArgument> args2;
    args2.push_back(passed_in);
    args2.push_back(filename);
    args2.push_back(min);
    args2.push_back(extent);
    sink2.define_extern("dump_to_file", args2, Int(32), 0);

    sink2.realize();

    if (!check_result())
        return -1;

    printf("Success!\n");
    return 0;
}
