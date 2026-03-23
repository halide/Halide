#include "HalideBuffer.h"

#include <iostream>
#include <limits>
#include <stdint.h>
#include <type_traits>
#include <vector>

using namespace std;
using namespace Halide::Runtime;

//-----------------------------------------------------------------------------
// Returns the dimension sizes of a statically sized array from inner to outer.
// E.g. ary[2][3][4] returns (4, 3, 2).

template<typename T>
vector<int> dimension_sizes(T const &, vector<int> sizes = vector<int>()) {
    return sizes;
}

template<typename Array, size_t N>
vector<int> dimension_sizes(Array (&vals)[N], vector<int> sizes = vector<int>()) {
    sizes = dimension_sizes(vals[0], sizes);
    sizes.push_back((int)N);
    return sizes;
}

//-----------------------------------------------------------------------------
// Return the address of the first element, no matter how many dimensions
// Array has.

template<typename T>
T const *first_of_array(T const &val) {
    return &val;
}

template<typename Array, size_t N>
typename remove_all_extents<Array>::type const *first_of_array(Array (&vals)[N]) {
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
    exit(1);
}

void verify_dimension_sizes() {
    int a1[2];
    int a2[4][3];
    int a3[7][6][5];
    int a4[11][10][9][8];

    vector<int> v1(1), v2(2), v3(3), v4(4);
    v1[0] = 2;
    v2[0] = 3;
    v2[1] = 4;
    v3[0] = 5;
    v3[1] = 6;
    v3[2] = 7;
    v4[0] = 8;
    v4[1] = 9;
    v4[2] = 10;
    v4[3] = 11;

    compare_vectors(dimension_sizes(a1), v1);
    compare_vectors(dimension_sizes(a2), v2);
    compare_vectors(dimension_sizes(a3), v3);
    compare_vectors(dimension_sizes(a4), v4);
}

template<typename Image>
void compare_extents(const Image &img, int reference, int dimension) {
    if (img.dim(dimension).extent() == reference)
        return;
    cout << "Extent of dimension " << dimension << " of " << img.dimensions()
         << " is " << img.dim(dimension).extent() << " instead of " << reference << "\n";
    exit(1);
}

template<typename Array, typename T = typename remove_all_extents<Array>::type>
void verify_image_construction_from_array(Array &vals) {
    Buffer<T> img(vals);
    vector<int> sizes(dimension_sizes(vals));
    int dims = (int)sizes.size();
    for (int i = 0; i < dims; ++i) {
        compare_extents(img, sizes[i], i);
    }
    const void *reference = (const void *)first_of_array(vals);
    const void *under_test = (const void *)(&img());
    if (reference != under_test) {
        cerr << "Start of array: " << reference
             << "Start of image: " << under_test << "\n";
        exit(1);
    }
}

template<typename T>
void test() {
    T a1[2];
    T a2[4][3];
    T a3[7][6][5];
    T a4[11][10][9][8];

    verify_image_construction_from_array(a1);
    verify_image_construction_from_array(a2);
    verify_image_construction_from_array(a3);
    verify_image_construction_from_array(a4);
}

//-----------------------------------------------------------------------------

int main() {
    // Verify dimension_sizes() works as intended.
    verify_dimension_sizes();

    test<uint8_t>();
    test<uint16_t>();
    test<uint32_t>();
    test<uint64_t>();

    test<int8_t>();
    test<int16_t>();
    test<int32_t>();
    test<int64_t>();

    test<float>();
    test<double>();

    printf("Success!\n");
    return 0;
}
