#include <Halide.h>
using namespace Halide;

#include <image_io.h>

#include <iostream>
#include <limits>
#include <vector>
#include <cmath>

#include <sys/time.h>

double now() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    static bool first_call = true;
    static time_t first_sec = 0;
    if (first_call) {
        first_call = false;
        first_sec = tv.tv_sec;
    }
    assert(tv.tv_sec >= first_sec);
    return (tv.tv_sec - first_sec) + (tv.tv_usec / 1000000.0);
}

static const float pi = std::acos(-1.0);

std::string infile, outfile;
float angle = 0.0f;
bool show_usage = false;
int schedule = 0;

void parse_commandline(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-r" && i+1 < argc) {
            angle = atof(argv[++i]);
        } else if (arg == "-s" && i+1 < argc) {
            schedule = atoi(argv[++i]);
            if (schedule < 0 || schedule > 3) {
                fprintf(stderr, "Invalid schedule\n");
                show_usage = true;
            }
        } else if (infile.empty()) {
            infile = arg;
        } else if (outfile.empty()) {
            outfile = arg;
        } else {
            fprintf(stderr, "Unexpected command line option '%s'.\n", arg.c_str());
        }
    }
}

Expr return_second(Expr a, Expr b) {
  return Internal::Call::make(b.type(), Internal::Call::return_second,
                              Internal::vec(a, b), Internal::Call::Intrinsic);
}

#define DBG_ASSIGN(lhs, rhs) lhs = return_second(print(#lhs " =", rhs), rhs);

Expr bilerp(Func f, Expr x, Expr y) {
  Expr x0, y0;
  x0 = cast<int>(x);
  y0 = cast<int>(y);

  Matrix A(2, 2, Float(32));
  A(0, 0) = f(x0, y0);     A(0, 1) = f(x0+1, y0);
  A(1, 0) = f(x0, y0+1);   A(1, 1) = f(x0+1, y0+1);

  Matrix xx(2, 1, Float(32));
  xx[0] = 1 - (x - x0);
  xx[1] = x - x0;

  Matrix yy(1, 2, Float(32));
  yy[0] = 1 - (y - y0);
  yy[1] = y - y0;

  // std::vector<Expr> print_args = {
  //   Expr("bilerp:"),
  //   Expr("\n\tx ="), x, Expr("y ="), y,
  //   Expr("\n\t(x - x0) ="), xx[1], Expr("(y - y0) ="), yy[1],
  //   Expr("\n\tA(0,0) ="), A(0,0), Expr("\tA(0,1) ="), A(0,1),
  //   Expr("\n\tA(1,0) ="), A(1,0), Expr("\tA(1,1) ="), A(1,1),
  //   Expr("\n\tf(x,y) ="), (yy * A * xx)[0]};

  // return return_second(print(print_args), (yy * A * xx)[0]);

  return (yy * A * xx)[0];
}

Expr bilerp(Func f, Matrix xy) {
  return bilerp(f, xy[0], xy[1]);
}

/*
  Transform f by applying an affine transformation to its domain. The
  transformation matrix T is assumed to be a 3x3 affine transformation
  matrix.
 */
Func transform(Func f, Matrix T) {
  Matrix T_inv = T.inverse();
  // Matrix T_inv(T.num_rows(), T.num_cols(), T.type());
  // DBG_ASSIGN(T_inv(0,0), T.inverse()(0,0));
  // DBG_ASSIGN(T_inv(0,1), T.inverse()(0,1));
  // DBG_ASSIGN(T_inv(0,2), T.inverse()(0,2));
  // DBG_ASSIGN(T_inv(1,0), T.inverse()(1,0));
  // DBG_ASSIGN(T_inv(1,1), T.inverse()(1,1));
  // DBG_ASSIGN(T_inv(1,2), T.inverse()(1,2));
  // DBG_ASSIGN(T_inv(2,0), T.inverse()(2,0));
  // DBG_ASSIGN(T_inv(2,1), T.inverse()(2,1));
  // DBG_ASSIGN(T_inv(2,1), T.inverse()(2,2));

  Var x("x"), y("y");

  Matrix xy(3, 1, Float(32));
  xy[0] = cast<float>(x);
  xy[1] = cast<float>(y);
  xy[2] = 1;

  Func Tf("xformed_func");
  Tf(x, y) = bilerp(f, T_inv * xy);

  return Tf;
}

