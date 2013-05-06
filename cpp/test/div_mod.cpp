#include <stdio.h>
#include <Halide.h>

#include <math.h>

using namespace Halide;

// Test program to check basic arithmetic.
// Pseudo-random numbers are generated and arithmetic operations performed on them.
// To ensure that the extremes of the data values are included in testing, the upper
// left corner of each matrix contains the extremes.

// The code uses 64 bit arithmetic to ensure that results are correct in 32 bits and fewer,
// even if overflow occurs.

// Dimensions of the test data, and rate of salting with extreme values (1 in SALTRATE)
#define WIDTH 2048
#define HEIGHT 2048
#define SALTRATE 50
// Portion of the test data to use for testing the simplifier
# define SWIDTH 32
# define SHEIGHT 2048

// Generate poor quality pseudo random numbers.
// For reproducibility, the array indices are used as the seed for each
// number generated.  The algorithm simply multiplies the seeds by large
// primes and combines them together, then multiplies by additional large primes.
// We don't want to use primes that are close to powers of 2 because they dont
// randomise the bits.
//
// unique: Use different values to get unique data in each array.
// i, j: Coordinates for which the value is being generated.
uint64_t ubits(int unique, int i, int j) {
    uint64_t bits, mi, mj, mk, ml, mu;
    mi = 982451653; // 50 M'th prime
    mj = 776531491; // 40 M'th prime
    mk = 573259391; // 30 M'th prime
    ml = 373587883; // 20 M'th prime
    mu = 275604541; // 15 M'th prime
    // Each of the above primes is at least 10^8 i.e. at least 24 bits
    // so we are assured that the initial value computed below occupies 64 bits
    // and then the subsequent operations help ensure that every bit is affected by
    // all three inputs.
    
    bits = ((unique * mu + i) * mi + j) * mj;  // All multipliers are prime
    bits = (bits ^ (bits >> 32)) * mk;
    bits = (bits ^ (bits >> 32)) * ml;
    bits = (bits ^ (bits >> 32)) * mi;
    bits = (bits ^ (bits >> 32)) * mu;
    return bits;
}

// Template to avoid autological comparison errors when comparing unsigned values for < 0
template <typename T>
bool less_than_zero(T val) {
    return (val < 0);
}

template <>
bool less_than_zero<unsigned long long>(unsigned long long val) {
    return false;
}

template <>
bool less_than_zero<unsigned long>(unsigned long val) {
    return false;
}

template <>
bool less_than_zero<unsigned int>(unsigned int val) {
    return false;
}

template <>
bool less_than_zero<unsigned short>(unsigned short val) {
    return false;
}

template <>
bool less_than_zero<unsigned char>(unsigned char val) {
    return false;
}

template<typename T,typename BIG,int bits>
BIG maximum() {
    Type t = type_of<T>();
    t.bits = bits;
    
    if (t.is_float()) {
        return (BIG) 1.0;
    }
    if (t.is_uint()) {
        uint64_t max;
        max = 0;
        max = ~max;
        if (t.bits < 64)
            max = (((uint64_t) 1) << t.bits) - 1;
        return (BIG) max;
    }
    if (t.is_int()) {
        uint64_t umax;
        umax = (((uint64_t) 1) << (t.bits - 1)) - 1;
        return (BIG) umax;
    }
    assert(0);
    return (BIG) 1;
}

template<typename T,typename BIG,int bits>
BIG minimum() {
    Type t = type_of<T>();
    t.bits = bits;
    
    if (t.is_float()) {
        return (BIG) 0.0;
    }
    if (t.is_uint()) {
        return (BIG) 0;
    }
    if (t.is_int()) {
        uint64_t umax;
        BIG min;
        umax = (((uint64_t) 1) << (t.bits - 1)) - 1;
        min = umax;
        min = -min - 1;
        return min;
    }
    assert(0);
    return (BIG) 0; 
}

// Construct an image for testing.
// Contents are poor quality pseudo-random numbers in the natural range for the specified type.
// The top left corner contains one of two patterns.  (Remember that first coordinate is column in Halide)
//  min  max      OR      min  max
//  min  max              max  min
// The left pattern occurs when unique is odd; the right pattern when unique is even.

