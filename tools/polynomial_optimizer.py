# Original author: Martijn Courteaux

# This script is used to fit polynomials to "non-trivial" functions (goniometric, transcendental, etc).
# A lot of these functions can be approximated using conventional Taylor expansion, but these
# minimize the error close to the point around which the Taylor expansion is made. Typically, when
# implementing functions numerically, there is a range in which you want to use those (while exploiting
# properties such as symmetries to get the full range). Therefore, it is beneficial to try to create a
# polynomial approximation which is specifically optimized to work well in the range of interest (lower, upper).
# Typically, this means that the error will be spread more evenly across the range of interest, and
# precision will be lost for the range close to the point around which you'd normally develop a Taylor
# expansion.
#
# This script provides an iterative approach to optimize these polynomials of given degree for a given
# function. The key element of this approach is to solve the least-squared error problem, but by iteratively
# adjusting the weights to approximate other loss functions instead of simply the MSE. If for example you
# whish to create an approximation which reduces the Maximal Absolute Error (MAE) across the range,
# The loss function actually could be conceptually approximated by E[abs(x - X)^(100)]. The high power will
# cause the biggest difference to be the one that "wins" because that error will be disproportionately
# magnified (compared to the smaller errors).
#
# This mechanism of the absolute difference raising to a high power is used to update the weights used
# during least-squared error solving.
#
# The coefficients of fast_atan are produced by this.
# The coefficients of other functions (fast_exp, fast_log, fast_sin, fast_cos) were all obtained by
# some other tool or copied from some reference material.

import numpy as np
import argparse
import tqdm

np.set_printoptions(linewidth=3000)

class SmartFormatter(argparse.HelpFormatter):
    def _split_lines(self, text, width):
        if text.startswith('R|'):
            return text[2:].splitlines()
        return argparse.HelpFormatter._split_lines(self, text, width)

parser = argparse.ArgumentParser(formatter_class=SmartFormatter)
parser.add_argument("func")
parser.add_argument("--order", type=int, nargs='+', required=True)
parser.add_argument("--loss", nargs='+', required=True,
                    choices=["mse", "mae", "mulpe", "mulpe_mae"],
                    default="mulpe",
                    help="R|What to optimize for.\n"
                    + " * mse: Mean Squared Error\n"
                    + " * mae: Maximal Absolute Error\n"
                    + " * mulpe: Maximal ULP Error  [default]\n"
                    + " * mulpe_mae: 50%% mulpe + 50%% mae")
parser.add_argument("--gui", action='store_true', help="Do produce plots.")
parser.add_argument("--print", action='store_true', help="Print while optimizing.")
parser.add_argument("--pbar", action='store_true', help="Create a progress bar while optimizing.")
parser.add_argument("--format", default="all", choices=["all", "switch", "array", "table", "consts"],
                    help="Output format for copy-pastable coefficients. (default: all)")
args = parser.parse_args()

loss_power = 1500

import collections

Metrics = collections.namedtuple("Metrics", ["mean_squared_error", "max_abs_error", "max_ulp_error"])

