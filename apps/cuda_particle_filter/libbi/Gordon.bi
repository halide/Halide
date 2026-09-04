/**
 * The Gordon, Salmond & Smith (1993) nonlinear state-space model, for
 * benchmarking a particle filter with the Metropolis resampler.
 */
model Gordon {
  const sigma_v = 3.1622776601683795;  // sqrt(10), process noise sd
  const sigma_w = 1.0;                 // observation noise sd

  state x;
  obs y;

  sub initial {
    x ~ gaussian(0.0, 2.23606797749979);  // sqrt(5)
  }

  sub transition {
    x ~ gaussian(0.5*x + 25.0*x/(1.0 + x*x) + 8.0*cos(1.2*(t_now - 1.0)), sigma_v);
  }

  sub observation {
    y ~ gaussian(x*x/20.0, sigma_w);
  }
}
