#include <iostream>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <map>
#include <random>

#include <unistd.h>
#include <sys/types.h>

#include "HalideBuffer.h"
#include "random_pipeline_inference.h"



using namespace Halide;

using inputT = int16_t;
using outputT = int16_t;
const int input_w = 64;
const int input_h = 64;
const int input_c = 1;
const int output_w = 60;
const int output_h = 60;
const int output_c = 1;

template <typename T>
void dump_buff(Runtime::Buffer<T> &buff) {
		for (int c = 0; c < buff.dim(2).extent(); c++) {
				for (int h = 0; h < buff.dim(1).extent(); h++) {
						for (int w = 0; w < buff.dim(0).extent(); w++) {
								std::cout << buff(w, h, c) << ",";
						}
						std::cout << "\n";
				}
				std::cout << "\n";
		}
}

template <typename T>
Runtime::Buffer<T> buffer_from_file(const std::string &filename, const std::vector<int> &shape) {
    Runtime::Buffer<T> buf(shape);

    std::ifstream i(filename, std::ios_base::binary);
    i.read((char *)(buf.data()), buf.size_in_bytes());
    i.close();

    if (i.fail()) {
        std::cerr << "Could not load buffer from file: " << filename << "\n";
				std::terminate();
    }

    return buf;
}

template <typename T>
void rand_fill(Runtime::Buffer<T> &buff, int seed) {
		std::mt19937 rng(seed);
		std::uniform_real_distribution<float> dist(0.f, 10.f);
		for (int c = 0; c < buff.dim(2).extent(); c++) {
				for (int h = 0; h < buff.dim(1).extent(); h++) {
						for (int w = 0; w < buff.dim(0).extent(); w++) {
								buff(w, h, c) = (inputT)dist(rng);
						}
				}
		}
}

int main(int argc, char *argv[]) {
    std::string data_dir(argv[1]);
		std::string output_dir(argv[2]);
		int num_images = argv[3];

   	const struct halide_filter_metadata_t *metadata;
		metadata = random_pipeline_inference_metadata();

		std::vector<int> input_shape = {input_w, input_h, input_c};
		std::vector<int> output_shape = {output_w, output_h, output_c};

    //std::cout << "num arguments: " << metadata->num_arguments << std::endl;
    std::vector<void*> pipe_args(metadata->num_arguments, nullptr);
    std::vector<Runtime::Buffer<int16_t>> int_buffers(metadata->num_arguments);
    std::vector<Runtime::Buffer<float>> float_buffers(metadata->num_arguments);
    std::vector<halide_scalar_value_t> scalars(metadata->num_arguments);
		int loss_id = -1;
		float loss = 0;
    // iterate over every image in data directory
	  for(auto& p: std::filesystem::directory_iterator(data_dir)) {
	      for (int i = 0; i < metadata->num_arguments; i++) {
						halide_filter_argument_t arg = metadata->arguments[i];

						/** load fixed inputs **/
						if (std::string(arg.name) == "correct_output") {
								Runtime::Buffer<outputT> correct_output = buffer_from_file<outputT>(p.path() + "/g_at_b.data", output_shape);
								int_buffers[i] = correct_output;
								pipe_args[i] = int_buffers[i].raw_buffer();				
						} else if (std::string(arg.name) == "input_0") {
								Runtime::Buffer<inputT> input = buffer_from_file<inputT>(p.path() + "/gr.data", input_shape);
								int_buffers[i] = input;
								pipe_args[i] = int_buffers[i].raw_buffer();
						} else if (std::string(arg.name) == "input_1") {
								Runtime::Buffer<inputT> input = buffer_from_file<inputT>(p.path() + "/r.data", input_shape);
								int_buffers[i] = input;
								pipe_args[i] = int_buffers[i].raw_buffer();
						} else if (std::string(arg.name) == "input_2") {
								Runtime::Buffer<inputT> input = buffer_from_file<inputT>(p.path() + "/b.data", input_shape);
								int_buffers[i] = input;
								pipe_args[i] = int_buffers[i].raw_buffer();
						} else if (std::string(arg.name) == "input_3") {
								Runtime::Buffer<inputT> input = buffer_from_file<inputT>(p.path() + "/gb.data", input_shape);
								int_buffers[i] = input;
								pipe_args[i] = int_buffers[i].raw_buffer();
						} else if (arg.buffer_estimates != nullptr) {
								// create buffers for other inputs / outputs 
								std::vector<int> dims(arg.dimensions, 0);
								for (int d = 0; d < arg.dimensions; d++) {
										dims[d] = *(arg.buffer_estimates[d*2+1]);
								}
								//std::cout << "creating buffer with shape: ";
								if (arg.type.code == 0) {
										int_buffers[i] = Runtime::Buffer<int16_t>(dims);
										/**
										for (int dd = 0; dd < int_buffers[i].dimensions(); dd++) 
												std::cout << "min: " << int_buffers[i].dim(dd).min() << " ext: " << int_buffers[i].dim(dd).extent() << ",  ";
										std::cout << std::endl;
										**/
										pipe_args[i] = int_buffers[i].raw_buffer();
								}
								else if (arg.type.code == 2) {
										float_buffers[i] = Runtime::Buffer<float>(dims);
										for (int dd = 0; dd < float_buffers[i].dimensions(); dd++) 
												std::cout << "min: " << float_buffers[i].dim(dd).min() << " ext: " << float_buffers[i].dim(dd).extent() << ",  ";
										std::cout << std::endl;
										pipe_args[i] = float_buffers[i].raw_buffer();
								}
						} else { // creating a scalar input param
								assert(arg.dimensions == 0);
								if (arg.kind != 0) { // type is a buffer of zero dim
										if (arg.type.code == 0) {
												int_buffers[i] = Runtime::Buffer<int16_t>::make_scalar();
												pipe_args[i] = int_buffers[i].raw_buffer();
										} else if (arg.type.code == 2) {
												float_buffers[i] = Runtime::Buffer<float>::make_scalar();
												pipe_args[i] = float_buffers[i].raw_buffer();
										}
								} else {
										// assume types are all 32 bit
										if (arg.type.code == 0) {
												scalars[i].u.i32 = 1;
												pipe_args[i] = &scalars[i];
										} else if (arg.type.code == 1) {
												scalars[i].u.u32 = 1;
												pipe_args[i] = &scalars[i];
										} else if (arg.type.code == 2) {
												scalars[i].u.f32 = 0.1f;
												pipe_args[i] = &scalars[i];
										}
								}
						}
						
						if (std::string(arg.name) == std::string("loss_output")) {
								loss_id = i;
						}
				}

				// run the pipeline
				random_pipeline_inference_argv(&pipe_args[0]);
				loss += (float_buffers[loss_id]() / (float)num_images);
		}
		// write average loss to file 
		std::ofstream loss_file;
		loss_file.open (output_dir + "/loss.txt");
		loss_file << loss << std::endl;
		loss_file.close();
}
