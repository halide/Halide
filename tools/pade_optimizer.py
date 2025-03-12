import numpy as np
import argparse
import scipy


import collections

Metrics = collections.namedtuple("Metrics", ["mean_squared_error", "max_abs_error", "max_ulp_error"])

np.set_printoptions(linewidth=3000, precision=20)

parser = argparse.ArgumentParser()
parser.add_argument("func")
parser.add_argument("--order", type=int, nargs='+', required=True)
args = parser.parse_args()

taylor_order = 30
func = None

taylor = None
if args.func == "cos":
    taylor = 1.0 / scipy.special.factorial(np.arange(taylor_order))
    taylor[1::2] = 0.0
    taylor[2::4] *= -1
    func = np.cos
    lower, upper = 0.0, np.pi / 2
    exponents = 2 * np.arange(10)
elif args.func == "atan":
    if hasattr(np, "atan"): func = np.atan
    elif hasattr(np, "arctan"): func = np.arctan
    else:
        print("Your numpy version doesn't support arctan.")
        exit(1)
    exponents = 1 + np.arange(10) * 2
    lower, upper = 0.0, 1.0
elif args.func == "tan":
    func = np.tan
    lower, upper = 0.0, np.pi / 4
    exponents = 1 + 2 * np.arange(taylor_order // 2)
elif args.func == "exp":
    func = np.exp
    exponents = np.arange(taylor_order)
    lower, upper = 0, np.log(2)

X_dense = np.linspace(lower, upper, 512 * 31 * 11)
y = func(X_dense)

if taylor is None:
    powers = np.power(X_dense[:,None], exponents)
    coeffs, res, rank, s = np.linalg.lstsq(powers, y, rcond=-1)

    degree = np.amax(exponents)
    taylor = np.zeros(degree + 1)
    for e, c in zip(exponents, coeffs):
        taylor[e] = c


def num_to_str(c):
    if c == 0.0: return "0"
    if c == 1.0: return "1"
    return c.hex()

def formula(coeffs, exponents=None):
    if exponents is None:
        exponents = np.arange(len(coeffs))
    terms = []
    for c, e in zip(coeffs, exponents):
        if c == 0: continue
        if c == 1: terms.append(f"x^{e}")
        else: terms.append(f"{c:.12f} * x^{e}")
    return " + ".join(terms)

print("Taylor")
print(formula(taylor))


for order in args.order:
    p, q = scipy.interpolate.pade(taylor, order, order)
    pa = np.array(p)[::-1]
    qa = np.array(q)[::-1]

    exponents = np.arange(order + 1)
    # Evaluate with float64 precision.

    def eval(dtype):
        ft_x_dense = X_dense.astype(dtype)
        ft_target_dense = func(X_dense).astype(dtype)
        ft_powers = np.power(ft_x_dense[:,None], exponents).astype(dtype)
        ft_y_hat = np.sum(ft_powers[:,:len(pa)] * pa, axis=-1).astype(dtype) / np.sum(ft_powers[:,:len(qa)] * qa, axis=-1).astype(np.float32)
        ft_diff = ft_y_hat - ft_target_dense.astype(dtype)
        ft_abs_diff = np.abs(ft_diff)
        # MSE metric
        ft_mean_squared_error = np.mean(np.square(ft_diff))
        # MAE metric
        ft_max_abs_error = np.amax(ft_abs_diff)
        # MaxULP metric
        ft_ulp_error = ft_diff.astype(np.float64) / np.spacing(np.abs(ft_target_dense).astype(dtype)).astype(np.float64)
        ft_abs_ulp_error = np.abs(ft_ulp_error)
        ft_max_ulp_error = np.amax(ft_abs_ulp_error)

        return Metrics(ft_mean_squared_error, ft_max_abs_error, ft_max_ulp_error)

    float16_metrics = eval(np.float16)
    float32_metrics = eval(np.float32)
    float64_metrics = eval(np.float64)

    print("{", f" /* Padé order {len(pa) - 1}/{len(qa) - 1}: ({formula(pa)})/({formula(qa)}) */")
    print(f"    /* f16 */ {{{float16_metrics.mean_squared_error:.6e}, {float16_metrics.max_abs_error:.6e}, {float16_metrics.max_ulp_error:.3e}}},")
    print(f"    /* f32 */ {{{float32_metrics.mean_squared_error:.6e}, {float32_metrics.max_abs_error:.6e}, {float32_metrics.max_ulp_error:.3e}}},")
    print(f"    /* f64 */ {{{float64_metrics.mean_squared_error:.6e}, {float64_metrics.max_abs_error:.6e}, {float64_metrics.max_ulp_error:.3e}}},")
    print("    /* p */ {" + ", ".join([f"{num_to_str(c)}" for c in pa]) + "}")
    print("    /* q */ {" + ", ".join([f"{num_to_str(c)}" for c in qa]) + "}")
    print("},")