Func rotate(Func f, Expr x0, Expr y0, Expr theta) {
  Matrix T(3, 3, Float(32));
  T(0,0) =  cos(theta);  T(0,1) = sin(theta);  T(0,2) = x0;
  T(1,0) = -sin(theta);  T(1,1) = cos(theta);  T(1,2) = y0;
  T(2,0) = 0.0f; T(2,1) = 0.0f; T(2,2) = 1.0f;

  // DBG_ASSIGN(T(0,0), cos(theta));  DBG_ASSIGN(T(0,1), sin(theta));  DBG_ASSIGN(T(0,2), x0);
  // DBG_ASSIGN(T(1,0), sin(-1.0f*theta));  DBG_ASSIGN(T(1,1),  cos(theta));  DBG_ASSIGN(T(1,2), y0);
  // DBG_ASSIGN(T(2,0), 0.0f); DBG_ASSIGN(T(2,1), 0.0f); DBG_ASSIGN(T(2,2), 1.0f);

  Var x("x"), y("y");

  Matrix xy(3, 1, Float(32));
  xy[0] = cast<float>(x) - x0;
  xy[1] = cast<float>(y) - y0;
  xy[2] = 1.0f;

  Matrix sample_pos = T * xy;

  Func Tf("rotateded_func");
  Tf(x, y) = bilerp(f, sample_pos);

  // std::vector<Expr> print_args = {
  //   Expr("x ="), x, Expr("y ="), y,
  //   Expr("\n(x - x0) ="), xy[0], Expr("(y - y0) ="), xy[1],
  //   Expr("\nT*xy[0] ="), (T*xy)[0],
  //   Expr("T*xy[1] ="), (T*xy)[1],
  //   Expr("T*xy[2] ="), (T*xy)[2] };

  // Tf(x, y) = return_second(print(print_args), bilerp(f, sample_pos));

  return Tf;
}

int main(int argc, char **argv) {
    parse_commandline(argc, argv);
    if (infile.empty() || outfile.empty() || show_usage) {
        fprintf(stderr,
                "Usage:\n"
                "\t./rotate [-r angle] [-s schedule] in.png out.png\n"
                "\t\tSchedules: 0=default 1=vectorized 2=parallel 3=vectorized+parallel\n");
        return 1;
    }

    ImageParam input(Float(32), 3);

    Var x("x"), y("y"), c("c");

    Func bound_input("bound_input");
    bound_input = BoundaryConditions::constant_exterior(input, 0.0f,
                                                        0, input.width(),
                                                        0, input.height(),
                                                        0, 3);

    // bound_input = BoundaryConditions::repeat_edge(input, 0, input.width()-1, 0, input.height()-1);
    // bound_input(x, y, c) = select(0 <= x && x < input.width() &&
    //                               0 <= y && y < input.height(),
    //                               input(x, y, c), 0.0f);

    Func r("r"), g("g"), b("b");
    r(x, y) = bound_input(x, y, 0);
    g(x, y) = bound_input(x, y, 1);
    b(x, y) = bound_input(x, y, 2);

    Expr x0 = cast<float>(input.width())  / 2.0f;
    Expr y0 = cast<float>(input.height()) / 2.0f;
    Expr theta = pi * angle / 180.0f;

    Func result("result");
    result(x, y, c) = select(c == 0, rotate(r, x0, y0, theta)(x, y),
                             c == 1, rotate(g, x0, y0, theta)(x, y),
                                     rotate(b, x0, y0, theta)(x, y));

    Func output("output");
    output(x, y, c) = clamp(result(x, y, c), 0.0f, 1.0f);

    // Scheduling
    bool parallelize = (schedule >= 2);
    bool vectorize = (schedule == 1 || schedule == 3);

    result.reorder(c, x, y).bound(c, 0, 3).unroll(c);

    if (vectorize) {
        result.vectorize(x, 4);
        output.vectorize(x, 4);
    }

    if (parallelize) {
        Var yo, yi;
        output.split(y, yo, y, 32).parallel(yo);
        result.store_at(output, yo).compute_at(output, y);
    } else {
        result.store_at(output, c).compute_at(output, y);
    }

    output.compile_to_lowered_stmt("rotate.stmt");

    Target target = get_jit_target_from_environment();
    // target.features |= Halide::Target::NoAsserts;
    // target.features |= Halide::Target::NoBoundsQuery;
    output.compile_jit(target);

    printf("Loading '%s'\n", infile.c_str());
    Image<float> in_png = load<float>(infile);
    const int width  = in_png.width();
    const int height = in_png.height();
    Image<float> out(width, height, 3);
    input.set(in_png);
    printf("Rotating '%s' by angle %g\n",
           infile.c_str(), angle);

    double min = std::numeric_limits<double>::infinity();
    const unsigned int iters = 1;

    for (unsigned int x = 0; x < iters; ++x) {
        double before = now();
        output.realize(out);
        double after = now();
        double amt = after - before;

        std::cout << "   " << amt * 1000 << std::endl;
        if (amt < min) min = amt;
    }
    std::cout << " took " << min * 1000 << " msec." << std::endl;

    save(out, outfile);
}
