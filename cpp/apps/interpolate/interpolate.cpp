#include "Halide.h"

using namespace Halide;

#include <image_io.h>

#include <iostream>
#include <limits>

#include <sys/time.h>

using std::vector;

double now() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    static bool first_call = true;
    static time_t first_sec = 0;
    if (first_call) {
        first_call = false;
        first_sec = tv.tv_sec;
    }
    assert(tv.tv_sec >= first_sec);
    return (tv.tv_sec - first_sec) + (tv.tv_usec / 1000000.0);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "Usage:\n\t./interpolate in.png out.png\n" << std::endl;
        return 1;
    }

    ImageParam input(Float(32), 3);

    unsigned int levels = 10;

    Func downsampled[levels];
    Func downx[levels];
    Func interpolated[levels];
    Func upsampled[levels];
    Func upsampledx[levels];
    Var x,y,c;

    Func clamped;
    clamped(c, x, y) = input(clamp(x, 0, input.width()-1), clamp(y, 0, input.height()-1), c);

    downsampled[0](c,x,y) = select(c < 3, clamped(c, x, y) * clamped(3, x, y), clamped(3, x, y));

    for (unsigned int l = 1; l < levels; ++l) {
        downx[l](c, x, y) = (downsampled[l-1](c, x*2-1, y) + 
                             2.0f * downsampled[l-1](c, x*2, y) + 
                             downsampled[l-1](c, x*2+1, y)) * 0.25f;
        downsampled[l](c, x, y) = (downx[l](c, x, y*2-1) + 
                                   2.0f * downx[l](c, x, y*2) + 
                                   downx[l](c, x, y*2+1)) * 0.25f;
    }
    interpolated[levels-1] = downsampled[levels-1];
    for (unsigned int l = levels-2; l < levels; --l) {
        upsampledx[l](c, x, y) = select((x % 2) == 0, 
                                        interpolated[l+1](c, x/2, y), 
                                        0.5f * (interpolated[l+1](c, x/2, y) + 
                                                interpolated[l+1](c, x/2+1, y)));
        upsampled[l](c, x, y) = select((y % 2) == 0,
                                       upsampledx[l](c, x, y/2), 
                                       0.5f * (upsampledx[l](c, x, y/2) + 
                                               upsampledx[l](c, x, y/2+1)));
        interpolated[l](c, x, y) = downsampled[l](c, x, y) + (1.0f - downsampled[l](3, x, y)) * upsampled[l](c, x, y);
    }

    Func final;
    final(x, y, c) = interpolated[0](c, x, y) / interpolated[0](3, x, y);
	
    std::cout << "Finished function setup." << std::endl;

    int sched = 2;
    switch (sched) {
    case 0:
    {
        std::cout << "Flat schedule." << std::endl;
        for (unsigned int l = 0; l < levels; ++l) {
            downsampled[l].compute_root();
            interpolated[l].compute_root();
        }
        final.compute_root();
        break;
    }
    case 1:
    {
        std::cout << "Flat schedule with vectorization." << std::endl;
        for (unsigned int l = 0; l < levels; ++l) {
            downsampled[l].compute_root().vectorize(x,4);
            interpolated[l].compute_root().vectorize(x,4);
        }
        final.compute_root();
        break;
    }
    case 2:
    {
        Var yi;
        std::cout << "Flat schedule with parallelization + vectorization." << std::endl;                
        clamped.compute_root().parallel(y).bound(c, 0, 4).vectorize(c, 4);
        for (unsigned int l = 1; l < levels-1; ++l) {
            downsampled[l].compute_root().parallel(y).vectorize(c, 4);
            interpolated[l].compute_root().parallel(y).vectorize(c, 4);
        }
        final.parallel(y).reorder(c, x, y).bound(c, 0, 3);
        break;
    }
    case 3:
    {
        std::cout << "Flat schedule with vectorization sometimes." << std::endl;
        for (unsigned int l = 0; l < levels; ++l) {
            if (l + 4 < levels) {
                Var yo,yi;
                downsampled[l].compute_root().vectorize(x,4);
                interpolated[l].compute_root().vectorize(x,4);
            } else {
                downsampled[l].compute_root();
                interpolated[l].compute_root();
            }
        }
        final.compute_root();
        break;
    }        
    default:
        assert(0 && "No schedule with this number.");
    }

    // JIT compile the pipeline eagerly, so we don't interfere with timing
    final.compile_jit();

    Image<float> in_png = load<float>(argv[1]);
    Image<float> out(in_png.width(), in_png.height(), 3);
    assert(in_png.channels() == 4);
    input.set(in_png);

    std::cout << "Running... " << std::endl;
    double min = std::numeric_limits<double>::infinity();
    const unsigned int iters = 20;

    for (unsigned int x = 0; x < iters; ++x) {                        
        double before = now();
        final.realize(out);
        double after = now();
        double amt = after - before;

        std::cout << "   " << amt * 1000 << std::endl;
        if (amt < min) min = amt;
        
    }
    std::cout << " took " << min * 1000 << " msec." << std::endl;

    vector<Argument> args;
    args.push_back(input);
    final.compile_to_assembly("test.s", args);

    save(out, argv[2]);

}
