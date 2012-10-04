#include <Halide.h>
using namespace Halide;

#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/time.h>
#include <image_io.h>

using namespace std;

Var x, y;

// ###### Standard differential quantities ######

Func dx(Func f){

  Func out;
  out(x,y) = 0.5f * (f(x+1,y) - f(x-1,y));

  return out;
}

Func dy(Func f){

  Func out;
  out(x,y) = 0.5f * (f(x,y+1) - f(x,y-1));

  return out;
}

Func lap(Func f){

  Func out;
  out(x,y) = f(x+1,y) + f(x-1,y) + f(x,y+1) + f(x,y-1) - 4.0f * f(x,y);

  return out;
}

// ###### Regularization term ######

Func distReg_p2(Func phi){

  Expr phi_x = dx(phi);
  Expr phi_y = dy(phi);
  Expr s = sqrt(phi_x * phi_x + phi_y * phi_y);

  Expr ps = select(s <= 1.0f,
                   sin(2.0f * (float)M_PI * s / (2.0f * (float)M_PI)),
                   s - 1.0f);

  Expr n = select(ps == 0.0f, 1.0f, ps);
  Expr d = select(s == 0.0f, 1.0f, s);  

  Func proxy_x;
  proxy_x = (n / d) * phi_x - phi_x;
  
  Func proxy_y;
  proxy_y = (n / d) * phi_y - phi_y;

  Func out;
  out = dx(proxy_x) + dy(proxy_y) + lap(phi);

  return out;
}


Func Dirac(Func input,
	   const float sigma){

  Func out;
  out(x,y) = select((input(x,y) <= sigma) && (input(x,y) >= -sigma),
		    1.0f / (2.0f * sigma) * (1.0f + cos((float)M_PI * input(x,y) / sigma)),
		    0.0f);

  return out;
}

Func drlse_edge(Func phi_0,
		Func g,
		const float lambda,
		const float mu,
		const float alpha,
		const float epsilon,
		const float timestep,
		const int   iter){
  
  vector<Func> phi(iter+1);
  phi[0](x,y) = phi_0(x,y);
  
  Expr vx,vy;
  vx = dx(g)(x,y);
  vy = dy(g)(x,y);

  for(int k = 0 ; k < iter ; ++k){
    
    Expr phi_x = dx(phi[k])(x,y);
    Expr phi_y = dy(phi[k])(x,y);

    Expr s = sqrt(phi_x * phi_x + phi_y * phi_y);
    
    const float smallNumber = 1e-10f;

    Func Nx,Ny;
    Nx(x,y) = phi_x / (s + smallNumber);
    Ny(x,y) = phi_y / (s + smallNumber);
       
    Expr ddx = dx(Nx)(x,y);
    Expr ddy = dy(Ny)(x,y);
    Expr curvature = ddx + ddy;
    Expr distRegTerm = distReg_p2(phi[k])(x,y);
    Expr diracPhi = Dirac(phi[k],epsilon)(x,y);
    Expr areaTerm = diracPhi * g(x,y);
    Expr edgeTerm = diracPhi * ((vx * Nx(x,y) + vy * Ny(x,y)) + g(x,y) * curvature);

    phi[k+1](x,y) = phi[k](x,y) + timestep * (mu * distRegTerm + lambda * edgeTerm + alpha * areaTerm);
  }

  return phi.back();
}

Func blur(Func image, const float sigma) {
  Func gaussian;
  gaussian(x) = exp(-(x/sigma)*(x/sigma)*0.5f);

  // truncate to 3 sigma and normalize
  int radius = int(3*sigma + 1.0f);
  RDom i(-radius, 2*radius+1);
  Func normalized;
  normalized(x) = gaussian(x) / sum(gaussian(i)); // Uses an inline reduction

  // Convolve the input using two reductions
  Func blurx, blury;
  blurx(x, y) += image(x+i, y) * normalized(i);
  blury(x, y) += blurx(x, y+i) * normalized(i);

  // Schedule the lot as root 
  image.root();
  gaussian.root();
  normalized.root();
  blurx.root();
  blury.root();

  return blury;
}

