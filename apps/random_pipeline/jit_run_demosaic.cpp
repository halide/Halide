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
#include "new_generator.cpp"


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
    int num_pipes = atoi(argv[4]);
    int job_id = atoi(argv[5]);

    std::vector<int> input_shape = {input_w, input_h, input_c};
    std::vector<int> output_shape = {output_w, output_h, output_c};
    float loss = 0;

    // create generator
    GeneratorContext context(Target("host"));
    auto gen = RandomPipeline<false>::create(context); 
    // set the constant parameters
    gen->num_input_buffers.set(4);
    gen->num_output_buffers.set(1);
    gen->input_w.set(input_w);
    gen->input_h.set(input_h);
    gen->input_c.set(input_c);
    gen->output_w.set(output_w);
    gen->output_h.set(output_h);
    gen->output_c.set(output_c);
    gen->max_stages.set(1);
    gen->shift.set(2);

    int batch_size = 1;
    float learning_rate = 0.1f;
    int timestep = 0;

    // for each pipeline do:
    for (int p = 0; p < num_pipes; p++) {
        // seed the generator with the pipeline id
        gen->seed.set(job_id * num_pipes + p);
        
        // iterate over every image in data directory
        std::string image_dir;
        std::ifstream data_files_f(data_files);
        while (getline(data_files_f, image_dir)) {
            Buffer<outputT> correct_output = buffer_from_file<outputT>(image_dir + "/g_at_b.data", output_shape);
            Buffer<inputT> input0 = buffer_from_file<inputT>(image_dir + "/gr.data", input_shape);

            Buffer<inputT> input1 = buffer_from_file<inputT>(image_dir + "/r.data", input_shape);
            Buffer<inputT> input2 = buffer_from_file<inputT>(image_dir + "/b.data", input_shape);
            Buffer<inputT> input3 = buffer_from_file<inputT>(image_dir + "/gb.data", input_shape);
            Buffer<lossT> loss_buff = Buffer<lossT>::make_scalar();

            Buffer<outputT> output_buff = Buffer<outputT>(output_w, output_h, 1);

            // run the pipeline
            gen->apply(batch_size, learning_rate, timestep, input0, input1, input2, input3, correct_output, output_buff, loss_buff);
            gen->realize(60, 60, 1);
            loss += (loss_buff() / (float)num_images);
        }
        // write average loss to file 
        std::ofstream loss_file;
        loss_file.open (output_dir + "/loss.txt");
        loss_file << loss << std::endl;
        loss_file.close();
    }
}

