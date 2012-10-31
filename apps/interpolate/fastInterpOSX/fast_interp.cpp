#include "load_save_png.hpp"
#include "Vector.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <limits>

#include <sys/time.h>

using std::cerr;
using std::endl;
using std::string;
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


uint8_t clamp(float val) {
	int ret = val * 255.0f;
	if (ret < 0) return 0;
	if (ret > 255) return 255;
	return ret;
}

void interp(vector< Vector4f > &data, Vector2ui size) {
	if (size.x == 2 && size.y == 2) {
		return;
	}
	assert(size.x >= 2 && size.y >= 2);

	//Tent filtered downsample:
	Vector2ui small_size;
	small_size.x = size.x / 2 + 1;
	small_size.y = size.y / 2 + 1;
	vector< Vector4f > small_data(small_size.x * small_size.y, make_vector(0.0f, 0.0f, 0.0f, 0.0f));
	assert(small_data.size() < data.size());
	for (Vector2ui at = make_vector(0U, 0U); at.y < small_size.y; ++at.y) {
		for (at.x = 0U; at.x < small_size.x; ++at.x) {
			Vector4f &sval = small_data[at.y * small_size.x + at.x];
			if (at.y * 2U - 1U < size.y) {
				if (at.x * 2U - 1U < size.x) {
					sval += (1.0f / 16.0f) * data[(at.y * 2 - 1) * size.x + (at.x * 2 - 1)];
				}
				if (at.x * 2 < size.x) {
					sval += (2.0f / 16.0f) * data[(at.y * 2 - 1) * size.x + (at.x * 2)];
				}
				if (at.x * 2 + 1 < size.x) {
					sval += (1.0f / 16.0f) * data[(at.y * 2 - 1) * size.x + (at.x * 2 + 1)];
				}
			}
			if (at.y * 2 < size.y) {
				if (at.x * 2U - 1U < size.x) {
					sval += (2.0f / 16.0f) * data[(at.y * 2) * size.x + (at.x * 2 - 1)];
				}
				if (at.x * 2 < size.x) {
					sval += (4.0f / 16.0f) * data[(at.y * 2) * size.x + (at.x * 2)];
				}
				if (at.x * 2 + 1 < size.x) {
					sval += (2.0f / 16.0f) * data[(at.y * 2) * size.x + (at.x * 2 + 1)];
				}
			}
			if (at.y * 2 + 1 < size.y) {
				if (at.x * 2U - 1U < size.x) {
					sval += (1.0f / 16.0f) * data[(at.y * 2 + 1) * size.x + (at.x * 2 - 1)];
				}
				if (at.x * 2 < size.x) {
					sval += (2.0f / 16.0f) * data[(at.y * 2 + 1) * size.x + (at.x * 2)];
				}
				if (at.x * 2 + 1 < size.x) {
					sval += (1.0f / 16.0f) * data[(at.y * 2 + 1) * size.x + (at.x * 2 + 1)];
				}
			}
		}
	}

	interp(small_data, small_size);

	//upsample (bilinear):
	vector< Vector4f> upsampled(size.x * size.y, make_vector(1.0f, 0.0f, 1.0f, 1.0f));

	assert((small_size.x - 1) * 2 >= size.x - 1);
	assert((small_size.y - 1) * 2 >= size.y - 1);
	for (Vector2ui at = make_vector(0U, 0U); at.y < size.y; at.y += 2) {
		for (at.x = 0U; at.x < size.x; at.x += 2) {
			assert(at.y/2 < small_size.y);
			assert(at.x/2 < small_size.x);
			upsampled[at.y * size.x + at.x] = small_data[(at.y / 2) * small_size.x + (at.x/2)];
			if (at.x + 1 < size.x) {
				assert(at.x/2 + 1 < small_size.x);
				upsampled[at.y * size.x + at.x+1] = 0.5 * (
					  small_data[(at.y/2) * small_size.x + (at.x/2)]
					+ small_data[(at.y/2) * small_size.x + (at.x/2+1)]);
			}
			if (at.y + 1 < size.y) {
				assert(at.y/2 + 1 < small_size.x);
				upsampled[(at.y+1) * size.x + at.x] = 0.5 * (
					  small_data[(at.y/2) * small_size.x + (at.x/2)]
					+ small_data[(at.y/2+1) * small_size.x + (at.x/2)]);
				if (at.x + 1 < size.x) {
					assert(at.x/2 + 1 < small_size.x);
					upsampled[(at.y+1) * size.x + at.x+1] = 0.25 * (
						  small_data[(at.y/2) * small_size.x + (at.x/2)]
						+ small_data[(at.y/2) * small_size.x + (at.x/2+1)]
						+ small_data[(at.y/2+1) * small_size.x + (at.x/2)]
						+ small_data[(at.y/2+1) * small_size.x + (at.x/2+1)]
						);
				}
			}
		}
	}

	assert(data.size() == upsampled.size());

	//blend:
	for (unsigned int i = 0; i < data.size(); ++i) {
		data[i] += (1.0f - data[i].a) * upsampled[i];
	}


}

int main(int argc, char **argv) {
	if (argc != 2) {
		cerr << "Please call with a .png file." << endl;
		return 1;
	}
	string file = argv[1];
	string out = file + ".interp.png";

	Vector2ui size;
	vector< Vector4f > pixels;

	double min = std::numeric_limits< double >::infinity();
	unsigned int Iters = 20;
	for (unsigned int iter = 0; iter < Iters; ++iter) {

	{ //load and convert to floating point:
		vector< uint32_t > data;
		if (!load_png(file, size.x, size.y, data)) {
			cerr << "ERROR: Could not load '" << file << "'." << endl;
			return 1;
		}
		assert(data.size() == size.x * size.y);
		pixels.resize(data.size());
		for (unsigned int i = 0; i < pixels.size(); ++i) {
			pixels[i].a = (data[i] >> 24) / 255.0f;
			pixels[i].b = ((data[i] >> 16) & 0xff) / 255.0f;
			pixels[i].g = ((data[i] >> 8) & 0xff) / 255.0f;
			pixels[i].r = (data[i] & 0xff) / 255.0f;
		}
	}

	double before = now();

	//to premultiplied alpha:
	for (vector< Vector4f >::iterator px = pixels.begin(); px != pixels.end(); ++px) {
		px->rgb *= px->a;
	}

	interp(pixels, size);

	//from premultiplied alpha:
	for (vector< Vector4f >::iterator px = pixels.begin(); px != pixels.end(); ++px) {
		if (px->a != 0.0f) {
			px->rgb /= px->a;
			px->a = 1.0f;
		} else {
			px->a = 0.0f;
		}
	}

	double after = now();
	double amt = after - before;
	std::cout << "   " << amt * 1000 << std::endl;
	if (amt < min) min = amt;

	if (iter + 1 == Iters) { //convert to ints and save:
		vector< uint32_t > data(pixels.size());
		for (unsigned int i = 0; i < data.size(); ++i) {
			uint32_t r = clamp(pixels[i].r);
			uint32_t g = clamp(pixels[i].g);
			uint32_t b = clamp(pixels[i].b);
			uint32_t a = clamp(pixels[i].a);
			data[i] = (a << 24) | (b << 16) | (g << 8) | (r);
		}
		save_png(out, size.x, size.y, &data[0]);
	}

	} //for (iter)

	std::cout << "When compiled with " << COMPILE << std::endl;
	std::cout << " took " << min * 1000 << " msec." << std::endl;
}
