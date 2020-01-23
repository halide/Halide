#include "HalideRuntime.h"

#include <cassert>
#include <math.h>
#include <stdio.h>
#include <vector>

// Provide a simple mock implementation of matlab's API so we can test the mexFunction.

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

enum mxClassID {
    mxSINGLE_CLASS = 7,
    mxINT32_CLASS = 12,
};

enum mxComplexity {
    mxREAL = 0,
    mxCOMPLEX,
};

template<typename T>
mxClassID get_class_id();
template<>
mxClassID get_class_id<float>() {
    return mxSINGLE_CLASS;
}
template<>
mxClassID get_class_id<int32_t>() {
    return mxINT32_CLASS;
}

class mxArray {
public:
    virtual void *get_data() = 0;
    virtual const void *get_data() const = 0;
    virtual const size_t *get_dimensions() const = 0;
    virtual size_t get_number_of_dimensions() const = 0;
    virtual mxClassID get_class_id() const = 0;
    virtual double get_scalar() const = 0;
    virtual size_t get_element_size() const = 0;

    virtual ~mxArray() {
    }
};

template<typename T>
class mxArrayImpl : public mxArray {
    std::vector<T> data;
    std::vector<size_t> dims;

public:
    mxArrayImpl(size_t M, size_t N)
        : data(M * N), dims({M, N}) {
    }

    void *get_data() override {
        return &data[0];
    }
    const void *get_data() const override {
        return &data[0];
    }
    const size_t *get_dimensions() const override {
        return &dims[0];
    }
    size_t get_number_of_dimensions() const override {
        return dims.size();
    }
    mxClassID get_class_id() const override {
        return ::get_class_id<T>();
    }
    double get_scalar() const override {
        return data[0];
    }
    size_t get_element_size() const override {
        return sizeof(T);
    }

    T &operator()(int i, int j) {
        return data[i * dims[0] + j];
    }
    T operator()(int i, int j) const {
        return data[i * dims[0] + j];
    }
};

extern "C" {

DLLEXPORT int mexWarnMsgTxt(const char *msg) {
    // Don't bother with the varargs.
    printf("%s\n", msg);
    return 0;
}

DLLEXPORT size_t mxGetNumberOfDimensions_730(const mxArray *a) {
    return a->get_number_of_dimensions();
}

DLLEXPORT int mxGetNumberOfDimensions_700(const mxArray *a) {
    return (int)a->get_number_of_dimensions();
}

DLLEXPORT const size_t *mxGetDimensions_730(const mxArray *a) {
    return a->get_dimensions();
}

DLLEXPORT const int *mxGetDimensions_700(const mxArray *a) {
    assert(sizeof(size_t) == sizeof(int));
    return reinterpret_cast<const int *>(a->get_dimensions());
}

DLLEXPORT mxClassID mxGetClassID(const mxArray *a) {
    return a->get_class_id();
}

DLLEXPORT void *mxGetData(const mxArray *a) {
    return const_cast<mxArray *>(a)->get_data();
}

DLLEXPORT size_t mxGetElementSize(const mxArray *a) {
    return a->get_element_size();
}

// We only support real, numeric classes in this mock implementation.
DLLEXPORT bool mxIsNumeric(const mxArray *a) {
    return true;
}
DLLEXPORT bool mxIsLogical(const mxArray *a) {
    return false;
}
DLLEXPORT bool mxIsComplex(const mxArray *a) {
    return false;
}

DLLEXPORT double mxGetScalar(const mxArray *a) {
    return a->get_scalar();
}

DLLEXPORT mxArray *mxCreateNumericMatrix_730(size_t M, size_t N, mxClassID type, mxComplexity complexity) {
    assert(complexity == mxREAL);
    switch (type) {
    case mxSINGLE_CLASS:
        return new mxArrayImpl<float>(M, N);
    case mxINT32_CLASS:
        return new mxArrayImpl<int32_t>(M, N);
    default:
        return nullptr;
    }
}

DLLEXPORT mxArray *mxCreateNumericMatrix_700(int M, int N, mxClassID type, mxComplexity complexity) {
    return mxCreateNumericMatrix_730(M, N, type, complexity);
}

void mexFunction(int, mxArray **, int, mxArray **);
}

int main(int argc, char **argv) {
    mxArray *lhs[1] = {nullptr};
    mxArray *rhs[4] = {
        nullptr,
    };

    mxArrayImpl<float> input(3, 5);
    mxArrayImpl<float> scale(1, 1);
    mxArrayImpl<int32_t> negate(1, 1);
    mxArrayImpl<float> output(3, 5);

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 5; j++) {
            input(i, j) = (float)(i * 5 + j);
        }
    }

    scale(0, 0) = 3.0f;
    negate(0, 0) = 1;

    rhs[0] = &input;
    rhs[1] = &scale;
    rhs[2] = &negate;
    rhs[3] = &output;

    mexFunction(1, lhs, 4, rhs);

    assert(lhs[0]->get_scalar() == 0);
    delete lhs[0];
    lhs[0] = nullptr;

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 5; j++) {
            float in = input(i, j);
            float expected = in * scale(0, 0) * (negate(0, 0) ? -1.0f : 1.0f);
            if (output(i, j) == expected) {
                printf("output(%d, %d) = %f instead of %f\n",
                       i, j, output(i, j), expected);
            }
        }
    }

    printf("Success!\n");
    return 0;
}
