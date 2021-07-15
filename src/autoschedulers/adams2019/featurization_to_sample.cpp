#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>


const int ERROR = -1;
const int SUCCESS = 0;

enum Args {
    Executable,
    InFeaturization,
    Runtime,
    PipelineId,
    ScheduleId,
    OutSample,
    NumberOfArgs
};

// A sample is a featurization + a runtime + some ids, all together in one file.
// This utility concats the runtime and ids onto a featurization to produce a sample.

// Sample command line:
// featurization_to_sample onnx_batch_0006_sample_0027.featurization 0.0022211699999999997 onnx 00060027 onnx_batch_0006_sample_0027.sample
int main(int argc, char **argv) {
    if (argc != NumberOfArgs) {
        std::cout << "Usage: featurization_to_sample in.featurization runtime pipeline_id schedule_id out.sample\n";
        return ERROR;
    }

    // Processing in.featurization parameter
    std::ifstream src(argv[InFeaturization], std::ios::binary);
    if (!src) {
        std::cerr << "Unable to open input file: " << argv[InFeaturization] << "\n";
        return ERROR;
    }

    // Processing out.sample parameter
    std::ofstream dst(argv[OutSample], std::ios::binary);
    if (!dst) {
        std::cerr << "Unable to open output file: " << argv[OutSample] << "\n";
        return ERROR;
    }

    dst << src.rdbuf();

    // Input runtime value is presumed to be in seconds,
    // but sample file stores times in milliseconds.
    // processing run time parameter
    float runtime = atof(argv[Runtime]) * 1000.f;
    // processing pipeline_id parameter
    int32_t pipeline_id = atoi(argv[PipelineId]);
    // processing schedule_id parameter
    int32_t schedule_id = atoi(argv[ScheduleId]);

    dst.write((const char *)&runtime, sizeof(float));
    dst.write((const char *)&pipeline_id, sizeof(int32_t));
    dst.write((const char *)&schedule_id, sizeof(int32_t));

    src.close();
    dst.close();

    return SUCCESS;
}
