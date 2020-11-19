/*
    tflite_exploder takes a given .tflite file and breaks it up
    into a separate .tflite file, each containing a single op.

    The intent here is to make it easy to microbenchmark and/or test individual ops
    vs reference implementations (eg TFLite itself) with minimum of special casing.

    Usage is basically something like:

    $ mkdir -p local_testdata/some_big_pipeline
    $ bin/host/tflite_exploder some_big_pipeline.tflite local_testdata/some_big_pipeline
    $ ls -l local_testdata/some_big_pipeline
        total 7320
        -rw-r--r--  1 srj  primarygroup     2016 Oct 22 17:14 0.tflite
        -rw-r--r--  1 srj  primarygroup     1536 Oct 22 17:14 1.tflite
        ...
        -rw-r--r--  1 srj  primarygroup      800 Oct 22 17:14 64.tflite

    (In this example there are 65 ops in `some_big_pipeline`)

    Then you may want to run them all thru the benchmark, eg

    $ for f in local_testdata/some_big_pipeline/*; do bin/host/benchmark $f; done

    (Note that `local_testdata` is in the .gitignore file for this app,
    so it's a good place to dump all your local experiments.)

    TODO: consider adding an option to strip out the data in the tensors (ie the buffers)?
    TODO: consider adding a filter to only extract ops of a certain type (eg Conv2D)?
*/

#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "app_util.h"
#include "flatbuffers/flatbuffers.h"
#include "tflite_schema_direct_generated.h"

using app_util::make_unique;

namespace {

tflite::BuiltinOperator get_builtin_code(const tflite::OperatorCode *op_code) {
    return std::max(
        op_code->builtin_code(),
        static_cast<tflite::BuiltinOperator>(op_code->deprecated_builtin_code()));
}

void write_entire_file(const std::string &filename, const void *source, size_t source_len) {
    std::ofstream f(filename, std::ios::out | std::ios::binary);
    APP_CHECK(f.is_open()) << "Unable to open file: " << filename;

    f.write(reinterpret_cast<const char *>(source), source_len);
    f.flush();
    APP_CHECK(f.good()) << "Unable to write file: " << filename;
    f.close();
}

void write_entire_file(const std::string &filename, const std::vector<char> &source) {
    write_entire_file(filename, source.data(), source.size());
}

}  // namespace

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: %s input.tflite output_dir\n", argv[0]);
        return 0;
    }

    std::string input_file = argv[1];
    std::string output_dir = argv[2];

    std::vector<char> buffer = app_util::read_entire_file(input_file);

    const tflite::Model *model = tflite::GetModel(buffer.data());

    const auto *opcodes = model->operator_codes();

    const auto *subgraphs = model->subgraphs();
    APP_CHECK(subgraphs->size() == 1) << "Only 1 subgraph is currently supported.";
    const tflite::SubGraph *subgraph = subgraphs->Get(0);

    int op_index = -1;
    for (const tflite::Operator *op : *subgraph->operators()) {
        op_index++;

        const auto *opcode = opcodes->Get(op->opcode_index());
        std::string op_name = tflite::EnumNameBuiltinOperator(get_builtin_code(opcode));

        std::map<int32_t, int32_t> old_to_new_tensor_map;
        APP_CHECK(op->inputs() != nullptr);
        for (int32_t i : *op->inputs()) {
            old_to_new_tensor_map[i] = 0;
        }
        APP_CHECK(op->outputs() != nullptr);
        for (int32_t i : *op->outputs()) {
            old_to_new_tensor_map[i] = 0;
        }
        // Apparently, iterating over a null flatbuffer::Vector will just provide
        // weird garbage values, so check for null
        if (op->intermediates() != nullptr) {
            for (int32_t i : *op->intermediates()) {
                old_to_new_tensor_map[i] = 0;
            }
        }

        std::map<uint32_t, uint32_t> old_to_new_buffer_map;
        // buffer 0 is reserved for 'nothing'
        old_to_new_buffer_map[0] = 0;
        for (auto &m : old_to_new_tensor_map) {
            const tflite::Tensor *t = subgraph->tensors()->Get(m.first);
            old_to_new_buffer_map[t->buffer()] = 0; // placeholder, will fill in below
        }

        std::vector<std::unique_ptr<tflite::BufferT>> new_buffers;
        new_buffers.emplace_back(model->buffers()->Get(0)->UnPack());
        for (auto &m : old_to_new_buffer_map) {
            if (m.first == 0) {
                continue;  // special case
            }
            m.second = (int32_t)new_buffers.size();
            new_buffers.emplace_back(model->buffers()->Get(m.first)->UnPack());
        }

        std::vector<std::unique_ptr<tflite::TensorT>> new_tensors;
        for (auto &m : old_to_new_tensor_map) {
            m.second = (int32_t)new_tensors.size();
            new_tensors.emplace_back(subgraph->tensors()->Get(m.first)->UnPack());
            auto *t = new_tensors.back().get();
            t->buffer = old_to_new_buffer_map[t->buffer];
        }

        // Make a copy of the op we can modify in place; all we need to do is update the tensor indices
        std::unique_ptr<tflite::OperatorT> new_op(op->UnPack());
        for (int32_t &i : new_op->inputs) {
            i = old_to_new_tensor_map[i];
        }
        for (int32_t &i : new_op->outputs) {
            i = old_to_new_tensor_map[i];
        }
        for (int32_t &i : new_op->intermediates) {
            i = old_to_new_tensor_map[i];
        }

        // Inputs and Outputs to the op are also the inputs and outputs to the subgraph.
        auto new_subgraph = make_unique<tflite::SubGraphT>();
        new_subgraph->tensors = std::move(new_tensors);
        for (int32_t i : *op->inputs()) {
            new_subgraph->inputs.emplace_back(old_to_new_tensor_map[i]);
        }
        for (int32_t i : *op->outputs()) {
            new_subgraph->outputs.emplace_back(old_to_new_tensor_map[i]);
        }
        new_subgraph->operators.emplace_back(std::move(new_op));

        std::unique_ptr<tflite::ModelT> new_model = tflite::UnPackModel(buffer.data());
        new_model->subgraphs.resize(1);
        new_model->subgraphs[0] = std::move(new_subgraph);
        if (!new_model->description.empty()) {
            new_model->description += " (tflite_exploder/" + std::to_string(op_index) + ")";
        }
        new_model->buffers = std::move(new_buffers);
        // Blow away all the metadata (we'll just assume we can live without it).
        new_model->metadata_buffer.clear();
        new_model->metadata.clear();
        // signature_defs is optional -- not sure if we need it for out purposes.
        // TODO: might need to translate it.
        new_model->signature_defs.clear();

        flatbuffers::FlatBufferBuilder fbb;
        auto model_offset = tflite::Model::Pack(fbb, new_model.get());

        fbb.Finish(model_offset, tflite::ModelIdentifier());

        std::string outpath = output_dir + "/" + std::to_string(op_index) + "." + op_name + ".tflite";
        std::cerr << "Writing to " << outpath << "\n";
        write_entire_file(outpath, fbb.GetBufferPointer(), fbb.GetSize());
    }

    return 0;
}
