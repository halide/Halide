#include "Halide.h"
#include <stdio.h>

using namespace Halide;

#ifdef _MSC_VER
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

extern "C" DLLEXPORT
int dump_to_file(buffer_t *input, const char *filename,
                 int desired_min, int desired_extent,
                 buffer_t *) {
    // Note the final output buffer argument is unused.
    if (input->host == NULL) {
        // Request some range of the input buffer
        input->min[0] = desired_min;
        input->extent[0] = desired_extent;
    } else {
        FILE *f = fopen(filename, "w");
        // Depending on the schedule, other consumers, etc, Halide may
        // have evaluated more than we asked for, so don't assume that
        // the min and extents match what we requested.
        int *base = ((int *)input->host) - input->min[0];
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

    FILE *f = fopen("halide_test_extern_consumer.txt", "r");
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
    Target target = get_jit_target_from_environment();
    if (target.has_feature(Target::JavaScript)) {
        // TODO: Add JavaScript extern support.
        printf("Skipping extern_consumer test for JavaScript as it uses a C extern function.\n");
        return 0;
    }

    // Define a pipeline that dumps some squares to a file using an
    // external consumer stage.
    Func source;
    Var x;
    source(x) = x*x;

    Param<int> min, extent;
    Param<const char *> filename;

    Func sink;
    std::vector<ExternFuncArgument> args;
    args.push_back(source);
    args.push_back(filename);
    args.push_back(min);
    args.push_back(extent);
    sink.define_extern("dump_to_file", args, Int(32), 0);

    source.compute_root();

    sink.compile_jit();

    // Dump the first 10 squares to a file
    filename.set("halide_test_extern_consumer.txt");
    min.set(0);
    extent.set(10);
    sink.realize();

    if (!check_result())
        return -1;

    // Test ImageParam ExternFuncArgument via passed in image.
    Image<int32_t> buf = source.realize(10);
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
