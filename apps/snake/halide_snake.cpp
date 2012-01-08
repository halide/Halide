#include <FImage.h>
using namespace FImage;

#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include "../png.h"

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

  Expr phi_x,phi_y;
  phi_x = dx(phi)(x,y);
  phi_y = dy(phi)(x,y);

  Expr s;
  s = sqrt(phi_x * phi_x + phi_y * phi_y);

  Expr ps;
  ps = Select(s <= 1.0f,
	      sin(2.0f * (float)M_PI * s / (2.0f * (float)M_PI)),
	      s - 1.0f);

  Expr dps;

  Expr n = Select(ps == 0.0f,
		  1.0f,
		  ps);

  Expr d = Select(s == 0.0f,
		  1.0f,
		  s);
  
  dps = n / d;

  Func proxy_x;
  proxy_x(x,y) = dps * phi_x - phi_x;
  
  Func proxy_y;
  proxy_y(x,y) = dps * phi_y - phi_y;

  Func out;
  out(x,y) =
    dx(proxy_x)(x,y)
    + dy(proxy_y)(x,y)
    + lap(phi)(x,y);

  return out;
}


Func Dirac(Func input,
	   const float sigma){

  Func out;
  out(x,y) = Select((input(x,y) <= sigma) && (input(x,y) >= -sigma),
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
    
    Expr phi_x,phi_y;
    phi_x = dx(phi[k])(x,y);
    phi_y = dy(phi[k])(x,y);

    Expr s;
    s = sqrt(phi_x * phi_x + phi_y * phi_y);

    const float smallNumber = 1e-10f;

    Func Nx,Ny;
    Nx(x,y) = phi_x / (s + smallNumber);
    Ny(x,y) = phi_y / (s + smallNumber);
    
    Expr ddx,ddy;
    ddx = dx(Nx)(x,y);
    ddy = dy(Ny)(x,y);

    Expr curvature;
    curvature = ddx + ddy;

    Expr distRegTerm;
    distRegTerm = distReg_p2(phi[k])(x,y);

    Expr diracPhi;
    diracPhi = Dirac(phi[k],epsilon)(x,y);

    Expr areaTerm;
    areaTerm = diracPhi * g(x,y);

    Expr edgeTerm;
    edgeTerm = diracPhi * ((vx * Nx(x,y) + vy * Ny(x,y)) + g(x,y) * curvature);

    phi[k+1](x,y) = phi[k](x,y) + timestep * (mu * distRegTerm + lambda * edgeTerm + alpha * areaTerm);
  }

  return phi.back();
}



// Blur function using the heat equation and the formula sigma = sqrt(2t)
Func blur(Func image,
	  const float sigma){

  const float dt = 0.25f;
  const int iter = 0.5f * sigma * sigma / dt;

  vector<Func> out(iter+1);
  out[0](x,y) = image(x,y);
  
  for(int n = 0 ; n < iter ; ++n){
    out[n+1](x,y) = out[n](x,y) + dt * lap(out[n])(x,y);
  }

  out.back().root();
  
  return out.back();
}


