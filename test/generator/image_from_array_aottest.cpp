#include "halide_image.h"

#include <stdint.h>
#include <iostream>
#include <limits>
#include <type_traits>
#include <vector>

using namespace std;
using namespace Halide::Tools;

//-----------------------------------------------------------------------------
// Returns the dimension sizes of a statically sized array from inner to outer.
// E.g. ary[2][3][4] returns (4, 3, 2).

template<typename T>
vector<int> dimension_sizes(T const &, vector<int> dimSizes = vector<int>() ) {
    return dimSizes;
}

template<typename Array, size_t N>
vector<int> dimension_sizes(Array (&vals)[N], vector<int> dimSizes = vector<int>()) {
    dimSizes = dimension_sizes(vals[0], dimSizes);
    dimSizes.push_back((int)N);
    return dimSizes;
}

//-----------------------------------------------------------------------------
// Return the address of the first element, no matter how many dimensions
// Array has.

template<typename T>
T const * first_of_array(T const &val) {
    return &val;
}

template<typename Array, size_t N>
typename remove_all_extents<Array>::type const * first_of_array(Array (&vals)[N]) {
    return first_of_array(vals[0]);
}

//-----------------------------------------------------------------------------
// Verify dimension_sizes() works as intended.

void print_vector(vector<int> const &v) {
    cout << "(";
    for (size_t i = 0, last = v.size(); i < last; ++i) {
        if (i)
            cout << ", ";
        cout << v[i];
    }
    cout << ")";
}

void compare_vectors(vector<int> const &under_test, vector<int> const &reference) {
    if (under_test == reference)
        return;

    cout << "Vector under test contained ";
    print_vector(under_test);
    cout << " instead of ";
    print_vector(reference);
    cout << "\n";
    exit(-1);
}

void verify_dimension_sizes() {
    int a1[2];
    int a2[4][3];
    int a3[7][6][5];
    int a4[11][10][9][8];

    vector<int> v1(1), v2(2), v3(3), v4(4);
    v1[0] = 2;
    v2[0] = 3; v2[1] = 4;
    v3[0] = 5; v3[1] = 6; v3[2] = 7;
    v4[0] = 8; v4[1] = 9; v4[2] = 10; v4[3] = 11;

    compare_vectors(dimension_sizes(a1), v1);
    compare_vectors(dimension_sizes(a2), v2);
    compare_vectors(dimension_sizes(a3), v3);
    compare_vectors(dimension_sizes(a4), v4);
}

//-----------------------------------------------------------------------------
// Convert integral values to 64-bit and set the number of digits to print
// for floating point values.

template<typename T
    , typename Int = typename is_integral<T>::type
    , typename Signed = integral_constant<bool, numeric_limits<T>::is_signed>
>
struct printable_t;

template<typename T> struct printable_t<T, true_type, true_type> {
    int64_t v;
    printable_t(T v) : v(static_cast<int64_t>(v)) {}
    void print(ostream &os) const {
        os << v;
    }
};

template<typename T> struct printable_t<T, true_type, false_type> {
    uint64_t v;
    printable_t(T v) : v(static_cast<uint64_t>(v)) {}
    void print(ostream &os) const {
        os << v;
    }
};

template<typename T> struct printable_t<T, false_type> {
    T v;
    printable_t(T v) : v(v) {}
    void print(ostream &os) const {
        streamsize prec = os.precision();
        os.precision(numeric_limits<T>::digits10 + 2);
        os << v;
        os.precision(prec);
    }
};

template<typename T>
inline ostream & operator<<(ostream &os, printable_t<T> const &pr) {
    pr.print(os);
    return os;
}

template<typename T>
inline printable_t<T> printable(T v) {
    return printable_t<T>(v);
}

//-----------------------------------------------------------------------------

template<typename T>
void compare_extents(Image<T> const &img, int reference, int dimension) {
    if (img.extent(dimension) == reference)
        return;
    cout << "Extent of dimension " << dimension << " of " << img.dimensions()
        << " is " << img.extent(dimension) << " instead of " << reference << "\n";
    exit(-1);
}

template<typename Array, typename T = typename remove_all_extents<Array>::type>
void verify_image_construction_from_array(Array &vals) {
    Image<T> img(vals);
    vector<int> dimSizes(dimension_sizes(vals));
    int dims = (int)dimSizes.size();
    int n = 1;
    for (int i = 0; i < dims; ++i) {
        compare_extents(img, dimSizes[i], i);
        n *= dimSizes[i];
    }
    T const *reference = first_of_array(vals);
    T const *under_test = img.data();
    for (int i = 0; i < n; ++i) {
        if (under_test[i] == reference[i])
            continue;

        cout << "Value at index " << i << " is "
            << printable(under_test[i]) << " instead of "
            << printable(reference[i]) << "\n";
        exit(-1);
    }
}

template<typename T>
void construct_various_dimensionalities(T q) {
    T a1[2];
    T a2[4][3];
    T a3[7][6][5];
    T a4[11][10][9][8];

    int v = 0;
    for (int i0 = 0; i0 < 11; ++i0) {
        if (i0 < 2)
            a1[i0] = static_cast<T>( (++v) * q );

        for (int i1 = 0; i1 < 10; ++i1) {
            if (i0 < 4 && i1 < 3)
                a2[i0][i1] = static_cast<T>( (++v) * q );

            for (int i2 = 0; i2 < 9; ++i2) {
                if (i0 < 7 && i1 < 6 && i2 < 5)
                    a3[i0][i1][i2] = static_cast<T>( (++v) * q );

                for (int i3 = 0; i3 < 8; ++i3)
                    a4[i0][i1][i2][i3] = static_cast<T>( (++v) * q );
            }
        }
    }

    verify_image_construction_from_array(a1);
    verify_image_construction_from_array(a2);
    verify_image_construction_from_array(a3);
    verify_image_construction_from_array(a4);
}

//-----------------------------------------------------------------------------

int main()
{
    // Verify dimension_sizes() works as intended.
    verify_dimension_sizes();

    construct_various_dimensionalities(static_cast<uint8_t >(1));
    construct_various_dimensionalities(static_cast<uint16_t>(2));
    construct_various_dimensionalities(static_cast<uint32_t>(4));
    construct_various_dimensionalities(static_cast<uint64_t>(8));

    construct_various_dimensionalities(static_cast<int8_t >(-1));
    construct_various_dimensionalities(static_cast<int16_t>(-2));
    construct_various_dimensionalities(static_cast<int32_t>(-3));
    construct_various_dimensionalities(static_cast<int64_t>(-4));

    construct_various_dimensionalities(1.1f);
    construct_various_dimensionalities(1.2);

    printf("Success!\n");
    return 0;
}