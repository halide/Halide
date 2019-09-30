#include <iostream>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <random>

#include <unistd.h>
#include <sys/types.h>

#include "HalideBuffer.h"
#include "random_pipeline_inference.h"

using namespace Halide;

using inputT = int16_t;
using outputT = int16_t;

template <typename T>
void dump_buff(T* data, int n) {
    for (int i = 0; i < n/20; i++) {
        for (int j = 0; j < 20; j++) {
            std::cout << data[i*20+j] << ",";
        }
        std::cout << "\n";
    }
    int remainder = n % 20;
    for (int i = 0; i < remainder; i++) {std::cout << data[n-remainder+i] << ", ";}
    std::cout << "\n";
}

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

int main() {
   	const struct halide_filter_metadata_t *metadata;
		metadata = random_pipeline_inference_metadata();

    std::cout << "num arguments: " << metadata->num_arguments << std::endl;
    std::vector<void*> pipe_args(metadata->num_arguments, nullptr);
    std::vector<Runtime::Buffer<int16_t>> int_buffers(metadata->num_arguments);
    std::vector<Runtime::Buffer<float>> float_buffers(metadata->num_arguments);
    std::vector<halide_scalar_value_t> scalars(metadata->num_arguments);
    int output_id = -1;
		int loss_id = -1;
		std::vector<int> input_ids;

    for (int i = 0; i < metadata->num_arguments; i++) {
        halide_filter_argument_t arg = metadata->arguments[i];
        std::cout << "\n" << arg.name << std::endl;

        std::cout << "has " << arg.dimensions << " dimensions. Type code: " << arg.type.code << ".\n";
        if (arg.buffer_estimates != nullptr) {
            std::vector<int> dims(arg.dimensions, 0);
            for (int d = 0; d < arg.dimensions; d++) {
                dims[d] = *(arg.buffer_estimates[d*2+1]);
            }
            std::cout << "creating buffer with shape: ";
						if (arg.type.code == 0) {
								int_buffers[i] = Runtime::Buffer<int16_t>(dims);
								for (int dd = 0; dd < int_buffers[i].dimensions(); dd++) 
										std::cout << "min: " << int_buffers[i].dim(dd).min() << " ext: " << int_buffers[i].dim(dd).extent() << ",  ";
								std::cout << std::endl;
            		pipe_args[i] = int_buffers[i].raw_buffer();
						}
						else if (arg.type.code == 2) {
								float_buffers[i] = Runtime::Buffer<float>(dims);
								for (int dd = 0; dd < float_buffers[i].dimensions(); dd++) 
										std::cout << "min: " << float_buffers[i].dim(dd).min() << " ext: " << float_buffers[i].dim(dd).extent() << ",  ";
								std::cout << std::endl;
            		pipe_args[i] = float_buffers[i].raw_buffer();
						}
        } else {
            assert(arg.dimensions == 0);
            if (arg.kind != 0) { // type is a buffer of zero dim
                std::cout << "creating scalar buffer" << std::endl;
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
                    std::cout << "creating int32\n";
                    scalars[i].u.i32 = 1;
                    pipe_args[i] = &scalars[i];
                } else if (arg.type.code == 1) {
                    scalars[i].u.u32 = 1;
                    pipe_args[i] = &scalars[i];
                } else if (arg.type.code == 2) {
                    std::cout << "creating float\n";
                    scalars[i].u.f32 = 0.1f;
                    pipe_args[i] = &scalars[i];
                }
            }
        }
				
      	size_t found = (std::string(arg.name)).find("correct_output");
        if (found != std::string::npos) {
						if (arg.type.code == 0)
            		int_buffers[i].fill(1);
						else if (arg.type.code == 2)
								float_buffers[i].fill(1.0f);
        }
				found = (std::string(arg.name)).find("input_");
        if (found != std::string::npos) {
            std::cout << "filling " << arg.name <<  " with uniform random values between 0 and 1\n";
						input_ids.push_back(i);
						if (arg.type.code == 0)
								rand_fill(int_buffers[i], i);
						else if (arg.type.code == 2)
								rand_fill(float_buffers[i], i);
				}
        if (std::string(arg.name) == std::string("output_0")) {
            output_id = i;
        }
				if (std::string(arg.name) == std::string("loss_output")) {
						loss_id = i;
				}
    }
		random_pipeline_inference_argv(&pipe_args[0]);
		std::cout << "input values: " << std::endl;
		for (auto i : input_ids) {
				std::cout << i << std::endl;
				dump_buff(int_buffers[i]);
				std::cout << "=================\n";
		}
		std::cout << "output values: " << std::endl;
		dump_buff(int_buffers[output_id]);
		std::cout << "loss output: " << std::endl;
		std::cout << float_buffers[loss_id]() << std::endl;		
		//std::cout << float_buffers[loss_id] << std::endl;
}
