#include <stdio.h>
#include <Halide.h>

using namespace Halide;

int main(int argc, char **argv) {
    // Compute the variance of a 3x3 patch about each pixel
    RDom r(-1, 3, -1, 3);

    // Test a complex summation
    Func input;
    Var x, y;
    input(x, y) = cast<float>(x*y+1);

    Func local_variance;
    Expr input_val = input(x+r.x, y+r.y);
    Expr local_mean = sum(input_val) / 9.0f;
    local_variance(x, y) = sum(input_val*input_val)/81.0f - local_mean*local_mean;    

    Image<float> result = local_variance.realize(10, 10);
    
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            float local_mean = 0;
            float local_variance = 0;
            for (int rx = -1; rx < 2; rx++) {
                for (int ry = -1; ry < 2; ry++) {
                    float val = (x+rx)*(y+ry)+1;
                    local_mean += val;
                    local_variance += val*val;
                }
            }
            local_mean /= 9.0f;
            float correct = local_variance/81.0f - local_mean*local_mean;
            float r = result(x, y);
            float delta = correct - r;
            if (delta < -0.001 || delta > 0.001) {
                printf("result(%d, %d) was %f instead of %f\n", x, y, r, correct);
                return -1;
            }
        }
    }

    
    // Test the other reductions
    Func local_product, local_max, local_min;
    local_product(x, y) = product(input_val);
    local_max(x, y) = maximum(input_val);
    local_min(x, y) = minimum(input_val);

    // Try a separable form of minimum too, so we test two reductions
    // in one pipeline
    Func min_x, min_y;
    RDom kx(-1, 3), ky(-1, 3);
    min_x(x, y) = minimum(input(x+kx, y));
    min_y(x, y) = minimum(min_x(x, y+ky));

    
    Image<float> prod_im = local_product.realize(10, 10);
    Image<float> max_im = local_max.realize(10, 10);
    Image<float> min_im = local_min.realize(10, 10);
    Image<float> min_im_separable = min_y.realize(10, 10);

    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            float correct_prod = 1.0f;
            float correct_min = 1e10f;
            float correct_max = -1e10f;
            for (int rx = -1; rx < 2; rx++) {
                for (int ry = -1; ry < 2; ry++) {
                    float val = (x+rx)*(y+ry)+1;
                    correct_prod *= val;
                    correct_min = std::min(correct_min, val);
                    correct_max = std::max(correct_max, val);                   
                }
            }

            float delta;
            delta = (correct_prod+10)/(prod_im(x, y)+10);
            if (delta < 0.99 || delta > 1.01) {
                printf("prod_im(%d, %d) = %f instead of %f\n", x, y, prod_im(x, y), correct_prod);
                return -1;
            }

            delta = correct_min - min_im(x, y);
            if (delta < -0.001 || delta > 0.001) {
                printf("min_im(%d, %d) = %f instead of %f\n", x, y, min_im(x, y), correct_min);
                return -1;
            }

            delta = correct_min - min_im_separable(x, y);
            if (delta < -0.001 || delta > 0.001) {
                printf("min_im(%d, %d) = %f instead of %f\n", x, y, min_im_separable(x, y), correct_min);
                return -1;
            }

            delta = correct_max - max_im(x, y);
            if (delta < -0.001 || delta > 0.001) {
                printf("max_im(%d, %d) = %f instead of %f\n", x, y, max_im(x, y), correct_max);
                return -1;
            }
        }
    }


    printf("Success!\n");
    return 0;
    
}
