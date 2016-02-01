/*! \file
  \verbatim

    Copyright (c) 2006, Sylvain Paris and Frédo Durand

    Permission is hereby granted, free of charge, to any person
    obtaining a copy of this software and associated documentation
    files (the "Software"), to deal in the Software without
    restriction, including without limitation the rights to use, copy,
    modify, merge, publish, distribute, sublicense, and/or sell copies
    of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be
    included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
    HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
    WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.

  \endverbatim
*/


#include <cmath>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

#define CHRONO
#include "geom.h"
#include "fast_lbf.h"


using namespace std;

typedef Array_2D<double> image_type;


int main(int argc,char** argv){

  if (argc!=5){
    cerr<<"error: wrong arguments"<<endl;
    cerr<<endl;
    cerr<<"usage: "<<argv[0]<<" input.ppm output.ppm sigma_s sigma_r"<<endl;
    cerr<<endl;
    cerr<<"spatial parameter (measured in pixels)"<<endl;
    cerr<<"---------------------------------------"<<endl;
    cerr<<"sigma_s    : parameter of the bilateral filter (try 16)"<<endl;
    cerr<<endl;
    cerr<<"range parameter (intensity is scaled to [0.0,1.0])"<<endl;
    cerr<<"---------------------------------------------------"<<endl;
    cerr<<"sigma_r    : parameter of the bilateral filter (try 0.1)"<<endl;
    cerr<<endl;
    exit(1);
  }


  // ##############################################################


  //cout<<"Load the input image '"<<argv[1]<<"'... "<<flush;

  ifstream ppm_in(argv[1],ios::binary);

  string magic_number("  ");

  ppm_in.get(magic_number[0]);
  ppm_in.get(magic_number[1]);

  if (magic_number != std::string("P6")){
    cerr<<"error: unrecognized file format\n"<<argv[1]<<" is not a PPM file.\n"<<endl;
    exit(2);
  }

  unsigned width,height,bpp;

  ppm_in>>width>>height>>bpp;

  if (bpp != 255){
    cerr<<"error: unsupported maximum value ("<<bpp<<")\n"<<"It must be 255."<<endl;
    exit(3);
  }

  image_type image(width,height);

  char ch;
  ppm_in.get(ch); // Trailing white space.

  char r,g,b;

  for(unsigned y=0;y<height;y++){
    for(unsigned x=0;x<width;x++){

      ppm_in.get(r);
      ppm_in.get(g);
      ppm_in.get(b);

      const unsigned char R = static_cast<unsigned char>(r);
      const unsigned char G = static_cast<unsigned char>(g);
      const unsigned char B = static_cast<unsigned char>(b);

      image(x,y) = (20.0 * R + 40.0 * G + 1.0 * B) / (61.0 * 255.0);
    }
  }

  ppm_in.close();

  //cout<<"Done"<<endl;

  double sigma_s,sigma_r,sampling_s,sampling_r;

  istringstream sigma_s_in(argv[3]);
  sigma_s_in>>sigma_s;

  istringstream sigma_r_in(argv[4]);
  sigma_r_in>>sigma_r;

  //cout<<"sigma_s    = "<<sigma_s<<"\n";
  //cout<<"sigma_r    = "<<sigma_r<<"\n";



  // ##############################################################


  //cout<<"Filter the image... "<<endl;

  image_type filtered_image(width,height);

  Image_filter::fast_LBF(image,image,
			 sigma_s,sigma_r,
			 false,
			 &filtered_image,&filtered_image);

  //cout<<"Filtering done"<<endl;


  // ##############################################################


  //cout<<"Write the output image '"<<argv[2]<<"'... "<<flush;

  ofstream ppm_out(argv[2],ios::binary);

  ppm_out<<"P6";
  ppm_out<<' ';
  ppm_out<<width;
  ppm_out<<' ';
  ppm_out<<height;
  ppm_out<<' ';
  ppm_out<<"255";
  ppm_out<<'\n';

  for(unsigned y=0;y<height;y++){
    for(unsigned x=0;x<width;x++){

      const double R = filtered_image(x,y) * 255.0;
      const double G = filtered_image(x,y) * 255.0;
      const double B = filtered_image(x,y) * 255.0;

      const char r = static_cast<unsigned char>(Math_tools::clamp(0.0,255.0,R));
      const char g = static_cast<unsigned char>(Math_tools::clamp(0.0,255.0,G));
      const char b = static_cast<unsigned char>(Math_tools::clamp(0.0,255.0,B));

      ppm_out<<r<<g<<b;
    }
  }

  ppm_out.flush();
  ppm_out.close();

  //cout<<"Done"<<endl;
}
