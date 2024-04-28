def make_oklch(l, c, h):
    return ("oklch(%.1f%% %.2f %.0f)" % (l * 100, c, h))


STEPS = 20

for i in range(STEPS):
    f = i / (STEPS - 1)
    col = make_oklch(0.9 - f * 0.5, 0.05 + 0.1 * f, 140)
    print(".block-CostColor%d:first-child { border-left: 8px solid %s; }" % (i, col))
print(".block-CostColorNone:first-child { border-left: transparent; }")
print()

for i in range(STEPS):
    f = i / (STEPS - 1)
    col = make_oklch(0.9 - f * 0.5, 0.05 + 0.1 * f, 140)
    print(".line-CostColor%d:first-child { border-right: 8px solid %s; }" % (i, col))
print(".line-CostColorNone:first-child { border-right: transparent; }")
print()


for i in range(STEPS):
    f = i / (STEPS - 1)
    col = make_oklch(0.9 - f * 0.5, 0.05 + 0.1 * f, 300)
    print(".block-CostColor%d:last-child { border-left: 8px solid %s; }" % (i, col))
print(".block-CostColorNone:last-child { border-left: transparent; }")
print()

for i in range(STEPS):
    f = i / (STEPS - 1)
    col = make_oklch(0.9 - f * 0.5, 0.05 + 0.1 * f, 300)
    print(".line-CostColor%d:last-child { border-right: 8px solid %s; }" % (i, col))
print(".line-CostColorNone:last-child { border-right: transparent; }")
print()