template<typename T,typename BIG,int bits>
Image<T> init(Type t, int unique, int width, int height) {
    if (width < 2) width = 2;
    if (height < 2) height = 2;
        
    Image<T> result(width, height);
    
    assert(t.bits == bits);
        
    if (t.is_int()) {
        // Signed integer type with specified number of bits.
        int64_t max, min, neg, v, vsalt;
        max = maximum<T,int64_t,bits>();
        min = minimum<T,int64_t,bits>();
        neg = (~((int64_t) 0)) ^ max;  // The bits that should all be 1 for negative numbers.
        for (int i = 0; i < width; i++) {
            for (int j = 0; j < height; j++) {
                v = (int64_t) (ubits(unique,i,j));      
                if (v < 0)
                    v |= neg; // Make all the high bits one
                else
                    v &= max;
                // Salting with extreme values
                vsalt = (int64_t) (ubits(unique|0x100,i,j));
                if (vsalt % SALTRATE == 0) {
                    if (vsalt & 0x1000000) {
                        v = max;
                    } else {
                        v = min;
                    }
                }
                result(i,j) = v;
            }
        }
        result(0,0) = min;
        result(1,0) = max;
        result(0,1) = (unique & 1) ? min : max;
        result(1,1) = (unique & 1) ? max : min;
    }
    else if (t.is_uint()) {
        uint64_t max, v, vsalt;
        max = maximum<T,BIG,bits>();
        for (int i = 0; i < width; i++) {
            for (int j = 0; j < height; j++) {
                v = ubits(unique,i,j) & max;
                // Salting with extreme values
                vsalt = (int64_t) (ubits(unique|0x100,i,j));
                if (vsalt % SALTRATE == 0) {
                    if (vsalt & 0x1000000) {
                        v = max;
                    } else {
                        v = 0;
                    }
                }
                result(i,j) = v;
            }
        }
        result(0,0) = 0;
        result(1,0) = max;
        result(0,1) = (unique & 1) ? 0 : max;
        result(1,1) = (unique & 1) ? max : 0;
    }
    else if (t.is_float()) {
        uint64_t uv, vsalt;
        uint64_t max = 0xffffffffffffffff;
        double v;
        for (int i = 0; i < width; i++) {
            for (int j = 0; j < height; j++) {
                uv = ubits(unique,i,j);
                v = (((double) uv) / ((double) (max))) * 2.0 - 1.0;
                // Salting with extreme values
                vsalt = (int64_t) (ubits(unique|0x100,i,j));
                if (vsalt % SALTRATE == 0) {
                    if (vsalt & 0x1000000) {
                        v = 1.0;
                    } else {
                        v = 0.0;
                    }
                }
                result(i,j) = v;
            }
        }
        result(0,0) = 0.0;
        result(1,0) = 1.0;
        result(0,1) = (unique & 1) ? 0.0 : 1.0;
        result(1,1) = (unique & 1) ? 1.0 : 0.0;
    }
    else {
        printf ("Unknown data type in init.\n");
    }
        
    return result;
}

// halide_div is the Halide definition of division, expression in BIG type 
// to avoid overflow issues.  Type cast the result to T.
template<typename T,typename BIG>
BIG halide_div(BIG a, BIG b) {
    Type t = type_of<T>();
    if (t.is_uint() || t.is_float()) {
        return a / b;
    }
    BIG q = a/b;
    if (b > 0 && q*b > a) q--;
    if (less_than_zero(b) && q*b < a) q--;
    return q;
}

#define DIV_METHOD 4

