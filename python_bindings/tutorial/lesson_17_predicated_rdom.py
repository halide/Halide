#!/usr/bin/python3

# Halide tutorial lesson 17: Reductions over non-rectangular domains

# This lesson demonstrates how to define updates that iterate over
# subsets of a reduction domain using predicates.

import halide as hl


def main():
    # In lesson 9, we learned how to use hl.RDom to define a "reduction
    # domain" to use in a Halide update definition. The domain
    # defined by an hl.RDom, however, is always rectangular, and the
    # update occurs at every point in that rectangular domain. In
    # some cases, we might want to iterate over some non-rectangular
    # domain, e.g. a circle. We can achieve this behavior by using
    # the hl.RDom.where directive.

    if True:
        # Starting with this pure definition:
        circle = hl.Func("circle")
        x, y = hl.Var("x"), hl.Var("y")
        circle[x, y] = x + y

        # Say we want an update that multiplies by two the values inside a
        # circular region centered at (3, 3) with radius of 3. To do
        # this, we first define the minimal bounding box over the
        # circular region using an hl.RDom.
        r = hl.RDom([(0, 7), (0, 7)])

        # The bounding box does not have to be minimal. In fact, the
        # box can be of any size, as long it covers the region we'd
        # like to update. However, the tighter the bounding box, the
        # tighter the generated loop bounds will be. Halide will
        # tighten the loop bounds automatically when possible, but in
        # general, it is better to define a minimal bounding box.

        # Then, we use hl.RDom.where to define the predicate over that
        # bounding box, such that the update is performed only if the
        # given predicate evaluates to true, i.e. within the circular
        # region.
        r.where((r.x - 3) * (r.x - 3) + (r.y - 3) * (r.y - 3) <= 10)

        # After defining the predicate, we then define the update.
        circle[r.x, r.y] *= 2

        halide_result = circle.realize([7, 7])

        # See figures/lesson_17_rdom_circular.mp4 for a visualization of
        # what this did, below.

        # The equivalent Python is:
        c_result = [[x + y for x in range(7)] for y in range(7)]
        for r_y in range(7):
            for r_x in range(7):
                # Update is only performed if the predicate evaluates to true.
                if (r_x - 3) * (r_x - 3) + (r_y - 3) * (r_y - 3) <= 10:
                    c_result[r_y][r_x] *= 2

        # Check the results match:
        for y in range(7):
            for x in range(7):
                assert halide_result[x, y] == c_result[y][x], (
                    f"halide_result({x}, {y}) = {halide_result[x, y]} instead of {c_result[y][x]}"
                )

    if True:
        # We can also define multiple predicates over an hl.RDom. Let's
        # say now we want the update to happen within some triangular
        # region. To do this we define three predicates, where each
        # corresponds to one side of the triangle.
        triangle = hl.Func("triangle")
        x, y = hl.Var("x"), hl.Var("y")
        triangle[x, y] = x + y
        # First, let's define the minimal bounding box over the triangular
        # region.
        r = hl.RDom([(0, 8), (0, 10)])
        # Next, let's add the three predicates to the hl.RDom using
        # multiple calls to hl.RDom.where
        r.where(r.x + r.y > 5)
        r.where(3 * r.y - 2 * r.x < 15)
        r.where(4 * r.x - r.y < 20)

        # We can also pack the multiple predicates into one like so:
        # r.where((r.x + r.y > 5) & (3 * r.y - 2 * r.x < 15) & (4 * r.x - r.y < 20))

        # Then define the update.
        triangle[r.x, r.y] *= 2

        halide_result = triangle.realize([10, 10])

        # See figures/lesson_17_rdom_triangular.mp4 for a
        # visualization of what this did, below.

        # The equivalent Python is:
        c_result = [[x + y for x in range(10)] for y in range(10)]
        for r_y in range(10):
            for r_x in range(8):
                # Update is only performed if the predicate evaluates to true.
                if r_x + r_y > 5 and 3 * r_y - 2 * r_x < 15 and 4 * r_x - r_y < 20:
                    c_result[r_y][r_x] *= 2

        # Check the results match:
        for y in range(10):
            for x in range(10):
                assert halide_result[x, y] == c_result[y][x], (
                    f"halide_result({x}, {y}) = {halide_result[x, y]} instead of {c_result[y][x]}"
                )

    if True:
        # The predicate is not limited to the hl.RDom's variables only
        # (r.x, r.y, ...). It can also refer to free variables in
        # the update definition, and even make calls to other Funcs,
        # or make recursive calls to the same Func. For example:
        f, g = hl.Func("f"), hl.Func("g")
        x, y = hl.Var("x"), hl.Var("y")
        f[x, y] = 2 * x + y
        g[x, y] = x + y

        # This hl.RDom's predicates depend on the initial value of 'f'.
        r1 = hl.RDom([(0, 5), (0, 5)])
        r1.where(f[r1.x, r1.y] >= 4)
        r1.where(f[r1.x, r1.y] <= 7)
        f[r1.x, r1.y] /= 10

        f.compute_root()

        # While this one involves calls to another Func.
        r2 = hl.RDom([(1, 3), (1, 3)])
        r2.where(f[r2.x, r2.y] < 1)
        g[r2.x, r2.y] += 17

        halide_result_g = g.realize([5, 5])

        # See figures/lesson_17_rdom_calls_in_predicate.mp4 for a
        # visualization of what this did, below.

        # The equivalent Python for 'f' is:
        c_result_f = [[2 * x + y for x in range(5)] for y in range(5)]
        for r1_y in range(5):
            for r1_x in range(5):
                # Update is only performed if the predicate evaluates to true.
                if 4 <= c_result_f[r1_y][r1_x] <= 7:
                    c_result_f[r1_y][r1_x] //= 10

        # And, the equivalent Python for 'g' is:
        c_result_g = [[x + y for x in range(5)] for y in range(5)]
        for r2_y in range(1, 4):
            for r1_x in range(1, 4):
                # Update is only performed if the predicate evaluates to true.
                if c_result_f[r2_y][r1_x] < 1:
                    c_result_g[r2_y][r1_x] += 17

        # Check the results match:
        for y in range(5):
            for x in range(5):
                assert halide_result_g[x, y] == c_result_g[y][x], (
                    f"halide_result_g({x}, {y}) = {halide_result_g[x, y]} instead of {c_result_g[y][x]}"
                )

    print("Success!")
    return 0


if __name__ == "__main__":
    main()
