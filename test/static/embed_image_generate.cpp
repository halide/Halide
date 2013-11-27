#include <Halide.h>
#include <stdio.h>

using std::vector;

using namespace Halide;

int main(int argc, char **argv) {
    ImageParam input(Float(32), 3);

    Image<float> matrix(3, 3);

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            matrix(i, j) = 0.0f;
        }
    }
    // Make the matrix a flip-channels-and-multiply-by-0.5 so that this is easy to test
    matrix(2, 0) = matrix(1, 1) = matrix(0, 2) = 0.5f;

    Func f;
    Var x, y, c;
    RDom j(0, 3);
    f(x, y, c) = sum(matrix(j, c) * input(x, y, j));

    // Don't include the matrix as an argument. Instead it will be
    // embedded in the object file.
    f.compile_to_file("embed_image", input);
    return 0;
}