def optimize_approximation(loss, order):
    func_fixed_part = lambda x: x * 0.0
    X = None
    will_invert = False
    if args.func == "atan":
        if hasattr(np, "atan"):
            func = np.atan
        elif hasattr(np, "arctan"):
            func = np.arctan
        else:
            print("Your numpy version doesn't support arctan.")
            exit(1)
        exponents = 1 + np.arange(order) * 2
        lower, upper = 0.0, 1.0
    elif args.func == "sin":
        func = np.sin
        exponents = 2 + np.arange(order)
        func_fixed_part = lambda x: x
        lower, upper = 0.0, np.pi / 2
    elif args.func == "cos":
        func = np.cos
        func_fixed_part = lambda x: np.ones_like(x)
        exponents = 1 + np.arange(order)
        lower, upper = 0.0, np.pi / 2
    elif args.func == "tan":
        func = np.tan
        func_fixed_part = lambda x: x
        exponents = 3 + np.arange(order - 1) * 2
        lower, upper = 0.0, np.pi / 4
        X = np.concatenate([np.logspace(-5, 0, num=2048 * 17), np.linspace(0, 1, 9000)]) * (np.pi / 4)
        X = np.sort(X)
        will_invert = True
    elif args.func == "exp":
        func = lambda x: np.exp(x)
        func_fixed_part = lambda x: 1 + x
        exponents = np.arange(2, order)
        lower, upper = 0, np.log(2)
    elif args.func == "expm1":
        func = lambda x: np.expm1(x)
        exponents = np.arange(1, order + 1)
        lower, upper = 0, np.log(2)
    elif args.func == "log":
        func = lambda x: np.log(x + 1.0)
        exponents = np.arange(1, order + 1)
        lower, upper = -0.25, 0.5
    else:
        print("Unknown function:", args.func)
        exit(1)


    if X is None: X = np.linspace(lower, upper, 512 * 31)
    target = func(X)
    fixed_part = func_fixed_part(X)
    target_fitting_part = target - fixed_part

    target_spacing = np.spacing(np.abs(target).astype(np.float32)).astype(np.float64) # Precision (i.e., ULP)
    # We will optimize everything using double precision, which means we will obtain more bits of
    # precision than the actual target values in float32, which means that our reconstruction and
    # ideal target value can be a non-integer number of float32-ULPs apart.

    if args.print: print("exponent:", exponents)
    coeffs = np.zeros(len(exponents))
    powers = np.power(X[:,None], exponents)
    assert exponents.dtype == np.int64




    # If the loss is MSE, then this is just a linear system we can solve for.
    # We will iteratively adjust the weights to put more focus on the parts where it goes wrong.
    weight = np.ones_like(target)

    lstsq_iterations = loss_power * 20
    if loss == "mse":
        lstsq_iterations = 1
    elif loss == "mulpe":
        lstsq_iterations = loss_power * 1
        weight = 0.2 * np.ones_like(target) + 0.2 * np.mean(target_spacing) / target_spacing

    #if will_invert: weight += 1.0 / (np.abs(target) + target_spacing)

    loss_history = np.zeros((lstsq_iterations, 3))

    try:
        for i in tqdm.trange(lstsq_iterations, disable=not args.pbar, leave=False):
            norm_weight = weight / np.mean(weight)
            coeffs, residuals, rank, s = np.linalg.lstsq(powers * norm_weight[:,None], target_fitting_part * norm_weight, rcond=-1)

            y_hat = fixed_part + np.sum((powers * coeffs)[:,::-1], axis=-1)
            diff = y_hat - target
            abs_diff = np.abs(diff)

            # MSE metric
            mean_squared_error = np.mean(np.square(diff))
            # MAE metric
            max_abs_error = np.amax(abs_diff)
            loss_history[i, 1] = max_abs_error
            # MaxULP metric
            ulp_error = diff / target_spacing
            abs_ulp_error = np.abs(ulp_error)
            max_ulp_error = np.amax(abs_ulp_error)
            loss_history[i, 2] = max_ulp_error

            if args.print and i % 10 == 0:
                print(f"[{((i+1) / lstsq_iterations * 100.0):3.0f}%] coefficients:", coeffs,
                      f" MaxAE: {max_abs_error:20.17f} MaxULPs: {max_ulp_error:20.0f}  mean weight: {weight.mean():.4e}")

            if loss == "mae":
                norm_error_metric = abs_diff / np.amax(abs_diff)
            elif loss == "mulpe":
                norm_error_metric = abs_ulp_error / max_ulp_error
            elif loss == "mulpe_mae":
                norm_error_metric = 0.5 * (abs_ulp_error / max_ulp_error + abs_diff / max_abs_error)
            elif loss == "mse":
                norm_error_metric = np.square(abs_diff)

            p = i / lstsq_iterations
            p = min(p * 1.25, 1.0)
            raised_error = np.power(norm_error_metric, 2 + loss_power * p)
            weight += raised_error

            mean_loss = np.mean(np.power(abs_diff, loss_power))
            loss_history[i, 0] = mean_loss

            if i == 0:
                init_coeffs = coeffs.copy()
                init_ulp_error = ulp_error.copy()
                init_abs_ulp_error = abs_ulp_error.copy()
                init_abs_error = abs_diff.copy()
                init_y_hat = y_hat.copy()

    except KeyboardInterrupt:
        print("Interrupted")

    float64_metrics = Metrics(mean_squared_error, max_abs_error, max_ulp_error)

    # Reevaluate with float32 precision.
    f32_powers = np.power(X[:,None].astype(np.float32), exponents).astype(np.float32)
    f32_y_hat = fixed_part.astype(np.float32) + np.sum((f32_powers * coeffs.astype(np.float32))[:,::-1], axis=-1).astype(np.float32)
    f32_diff = f32_y_hat - target.astype(np.float32)
    f32_abs_diff = np.abs(f32_diff)
    # MSE metric
    f32_mean_squared_error = np.mean(np.square(f32_diff))
    # MAE metric
    f32_max_abs_error = np.amax(f32_abs_diff)
    # MaxULP metric
    f32_ulp_error = f32_diff / np.spacing(np.abs(target).astype(np.float32))
    f32_abs_ulp_error = np.abs(f32_ulp_error)
    f32_max_ulp_error = np.amax(f32_abs_ulp_error)

    float32_metrics = Metrics(f32_mean_squared_error, f32_max_abs_error, f32_max_ulp_error)

    if args.gui:
        import matplotlib.pyplot as plt

        fig, ax = plt.subplots(2, 4, figsize=(12, 6))
        ax = ax.flatten()
        ax[0].set_title("Comparison of exact\nand approximate " + args.func)
        ax[0].plot(X, target, label=args.func)
        ax[0].plot(X, y_hat, label='approx')
        ax[0].grid()
        ax[0].set_xlim(lower, upper)
        ax[0].legend()

        ax[1].set_title("Error")
        ax[1].axhline(0, linestyle='-', c='k', linewidth=1)
        ax[1].plot(X, init_y_hat - target, label='init')
        ax[1].plot(X, y_hat - target, label='final')
        ax[1].grid()
        ax[1].set_xlim(lower, upper)
        ax[1].legend()

        ax[2].set_title("Absolute error\n(log-scale)")
        ax[2].semilogy(X, init_abs_error, label='init')
        ax[2].semilogy(X, abs_diff, label='final')
        ax[2].axhline(np.amax(init_abs_error), linestyle=':', c='C0')
        ax[2].axhline(np.amax(abs_diff), linestyle=':', c='C1')
        ax[2].grid()
        ax[2].set_xlim(lower, upper)
        ax[2].legend()

        ax[3].set_title("Maximal Absolute Error\nprogression during\noptimization")
        ax[3].semilogx(1 + np.arange(loss_history.shape[0]), loss_history[:,1])
        ax[3].set_xlim(1, loss_history.shape[0] + 1)
        ax[3].axhline(y=loss_history[0,1], linestyle=':', color='k')
        ax[3].grid()

        ax[5].set_title("ULP distance")
        ax[5].axhline(0, linestyle='-', c='k', linewidth=1)
        ax[5].plot(X, init_ulp_error, label='init')
        ax[5].plot(X, ulp_error, label='final')
        ax[5].grid()
        ax[5].set_xlim(lower, upper)
        ax[5].legend()


        ax[6].set_title("Absolute ULP distance\n(log-scale)")
        ax[6].semilogy(X, init_abs_ulp_error, label='init')
        ax[6].semilogy(X, abs_ulp_error, label='final')
        ax[6].axhline(np.amax(init_abs_ulp_error), linestyle=':', c='C0')
        ax[6].axhline(np.amax(abs_ulp_error), linestyle=':', c='C1')
        ax[6].grid()
        ax[6].set_xlim(lower, upper)
        ax[6].legend()

        ax[7].set_title("Maximal ULP Error\nprogression during\noptimization")
        ax[7].loglog(1 + np.arange(loss_history.shape[0]), loss_history[:,2])
        ax[7].set_xlim(1, loss_history.shape[0] + 1)
        ax[7].axhline(y=loss_history[0,2], linestyle=':', color='k')
        ax[7].grid()

        ax[4].set_title("LstSq Weight\n(log-scale)")
        ax[4].semilogy(X, norm_weight, label='weight')
        ax[4].grid()
        ax[4].set_xlim(lower, upper)
        ax[4].legend()

        plt.tight_layout()
        plt.show()

    return init_coeffs, coeffs, float32_metrics, float64_metrics, loss_history


