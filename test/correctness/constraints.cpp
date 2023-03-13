#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>
#include <fstream>

using namespace Halide;

bool error_occurred = false;
void my_error_handler(JITUserContext *user_context, const char *msg) {
    // printf("%s\n", msg);
    error_occurred = true;
}

int basic_constraints() {
    Func f, g;
    Var x, y;
    ImageParam param(Int(32), 2);
    Buffer<int> image1(128, 73);
    Buffer<int> image2(144, 23);

    f(x, y) = param(x, y) * 2;

    param.dim(0).set_bounds(0, 128);

    f.jit_handlers().custom_error = my_error_handler;

    // This should be fine
    param.set(image1);
    error_occurred = false;
    f.realize({20, 20});

    if (error_occurred) {
        printf("Error incorrectly raised\n");
        return 1;
    }
    // This should be an error, because dimension 0 of image 2 is not from 0 to 128 like we promised
    param.set(image2);
    error_occurred = false;
    f.realize({20, 20});

    if (!error_occurred) {
        printf("Error incorrectly not raised\n");
        return 1;
    }

    // Now try constraining the output buffer of a function
    g(x, y) = x * y;
    g.jit_handlers().custom_error = my_error_handler;
    g.output_buffer().dim(0).set_stride(2);
    error_occurred = false;
    g.realize(image1);
    if (!error_occurred) {
        printf("Error incorrectly not raised when constraining output buffer\n");
        return 1;
    }

    Func h;
    h(x, y) = x * y;
    h.jit_handlers().custom_error = my_error_handler;
    h.output_buffer()
        .dim(0)
        .set_stride(1)
        .set_bounds(0, ((h.output_buffer().dim(0).extent()) / 8) * 8)
        .dim(1)
        .set_bounds(0, image1.dim(1).extent());
    error_occurred = false;
    h.realize(image1);

    std::string assembly_file = Internal::get_test_tmp_dir() + "h.s";
    Internal::ensure_no_file_exists(assembly_file);

    // Also check it compiles ok without an inferred argument list
    h.compile_to_assembly(assembly_file, {image1}, "h");
    if (error_occurred) {
        printf("Error incorrectly raised when constraining output buffer\n");
        return 1;
    }

    Internal::assert_file_exists(assembly_file);

    return 0;
}

std::string load_file_to_string(const std::string &filename) {
    std::stringstream contents;
    std::ifstream file(filename);
    std::string line;
    while (std::getline(file, line)) {
        contents << line << "\n";
    }

    return contents.str();
}

int alignment_constraints() {
    Var x, y;
    ImageParam p_aligned(Float(32), 2);
    ImageParam p_unaligned(Float(32), 2);

    const int alignment = 4;
    p_aligned.set_host_alignment(alignment * sizeof(float));
    p_aligned.dim(0).set_min((p_aligned.dim(0).min() / alignment) * alignment);
    p_aligned.dim(0).set_extent((p_aligned.dim(0).extent() / alignment) * alignment);
    p_aligned.dim(1).set_stride((p_aligned.dim(1).stride() / alignment) * alignment);

    Func aligned, unaligned;
    aligned(x, y) = p_aligned(x, y);
    unaligned(x, y) = p_unaligned(x, y);

    aligned.vectorize(x, 4);
    unaligned.vectorize(x, 4);

    aligned.output_buffer().dim(0).set_min(0);
    unaligned.output_buffer().dim(0).set_min(0);

    Target target = get_jit_target_from_environment();
    target.set_feature(Target::NoRuntime);

    std::string unaligned_ll_file = Internal::get_test_tmp_dir() + "unaligned.ll";
    Internal::ensure_no_file_exists(unaligned_ll_file);
    unaligned.compile_to_llvm_assembly(unaligned_ll_file, {p_unaligned}, "unaligned", target);
    std::string unaligned_code = load_file_to_string(unaligned_ll_file);
    if (unaligned_code.find("align 16") != std::string::npos) {
        printf("Found aligned load from unaligned buffer!\n");
        return 1;
    }

    std::string aligned_ll_file = Internal::get_test_tmp_dir() + "aligned.ll";
    Internal::ensure_no_file_exists(aligned_ll_file);
    aligned.compile_to_llvm_assembly(aligned_ll_file, {p_aligned}, "aligned", target);
    std::string aligned_code = load_file_to_string(aligned_ll_file);
    if (aligned_code.find("align 16") == std::string::npos) {
        printf("Did not find aligned load from aligned buffer!\n");
        return 1;
    }

    return 0;
}

int unstructured_constraints() {
    Func f, g;
    Var x, y;
    ImageParam param(Int(32), 2);
    Buffer<int> image1(128, 73);
    Buffer<int> image2(144, 23);

    f(x, y) = param(x, y) * 2;

    Param<int> required_min, required_extent;
    required_min.set(0);
    required_extent.set(128);

    Pipeline pf(f);
    pf.add_requirement(param.dim(0).min() == required_min && param.dim(0).extent() == required_extent,
                       "Custom message:", param.dim(0).min(), param.dim(0).max());

    pf.jit_handlers().custom_error = my_error_handler;

    // This should be fine
    param.set(image1);
    error_occurred = false;
    pf.realize({20, 20});

    if (error_occurred) {
        printf("Error incorrectly raised\n");
        return 1;
    }
    // This should be an error, because dimension 0 of image 2 is not from 0 to 128 like we promised
    param.set(image2);
    error_occurred = false;
    pf.realize({20, 20});

    if (!error_occurred) {
        printf("Error incorrectly not raised\n");
        return 1;
    }

    // Now try constraining the output buffer of a function
    g(x, y) = x * y;

    Pipeline pg(g);

    Param<int> required_stride;
    required_stride.set(2);
    pg.add_requirement(g.output_buffer().dim(0).stride() == required_stride);
    pg.jit_handlers().custom_error = my_error_handler;

    error_occurred = false;
    pg.realize(image1);
    if (!error_occurred) {
        printf("Error incorrectly not raised when constraining output buffer\n");
        return 1;
    }

    Func h;
    h(x, y) = x * y;

    Pipeline ph(h);
    ph.jit_handlers().custom_error = my_error_handler;
    ph.add_requirement(h.output_buffer().dim(0).stride() == 1);
    ph.add_requirement(h.output_buffer().dim(0).min() == 0);
    ph.add_requirement(h.output_buffer().dim(0).extent() % 8 == 0);
    ph.add_requirement(h.output_buffer().dim(1).min() == 0);

    ph.add_requirement(h.output_buffer().dim(1).extent() == image1.dim(1).extent());

    error_occurred = false;
    h.realize(image1);

    if (error_occurred) {
        printf("Error incorrectly raised when constraining output buffer\n");
        return 1;
    }

    return 0;
}

int main(int argc, char **argv) {
    int result;

    result = basic_constraints();
    if (result != 0) return result;

    result = alignment_constraints();
    if (result != 0) return result;

    result = unstructured_constraints();
    if (result != 0) return result;

    printf("Success!\n");
    return 0;
}