int main(int argc, char **argv) {
  
  const float timestep = 5.0f;
  const float mu = 0.2f / timestep;
  const int iter_inner = 1; // 5;
  const int iter_outer = 100; // 1000;
  const int iter_refine = 10; // 10;
  const float lambda = 6.0f; //6.0;
  float alpha = 1.5f;
  const float epsilon = 1.5;
  const float sigma = 1.5f;
  const int padding = 5;
  const float background = 255.0f * 0.98f;
  const int selectPadding = 2;
  const int phiPadding = selectPadding + 2;

  UniformImage image(UInt(8),3);

  printf("Loading input image\n");
  
  Image<uint8_t> im = load<uint8_t>(argv[1]);
  image = im; // This binds a concrete image to the uniform image.



  
  // ###### Prepare the input image ######
  // Convert to gray scale, add a border, define boundary conditions
  
  Func gray;
  gray(x, y) = Max(Cast<float>(image(x, y, 0)), Max(Cast<float>(image(x, y, 1)), Cast<float>(image(x, y, 2))));
  
  Func clampedImage;
  clampedImage(x,y) = gray(Clamp(x,0,im.width()-1),
			   Clamp(y,0,im.height()-1));
  
  Func paddedImage;
  paddedImage(x,y) = Select((x < 0)
			    || (x >= im.width())
			    || (y < 0)
			    || (y >= im.height()),
			    background,
			    clampedImage(x,y));    
  
  Func input("input");
  input(x,y) = Cast<float>(paddedImage(x,y));





  
  // ###### Compute the G function ######
  // Some simple computation, cache the result because never changes later
  
  Func blurred_input("blurred_input");
  blurred_input = blur(input,sigma);

  Expr input_dx,input_dy;
  input_dx = dx(blurred_input)(x,y);
  input_dy = dy(blurred_input)(x,y);

  Func g_proxy("g_proxy");
  g_proxy(x,y) = 1.0f / (1.0f + input_dx * input_dx + input_dy * input_dy);

  Func offset_g("offset_phi_init");
  offset_g(x,y) = g_proxy(x - phiPadding, y - phiPadding);
  
  UniformImage g_buffer(Float(32),2);
  Image<float> g_buf = offset_g.realize(im.width() + 2 * phiPadding,im.height() + 2 * phiPadding);
  g_buffer = g_buf;
  
  Func g("g");
  g(x,y) = g_buffer(Clamp(x + phiPadding,0,g_buf.width()-1),
		    Clamp(y + phiPadding,0,g_buf.height()-1));


  

  // ###### Initialize the selection  ######
  // Create a big rectangle, store it in the temporary buffer
  
  float c0 = 2.0f;

  Func phi_init("phi_init");
  phi_init(x,y) = Select((x >= -selectPadding)
			 && (x < im.width() + selectPadding)
			 && (y >= -selectPadding)
			 && (y < im.height() + selectPadding),
			 -c0,
			 c0);

  Func offset_phi_init("offset_phi_init");
  offset_phi_init(x,y) = phi_init(x - phiPadding, y - phiPadding);

  UniformImage phi_buffer(Float(32),2);
  Image<float> buf = offset_phi_init.realize(im.width() + 2 * phiPadding,im.height() + 2 * phiPadding);




  // ###### Define the outer loop ######
  // Read from the buffer, compute the result, possibly create code for intermediate images
  
  Func phi_begin("phi_begin");
  phi_begin(x,y) = phi_buffer(Clamp(x + phiPadding,0,buf.width()-1),
			      Clamp(y + phiPadding,0,buf.height()-1));
  
  
  printf("Defining phi_end\n");
  Func phi_end("phi_end");
  phi_end = drlse_edge(phi_begin,g,lambda,mu,alpha,epsilon,timestep,iter_inner);
  
  Func offset_phi("offset_phi");
  offset_phi(x,y) = phi_end(x - phiPadding, y - phiPadding);

//   Func output_stage; // for intermediate images
//   output_stage(x,y,Var()) = Cast<uint8_t>(Select(phi_end(x,y) < 0.0f, 255, 0));



  

  // ###### Outer loop ######
  
  for(int n = 0 ; n < iter_outer ; ++n){

    clog << "### iteration " << (n + 1) << " of " << iter_outer << endl;
    
    phi_buffer = buf;
    buf = offset_phi.realize(im.width() + 2 * phiPadding,im.height() + 2 * phiPadding);

/*
    ostringstream out_name; // for intermediate images
    out_name << "output_" << setw(4) << setfill('0') << n << ".png"; 
    
//     Image<uint8_t> out = output_stage.realize(im.width() + 2 * phiPadding,im.height() + 2 * phiPadding, 1);
    Image<uint8_t> out = output_stage.realize(im.width(),im.height(), 1);
    save(out, out_name.str().c_str());
*/
  }



  
  
  // ###### Save the result ######
  // Read data from the buffer, threshold, save
  
  Func phi_final("phi_final");
  
  phi_final(x,y) = phi_buffer(Clamp(x + phiPadding,0,buf.width()-1),
			      Clamp(y + phiPadding,0,buf.height()-1));
  
  Func output("output");
  output(x,y,Var()) = Cast<uint8_t>(Select(phi_final(x,y) < 0.0f, 255, 0));


  Image<uint8_t> out = output.realize(im.width(),im.height(), 1);

  save(out, argv[2]);
}
