#include <Halide.h>

using namespace Halide;
using namespace Halide::Internal;

void print_grid_info(const BranchedLoopsGrid<int>& grid) {
  std::cout << "Grid of " << grid.dim() << " dimensions.\n"
            << "\tExtents: ";
  for (int i = 0; i < grid.dim(); ++i) {
    std::cout << grid.size(i);
    if (i < grid.dim() - 1) {
      std::cout << " x ";
    } else {
      std::cout << "\n";
    }
  }

  for (int i = 0; i < grid.dim(); ++i) {
    std::cout << "\t" << grid.var(i) << ": ";
    for (int j = 0; j < grid.size(i); ++j) {
      std::cout << grid.coords(i)[j] << " ";
    }
    std::cout << "\n";
  }
}

int main() {
  BranchedLoopsGrid<int> grid;

  grid.push_dim("w", 0, 6);
  grid.push_dim("z", 0, 6);
  grid.push_dim("y", 0, 6);
  grid.push_dim("x", 0, 6);

  print_grid_info(grid);

  grid.split("x", 0, 2);
  grid.split("x", 1, 4);

  print_grid_info(grid);

  grid.split("y", 0, 2);
  grid.split("y", 1, 4);

  print_grid_info(grid);

  grid.split("z", 0, 2);
  grid.split("z", 1, 4);

  print_grid_info(grid);

  grid.split("w", 0, 2);
  grid.split("w", 1, 4);

  print_grid_info(grid);

  printf("Success.\n");
  return 0;
}
