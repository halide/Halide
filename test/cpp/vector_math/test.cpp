#include <Halide.h>
#include <math.h>
#include <sys/time.h>

using namespace Halide;

// Make some functions for turning types into strings
template<typename A>
const char *string_of_type();

#define DECL_SOT(name)                                          \
    template<>                                                  \
    const char *string_of_type<name>() {return #name;}          

DECL_SOT(uint8_t);    
DECL_SOT(int8_t);    
DECL_SOT(uint16_t);    
DECL_SOT(int16_t);    
DECL_SOT(uint32_t);    
DECL_SOT(int32_t);    
DECL_SOT(float);    
DECL_SOT(double);    

double currentTime() {
    timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec * 1000.0 + t.tv_usec / 1000.0f;
}

template<typename A>
A mod(A x, A y);

template<>
float mod(float x, float y) {
    return fmod(x, y);
}

template<>
double mod(double x, double y) {
    return fmod(x, y);
}

template<typename A>
A mod(A x, A y) {
    return x % y;
}
    

template<typename A>
bool test(int vec_width) {
    const int W = 3200;
    const int H = 16;
    
    Image<A> input(W+16, H+16);
    for (int y = 0; y < H+16; y++) {
        for (int x = 0; x < W+16; x++) {
            input(x, y) = (A)(rand()*0.125 + 1.0);
        }
    }
    Var x, y;

    // Add
    Func f1;
    f1(x, y) = input(x, y) + input(x+1, y);
    f1.vectorize(x, vec_width);
    Image<A> im1 = f1.realize(W, H);
    
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            A correct = input(x, y) + input(x+1, y);
            if (im1(x, y) != correct) {
                printf("im1(%d, %d) = %f instead of %f\n", x, y, (double)(im1(x, y)), (double)(correct));
                return false;
            }
        }
    }
    
    // Sub
    Func f2;
    f2(x, y) = input(x, y) - input(x+1, y);
    f2.vectorize(x, vec_width);
    Image<A> im2 = f2.realize(W, H);
    
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            A correct = input(x, y) - input(x+1, y);
            if (im2(x, y) != correct) {
                printf("im2(%d, %d) = %f instead of %f\n", x, y, (double)(im2(x, y)), (double)(correct));
                return false;
            }
        }
    }

    // Mul
    Func f3;
    f3(x, y) = input(x, y) * input(x+1, y);
    f3.vectorize(x, vec_width);
    Image<A> im3 = f3.realize(W, H);
    
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            A correct = input(x, y) * input(x+1, y);
            if (im3(x, y) != correct) {
                printf("im3(%d, %d) = %f instead of %f\n", x, y, (double)(im3(x, y)), (double)(correct));
                return false;
            }
        }
    }

    // select
    Func f4;
    f4(x, y) = select(input(x, y) > input(x+1, y), input(x+2, y), input(x+3, y));
    f4.vectorize(x, vec_width);
    Image<A> im4 = f4.realize(W, H);
    
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            A correct = input(x, y) > input(x+1, y) ? input(x+2, y) : input(x+3, y);
            if (im4(x, y) != correct) {
                printf("im4(%d, %d) = %f instead of %f\n", x, y, (double)(im4(x, y)), (double)(correct));
                return false;
            }
        }
    }


    // Gather
    Func f5;
    Expr xCoord = clamp(cast<int>(input(x, y)), 0, W-1);
    Expr yCoord = clamp(cast<int>(input(x+1, y)), 0, H-1);
    f5(x, y) = input(xCoord, yCoord);
    f5.vectorize(x, vec_width);
    Image<A> im5 = f5.realize(W, H);
    
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int xCoord = (int)(input(x, y));
            if (xCoord >= W) xCoord = W-1;
            if (xCoord < 0) xCoord = 0;

            int yCoord = (int)(input(x+1, y));
            if (yCoord >= H) yCoord = H-1;
            if (yCoord < 0) yCoord = 0;

            A correct = input(xCoord, yCoord);

            if (im5(x, y) != correct) {
                printf("im5(%d, %d) = %f instead of %f\n", x, y, (double)(im5(x, y)), (double)(correct));
                return false;
            }
        }
    }

    // Scatter
    Func f6;
    RDom i(0, H);
    // Set one entry in each row high
    xCoord = clamp(cast<int>(input(2*i, i)), 0, W-1);
    f6(x, y) = 0;
    f6(xCoord, i) = 1;

    f6.vectorize(x, vec_width);

    Image<int> im6 = f6.realize(W, H);
    
    for (int y = 0; y < H; y++) {
        int xCoord = (int)(input(2*y, y));
        if (xCoord >= W) xCoord = W-1;
        if (xCoord < 0) xCoord = 0;
        for (int x = 0; x < W; x++) {
            int correct = x == xCoord ? 1 : 0;
            if (im6(x, y) != correct) {
                printf("im6(%d, %d) = %d instead of %d\n", x, y, im6(x, y), correct);
                return false;
            }
        }
    }
    
    // Min/max
    Func f7;
    f7(x, y) = clamp(input(x, y), cast<A>(10), cast<A>(20));
    f7.vectorize(x, vec_width);
    Image<A> im7 = f7.realize(W, H);
    
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (im7(x, y) < (A)10 || im7(x, y) > (A)20) {
                printf("im7(%d, %d) = %f instead of %f\n", x, y, (double)(im7(x, y)));
                return false;
            }
        }
    }

    // Extern function call
    Func f8;
    f8(x, y) = pow(1.1f, cast<float>(input(x, y)));
    f8.vectorize(x, vec_width);
    Image<float> im8 = f8.realize(W, H);
    
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float correct = powf(1.1f, (float)input(x, y));
            if (im8(x, y) != correct) {
                printf("im8(%d, %d) = %f instead of %f\n", x, y, im8(x, y));
                return false;
            }
        }
    }
    
    // Div
    Func f9;
    f9(x, y) = input(x, y) / clamp(input(x+1, y), cast<A>(1), cast<A>(3));
    f9.vectorize(x, vec_width);
    Image<A> im9 = f9.realize(W, H);
    
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            A clamped = input(x+1, y);
            if (clamped < (A)1) clamped = (A)1;
            if (clamped > (A)3) clamped = (A)3;
            A correct = input(x, y) / clamped;
            if (im9(x, y) != correct) {
                printf("im9(%d, %d) = %f instead of %f\n", x, y, (double)(im9(x, y)), (double)(correct));
                return false;
            }
        }
    }

    // Interleave
    Func f11;
    f11(x, y) = select((x%2)==0, input(x/2, y), input(x/2, y+1));
    f11.vectorize(x, vec_width);
    Image<A> im11 = f11.realize(W, H);
    
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            A correct = ((x%2)==0) ? input(x/2, y) : input(x/2, y+1);
            if (im11(x, y) != correct) {
                printf("im11(%d, %d) = %f instead of %f\n", x, y, (double)(im11(x, y)), (double)(correct));
                return false;
            }
        }
    }

    return true;
}

int main(int argc, char **argv) {

    bool ok = true;

    // Only native vector widths - llvm doesn't handle others well
    ok = ok && test<float>(4);
    ok = ok && test<float>(8);
    ok = ok && test<double>(2);
    ok = ok && test<uint8_t>(16);
    ok = ok && test<int8_t>(16);
    ok = ok && test<uint16_t>(8);
    ok = ok && test<int16_t>(8);
    ok = ok && test<uint32_t>(4);
    ok = ok && test<int32_t>(4);

    if (!ok) return -1;
    printf("Success!\n");
    return 0;
}
