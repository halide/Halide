#include <Halide.h>
using namespace Halide;

int main(int argc, char **argv) {

  int schedule = atoi(argv[1]);

  char buf[1024], blur_x_name[1024], blur_y_name[1024];
  snprintf(buf, 1023, "blur_%d", schedule);
  snprintf(blur_x_name, 1023, "blur_x_%d", schedule);
  snprintf(blur_y_name, 1023, "blur_y_%d", schedule);

  UniformImage input(UInt(16), 2);
  
  Func blur_x(blur_x_name), blur_y(blur_y_name);
  Var x("x"), y("y"), xi("xi"), yi("yi");

  // The algorithm
  blur_x(x, y) = (input(x+1, y) + input(x, y) + input(x-1, y))/3;
  blur_y(x, y) = (blur_x(x, y+1) + blur_x(x, y) + blur_x(x, y-1))/3;
  
  setenv("HL_DISABLE_BOUNDS_CHECKING", "1", 1);      
  if (schedule > 6) {
      setenv("HL_TRACE", "2", 1);      
  } else {
      setenv("HL_TRACE", "0", 1);
  }

  switch (schedule) {
  case 0:
  case 6:
      blur_x.chunk(root, root);
      break;
  case 1:
  case 7:
      blur_x.chunk(x, x);
      break;
  case 2:
  case 8:
      blur_x.chunk(root, x);
      break;
  case 3:
  case 9:
      blur_y.tile(x, y, xi, yi, 8, 8).parallel(y).parallel(x).vectorize(xi, 4);
      blur_x.chunk(x).vectorize(x, 4);
      break;
  case 4:
  case 10:
      blur_x.chunk(root, y).split(x, x, xi, 12).vectorize(xi, 4).parallel(x);
      blur_y.split(x, x, xi, 12).vectorize(xi, 4).parallel(x);
      break;
  case 5:
  case 11:
      blur_y.split(y, y, yi, 6).parallel(y).vectorize(x, 4);
      blur_x.chunk(y, yi).vectorize(x, 4);    
      break;      
  default:
      break;
  }

  blur_y.compileToFile(buf);

  return 0;
}
