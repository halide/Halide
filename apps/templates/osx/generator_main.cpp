#include "Halide.h"

#include <fstream>

using namespace Halide;
using namespace Halide::Internal;
using namespace std;

int main(int argc, char **argv) {

  return generate_filter_main(argc, argv, std::cerr);
}
