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

int main(int argc, char **argv) {
    using std::string;

    string weights_dir = getenv_safe("HL_WEIGHTS_DIR");
    if (weights_dir.empty()) {
        std::cout << "No weights_dir specified. Exiting.\n";
        return 0;
    }

    std::unique_ptr<CostModel> tpp = CostModel::make_default(weights_dir, weights_dir, true);

    std::cout << "Saving new weights...\n";
    tpp->save_weights();
    std::cout << "Weights saved.\n";

    return 0;
}
