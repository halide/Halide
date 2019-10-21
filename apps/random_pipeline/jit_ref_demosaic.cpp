#include <cstdlib>
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

#include "Halide.h"
#include "HalideBuffer.h"

#ifdef G_B
#include "demosaic_ref_g_b_generator.cpp"
#define TARGET_DATA "/g_at_b_dense.data"
#endif

#ifdef B_GR
#include "demosaic_ref_b_gr_generator.cpp"
#define TARGET_DATA "/b_at_gr_dense.data"
#endif

using namespace Halide;

using inputT = int16_t;
using outputT = int16_t;
using lossT = float;

const int input_w = 64;
const int input_h = 64;
const int input_c = 1;
const int output_w = 60;
const int output_h = 60;
const int output_c = 1;

template <typename T>
void dump_buff(Buffer<T> &buff) {
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
Buffer<T> buffer_from_file(const std::string &filename, const std::vector<int> &shape) {
    Buffer<T> buf(shape);

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
void load_buffer_from_file(Buffer<T> &buf, const std::string &filename) {
    std::ifstream i(filename, std::ios_base::binary);
    i.read((char *)(buf.data()), buf.size_in_bytes());
    i.close();

    if (i.fail()) {
        std::cerr << "Could not load buffer from file: " << filename << "\n";
        std::terminate();
    }
}

template <typename T>
void rand_fill(Buffer<T> &buff, int seed) {
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
    std::string data_files(argv[1]);
    std::string output_dir(argv[2]);
    int num_images = atoi(argv[3]);

    std::vector<int> input_shape = {input_w, input_h, input_c};
    std::vector<int> output_shape = {output_w, output_h, output_c};
    float loss = 0;

    int batch_size = 1;
    float learning_rate = 0.1f;
    int timestep = 0;

    std::string loss_filename = output_dir + "/losses.txt";

    // create generator
    GeneratorContext context(Target("host"));
    auto gen = DemosaicPipeline<false>::create(context); 
    // set the constant parameters
    gen->num_input_buffers.set(4);
    gen->num_output_buffers.set(1);
    gen->input_w.set(input_w);
    gen->input_h.set(input_h);
    gen->input_c.set(input_c);
    gen->output_w.set(output_w);
    gen->output_h.set(output_h);
    gen->output_c.set(output_c);
    gen->shift.set(2);

    // create input buffers 
    Buffer<outputT> correct_output = Buffer<outputT>(output_w, output_h, output_c);
    Buffer<inputT> input0 = Buffer<inputT>(input_w, input_h, input_c);
    Buffer<inputT> input1 = Buffer<inputT>(input_w, input_h, input_c);
    Buffer<inputT> input2 = Buffer<inputT>(input_w, input_h, input_c);
    Buffer<inputT> input3 = Buffer<inputT>(input_w, input_h, input_c);
    // output buffers
    Buffer<lossT> loss_buff = Buffer<lossT>::make_scalar();
    Buffer<outputT> output_buff = Buffer<outputT>(output_w, output_h, output_c);

    // configure the pipeline
    gen->apply(batch_size, learning_rate, timestep, input0, input1, input2, input3, correct_output);

    // hook up the input buffers correctly
    std::vector<Buffer<inputT>> input_buffs = {input0, input1, input2, input3};
    gen->set_inputs(input_buffs);
    
    // iterate over every image in data directory
    std::string image_dir;
    std::ifstream data_files_f(data_files);
    while (getline(data_files_f, image_dir)) {
        load_buffer_from_file<outputT>(correct_output, image_dir + TARGET_DATA);
        load_buffer_from_file<inputT>(input0, image_dir + "/gr.data");
        load_buffer_from_file<inputT>(input1, image_dir + "/r.data");
        load_buffer_from_file<inputT>(input2, image_dir + "/b.data");
        load_buffer_from_file<inputT>(input3, image_dir + "/gb.data");

        Realization r(loss_buff, output_buff);
        gen->realize(r);
        loss += (loss_buff() / (float)num_images);
    }
    // write average loss to file 
    std::ofstream loss_file;
    loss_file.open(loss_filename, std::ofstream::app);
    loss_file << " loss: " << loss << std::endl;
    loss_file.close();
}
