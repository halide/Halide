def make_oklch(l, c, h):
    return (f"oklch({l * 100:.1f}% {c:.2f} {h:.0f})")


STEPS = 20
for color_theme, fL in [("light", 1.0), ("dark", 0.7)]:
    print()
    print("[data-theme=\"" + color_theme + "\"] {")

    for i in range(STEPS):
        f = i / (STEPS - 1)
        col = make_oklch((0.9 - f * 0.5) * fL, 0.05 + 0.1 * f, 140)
        print("    .block-CostColor%d:first-child { border-left: 8px solid %s; }" % (i, col))
    print("    .block-CostColorNone:first-child { border-left: transparent; }")
    print()

    for i in range(STEPS):
        f = i / (STEPS - 1)
        col = make_oklch((0.9 - f * 0.5) * fL, 0.05 + 0.1 * f, 140)
        print("    .line-CostColor%d:first-child { border-right: 8px solid %s; }" % (i, col))
    print("    .line-CostColorNone:first-child { border-right: transparent; }")
    print()


    for i in range(STEPS):
        f = i / (STEPS - 1)
        col = make_oklch((0.9 - f * 0.5) * fL, 0.05 + 0.1 * f, 300)
        print("    .block-CostColor%d:last-child { border-left: 8px solid %s; }" % (i, col))
    print("    .block-CostColorNone:last-child { border-left: transparent; }")
    print()

    for i in range(STEPS):
        f = i / (STEPS - 1)
        col = make_oklch((0.9 - f * 0.5) * fL, 0.05 + 0.1 * f, 300)
        print("    .line-CostColor%d:last-child { border-right: 8px solid %s; }" % (i, col))
    print("    .line-CostColorNone:last-child { border-right: transparent; }")

    print("} /* End of", color_theme, "theme. */")

print()
print("Theme agnostic")
print()

def make_oklch(l, c, h):
    return (f"oklch(calc({l * 100:.1f}% * var(--cost-Lf)) {c:.2f} {h:.0f})")


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
