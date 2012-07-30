#include "Halide.h"

using namespace Halide;

#include "../png.h"

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
	Func interpolated[levels];
	Uniform< unsigned int > level_widths[levels];
	Uniform< unsigned int > level_heights[levels];
	Var x,y,c;

	downsampled[0](x,y) = (
		input(x,y,0) * input(x,y,3),
		input(x,y,1) * input(x,y,3),
		input(x,y,2) * input(x,y,3),
			input(x,y,3));

	//generate downsample levels:
	for (unsigned int l = 1; l < levels; ++l) {
		Func clamped;
		clamped(x,y,c) = downsampled[l-1](clamp(x,0,level_widths[l-1]-1), clamp(y,0,level_heights[l-1]-1), c);
		Func downx;
		downx(x,y,c) = (clamped(x*2-1,y,c) + 2.0f * clamped(x*2,y,c) + clamped(x*2+1,y,c)) / 4.0f;
		downsampled[l](x,y,c) = (downx(x,y*2-1,c) + 2.0f * downx(x,y*2,c) + downx(x,y*2+1,c)) / 4.0f;
	}
	interpolated[levels-1](x,y,c) = downsampled[levels-1](x,y,c);
	//generate interpolated levels:
	for (unsigned int l = levels-2; l < levels; --l) {
		Func upsampledx, upsampled;
		upsampledx(x,y,c) = 0.5f * (interpolated[l+1](x/2 + (x%2),y,c) + interpolated[l+1](x/2,y,c));
		upsampled(x,y,c) = 0.5f * (upsampledx(x, y/2 + (y%2),c) + upsampledx(x,y/2,c));
		interpolated[l](x,y,c) = downsampled[l](x,y,c) + (1.0 - downsampled[l](x,y,3)) * upsampled(x,y,c);
	}

	Func final;
	final(x,y) = (
		interpolated[0](x,y,0) / interpolated[0](x,y,3),
		interpolated[0](x,y,1) / interpolated[0](x,y,3),
		interpolated[0](x,y,2) / interpolated[0](x,y,3),
			1.0f/*interpolated[0](x,y,3)*/);
	
	std::cout << "Finished function setup." << std::endl;



	int sched = 3;
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
		std::cout << "Flat schedule with parallelization + vectorization." << std::endl;
		for (unsigned int l = 0; l < levels; ++l) {
			if (l + 2 < levels) {
				Var yo,yi;
				downsampled[l].root().split(y,yo,yi,4).parallel(yo).vectorize(x,4);
				interpolated[l].root().split(y,yo,yi,4).parallel(yo).vectorize(x,4);
			} else {
				downsampled[l].root();
				interpolated[l].root();
			}
		}
		final.root();
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


	default:
		assert(0 && "No schedule with this number.");
	}

	final.compileJIT();

	std::cout << "Running... " << std::endl;
	double min = std::numeric_limits< double >::infinity();
	const unsigned int Iters = 20;
	for (unsigned int x = 0; x < Iters; ++x) {

		Image< float > in_png = load< float >(argv[1]);
		assert(in_png.channels() == 4);

		input = in_png;

		{ //set up level sizes:
			unsigned int width = in_png.width();
			unsigned int height = in_png.height();
			for (unsigned int l = 0; l < levels; ++l) {
				level_widths[l] = width;
				level_heights[l] = height;
				width = width / 2 + 1;
				height = height / 2 + 1;
			}
		}

		double before = now();
		Image< float > out = final.realize(in_png.width(), in_png.height(), 4);
		double after = now();
		double amt = after - before;
		std::cout << "   " << amt * 1000 << std::endl;
		if (amt < min) min = amt;

		if (x + 1 == Iters) {
			Image< float > out = final.realize(in_png.width(), in_png.height(), 4);
			save(out, argv[2]);
		}
	}
	std::cout << " took " << min * 1000 << " msec." << std::endl;


}
