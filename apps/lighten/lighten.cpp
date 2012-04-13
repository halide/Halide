#include "Halide.h"

using namespace Halide;

#include "../png.h"

#include <iostream>

Func make_lighten(UniformImage input, Uniform< uint8_t > amt) {
	Var x,y,c;
	Func func;

	func(x,y,c) = cast< uint8_t >(min(cast< uint16_t >(input(x,y,c)) + cast< uint16_t >(amt), cast< uint16_t >(255)));

	return func;
}

int main(int argc, char **argv) {
	if (argc < 3) {
		std::cerr << "Usage:\n\t./lighten in.png out.png\n" << std::endl;
		return 1;
	}

	UniformImage input(UInt(8), 3);
	Uniform< uint8_t > amt;

	Func func = make_lighten(input, amt);
	func.root();


	Image< uint8_t > in_png = load< uint8_t >(argv[1]);
	Image< uint8_t > out(in_png.width(), in_png.height(), in_png.channels());
	
	//Lighten input image by 50/255ths of a pixel value:
	amt = 50;
	input = in_png;
	out = func.realize(in_png.width(), in_png.height(), in_png.channels());

	save(out, argv[2]);
}
