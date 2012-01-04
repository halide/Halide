#include <sys/time.h>
#include <FImage.h>
using namespace FImage;

void *watchdog(void *arg) {
  usleep(((useconds_t *)arg)[0]);
  printf("Took too long, bailing out\n");
  exit(-1);
}

int main(int argc, char **argv) {

  UniformImage input(UInt(16), 2);
  Func blur_x, blur_y;  
  Var x, y, xi, yi;

  // The algorithm
  blur_x(x, y) = (input(x+7, y) + input(x+8, y) + input(x+9, y))/3;
  blur_y(x, y) = (blur_x(x, y+7) + blur_x(x, y+8) + blur_x(x, y+9))/3; 
  
  /*blur_y.tile(x, y, xi, yi, 64, 64);
  blur_y.vectorize(xi, 8);
  blur_y.parallel(y);
  blur_x.chunk(x);
  blur_x.vectorize(x, 8);*/
  
  Image<uint16_t> test_input(256*9, 256*7);
  input = test_input;
  return blur_y.autotune(argc, argv, {256*8, 256*6});
}
