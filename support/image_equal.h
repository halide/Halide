#ifndef _image_equal_h
#define _image_equal_h

#include <image_io.h>

template<class T>
bool images_equal(Image<T> &a, Image<T> &b, T eps, bool verbose=false) {
    if (a.width() != b.width() || a.height() != b.height() || a.channels() != b.channels()) {
        if (verbose) {
            printf("Images not equal: a: %dx%dx%d, b: %dx%dx%d\n", a.width(), a.height(), a.channels(), b.width(), b.height(), b.channels());
        }
        return false;
    }
    for (int c = 0; c < a.channels(); c++) {
        for (int y = 0; y < a.height(); y++) {
            for (int x = 0; x < a.width(); x++) {
                T ac = a(x,y,c);
                T bc = b(x,y,c);
                T delta = ac > bc ? ac - bc: bc - ac;
                if (delta > eps) {
                    if (verbose) {
                        printf("Images not equal: a(%d,%d,%d): %f, b: %f", x, y, c, 1.0*ac, 1.0*bc);
                    }
                    return false;
                }
            }
        }
    }
    return true;
}

#endif