// new_div is a new implementation of division, intended to produce the same results
// as halide_div for various types T.
template<typename T>
T new_div(T a, T b) {
    Type t = type_of<T>();
    if (t.is_uint() || t.is_float()) {
        return a / b;
    }
    // Correction.  Ensure that the quotient is rounded towards -infinity.
    T pre, post;
    // Method 1. Explicit pre and post correction based on signs of a and b.
#if DIV_METHOD==1
    if (a < 0 && b < 0) { pre = 0; post = 0; }
    if (a >= 0 && b >= 0) { pre = 0; post = 0; }
    if (a >= 0 && b < 0) { pre = a > 0 ? -1 : 0; post = pre; } // Note a == 0 has no correction.
    if (a < 0 && b >= 0) { pre = 1; post = -1; }
#endif
    // Method 2. Logical simplification
#if DIV_METHOD==2
    if ((a < 0) ^ (b < 0)) { post = -1; } else { post = 0; }
    if (a == 0) { post = 0; }
    if (a < 0) { pre = -post; } else { pre = post; }
#endif
    // Method 3. Bit manipulations.
#if DIV_METHOD==3
    T axorb = a ^ b;
    T anotzero = a | (T) (-a);
    post = (axorb >> (t.bits-1)) & (anotzero >> (t.bits-1));
    T amask = a >> (t.bits-1);
    pre = (T)(post ^ amask) - amask;
#endif
    // Method 4. Bit manipulations and select.  select is written in C notation.
#if DIV_METHOD==4
    T axorb = a ^ b;
    post = a != 0 ? ((axorb) >> (t.bits-1)) : 0;
    pre = less_than_zero(a) ? -post : post;
#endif
    T num = a + pre;
    T quo = num / b;
    T result = quo + post;
    return result;
}

// halide_mod is the Halide definition of mod expression in BIG type to
// avoid overflow issues.  Cast it to type T to use the result.
template<typename T,typename BIG>
BIG halide_mod(BIG a, BIG b) {
    Type t = type_of<T>();
    if (t.is_uint()) {
        return a % b;
    }
    if (t.is_float()) {
        return ((double)a - double(b)*floor(double(a)/double(b)));
    }
    BIG rem = (a % b + b) % b; // Halide definition of remainder
    return rem;
}

#define MOD_METHOD 4

// new_mod is a new implementation of mod intended to compute the same
// result as halide_mod for all values of type T.
template<typename T>
T new_mod(T a, T b) {
    Type t = type_of<T>();
    if (t.is_uint()) {
        return a % b;
    }
    if (t.is_float()) {
        return ((double)a - double(b)*floor(double(a)/double(b)));
    }
    T rem = a % b;
    // Correction.  Ensures that remainder has the same sign as b.
    // Method 1.  Explicit corrections when signs are opposite.
#if MOD_METHOD==1
    if (rem < 0 && b > 0) { rem = rem + b; }
    if (rem > 0 && b < 0) { rem = rem + b; } // Note rem==0 has no correction applied.
#endif
    // Method 2.
#if MOD_METHOD==2
    if (((rem < 0) ^ (b < 0)) & (rem != 0)) { rem = rem + b; }
#endif
    // Method 3.  Using bit manipulations.
    // Mask should be -1 in the cases:
    // (rem ^ b) is negative and (rem | -rem) is negative.
    // Negative test is implemented by arithmetic right shift
    // by the word size less 1 bit. 
#if MOD_METHOD==3
    T mask = (((T)(rem ^ b)) >> (t.bits-1)) & (((T)(rem | -rem)) >> (t.bits-1));
    rem = rem + (b & mask);
#endif
    // Method 4.  Using bit manipulations and select.
#if MOD_METHOD==4
    rem = rem + (rem != 0 && less_than_zero(rem ^ b) ? b : 0);
#endif
    return rem;
}

