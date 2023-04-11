#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>

#include "Weights.h"

// Utility to convert from the old dir-of-raw-data into a new .weights file.
// Should live only long enough for downstream users to convert existing data files
// to the new format.
int main(int argc, char **argv) {
    if (argc != 3) {
        std::cout << "Usage: weights_dir weights_file.weights\n";
        return -1;
    }

    Halide::Internal::Weights w;
    if (!w.load_from_dir(argv[1])) {
        std::cerr << "Unable to read input dir: " << argv[1] << "\n";
        return -1;
    }

    if (!w.save_to_file(argv[2])) {
        std::cerr << "Unable to save output file: " << argv[2] << "\n";
        return -1;
    }

    return 0;
}
