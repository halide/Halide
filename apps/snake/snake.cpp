#include <string>

#include <qapplication.h>

#include "argparser.h"
#include "array_tools.h"
#include "fft.h"
#include "function_2D.h"
#include "image_file.h"
#include "image_container_traits.h"
#include "math_tools.h"


// conversion of Matlab code of this paper: http://www.engr.uconn.edu/~cmli/DRLSE/
// settings tuned for http://www.cap.org/apps/docs/reference/myBiopsy/images/CLL_Normal_1000x.jpg


using namespace std;

typedef double   real_type;
typedef unsigned size_type;

typedef Array_2D<real_type> real_array_type;
typedef Array_2D<bool> bool_array_type;

size_type width;
size_type height;


void NeumannBoundCond(const real_array_type& f,
  	      real_array_type* const g){

  real_array_type& out = *g;
  out = f;

  out(0,      0)        = out(2,      2);
  out(0,      height-1) = out(2,      height-3);
  out(width-1,0)        = out(width-3,2);
  out(width-1,height-1) = out(width-3,height-3);

  for(size_type x = 1 ; x < width - 1 ; ++x){
    out(x,0) = out(x,2);
    out(x,height-1) = out(x,height-3);
  }

  for(size_type y = 1 ; y < height - 1 ; ++y){
    out(0,y) = out(2,y);
    out(width-1,y) = out(width-3,y);
  }
  
}

void Neumann_dx(const real_array_type& input,
		real_array_type* const grad_x){

  real_array_type& dx = *grad_x;
  
  for(size_type x = 0 ; x < width ; ++x){

    const size_type xm = (x > 0) ? (x - 1) : 0;
    const size_type xp = (x < width - 2) ? (x + 1) : (width - 1);
    
    for(size_type y = 0 ; y < height ; ++y){

      dx(x,y) = 0.5 * (input(xp,y) - input(xm,y));
      
    }
  }  
}

void Neumann_dy(const real_array_type& input,
		real_array_type* const grad_y){

  real_array_type& dy = *grad_y;
  
  for(size_type x = 0 ; x < width ; ++x){
    
    for(size_type y = 0 ; y < height ; ++y){

      const size_type ym = (y > 0) ? (y - 1) : 0;
      const size_type yp = (y < height - 2) ? (y + 1) : (height - 1);

      dy(x,y) = 0.5 * (input(x,yp) - input(x,ym));
      
    }
  }  
}

void Neumann_Laplacian(const real_array_type& input,
		       real_array_type* const lap){

  real_array_type& l = *lap;
  
  for(size_type x = 0 ; x < width ; ++x){

    const size_type xm = (x > 0) ? (x - 1) : 0;
    const size_type xp = (x < width - 2) ? (x + 1) : (width - 1);
    
    for(size_type y = 0 ; y < height ; ++y){

      const size_type ym = (y > 0) ? (y - 1) : 0;
      const size_type yp = (y < height - 2) ? (y + 1) : (height - 1);
      
      l(x,y) = input(xp,y) + input(xm,y) + input(x,yp) + input(x,ym) - 4.0 * input(x,y);
      
    }
  }
  
}


void normalize_and_output(const real_array_type& data,
			  const string&          file_name){

  const real_type min_data = *min_element(data.begin(),data.end());
  const real_type max_data = *max_element(data.begin(),data.end());

  real_array_type proxy(width,height);
  Array_tools::sub(data,min_data,&proxy);
  Array_tools::mul(proxy,1.0 / (max_data - min_data + 1e-10),&proxy);
  Image_file::save(file_name.c_str(),proxy);
}


void threshold_and_output(const real_array_type& data,
			  const string&          file_name){

  real_array_type proxy(width,height);

  real_array_type::const_iterator d = data.begin();
  for(real_array_type::iterator i = proxy.begin();
      i != proxy.end() ; ++i, ++d){

    *i = (*d < 0) ? 1.0 : 0.0;
  }

  Image_file::save(file_name.c_str(),proxy);
}



