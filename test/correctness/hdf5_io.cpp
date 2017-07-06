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
string buffer_info(BufferType buf, string name) {
    string retval = "Buffer '" + name + "':\n";
    retval += "  Dimensions: " + std::to_string(buf.dimensions()) + "\n";
    retval += "  Size: (";
    for(size_t idx=0; idx<(size_t)buf.dimensions(); idx++) {
        if(idx>0) retval += "x";
        retval += std::to_string(buf.dim(idx).extent());
    }
    retval += ")\n";
    return retval;
}


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
bool check_buffer(T &buf, T &ref, string test_name, int cur_dim) {
    if(buf.dimensions() != ref.dimensions()) {
        fprintf(stderr,"[ERROR @ test %s] Dimensions mismatch: orginal has %d, reference has %d\n",
                test_name.c_str(),
                buf.dimensions(),
                ref.dimensions());
        string info = "Buffer information:\n" + buffer_info(buf, "loaded buffer") + "\n" + buffer_info(ref, "reference buffer") + "\n";
        fprintf(stderr, "%s", info.c_str());
        return false;
    }

    if(buf.dimensions() > 0) {
        //we do not have test vectors with nonzero mins.
        assert(buf.dim(0).min() == 0);
        assert(ref.dim(0).min() == 0);

        if(buf.dim(0).extent() != ref.dim(0).extent()) {
            fprintf(stderr,"[ERROR @ test %s] Size mismatch in dimension %d: orginal has %d, reference has %d\n",
                    test_name.c_str(),
                    cur_dim,
                    buf.dim(0).extent(),
                    ref.dim(0).extent());
            string info = "Buffer information:\n" + buffer_info(buf, "loaded buffer") + "\n" + buffer_info(ref, "reference buffer") + "\n";
            fprintf(stderr, "%s", info.c_str());
            return false;
        }

        for(int i=0; i<buf.dim(0).extent(); i++) {
            auto slice = buf.sliced(0, i);
            auto refslice = ref.sliced(0, i);
            if(!check_buffer(slice, refslice, test_name, cur_dim+1)) return false;
        }
    }

    //If we arrive here, the buffers are 0-dimensional. Check the single element.
    return buf() == ref();
}

template<typename T>
bool roundtrip_test(T &buf, string test_name) {

    save_hdf5({"testbuffer"}, "test.h5", buf);
    T inbuf = load_from_hdf5<T>("test.h5", "testbuffer");

    return check_buffer(inbuf, buf, test_name, 0);
}

template<typename BufferType>
BufferType make_buf(
        vector<int> sizes
        ) {
    BufferType buf = BufferType(sizes);
    buf.allocate();
    make_noise(buf);
    return buf;
}

template<typename BufferType>
bool do_roundtrip_test(
        vector<int> sizes,
        string test_name
        ) {
    BufferType buf = make_buf<BufferType>(sizes);
    return roundtrip_test(buf, test_name);
}

int main() {
    //round-trip tests.
    bool success = true;
    if(!do_roundtrip_test< Runtime::Buffer<int8_t> >({5, 6, 12},     "int8")) success = false;
    if(!do_roundtrip_test< Runtime::Buffer<int16_t> >({1, 3, 2, 1}, "int16")) success = false;
    if(!do_roundtrip_test< Runtime::Buffer<int32_t> >({5, 8, 2},    "int32")) success = false;
    if(!do_roundtrip_test< Runtime::Buffer<int64_t> >({7, 6, 2},    "int64")) success = false;
    if(!do_roundtrip_test< Runtime::Buffer<uint8_t> >({5, 6, 2},     "uint8")) success = false;
    if(!do_roundtrip_test< Runtime::Buffer<uint16_t> >({1, 3, 2, 1, 1}, "uint16")) success = false;
    if(!do_roundtrip_test< Runtime::Buffer<uint32_t> >({5, 8, 2},    "uint32")) success = false;
    if(!do_roundtrip_test< Runtime::Buffer<uint64_t> >({7, 6, 2},    "uint64")) success = false;
    if(!do_roundtrip_test< Runtime::Buffer<float> >({10, 2, 3, 6}, "float")) success = false;
    if(!success) return -1;
    return 0;
}