// division tests division operations.
// BIG should be uint64_t, int64_t or double as appropriate.
// T should be a type known to Halide.
template<typename T,typename BIG,int bits>
bool division() {
    int i, j;
    Type t = type_of<T>();
    BIG minval = minimum<T,BIG,bits>();
    bool success = true;
    
    std::cout << "Test division of " << t << '\n';
    t.bits = bits; // Override the bits
    
    // The parameter bits can be used to control the maximum data value.
    Image<T> a = init<T,BIG,bits>(t, 1, WIDTH, HEIGHT);
    Image<T> b = init<T,BIG,bits>(t, 2, WIDTH, HEIGHT);
    Image<T> out(WIDTH,HEIGHT);
    
    // Filter the input values for the operation to be tested.
    // Cannot divide by zero, so remove zeroes from b.
    // Also, cannot divide the most negative number by -1.
    for (i = 0; i < WIDTH; i++) {
        for (j = 0; j < HEIGHT; j++) {
            if (b(i,j) == 0) {
                b(i,j) = 1; // Replace zero with one
            }
            if (a(i,j) == minval && less_than_zero(minval) && b(i,j) == -1) {
                a(i,j) = a(i,j) + 1; // Fix it into range.
            }
        }
    }
    
    // Compute division result and check it.
    Func f;
    f = a / b;  // Using Halide division operation.
    f.realize(out);
    
    int ecount = 0;
    for (i = 0; i < WIDTH; i++) {
        for (j = 0; j < HEIGHT; j++) {
            T v = halide_div<T,BIG>(a(i,j), b(i,j));
            if (v != out(i,j) && (ecount++) < 10) {
                printf ("halide division (%d / %d) yielded %d; expected %d\n", a(i,j), b(i,j), out(i,j), v);
                success = false;
            }
        }
    }
    
    // Explicit checks of the simplifier
    ecount = 0;
    for (i = 0; i < std::min(SWIDTH,WIDTH); i++) {
        for (j = 0; j < std::min(SHEIGHT,HEIGHT); j++) {
            T arg_a = a(i,j);
            T arg_b = b(i,j);
            T v = halide_div<T,BIG>(arg_a, arg_b);
            Expr in_e = cast<T>((int) arg_a) / cast<T>((int) arg_b);
            Expr e = simplify(in_e);
            Expr eout = cast<T>((int) v);
            if (! Internal::equal(e, eout) && (ecount++) < 10) {
                std::cout << "simplify(" << in_e << ") yielded " << e << "; expected " << eout << "\n";
                success = false;
            }
        }
    }
    
    /* Test alternative C implementation to match Halide definition. */
    ecount = 0;
    for (i = 0; i < WIDTH; i++) {
        for (j = 0; j < HEIGHT; j++) {
            T w = halide_div<T,BIG>(a(i,j), b(i,j));
            T u = new_div<T>(a(i,j), b(i,j));
            if (u != w && ecount++ < 10) {
                printf ("new_div(%d, %d) yielded %d; expected %d\n", a(i,j), b(i,j), u, w);
            }
        }
    }

    return success;
}

// mod tests mod operations.
// BIG should be uint64_t or int64_t as appropriate.
// T should be a type known to Halide.
template<typename T,typename BIG,int bits>
bool mod() {
    int i, j;
    Type t = type_of<T>();
    BIG minval = minimum<T,BIG,bits>();
    bool success = true;
    
    std::cout << "Test mod of " << t << '\n';
    t.bits = bits; // Override the bits
    
    // The parameter bits can be used to control the maximum data value.
    Image<T> a = init<T,BIG,bits>(t, 1, WIDTH, HEIGHT);
    Image<T> b = init<T,BIG,bits>(t, 2, WIDTH, HEIGHT);
    Image<T> out(WIDTH,HEIGHT);
    
    // Filter the input values for the operation to be tested.
    // Cannot divide by zero, so remove zeroes from b.
    // Also, cannot divide the most negative number by -1.
    for (i = 0; i < WIDTH; i++) {
        for (j = 0; j < HEIGHT; j++) {
            if (b(i,j) == 0) {
                b(i,j) = 1; // Replace zero with one
            }
            if (a(i,j) == minval && less_than_zero(minval) && b(i,j) == -1) {
                a(i,j) = a(i,j) + 1; // Fix it into range.
            }
        }
    }
    
    // Compute modulus result and check it.
    Func f;
    f = a % b;  // Using Halide mod operation.
    f.realize(out);
    
    int ecount = 0;
    for (i = 0; i < WIDTH; i++) {
        for (j = 0; j < HEIGHT; j++) {
            T v = halide_mod<T,BIG>(a(i,j), b(i,j));
            if (v != out(i,j) && (ecount++) < 10) {
                printf ("halide mod (%d %% %d) yielded %d; expected %d\n", a(i,j), b(i,j), out(i,j), v);
                success = false;
            }
        }
    }
    
    // Explicit checks of the simplifier
    ecount = 0;
    for (i = 0; i < std::min(SWIDTH,WIDTH); i++) {
        for (j = 0; j < std::min(SHEIGHT,HEIGHT); j++) {
            T arg_a = a(i,j);
            T arg_b = b(i,j);
            T v = halide_mod<T,BIG>(arg_a, arg_b);
            Expr in_e = cast<T>((int) arg_a) % cast<T>((int) arg_b);
            Expr e = simplify(in_e);
            Expr eout = cast<T>((int) v);
            if (! Internal::equal(e, eout) && (ecount++) < 10) {
                std::cout << "simplify(" << in_e << ") yielded " << e << "; expected " << eout << "\n";
                success = false;
            }
        }
    }
    
    /* Test alternative C implementation to match Halide definition. */
    ecount = 0;
    for (i = 0; i < WIDTH; i++) {
        for (j = 0; j < HEIGHT; j++) {
            T w = halide_mod<T,BIG>(a(i,j), b(i,j));
            T u = new_mod<T>(a(i,j), b(i,j));
            if (u != w && ecount++ < 10) {
                printf ("new_mod(%d, %d) yielded %d; expected %d\n", a(i,j), b(i,j), u, w);
            }
        }
    }

    return success;
}

