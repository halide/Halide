
/* Only propagates right currently (and only 1 propagate per iteration). Try some parallel propagate
  (e.g. parallel within diagonal). Also reads invalid data at boundary when propagating. */
  
#include "Halide.h"

using namespace Halide;

#include "../png.h"

#include <iostream>
#include <ctime>
#include <sys/time.h>

// For GPU, grep use_gpu, HL_TARGET PTX

Expr sqr(Expr a) {
	return a*a;
}

Func make_patchmatch(UniformImage a, UniformImage b, int init_only) {
	int pw = 7;
	Var x,y,c,dx,dy;
	Var xi,yi;
	RVar rdx(0,pw),rdy(0,pw),rc(0,3);
	Func init_nnf_offset("init_nnf_offset");
	Func init_nnfd("init_nnfd");
	Func init_nnfd_full("init_nnfd_full");
	Func init_nnf("init_nnf");

	Func prev_nnf("prev_nnf");
		
	init_nnf_offset(x,y,c) = cast< int32_t > (select(c==0, (39733*(x+y*512))%(a.width()-pw+1), 
	                                     select(c==1, (36913*(y+x*512))%(a.height()-pw+1), 0))); //;

	init_nnfd_full(x,y,c,dx,dy) = cast< int32_t > (
									sqr(cast<int32_t > (a(init_nnf_offset(x,y,0)+dx,init_nnf_offset(x,y,1)+dy,c)) -
									    cast<int32_t > (b(init_nnf_offset(x,y,0)+dx,init_nnf_offset(x,y,1)+dy,c)))
										);
	init_nnfd(x,y) += init_nnfd_full(x,y,rc,rdx,rdy);//);
//	init_nnf.root().parallel(y);
	init_nnfd.update().parallel(y);//.vectorize(x,4);
	init_nnf(x,y) = (init_nnf_offset(x,y,0), init_nnf_offset(x,y,1), init_nnfd(x,y));
	init_nnf.root();
	
	if (init_only) {
		return init_nnf;
	}
//	init_nnf_comb(x,y,c) = select(c==0||c==1, init_nnf(x,y,c), init_nnfd(x,y));
	//init_nnf_comb.parallel(y);
	
	prev_nnf = init_nnf;
	
	int nn_iters = 5;
	for (int iter = 0; iter < nn_iters; iter++) {
		char buf[256];
		sprintf(buf, "prop_nnfd_full%d", iter);
		Func prop_nnfd_full(buf);
		sprintf(buf, "prop_nnfd%d", iter);
		Func prop_nnfd(buf);
		sprintf(buf, "prop_nnf%d", iter);
		Func prop_nnf(buf);
//		Func comb("comb");
		
		prop_nnfd_full(x,y,c,dy) =		sqr(cast<int32_t > (a(prev_nnf(x-1,y,0)+1+pw-1,prev_nnf(x-1,y,1)+dy,c)) -
											cast<int32_t > (b(prev_nnf(x-1,y,0)+1+pw-1,prev_nnf(x-1,y,1)+dy,c))) -
										sqr(cast<int32_t > (a(prev_nnf(x-1,y,0)       ,prev_nnf(x-1,y,1)+dy,c)) -
											cast<int32_t > (b(prev_nnf(x-1,y,0)       ,prev_nnf(x-1,y,1)+dy,c)));
		prop_nnfd(x,y) = prev_nnf(x-1,y,2);
		prop_nnfd(x,y) += prop_nnfd_full(x,y,rc,rdy);
		prop_nnfd.update().parallel(y); //.vectorize(x,8);
//		prop_nnf(x,y,c) = select(c==0,  select(prop_nnfd(x,y) < init_nnfd(x,y), init_nnf(x-1,y,0)+1, init_nnf(x,y,0)),
//										select(prop_nnfd(x,y) < init_nnfd(x,y), init_nnf(x-1,y,1)+1, init_nnf(x,y,1)));
		prop_nnf(x,y) = select(prop_nnfd(x,y) < prev_nnf(x,y,2), (prev_nnf(x-1,y,0)+1, prev_nnf(x-1,y,1), prop_nnfd(x,y)),
																 (prev_nnf(x,y,0),     prev_nnf(x,y,1),   prev_nnf(x,y,2))); 
		prop_nnf.root();
		
		prev_nnf = prop_nnf;
		if (iter == nn_iters-1) {
			return prop_nnf;
		}
	}
	
//	comb(x,y,c) = select(c==0||c==1, prop_nnf(x,y,c), prop_nnfd(x,y));
//	return //init_nnf_comb;
	//return prop_nnf;
}


double float_timer() {
	struct timeval t_time;
	gettimeofday(&t_time, 0);

	return 1.0 * t_time.tv_sec  + 0.000001 * t_time.tv_usec;
}

int main(int argc, char **argv) {
	if (argc < 4) {
		std::cerr << "Usage:\n\t./patchmatch a.png b.png out.dat\n" << std::endl;
		return 1;
	}

	UniformImage a(UInt(8), 3);
	UniformImage b(UInt(8), 3);
	Uniform< uint8_t > amt;

	Func func = make_patchmatch(a, b, 0);
	//func.root();


	Image< uint8_t > a_png = load< uint8_t >(argv[1]);
	Image< uint8_t > b_png = load< uint8_t >(argv[2]);
	Image< int32_t > out(a_png.width(), a_png.height(), a_png.channels());
	
	a = a_png;
	b = b_png;

	double T0 = float_timer();
	func.compileJIT();
	double T1 = float_timer();
	out = func.realize(out.width(), out.height(), out.channels());
	double T2 = float_timer();
	printf("Time: %f secs run (%f secs compile)\n", T2-T1, T1-T0);

//	save(out, argv[3]);
	FILE *f = fopen(argv[3], "wb");
	fwrite((void *) out.data(), out.width()*out.height()*out.channels(), sizeof(int32_t), f);
	fclose(f);
}