void distReg_p2(const real_array_type& phi,
		real_array_type* const f){

  static real_array_type phi_x(width,height);
  static real_array_type phi_y(width,height);

  Neumann_dx(phi,&phi_x);
  Neumann_dy(phi,&phi_y);


  // S
  
  static real_array_type s(width,height);

  for(real_array_type::iterator si = s.begin(), xi = phi_x.begin(), yi = phi_y.begin();
      si != s.end() ; ++si, ++xi, ++yi){

    *si = sqrt(*xi * *xi + *yi * *yi);
  }


  // PS
  
  static real_array_type ps(width,height);
  
  for(real_array_type::iterator psi = ps.begin(), si = s.begin();
      psi != ps.end() ; ++psi, ++si){

    if (*si <= 1){
      *psi = sin(2.0 * M_PI * *si) / (2.0 * M_PI);
    }
    else{
      *psi = *si - 1.0;
    }
  }


  // DPS

  static real_array_type dps(width,height);
  for(real_array_type::iterator dpsi = dps.begin(), psi = ps.begin(), si = s.begin();
      dpsi != dps.end() ; ++dpsi, ++psi, ++si){

    const real_type n = (*psi == 0.0) ? 1.0 : *psi;
    const real_type d = (*si == 0.0) ? 1.0 : *si;
    *dpsi = n / d;
  }


  // OUTPUT

  static real_array_type ddx(width,height);
  static real_array_type ddy(width,height);
  static real_array_type proxy(width,height);

  Array_tools::mul(dps,phi_x,&proxy);
  Array_tools::sub(proxy,phi_x,&proxy);
  Neumann_dx(proxy,&ddx);

  Array_tools::mul(dps,phi_y,&proxy);
  Array_tools::sub(proxy,phi_y,&proxy);
  Neumann_dy(proxy,&ddy);

  static real_array_type lap(width,height);
  Neumann_Laplacian(phi,&lap);

  Array_tools::add(ddx,ddy,f);
  Array_tools::add(*f,lap,f);
}

void Dirac(const real_array_type& input,
	   const real_type        sigma,
	   real_array_type* const output){
  
  real_array_type::const_iterator x = input.begin();
  for(real_array_type::iterator f = output->begin();
      x != input.end() ; ++x, ++f){

    if ((*x <= sigma) && (*x >= -sigma)){
      *f = 1.0 / 2.0 / sigma * (1.0 + cos(M_PI * *x / sigma));
    }
    else{
      *f = 0.0;
    }
  }
}

void drlse_edge(const real_array_type& phi_0,
		const real_array_type& g,
		const real_type        lambda,
		const real_type        mu,
		const real_type        alpha,
		const real_type        epsilon,
		const real_type        timestep,
		const size_type        iter,
		real_array_type* const output){

//   normalize_and_output(phi_0,"phi_0.png");
  
  real_array_type& phi = *output;
  phi = phi_0;

  static real_array_type vx(width,height);
  static real_array_type vy(width,height);
  
  Neumann_dx(g,&vx);
  Neumann_dy(g,&vy);

//   normalize_and_output(vx,"vx.png");
//   normalize_and_output(vy,"vy.png");
  
  for(size_type k = 0 ; k < iter ; ++k){

//     NeumannBoundCond(phi,&phi);
    
//     normalize_and_output(phi,"phi.png");
    
    static real_array_type phi_x(width,height);
    static real_array_type phi_y(width,height);

    Neumann_dx(phi,&phi_x);
    Neumann_dy(phi,&phi_y);

//     normalize_and_output(phi_x,"phi_x.png");
//     normalize_and_output(phi_y,"phi_y.png");

    // S
  
    static real_array_type s(width,height);
    
    for(real_array_type::iterator si = s.begin(), xi = phi_x.begin(), yi = phi_y.begin();
	si != s.end() ; ++si, ++xi, ++yi){
      
      *si = sqrt(*xi * *xi + *yi * *yi);
    }


    const real_type smallNumber = 1e-10;

    static real_array_type Nx(width,height);
    static real_array_type Ny(width,height);

    for(real_array_type::iterator si = s.begin(), nxi = Nx.begin(), pxi = phi_x.begin();
	si != s.end() ; ++si, ++nxi, ++pxi){
      
      *nxi = *pxi / (*si + smallNumber);
    }
    
    for(real_array_type::iterator si = s.begin(), nyi = Ny.begin(), pyi = phi_y.begin();
	si != s.end() ; ++si, ++nyi, ++pyi){
      
      *nyi = *pyi / (*si + smallNumber);
    }


//     normalize_and_output(Nx,"Nx.png");
//     normalize_and_output(Ny,"Ny.png");
    
    
    static real_array_type ddx(width,height);
    static real_array_type ddy(width,height);

    Neumann_dx(Nx,&ddx);
    Neumann_dy(Ny,&ddy);

    static real_array_type curvature(width,height);
    Array_tools::add(ddx,ddy,&curvature);

//     normalize_and_output(curvature,"curvature.png");

    
    static real_array_type distRegTerm(width,height);
    distReg_p2(phi,&distRegTerm);

//     normalize_and_output(distRegTerm,"distRegTerm.png");

    
    static real_array_type diracPhi(width,height);
    Dirac(phi,epsilon,&diracPhi);

//     normalize_and_output(diracPhi,"diracPhi.png");

    
    static real_array_type areaTerm(width,height);
    Array_tools::mul(diracPhi,g,&areaTerm);

//     normalize_and_output(areaTerm,"areaTerm.png");

    
    static real_array_type edgeTerm(width,height);
    static real_array_type proxy_mul(width,height);
    static real_array_type proxy_add(width,height);
    Array_tools::mul(vx,Nx,&proxy_add);
    Array_tools::mul(vy,Ny,&proxy_mul);
    Array_tools::add(proxy_add,proxy_mul,&proxy_add);
    Array_tools::mul(g,curvature,&proxy_mul);
    Array_tools::add(proxy_add,proxy_mul,&proxy_add);
    Array_tools::mul(proxy_add,diracPhi,&edgeTerm);

//     normalize_and_output(edgeTerm,"edgeTerm.png");

    
    // PHI
    Array_tools::mul(distRegTerm,mu,&proxy_add);
    Array_tools::mul(edgeTerm,lambda,&proxy_mul);
    Array_tools::add(proxy_add,proxy_mul,&proxy_add);
    Array_tools::mul(areaTerm,alpha,&proxy_mul);
    Array_tools::add(proxy_add,proxy_mul,&proxy_add);
    Array_tools::mul(proxy_add,timestep,&proxy_mul);
    Array_tools::add(phi,proxy_mul,&phi);

//     normalize_and_output(phi,"output.png");

//     exit(0);
    
  } // END OF for k

//   exit(0);
}
		