for loss in args.loss:
    print_nl = args.format == "all"
    for order in args.order:
        if args.print: print("Optimizing {loss} with {order} terms...")
        init_coeffs, coeffs, float32_metrics, float64_metrics, loss_history = optimize_approximation(loss, order)


        if args.print:
            print("Init  coeffs:", init_coeffs)
            print("Final coeffs:", coeffs)
            print(f"mse: {mean_loss:40.27f}  max abs error: {max_abs_error:20.17f}  max ulp error: {max_ulp_error:e}")

        def print_comment(indent=""):
            print(indent + "// "
                  + {"mae": "Max Absolute Error",
                     "mse": "Mean Squared Error",
                     "mulpe": "Max ULP Error",
                     "mulpe_mae": "MaxUlpAE"
                    }[loss]
                  + f" optimized (MSE={mean_squared_error:.4e}, MAE={max_abs_error:.4e}, MaxUlpE={max_ulp_error:.4e})")


        if args.format in ["all", "consts"]:
            print_comment()
            for i, (e, c) in enumerate(zip(exponents, coeffs)):
                print(f"const float c_{e}({c:+.12e}f);")
            if print_nl: print()

        if args.format in ["all", "array"]:
            print_comment()
            print("const float coef[] = {");
            for i, (e, c) in enumerate(reversed(list(zip(exponents, coeffs)))):
                print(f"    {c:+.12e}, // * x^{e}")
            print("};")
            if print_nl: print()

        if args.format in ["all", "switch"]:
            print("case ApproximationPrecision::" + loss.upper() + "_Poly" + str(order) + ":" +
                  f" // (MSE={mean_squared_error:.4e}, MAE={max_abs_error:.4e}, MaxUlpE={max_ulp_error:.4e})")
            print("    c = {" + (", ".join([f"{c:+.12e}f" for c in coeffs])) + "}; break;")
            if print_nl: print()

        if args.format in ["all", "table"]:
            print("{OO::" + loss.upper() + ", "
                  + f"{{{float32_metrics.mean_squared_error:.6e}, {float32_metrics.max_abs_error:.6e}, {float32_metrics.max_ulp_error:.3e}}}, "
                  + f"{{{float64_metrics.mean_squared_error:.6e}, {float64_metrics.max_abs_error:.6e}, {float64_metrics.max_ulp_error:.3e}}}, "
                  + "{" + ", ".join([f"{c:+.12e}" for c in coeffs]) + "}},")
            if print_nl: print()


        if args.print: print("exponent:", exponents)

