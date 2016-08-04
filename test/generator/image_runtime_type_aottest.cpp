#include "halide_image.h"

#include <stdint.h>
#include <iostream>
#include <limits>
#include <type_traits>
#include <vector>

using namespace std;
using namespace Halide::Tools;

//-----------------------------------------------------------------------------

template<typename T>
ImageBase construct_with_type() {
    Image<T> img(2, 3, 4);
    return img;
}

template<typename T>
void work_with_type(Image<T> img) {
    if (!img.empty())
        *img.data() = T();
}

template<typename T>
void test_with_type() {
    Image<T> img_default;
    work_with_type(img_default);

    ImageBase base_default;
    work_with_type<T>(base_default);

    Image<T> img_init(2, 3, 4);
    work_with_type(img_init);

    Image<T> img_copy(img_init);
    work_with_type(img_copy);

    ImageBase base_copy_img(img_init);
    work_with_type<T>(base_copy_img);

    ImageBase base_copy(base_copy_img);
    work_with_type<T>(base_copy);

    Image<T> img_copy_base(base_copy_img);
    work_with_type(img_copy_base);

    Image<T> img_assign_from(2, 3, 4);
    ImageBase base_assign_to;
    base_assign_to = img_assign_from;
    work_with_type<T>(base_assign_to);

    ImageBase base_assign_from(base_copy);
    Image<T> img_assign_to;
    img_assign_to = base_assign_from;
    work_with_type(img_assign_to);

    ImageBase base_with_type(construct_with_type<T>());
    work_with_type<T>(base_with_type);
}

//-----------------------------------------------------------------------------

int main()
{
    test_with_type<uint8_t >();
    test_with_type<uint16_t>();
    test_with_type<uint32_t>();
    test_with_type<uint64_t>();

    test_with_type<int8_t >();
    test_with_type<int16_t>();
    test_with_type<int32_t>();
    test_with_type<int64_t>();

    test_with_type<float >();
    test_with_type<double>();

    printf("Success!\n");
    return 0;
}