int main(int argc, char** argv)
{
  // Read command lines arguments.
  QApplication application(argc,argv);

  string input_name;
  
  parse(argc,argv)
    .forParameter('i',"input",input_name,"input image file")
    .forErrors(std::cerr);


  cout << "Loading input image" <<endl;
  
  real_array_type image;
  Image_file::load(input_name.c_str(),&image);

  const size_type padding = 5;
  
  width  = image.width() + 2 * padding;
  height = image.height() + 2 * padding;

  real_array_type input(width,height,0.98);
  
  for(size_type x = 0 ; x < image.width() ; ++x){
    for(size_type y = 0 ; y < image.height() ; ++y){

      input(padding + x, padding + y) = image(x,y);
    }
  }
  
  Image_file::save("input.png",input);

  Array_tools::mul(input,255.0,&input);
  

  const real_type timestep = 5.0;
  const real_type mu = 0.2 / timestep;
  const size_type iter_inner = 5;
  const size_type iter_outer = 1000;
  const size_type iter_refine = 10;
  const real_type lambda = 6.0; //5.0;
  real_type alpha = 1.5;
  const real_type epsilon = 1.5;
  const real_type sigma = 1.5;

  real_array_type G(width,height);
  Function_2D::Normalized_gaussian Gaussian(0,0,sigma,sigma);
  Function_2D::fill(Gaussian,&G);
  real_array_type Img_smooth(width,height);
  FFT::convolve(input,G,&Img_smooth);

  Image_file::save("Img_smooth.png",Img_smooth);

  
  real_array_type Ix(width,height);
  real_array_type Iy(width,height);

  Neumann_dx(Img_smooth,&Ix);
  Neumann_dy(Img_smooth,&Iy);

  real_array_type g(width,height);
  for(real_array_type::iterator gi = g.begin(), xi = Ix.begin(), yi = Iy.begin();
      gi != g.end() ; ++gi, ++xi, ++yi){

    *gi = 1.0 / (1.0 + *xi * *xi + *yi * *yi);
  }


  normalize_and_output(g,"g.png");
  
  real_type c0 = 2.0;

  real_array_type phi(width,height,c0);
//   for(size_type x = 10 ; x < 75 ; ++x){
  for(size_type x = 3 ; x < width - 3 ; ++x){
//     for(size_type y = 10 ; y < 55 ; ++y){
    for(size_type y = 3 ; y < height - 3 ; ++y){
      phi(x,y) = -c0;
    }
  }

  normalize_and_output(phi,"init.png");
  
  for(size_type n = 0 ; n < iter_outer ; ++n){

    cout << "Iteration: " << n << endl;
    
    drlse_edge(phi,g,lambda,mu,alpha,epsilon,timestep,iter_inner,&phi); //exit(0);

    {
      ostringstream out_name;
      out_name << "phi_at_"<< setfill('0') << setw(3) << n << ".png";
      normalize_and_output(phi,out_name.str());
    }

    {
      ostringstream out_name;
      out_name << "select_at_"<< setfill('0') << setw(3) << n << ".png";
      threshold_and_output(phi,out_name.str());
    }
    
  }

  alpha = 0.0;
  drlse_edge(phi,g,lambda,mu,alpha,epsilon,timestep,iter_refine,&phi); //exit(0);

  real_array_type output(width,height);

  for(real_array_type::iterator i = phi.begin(), o = output.begin();
      i != phi.end() ; ++i, ++o){

    *o = (*i < 0) ? 1.0 : 0.0;
  }


  Image_file::save("output.png",output);
}
 
