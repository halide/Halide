#include "Halide.h"

using namespace Halide;

const int CHANNELS = 4;
int main(int argc, char** argv) {
	ImageParam input(UInt(8), 3, "input");
    input.set_stride(0, CHANNELS);

	Var x, y, c;
	Func f("f");
	f(x, y, c) = input(x, y, c);
	f.bound(c, 0, CHANNELS);

	Func g("g");
	g(x, y, c) = f(x, y, c);
	g.bound(c, 0, CHANNELS);
    g.output_buffer().set_stride(0, CHANNELS);

	f.compute_root().shader(x, y, c, DeviceAPI::Renderscript);
	f.vectorize(c);

	g.compute_root().shader(x, y, c, DeviceAPI::Renderscript);
	g.vectorize(c);

    Target target = get_target_from_environment();
    std::string filename(target.arch == Target::Arch::ARM? "generated_test_two_kernels": "generated_test_two_kernels");
    g.compile_to_file(filename + (argc > 1? argv[1]: ""), {input});
}