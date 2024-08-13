import numpy as np
import argparse

np.set_printoptions(linewidth=3000)

parser = argparse.ArgumentParser()
parser.add_argument("func")
parser.add_argument("order", type=int)
parser.add_argument("loss", choices=["mse", "mae", "mulpe", "mulpe_mae"], default="mulpe")
parser.add_argument("--no-gui", action='store_true')
parser.add_argument("--print", action='store_true')
parser.add_argument("--pbar", action='store_true')
parser.add_argument("--format", default="all", choices=["all", "switch", "array", "consts"])
args = parser.parse_args()

order = args.order
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
    exponents = 1 + np.arange(order) * 2
    lower, upper = 0.0, np.pi / 2
elif args.func == "cos":
    func = np.cos
    exponents = np.arange(order) * 2
    lower, upper = 0.0, np.pi / 2
elif args.func == "exp":
    func = lambda x: np.exp(x)
    exponents = np.arange(order)
    lower, upper = 0, np.log(2)
elif args.func == "log":
    func = lambda x: np.log(x + 1.0)
    exponents = np.arange(order)
    lower, upper = 0, np.log(2)
else:
    print("Unknown function:", args.func)
    exit(1)

X = np.linspace(lower, upper, 2048 * 8)
target = func(X)
target_spacing = np.spacing(np.abs(target).astype(np.float32)).astype(np.float64) # Precision (aka ULP)

print("exponent:", exponents)
coeffs = np.zeros(len(exponents))
powers = np.power(X[:,None], exponents)


loss_power = 500

lstsq_iterations = loss_power * 10

# If the loss is MSE, then this is just a linear system we can solve for.
# We will iteratively adjust the weights to put more focus on the parts where it goes wrong.
weight = np.ones_like(target)

if args.loss == "mse":
    lstsq_iterations = 1

loss_history = np.zeros((lstsq_iterations, 3))

iterator = range(lstsq_iterations)
if args.pbar:
    import tqdm
    iterator = tqdm.trange(lstsq_iterations)

try:
    for i in iterator:
        norm_weight = weight / np.mean(weight)
        coeffs, residuals, rank, s = np.linalg.lstsq(powers * norm_weight[:,None], target * norm_weight, rcond=None)

        y_hat = np.sum((powers * coeffs)[:,::-1], axis=-1)
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

        if args.loss == "mae":
            norm_error_metric = abs_diff / np.amax(abs_diff)
        elif args.loss == "mulpe":
            norm_error_metric = abs_ulp_error / max_ulp_error
        elif args.loss == "mulpe_mae":
            norm_error_metric = 0.5 * (abs_ulp_error / max_ulp_error + abs_diff / max_abs_error)
        elif args.loss == "mse":
            norm_error_metric = np.square(abs_diff)

        p = i / lstsq_iterations
        p = min(p * 1.25, 1.0)
        raised_error = np.power(norm_error_metric, 2 + loss_power * p)
        #weight += raised_error / np.mean(raised_error)
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


print("Init  coeffs:", init_coeffs)
print("Final coeffs:", coeffs)
print(f"mse: {mean_loss:40.27f}  max abs error: {max_abs_error:20.17f}  max ulp error: {max_ulp_error:e}")

def print_comment(indent=""):
    print(indent + "// "
          + {"mae": "Max Absolute Error", "mse": "Mean Squared Error", "mulpe": "Max ULP Error", "mulpe_mae": "MaxUlpAE"}[args.loss]
          + f" optimized (MSE={mean_squared_error:.4e}, MAE={max_abs_error:.4e}, MaxUlpE={max_ulp_error:.4e})")


if args.format in ["all", "consts"]:
    print()
    print_comment()
    for i, (e, c) in enumerate(zip(exponents, coeffs)):
        print(f"const float c_{e}({c:+.12e}f);")
    print()


if args.format in ["all", "array"]:
    print()
    print_comment()
    print("const float coef[] = {");
    for i, (e, c) in enumerate(reversed(list(zip(exponents, coeffs)))):
        print(f"    {c:+.12e}, // * x^{e}")
    print("};\n")

if args.format in ["all", "switch"]:
    print()
    print("case ApproximationPrecision::" + args.loss.upper() + "_Poly" + str(args.order) + ":" +
          f" // (MSE={mean_squared_error:.4e}, MAE={max_abs_error:.4e}, MaxUlpE={max_ulp_error:.4e})")
    print("    c = {" + (", ".join([f"{c:+.12e}f" for c in coeffs])) + "}; break;")
    print()


print()
print("exponent:", exponents)

if args.no_gui:
    exit()

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
