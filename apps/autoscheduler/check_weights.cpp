#include <cmath>
#include <fstream>
#include <string>
#include <iostream>

#include "CostModel.h"
#include "NetworkSize.h"

using namespace Halide;

std::string getenv_safe(const char *key) {
    const char *value = getenv(key);
    if (!value) value = "";
    return value;
}

int check_weights(const std::string &filename, const std::vector<int> &shape) {
    Runtime::Buffer<float> buf(shape);

    std::ifstream i(filename, std::ios_base::binary);
    i.read((char *)(buf.data()), buf.size_in_bytes());
    i.close();

    int num_nans = 0;

    buf.for_each_value([&filename, &num_nans](float &f) {
        if (std::isnan(f)) {
            std::cerr << "NaN found in weights: " << filename << "\n";
            ++num_nans;
        }
    });

    return num_nans;
}

int main(int argc, char **argv) {
    using std::string;

    string weights_dir = getenv_safe("HL_WEIGHTS_DIR");
    if (weights_dir.empty()) {
        std::cout << "No weights_dir specified. Exiting.\n";
        return 0;
    }

    std::cout << "Checking weights...\n";

    int num_nans = check_weights(weights_dir + "/head1_conv1_weight.data", {head1_channels, head1_w, head1_h});
    num_nans = check_weights(weights_dir + "/head1_conv1_bias.data", {head1_channels});

    num_nans = check_weights(weights_dir + "/head2_conv1_weight.data", {head2_channels, head2_w});
    num_nans = check_weights(weights_dir + "/head2_conv1_bias.data", {head2_channels});

    num_nans = check_weights(weights_dir + "/trunk_conv1_weight.data", {conv1_channels, head1_channels + head2_channels});
    num_nans = check_weights(weights_dir + "/trunk_conv1_bias.data", {conv1_channels});

    std::cout << "Number of NaNs found: " << num_nans << "\n";

    return 0;
}
