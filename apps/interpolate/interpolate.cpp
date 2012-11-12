#include "Halide.h"

using namespace Halide;

#include <image_io.h>

#include <iostream>
#include <limits>

#include <sys/time.h>

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

    UniformImage input(Float(32), 3);

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

    //generate downsample levels:
    for (unsigned int l = 1; l < levels; ++l) {
        //Func downx;
        downx[l](c, x, y) = (downsampled[l-1](c, x*2-1, y) + 
                          2.0f * downsampled[l-1](c, x*2, y) + 
                          downsampled[l-1](c, x*2+1, y)) * 0.25f;
        downsampled[l](c, x, y) = (downx[l](c, x, y*2-1) + 
                                   2.0f * downx[l](c, x, y*2) + 
                                   downx[l](c, x, y*2+1)) * 0.25f;
    }
    interpolated[levels-1] = downsampled[levels-1];
    //generate interpolated levels:
    for (unsigned int l = levels-2; l < levels; --l) {
        //Func upsampledx, upsampled;
        upsampledx[l](c, x, y) = 0.5f * (interpolated[l+1](c, x/2 + (x%2), y) + interpolated[l+1](c, x/2, y));
        upsampled[l](c, x, y) = 0.5f * (upsampledx[l](c, x, y/2 + (y%2)) + upsampledx[l](c, x, y/2));
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
        //schedule:
        for (unsigned int l = 0; l < levels; ++l) {
            downsampled[l].root();
            interpolated[l].root();
        }
        final.root();
        break;
    }
    case 1:
    {
        std::cout << "Flat schedule with vectorization." << std::endl;
        for (unsigned int l = 0; l < levels; ++l) {
            downsampled[l].root().vectorize(x,4);
            interpolated[l].root().vectorize(x,4);
        }
        final.root();
        break;
    }
    case 2:
    {
        Var yi;
        std::cout << "Flat schedule with parallelization + vectorization." << std::endl;                
        //clamped.root().parallel(y).bound(c, 0, 4).vectorize(c, 4);
        for (unsigned int l = 1; l < levels-1; ++l) {
            downsampled[l].root().parallel(y).vectorize(c, 4);
            interpolated[l].root().parallel(y).vectorize(c, 4);
        }
        final.parallel(y).bound(c, 0, 3).vectorize(c, 4);
        break;
    }
    case 3:
    {
        std::cout << "Flat schedule with vectorization sometimes." << std::endl;
        for (unsigned int l = 0; l < levels; ++l) {
            if (l + 4 < levels) {
                Var yo,yi;
                downsampled[l].root().vectorize(x,4);
                interpolated[l].root().vectorize(x,4);
            } else {
                downsampled[l].root();
                interpolated[l].root();
            }
        }
        final.root();
        break;
    }
    case 4:
    {
        std::cout << "Autotuned schedule." << std::endl;
        Var _c0("_c0"), _c1("_c1");
        downsampled[0].chunk(y,y);
        downsampled[1].root().tile(x,y,_c0,_c1,4,4).vectorize(_c0,4).parallel(y);
        downsampled[2].chunk(x,x).vectorize(c,4);
        downsampled[3].chunk(y,y).vectorize(c,2);
        downsampled[4].chunk(y,y).vectorize(c,2);
        downsampled[5].chunk(y,y).vectorize(c,4);


        downsampled[8].root();


        downx[2].chunk(x,x).vectorize(c,4);
        downx[3].root().tile(x,y,_c0,_c1,4,4).vectorize(_c0,4).parallel(y);
        downx[4].root().tile(x,y,_c0,_c1,2,2).vectorize(_c0,2).parallel(y);
        downx[5].root();
        downx[6].chunk(x,x).vectorize(c,2);
        downx[7].root().parallel(y);
        downx[8].chunk(y,y).vectorize(c,4);
        downx[9].root().parallel(y);
        final.root().tile(x,y,_c0,_c1,8,8).vectorize(_c0,8).parallel(y);

        interpolated[1].chunk(y,y).parallel(y);
        interpolated[2].chunk(y,y).vectorize(c,4);
        interpolated[3].chunk(y,y).vectorize(c,8);
        interpolated[4].chunk(y,y).vectorize(c,2);
        interpolated[5].chunk(y,y).vectorize(c,4);
        interpolated[6].chunk(y,y).vectorize(c,4);
        interpolated[7].root().parallel(y);
        interpolated[8].root().parallel(y);
        interpolated[9].chunk(y,y).vectorize(c,4).split(c,c,_c0,64);


        upsampled[2].root().tile(x,y,_c0,_c1,8,8).vectorize(_c0,8).parallel(y);

        upsampled[4].chunk(y,y).vectorize(c,2);
        upsampled[5].chunk(y,y).vectorize(c,4);
        upsampled[6].chunk(x,x).vectorize(c,2);

        upsampled[8].chunk(y,y).vectorize(c,4);
        upsampledx[0].root().parallel(y).unroll(x,4);
        upsampledx[1].root().tile(x,y,_c0,_c1,4,4).vectorize(_c0,4).parallel(y);
        upsampledx[2].chunk(y,y).vectorize(c,4);
        upsampledx[3].root().tile(x,y,_c0,_c1,2,2).vectorize(_c0,2).parallel(y);
        upsampledx[4].chunk(y,y).vectorize(c,4);
        upsampledx[5].chunk(y,y).vectorize(c,4);
        upsampledx[6].root().vectorize(c,2);
        upsampledx[7].root().parallel(y);
        upsampledx[8].chunk(y,y).vectorize(c,4).unroll(c,4);
        break;
    }


    default:
        assert(0 && "No schedule with this number.");
    }

    final.compileJIT();

    std::cout << "Running... " << std::endl;
    double min = std::numeric_limits< double >::infinity();
    const unsigned int Iters = 20;

    Image< float > in_png = load< float >(argv[1]);
    Image< float > out(in_png.width(), in_png.height(), 3);
    assert(in_png.channels() == 4);
    input = in_png;

    for (unsigned int x = 0; x < Iters; ++x) {                        
        double before = now();
        final.realize(out);
        double after = now();
        double amt = after - before;

        std::cout << "   " << amt * 1000 << std::endl;
        if (amt < min) min = amt;
        
    }
    std::cout << " took " << min * 1000 << " msec." << std::endl;

    save(out, argv[2]);

}
