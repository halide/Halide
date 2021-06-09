#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>

// A sample is a featurization + a runtime + some ids, all together in one file.
// This utility concats the runtime and ids onto a featurization to produce a sample.
int main(int argc, char **argv) {
    if (argc != 6) {
        std::cout << "Usage: featurization_to_sample in.featurization runtime pipeline_id schedule_id out.sample\n";
        return -1;
    }

    std::ifstream src(argv[1], std::ios::binary);
    if (!src) {
        std::cerr << "Unable to open input file: " << argv[1] << "\n";
        return -1;
    }

    std::ofstream dst(argv[5], std::ios::binary);
    if (!dst) {
        std::cerr << "Unable to open output file: " << argv[5] << "\n";
        return -1;
    }

    dst << src.rdbuf();

    // Input runtime value is presumed to be in seconds,
    // but sample file stores times in milliseconds.
    float r = atof(argv[2]) * 1000.f;
    int32_t pid = atoi(argv[3]);
    int32_t sid = atoi(argv[4]);

    dst.write((const char *)&r, 4);
    dst.write((const char *)&pid, 4);
    dst.write((const char *)&sid, 4);

    src.close();
    dst.close();

    return 0;
}
