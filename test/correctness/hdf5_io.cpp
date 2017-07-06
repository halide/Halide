#include "Halide.h"
#include "halide_hdf5_io.h"

#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <assert.h>
#include <time.h>
#include <random>

using namespace Halide;
using namespace Halide::Tools;
using std::map;
using std::string;
using std::vector;

typedef std::mt19937 random_num_gen;
random_num_gen random_gen;

void init_random()
{
  random_gen.seed(time(NULL));
}

template<typename T>
T random_integer() {
    std::uniform_int_distribution<T> dist;
    return dist(random_gen);
}

template<typename T>
T random_real() {
    std::uniform_real_distribution<T> dist;
    return dist(random_gen);
}

template<typename T>
T random_number() {return (T)0;}

template<> int8_t random_number<int8_t>() { return random_integer<int8_t>(); }
template<> int16_t random_number<int16_t>() { return random_integer<int16_t>(); }
template<> int32_t random_number<int32_t>() { return random_integer<int32_t>(); }
template<> int64_t random_number<int64_t>() { return random_integer<int64_t>(); }

template<> uint8_t random_number<uint8_t>() { return random_integer<uint8_t>(); }
template<> uint16_t random_number<uint16_t>() { return random_integer<uint16_t>(); }
template<> uint32_t random_number<uint32_t>() { return random_integer<uint32_t>(); }
template<> uint64_t random_number<uint64_t>() { return random_integer<uint64_t>(); }

template<> float random_number<float>() { return random_real<float>(); }
template<> double random_number<double>() { return random_real<double>(); }


template<typename BufferType>
void make_noise(BufferType &b) {
    using ElemType = typename BufferType::ElemType;

    //Recursively call self to cover all dimensions of the buffer
    if(b.dimensions() > 0) {
        for(int i=0; i<b.dim(0).extent(); i++) {
            auto slice = b.sliced(0,i);
            make_noise(slice);
        }
        return;
    }

    //Should be 0-dimensional if reached here.
    b() = random_number<ElemType>();
}

template<typename T>
void test(T &buf) {

    save_hdf5({"testbuffer"}, "test.h5", buf);
    T inbuf = load_from_hdf5<T>("test.h5", "testbuffer");

    if( (buf.width() != inbuf.width()) || (buf.height() != inbuf.height()) ) {
        printf("[ERROR] Size mismatch: original %dx%d, re-loaded %dx%d\n", buf.width(), buf.height(), inbuf.width(), inbuf.height());
        exit(-1);
    }

    bool mismatch = false;
    for (int y = 0; y < buf.height(); y++) {
        for (int x = 0; x < buf.width(); x++) {
            if(buf(x,y) != inbuf(x,y)) {
                printf("[ERROR] difference at (%d,%d)\n", x, y);
                mismatch = true;
            }
        }
    }

    if(mismatch) exit(-1);
}

template<typename BufferType>
void do_test(
        vector<int> sizes
        ) {

    BufferType buf = BufferType(sizes);
    buf.allocate();
    make_noise(buf);
    test(buf);
}

int main() {
    do_test< Runtime::Buffer<int32_t, 3> >({5, 6, 2});
    do_test< Runtime::Buffer<float, 4> >({10, 2, 3, 6});
    return 0;
}


