// An example plugin to be used with the -plugin operator. 
//
// On OS X you compile it like this:
// g++ -c plugin.cpp -I ../src
// ld -dylib plugin.o -o plugin.so -undefined dynamic_lookup
//
// On linux you compile it like this:
// g++ -shared plugin.cpp -o plugin.so -I ../src -fPIC
// 
// And use it like this:
// ImageStack -load ../pics/dog1.jpg -plugin ./plugin.so -help foo -foo -display


// llc -O3 test.bc
// g++ -DNAME="myName" -DHELP="Help string" -DNUM_POPPED=2 -c test_plugin.cpp -I ../ImageStack/src
// ld -dylib test_plugin.o test.s 
//
// clang++ -g -O0 -DCLASSNAME='TestBrightness' -DNUM_POPPED=1 -DHELPSTR='"input, width, height, channels, coefficient"' -DNAMESTR='"test_brightness"' test_plugin.cpp test_brightness.s -I ../ImageStack/src -Xlinker -dylib -Xlinker -undefined -Xlinker dynamic_lookup -o test_brightness.so

#include "test_plugin.h"
#define MAX_NAME 256
#define TIME_RUNS 100

using std::vector;
using std::string;
using namespace ImageStack;

typedef union {
    void* ptr;
    int32_t i32;
    float f32;
} ArgT;

extern "C" {
    void _im_main_runner(ArgT args[]);
}

// here's our new operation to add
class CLASSNAME : public Operation {
protected:
    // TODO: templatize on type - split into convert<T> helper and main load<T>
    // Note: mallocs memory which must be freed by caller
    float* load_im_f32(Window im) {
        // Allocate our own buffer (caller-freed)
        float* buf = (float*)malloc(im.frames*im.height*im.width*im.channels * sizeof(float));

        printf("Load: %d x %d x %dc x %dt\n", im.width, im.height, im.channels, im.frames);
        assert(im.frames == 1, "Only support 1 frame for now");

        // Copy ImageStack Image layout into FImage desired layout
        for (int y = 0; y < im.height; y++) {
            for (int x = 0; x < im.width; x++) {
                for (int c = 0; c < im.channels; c++) {
                    int idx = ((c * im.height + y) * im.width + x);
                    buf[idx] = im(x, y)[c];
                }
            }
        }

        return buf;
    }

    void store_im_f32(float* buf, Window im) {
        printf("Store: %d x %d x %dc x %dt\n", im.width, im.height, im.channels, im.frames);
        // Copy FImage layout into ImageStack Image layout
        for (int y = 0; y < im.height; y++) {
            for (int x = 0; x < im.width; x++) {
                for (int c = 0; c < im.channels; c++) {
                    int idx = ((c * im.height + y) * im.width + x);
                    im(x, y)[c] = buf[idx];
                }
            }
        }
    }
    
public:
    void parse(vector<string> arglist) {

        // Argument list is interpreted as an in-order mapping to the argument list 
        //  of the 
        //
        // Arguments:
        // -width,height,channels,frames N: 
        // -im N: ptr to image at stack(N)
        // TODO: -im_u8? u16?: ptr to image stack(N) translated to correct type?
        // -int X: X as int32
        // -float X: X as float32

        vector<ArgT> args;
        ArgT a;

        // TODO: output size/allocation? For now, allocate based on stack head
        Image outim(stack(0).width, stack(0).height, stack(0).frames, stack(0).channels);
        float* out = load_im_f32(outim);
        a.ptr = out;
        args.push_back(a);
        
        // TODO: timestamp - run 100x to time - use currentTime() -> float (secs) helper
        // TODO: add extern calls to runtime, for e.g. timing code?
        vector<string>::iterator arg = arglist.begin();
        while (arg != arglist.end()) {
            string argname = *arg;
            const char* argval = (++arg)->c_str();

            if (argname == "/width" || argname == "/height"
                || argname == "/channels" || argname == "/frames" || argname == "/im")
            {
                int n = atoi(argval);
                Window im = stack(n);
                if (argname == "/width")    a.i32 = im.width;
                if (argname == "/height")   a.i32 = im.height;
                if (argname == "/channels") a.i32 = im.channels;
                if (argname == "/frames")   a.i32 = im.frames;
                if (argname == "/im")       a.ptr = load_im_f32(im); // TODO: free memory
            }
            else if (argname == "/int")
            {
                int x = atoi(argval);
                a.i32 = x;
            }
            else if (argname == "/float")
            {
                float x = atof(argval);
                a.f32 = x;
            }
            args.push_back(a);
            ++arg;
        }

        // TODO: assert num/type of args matches
        assert(arg == arglist.end(), "Parsed incomplete set of arguments");

        fprintf(stderr, "Args: {\n");
        for (int i = 0; i < args.size(); i++) {
            fprintf(stderr, "\t0x%p\n", args[i].ptr);
        }
        fprintf(stderr, "}\n\n");

        float start = currentTime();
        for (int i = 0; i < TIME_RUNS; i++) {
            // Run the generated function
            _im_main_runner(&(args[0]));
        }
        float end = currentTime();
        float endOverhead = currentTime();
        float time = (end - start - (endOverhead-end)) / TIME_RUNS;
        printf("_im_time: %f\n", time);

        store_im_f32(out, outim);

        for (int i = 0; i < NUM_POPPED; ++i) { pop(); }
        push(outim);
        free(out);
    }

    void help() {
        pprintf(HELPSTR);
    }
};

extern "C" void init_imagestack_plugin(map<string, Operation *> &operationMap) {
    char arg[MAX_NAME+1] = "";
    sprintf(arg, "-%s", NAMESTR);
    operationMap[arg] = new CLASSNAME();
}
