import numpy as np
import argparse

np.set_printoptions(linewidth=3000)

parser = argparse.ArgumentParser()
parser.add_argument("func")
parser.add_argument("order", type=int)
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
    lower, upper = 0.0, np.pi
elif args.func == "cos":
    func = np.cos
    exponents = np.arange(order) * 2
    lower, upper = 0.0, np.pi
elif args.func == "exp":
    func = lambda x: np.exp(x)
    exponents = np.arange(order)
    lower, upper = -np.log(2), np.log(2)
else:
    print("Unknown function:", args.func)
    exit(1)

X = np.linspace(lower, upper, 2048 * 8)
target = func(X)

print("exponent:", exponents)
coeffs = np.zeros(len(exponents))
powers = np.power(X[:,None], exponents)


loss_power = 120

lstsq_iterations = 15000
loss_history = np.zeros((lstsq_iterations, 2))

# If the loss is MSE, then this is just a linear system we can solve for.
# We will iteratively adjust the weights to put more focus on the parts where it goes wrong.
weight = np.ones_like(target)

try:
    for i in range(lstsq_iterations):
        norm_weight = weight / np.mean(weight)
        coeffs, residuals, rank, s = np.linalg.lstsq(powers * norm_weight[:,None], target * norm_weight, rcond=None)
        if i == 0:
            init_coeffs = coeffs.copy()

        y_hat = np.sum((powers * coeffs)[:,::-1], axis=-1)
        diff = y_hat - target
        abs_diff = np.abs(diff)
        max_abs_error = np.amax(np.abs(diff))
        if i % 10 == 0:
            print("coefficients:", coeffs, f"  MaxAE: {max_abs_error:20.17f}  mean weight: {weight.mean():10.8f}")
        norm_abs_diff = abs_diff / np.mean(abs_diff)
        p = i / lstsq_iterations
        p = min(np.sqrt(p) * 1.25, 1.0)
        weight += np.power(norm_abs_diff, 2 + int(loss_power * p) // 2 * 2)

        loss = np.power(diff, loss_power)
        loss_history[i, 0] = np.mean(loss)
        loss_history[i, 1] = max_abs_error

except KeyboardInterrupt:
    print("Interrupted")


print(coeffs)
y_hat = np.sum((powers * coeffs)[:,::-1], axis=-1)
y_hat_init = np.sum((powers * init_coeffs)[:,::-1], axis=-1)
diff = y_hat - target
loss = np.power(diff, loss_power)
mean_loss = np.mean(loss)
diff = y_hat - target
print(f"mse: {mean_loss:40.27f}  max abs error: {max_abs_error:20.17f}")

print()
print(f"// Coefficients with max error: {max_abs_error:.4e}")
for i, (e, c) in enumerate(zip(exponents, coeffs)):
    print(f"const float c_{e}({c:.12e}f);")
print()
print()
print(f"// Coefficients with max error: {max_abs_error:.4e}")
for i, (e, c) in enumerate(zip(exponents, coeffs)):
    print(f"c.push_back({c:.12e}f);")
print()
print("exponent:", exponents)

import matplotlib.pyplot as plt

fig, ax = plt.subplots(5, figsize=(5.5, 8))
ax[0].set_title("Comparison of exact and approximate " + args.func)
ax[0].plot(X, target, label=args.func)
ax[0].plot(X, y_hat, label='approx')
ax[0].grid()
ax[0].set_xlim(lower, upper)
ax[0].legend()

ax[1].set_title("Absolute error in log-scale")
ax[1].semilogy(X, np.abs(y_hat_init - target), label='abs error (init)')
ax[1].semilogy(X, np.abs(diff), label='abs error (final)')
ax[1].axhline(np.amax(np.abs(y_hat_init - target)), linestyle=':', c='C0')
ax[1].axhline(np.amax(np.abs(diff)), linestyle=':', c='C1')
ax[1].grid()
ax[1].set_xlim(lower, upper)
ax[1].legend()

ax[2].set_title("Error")
ax[2].plot(X, y_hat_init - target, label='init diff')
ax[2].plot(X, y_hat - target, label='final diff')
ax[2].grid()
ax[2].set_xlim(lower, upper)
ax[2].legend()

ax[3].set_title("LstSq Weight (log-scale)")
ax[3].semilogy(X, norm_weight, label='weight')
ax[3].grid()
ax[3].set_xlim(lower, upper)
ax[3].legend()

ax[4].set_title("Maximal Absolute Error progression during optimization")
ax[4].semilogx(1 + np.arange(loss_history.shape[0]), loss_history[:,1], label='MaxAE')
ax[4].set_xlim(1, loss_history.shape[0] + 1)
ax[4].axhline(y=loss_history[0,1], linestyle=':', color='k')
ax[4].grid()
ax[4].legend()
plt.tight_layout()
plt.show()