int main(int argc, char **argv) {
  
  if (argc < 2) {
      printf("Usage: ./halide_snake blood_cells.png max_iterations\n");
      return 0;
  }

  const float timestep = 5.0f;
  const float mu = 0.2f / timestep;
  const int iter_inner = 1; // 5;
  const int iter_outer = argc > 3 ? atoi(argv[3]) : 1000;
  const float lambda = 6.0f; //6.0;
  float alpha = 1.5f;
  const float epsilon = 1.5;
  const float sigma = 1.5f;
  const int padding = 5;
  const float background = 255.0f * 0.98f;
  const int selectPadding = 10;

  // ###### Prepare the input image ######
  // Convert to gray scale, define boundary conditions

  printf("Loading input image\n");  
  Image<uint8_t> im = load<uint8_t>(argv[1]); 
  
  Func gray;
  gray(x, y) = max(cast<float>(im(x, y, 0)), max(cast<float>(im(x, y, 1)), cast<float>(im(x, y, 2))));

  Func clamped;
  clamped(x, y) = gray(clamp(x, 0, im.width()),
                       clamp(y, 0, im.height()));
    
  // ###### Compute the G function ######
  // Some simple computation, cache the result because never changes later
  
  Func blurred_input;
  blurred_input = blur(clamped,sigma);

  Func input_dx, input_dy;
  input_dx = dx(blurred_input);
  input_dy = dy(blurred_input);

  Func g_proxy;
  g_proxy = 1.0f / (1.0f + input_dx * input_dx + input_dy * input_dy);
  
  // Spill to a concrete array
  Image<float> g_buf = g_proxy.realize(im.width(), im.height());

  // ###### Initialize the selection  ######
  // Create a big rectangle, store it in an array
  
  Func phi_init;
  phi_init(x,y) = select((x >= selectPadding)
			 && (x < im.width() - selectPadding)
			 && (y >= selectPadding)
			 && (y < im.height() - selectPadding),
			 -2.0f, 2.0f);
  Image<float> phi_buf = phi_init.realize(im.width(), im.height());
  Image<float> phi_buf2(im.width(), im.height());

  // ###### Define the outer loop ######
  // Read from the buffer, compute the result, possibly create code for intermediate images
  
  // Make a uniform image to hold this input, because we'll be wanting
  // to change it every iteration
  UniformImage phi_input(Float(32), 2);

  Func phi_clamped;
  phi_clamped(x,y) = phi_input(clamp(x,0,phi_buf.width()-1),
                               clamp(y,0,phi_buf.height()-1));
  
  // g stays fixed, so we can skip the uniform image and have it
  // always read straight from the buffer
  Func g_clamped;
  g_clamped(x, y) = g_buf(clamp(x, 0, g_buf.width()-1),
                          clamp(y, 0, g_buf.height()-1));
    
  Func phi_new;
  phi_new = drlse_edge(phi_clamped,g_clamped,lambda,mu,alpha,epsilon,timestep,iter_inner);

  if (use_gpu()) {
    phi_new.cudaTile(x, y, 16, 16);
  } else {
    phi_new.parallel(y).vectorize(x, 4);
  }
  phi_new.compileJIT();
  
  // ###### Run the outer loop ######
  
  timeval t1, t2;
  gettimeofday(&t1, NULL);
  for(int n = 0 ; n < iter_outer ; ++n){

    if (n % 10 == 9) {
      // Timing code. The time per update increases as the distance
      // function grows inwards.
      gettimeofday(&t2, NULL);
      float t = (t2.tv_sec - t1.tv_sec)*1000.0f + (t2.tv_usec - t1.tv_usec)/1000.0f;
      printf("Iteration %d / %d. Averate time per iteration = %f ms\n", 
             (n+1), iter_outer, t/n);
    }

    phi_input = phi_buf;
    phi_new.realize(phi_buf2);
    std::swap(phi_buf, phi_buf2);
  }
    
  // ###### Save the result ######

  Func masked;
  Var c;
  // Dim the unselected areas for visualization
  masked(x, y, c) = select(phi_buf(x, y) < 0.0f, im(x, y, c), im(x, y, c)/4);
  Image<uint8_t> out = masked.realize(im.width(), im.height(), 3);
  save(out, argv[2]);
}
