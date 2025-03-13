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
import rich.console
import rich.progress
import concurrent.futures

console = rich.console.Console()
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
                    help=("R|What to optimize for.\n"
                          + " * mse: Mean Squared Error\n"
                          + " * mae: Maximal Absolute Error\n"
                          + " * mulpe: Maximal ULP Error  [default]\n"
                          + " * mulpe_mae: 50%% mulpe + 50%% mae"))
parser.add_argument("--gui", action='store_true', help="Do produce plots.")
parser.add_argument("--with-max-error", action='store_true', help="Fill out the observed max abs/ulp error in the printed table.")
parser.add_argument("--print", action='store_true', help="Print while optimizing.")
parser.add_argument("--pbar", action='store_true', help="Create a progress bar while optimizing.")
args = parser.parse_args()

loss_power = 1500

import collections

Metrics = collections.namedtuple("Metrics", ["mean_squared_error", "max_abs_error", "max_ulp_error"])


def optimize_approximation(loss, order, progress):
    fixed_part_taylor = []
    X = None
    will_invert = False
    if args.func == "atan":
        if hasattr(np, "atan"):
            func = np.atan
        elif hasattr(np, "arctan"):
            func = np.arctan
        else:
            console.print("Your numpy version doesn't support arctan.")
            exit(1)
        exponents = 1 + np.arange(order) * 2
        lower, upper = 0.0, 1.0
    elif args.func == "sin":
        func = np.sin
        exponents = 1 + np.arange(order)
        if loss == "mulpe":
            fixed_part_taylor = [0, 1]
        else:
            fixed_part_taylor = [0]
        lower, upper = 0.0, np.pi / 2
    elif args.func == "cos":
        func = np.cos
        fixed_part_taylor = [1]
        exponents = 1 + np.arange(order)
        lower, upper = 0.0, np.pi / 2
    elif args.func == "tan":
        func = np.tan
        fixed_part_taylor = [0, 1, 0, 1 / 3]  # We want a very accurate approximation around zero, because we will need it to invert and compute the tan near the poles.
        if order == 2:
            fixed_part_taylor = [0]  # Let's optimize at least the ^1 term
        if order == 2:
            fixed_part_taylor = [0, 1]  # Let's optimize at least the ^3 term
        exponents = 1 + np.arange(order) * 2
        lower, upper = 0.0, np.pi / 4
        X = np.concatenate([np.logspace(-5, 0, num=2048 * 17), np.linspace(0, 1, 9000)]) * (np.pi / 4)
        X = np.sort(X)
        will_invert = True
    elif args.func == "exp":
        func = np.exp
        fixed_part_taylor = [1, 1]
        exponents = np.arange(2, order)
        lower, upper = 0, np.log(2)
    elif args.func == "expm1":
        func = np.expm1
        exponents = np.arange(1, order + 1)
        lower, upper = 0, np.log(2)
    elif args.func == "log":
        def func(x): return np.log(x + 1.0)
        exponents = np.arange(1, order + 1)
        lower, upper = -0.25, 0.5
    elif args.func == "tanh":
        func = np.tanh
        fixed_part_taylor = [0, 1]
        exponents = np.arange(2, order + 1)
        lower, upper = 0.0, 4.0
    elif args.func == "asin":
        func = np.arcsin
        fixed_part_taylor = [0, 1]
        exponents = 1 + 2 * np.arange(0, order)
        lower, upper = -1.0, 1.0
    elif args.func == "asin_invx":
        def func(x): return np.arcsin(1/x)
        exponents = 1 + np.arange(order)
        lower, upper = 1.0, 2.0
    else:
        console.print("Unknown function:", args.func)
        exit(1)

    # Make sure we never optimize the coefficients of the fixed part.
    exponents = exponents[exponents >= len(fixed_part_taylor)]

    X_dense = np.linspace(lower, upper, 512 * 31 * 11)
    # if lower >= 0.0:
    #    loglow = -5.0 if lower == 0.0 else np.log(lower)
    #    X_dense = np.concatenate([X_dense, np.logspace(loglow, np.log(upper), num=2048 * 17)])
    #    X_dense = np.sort(X_dense)

    def func_fixed_part(x):
        return x * 0.0

    if len(fixed_part_taylor) > 0:
        assert len(fixed_part_taylor) <= 4

        def ffp(x):
            x2 = x * x
            x3 = x2 * x
            x4 = x2 * x2
            return np.sum([xp * c for xp, c in zip([np.ones_like(x), x, x2, x3, x4], fixed_part_taylor)], axis=0)
        func_fixed_part = ffp

    if X is None:
        X = np.linspace(lower, upper, 512 * 31)
    target = func(X)
    fixed_part = func_fixed_part(X)
    target_fitting_part = target - fixed_part

    target_spacing = np.spacing(np.abs(target).astype(np.float32)).astype(np.float64)  # Precision (i.e., ULP)
    # We will optimize everything using double precision, which means we will obtain more bits of
    # precision than the actual target values in float32, which means that our reconstruction and
    # ideal target value can be a non-integer number of float32-ULPs apart.

    if args.print:
        console.print("exponent:", exponents)
    coeffs = np.zeros(len(exponents))
    powers = np.power(X[:, None], exponents)
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

    # if will_invert: weight += 1.0 / (np.abs(target) + target_spacing)

    loss_history = np.zeros((lstsq_iterations, 3))

    try:
        task = progress.add_task(f"{args.func} {loss} order={order}", total=lstsq_iterations)
        for i in progress.track(range(lstsq_iterations), task_id=task):
            norm_weight = weight / np.mean(weight)
            coeffs, residuals, rank, s = np.linalg.lstsq(powers * norm_weight[:, None], target_fitting_part * norm_weight, rcond=-1)

            y_hat = fixed_part + np.sum((powers * coeffs)[:, ::-1], axis=-1)
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
                console.log(f"[{((i + 1) / lstsq_iterations * 100.0):3.0f}%] coefficients:", coeffs,
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
        console.log("Interrupted")

    def eval(dtype):
        ft_x_dense = X_dense.astype(dtype)
        ft_target_dense = func(X_dense).astype(dtype)
        ft_powers = np.power(ft_x_dense[:, None], exponents).astype(dtype)
        ft_fixed_part = func_fixed_part(ft_x_dense).astype(dtype)
        ft_y_hat = ft_fixed_part + np.sum(ft_powers * coeffs, axis=-1).astype(dtype)
        ft_diff = ft_y_hat - ft_target_dense.astype(dtype)
        ft_abs_diff = np.abs(ft_diff)
        # MSE metric
        ft_mean_squared_error = np.mean(np.square(ft_diff))
        # MAE metric
        ft_max_abs_error = np.amax(ft_abs_diff)
        # MaxULP metric
        ft_ulp_error = ft_diff / np.spacing(np.abs(ft_target_dense).astype(dtype))
        ft_abs_ulp_error = np.abs(ft_ulp_error)
        ft_max_ulp_error = np.amax(ft_abs_ulp_error).astype(np.int64)

        return Metrics(ft_mean_squared_error, ft_max_abs_error, ft_max_ulp_error)

    float16_metrics = eval(np.float16)
    float32_metrics = eval(np.float32)
    float64_metrics = eval(np.float64)

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
        ax[3].semilogx(1 + np.arange(loss_history.shape[0]), loss_history[:, 1])
        ax[3].set_xlim(1, loss_history.shape[0] + 1)
        ax[3].axhline(y=loss_history[0, 1], linestyle=':', color='k')
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
        ax[7].loglog(1 + np.arange(loss_history.shape[0]), loss_history[:, 2])
        ax[7].set_xlim(1, loss_history.shape[0] + 1)
        ax[7].axhline(y=loss_history[0, 2], linestyle=':', color='k')
        ax[7].grid()

        ax[4].set_title("LstSq Weight\n(log-scale)")
        ax[4].semilogy(X, norm_weight, label='weight')
        ax[4].grid()
        ax[4].set_xlim(lower, upper)
        ax[4].legend()

        plt.tight_layout()
        plt.show()

    return exponents, fixed_part_taylor, init_coeffs, coeffs, float16_metrics, float32_metrics, float64_metrics, loss_history


def num_to_str(c):
    if c == 0.0:
        return "0"
    if c == 1.0:
        return "1"
    return c.hex()


def formula(coeffs, exponents=None):
    if exponents is None:
        exponents = np.arange(len(coeffs))
    terms = []
    for c, e in zip(coeffs, exponents):
        if c == 0:
            continue
        if c == 1:
            terms.append(f"x^{e}")
        else:
            terms.append(f"{c:.12f} * x^{e}")
    return " + ".join(terms)


with concurrent.futures.ThreadPoolExecutor(4) as pool, rich.progress.Progress(console=console, disable=not args.pbar) as progress:
    futures = []
    for loss in args.loss:
        for order in args.order:
            futures.append((loss, order, pool.submit(optimize_approximation, loss, order, progress)))

    for loss, order, future in futures:
        exponents, fixed_part_taylor, init_coeffs, coeffs, float16_metrics, float32_metrics, float64_metrics, loss_history = future.result()

        degree = len(fixed_part_taylor) - 1
        if len(exponents) > 0:
            degree = max(degree, np.amax(exponents))
        all_coeffs = np.zeros(degree + 1)
        for e, c in enumerate(fixed_part_taylor):
            all_coeffs[e] = c
        for e, c in zip(exponents, coeffs):
            all_coeffs[e] = c

        code = "{"
        code += f" /* {loss.upper()} Polynomial degree {degree}: {formula(all_coeffs)} */\n"
        if args.with_max_error:
            code += f"    /* f16 */ {{{float16_metrics.mean_squared_error:.6e}, {float16_metrics.max_abs_error:.6e}, {float16_metrics.max_ulp_error}u}},\n"
            code += f"    /* f32 */ {{{float32_metrics.mean_squared_error:.6e}, {float32_metrics.max_abs_error:.6e}, {float32_metrics.max_ulp_error}u}},\n"
            code += f"    /* f64 */ {{{float64_metrics.mean_squared_error:.6e}, {float64_metrics.max_abs_error:.6e}, {float64_metrics.max_ulp_error}u}},\n"
        else:
            code += f"    /* f16 */ {{{float16_metrics.mean_squared_error:.6e}}},\n"
            code += f"    /* f32 */ {{{float32_metrics.mean_squared_error:.6e}}},\n"
            code += f"    /* f64 */ {{{float64_metrics.mean_squared_error:.6e}}},\n"
        code += "    /* p */ {" + ", ".join([f"{num_to_str(c)}" for c in all_coeffs]) + "}\n"
        code += "},"
        console.print(code)

        if args.print:
            console.print("exponent:", exponents)