// f_mod tests floating mod operations.
// BIG should be double.
// T should be a type known to Halide.
template<typename T,typename BIG,int bits>
bool f_mod() {
    int i, j;
    Type t = type_of<T>();
    BIG minval = 0.0;
    BIG maxval = 1.0;
    bool success = true;
    
    std::cout << "Test mod of " << t << '\n';
    t.bits = bits; // Override the bits
    
    // The parameter bits can be used to control the maximum data value.
    Image<T> a = init<T,BIG,bits>(t, 1, WIDTH, HEIGHT);
    Image<T> b = init<T,BIG,bits>(t, 2, WIDTH, HEIGHT);
    Image<T> out(WIDTH,HEIGHT);
    
    // Filter the input values for the operation to be tested.
    // Cannot divide by zero, so remove zeroes from b.
    for (i = 0; i < WIDTH; i++) {
        for (j = 0; j < HEIGHT; j++) {
            if (b(i,j) == 0.0) {
                b(i,j) = 1.0; // Replace zero with one.
            }
        }
    }
    
    // Compute modulus result and check it.
    Func f;
    f = a % b;  // Using Halide mod operation.
    f.realize(out);
    
    // Explicit checks of the simplifier for consistency with runtime computation
    int ecount = 0;
    for (i = 0; i < std::min(SWIDTH,WIDTH); i++) {
        for (j = 0; j < std::min(SHEIGHT,HEIGHT); j++) {
            T arg_a = a(i,j);
            T arg_b = b(i,j);
            T v = out(i,j);
            Expr in_e = cast<T>((float) arg_a) % cast<T>((float) arg_b);
            Expr e = simplify(in_e);
            Expr eout = cast<T>((float) v);
            if (! Internal::equal(e, eout) && (ecount++) < 10) {
                Expr diff = simplify(e - eout);
                Expr smalldiff = simplify(diff < (float) (0.000001) && diff > (float) (-0.000001));
                if (! Internal::is_one(smalldiff)) {
                    std::cout << "simplify(" << in_e << ") yielded " << e << "; expected " << eout << "\n";
                    std::cout << "          difference=" << diff << "\n";
                    success = false;
                }
            }
        }
    }

    return success;
}


int main(int argc, char **argv) {
    bool success = true;
    success &= f_mod<float,double,32>();
    success &= division<uint8_t,uint64_t,8>();
    success &= mod<uint8_t,uint64_t,8>();
    success &= division<uint16_t,uint64_t,16>();
    success &= mod<uint16_t,uint64_t,16>();
    success &= division<uint32_t,uint64_t,32>();
    success &= mod<uint32_t,uint64_t,32>();
    success &= division<int8_t,int64_t,8>();
    success &= mod<int8_t,int64_t,8>();
    success &= division<int16_t,int64_t,16>();
    success &= mod<int16_t,int64_t,16>();
    success &= division<int32_t,int64_t,32>();
    success &= mod<int32_t,int64_t,32>();

    if (! success) {
        printf ("Failure!\n");
        return -1;
        //return 0;
    }
    printf("Success!\n");
    return 0;
}
