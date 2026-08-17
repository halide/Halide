#!/usr/bin/python3

# Halide tutorial lesson 18: Factoring an associative reduction using rfactor

# This lesson demonstrates how to parallelize or vectorize an associative
# reduction using the scheduling directive 'rfactor'.

import random

import halide as hl


def main():
    # Declare some Vars to use below.
    x, y, i, u, v = (
        hl.Var("x"),
        hl.Var("y"),
        hl.Var("i"),
        hl.Var("u"),
        hl.Var("v"),
    )

    # Create an input with random values.
    input = hl.Buffer(hl.UInt(8), [8, 8], "input")
    for yy in range(8):
        for xx in range(8):
            input[xx, yy] = random.randint(0, 255)

    if True:
        # As mentioned previously in lesson 9, parallelizing variables that
        # are part of a reduction domain is tricky, since there may be data
        # dependencies across those variables.

        # Consider the histogram example in lesson 9:
        histogram = hl.Func("hist_serial")
        histogram[i] = 0
        r = hl.RDom([(0, input.width()), (0, input.height())])
        histogram[input[r.x, r.y] // 32] += 1

        histogram.vectorize(i, 8)
        histogram.realize([8])

        # See figures/lesson_18_hist_serial.mp4 for a visualization of
        # what this does, below.

        # We can vectorize the initialization of the histogram
        # buckets, but since there are data dependencies across r.x
        # and r.y in the update definition (i.e. the update refers to
        # value computed in the previous iteration), we can't
        # parallelize or vectorize r.x or r.y without introducing a
        # race condition. The following code would produce an error:
        # histogram.update().parallel(r.y)

    if True:
        # Note, however, that the histogram operation (which is a
        # kind of sum reduction) is associative. A common trick to
        # speed-up associative reductions is to slice up the
        # reduction domain into smaller slices, compute a partial
        # result over each slice, and then merge the results. Since
        # the computation of each slice is independent, we can
        # parallelize over slices.

        # Going back to the histogram example, we slice the reduction
        # domain into rows by defining an intermediate function that
        # computes the histogram of each row independently:
        intermediate = hl.Func("intm_par_manual")
        intermediate[i, y] = 0
        rx = hl.RDom([(0, input.width())])
        intermediate[input[rx, y] // 32, y] += 1

        # We then define a second stage which sums those partial
        # results:
        histogram = hl.Func("merge_par_manual")
        histogram[i] = 0
        ry = hl.RDom([(0, input.height())])
        histogram[i] += intermediate[i, ry]

        # Since the intermediate no longer has data dependencies
        # across the y dimension, we can parallelize it over y:
        intermediate.compute_root().update().parallel(y)

        # We can also vectorize the initializations.
        intermediate.vectorize(i, 8)
        histogram.vectorize(i, 8)

        histogram.realize([8])

        # See figures/lesson_18_hist_manual_par.mp4 for a visualization of
        # what this does, below.

    if True:
        # This manual factorization of an associative reduction can
        # be tedious and bug-prone. Although it's fairly easy to do
        # manually for the histogram, it can get complex pretty fast,
        # especially if the hl.RDom may has a predicate (hl.RDom.where),
        # or when the function reduces onto a multi-dimensional
        # tuple.

        # Halide provides a way to do this type of factorization
        # through the scheduling directive 'rfactor'. rfactor splits
        # an associative update definition into an intermediate which
        # computes the partial results over slices of a reduction
        # domain and replaces the current update definition with a
        # new definition which merges those partial results.

        # Using rfactor, we don't need to change the algorithm at all:
        histogram = hl.Func("hist_rfactor_par")
        histogram[x] = 0
        r = hl.RDom([(0, input.width()), (0, input.height())])
        histogram[input[r.x, r.y] // 32] += 1

        # The task of factoring of associative reduction is moved
        # into the schedule, via rfactor. rfactor takes as input a
        # list of (RVar, Var) pairs, which contains list of reduction
        # variables (RVars) to be made "parallelizable". In the
        # generated intermediate Func, all references to this
        # reduction variables are replaced with references to "pure"
        # variables (the Vars). Since, by construction, Vars are
        # race-condition free, the intermediate reduction is now
        # parallelizable across those dimensions. All reduction
        # variables not in the list are removed from the original
        # function and "lifted" to the intermediate.

        # To generate the same code as the manually-factored version,
        # we do the following:
        intermediate = histogram.update().rfactor([(r.y, y)])
        # We pass [(r.y, y)] as the argument to rfactor to make the
        # histogram parallelizable across the y dimension, similar to
        # the manually-factored version.
        intermediate.compute_root().update().parallel(y)

        # In the case where you are only slicing up the domain across
        # a single variable, you can actually drop the brackets and
        # write the rfactor the following way.
        # intermediate = histogram.update().rfactor(r.y, y)

        # Vectorize the initializations, as we did above.
        intermediate.vectorize(x, 8)
        histogram.vectorize(x, 8)

        # It is important to note that rfactor (or reduction
        # factorization in general) only works for associative
        # reductions. Associative reductions have the nice property
        # that their results are the same no matter how the
        # computation is grouped (i.e. split into chunks). If rfactor
        # can't prove the associativity of a reduction, it will throw
        # an error.

        halide_result = histogram.realize([8])

        # See figures/lesson_18_hist_rfactor_par.mp4 for a
        # visualization of what this does, below.

        # The equivalent Python is:
        c_intm = [[0 for _ in range(8)] for _ in range(input.height())]
        for yy in range(input.height()):  # parallel
            for r_x in range(input.width()):
                c_intm[yy][input[r_x, yy] // 32] += 1

        c_result = [0] * 8
        for xx in range(8):
            for r_y in range(input.height()):
                c_result[xx] += c_intm[r_y][xx]

        # Check the answers agree:
        for xx in range(8):
            assert c_result[xx] == halide_result[xx], (
                f"halide_result({xx}) = {halide_result[xx]} instead of {c_result[xx]}"
            )

    if True:
        # Now that we can factor associative reductions with the
        # scheduling directive 'rfactor', we can explore various
        # factorization strategies using the schedule alone. Given
        # the same serial histogram code:
        histogram = hl.Func("hist_rfactor_vec")
        histogram[x] = 0
        r = hl.RDom([(0, input.width()), (0, input.height())])
        histogram[input[r.x, r.y] // 32] += 1

        # Instead of r.y, we rfactor on r.x this time to slice the
        # domain into columns.
        intermediate = histogram.update().rfactor(r.x, u)

        # Now that we're computing an independent histogram
        # per-column, we can vectorize over columns.
        intermediate.compute_root().update().vectorize(u, 8)

        # Note that since vectorizing the inner dimension changes the
        # order in which values are added to the final histogram
        # buckets computations, so this trick only works if the
        # associative reduction is associative *and*
        # commutative. rfactor will attempt to prove these properties
        # hold and will throw an error if it can't.

        # Vectorize the initializations.
        intermediate.vectorize(x, 8)
        histogram.vectorize(x, 8)

        halide_result = histogram.realize([8])

        # See figures/lesson_18_hist_rfactor_vec.mp4 for a
        # visualization of what this does, below.

        # The equivalent Python is:
        c_intm = [[0 for _ in range(8)] for _ in range(input.width())]
        for r_y in range(input.height()):
            for uu in range(input.width() // 8):
                for u_i in range(8):  # vectorize
                    c_intm[uu * 8 + u_i][input[uu * 8 + u_i, r_y] // 32] += 1

        c_result = [0] * 8
        for xx in range(8):
            for r_x in range(input.width()):
                c_result[xx] += c_intm[r_x][xx]

        # Check the answers agree:
        for xx in range(8):
            assert c_result[xx] == halide_result[xx], (
                f"halide_result({xx}) = {halide_result[xx]} instead of {c_result[xx]}"
            )

    if True:
        # We can also slice a reduction domain up over multiple
        # dimensions at once. This time, we'll compute partial
        # histograms over tiles of the domain.
        histogram = hl.Func("hist_rfactor_tile")
        histogram[x] = 0
        r = hl.RDom([(0, input.width()), (0, input.height())])
        histogram[input[r.x, r.y] // 32] += 1

        # We first split both r.x and r.y by a factor of four.
        rx_outer, rx_inner = hl.RVar("rx_outer"), hl.RVar("rx_inner")
        ry_outer, ry_inner = hl.RVar("ry_outer"), hl.RVar("ry_inner")
        histogram.update().split(r.x, rx_outer, rx_inner, 4).split(
            r.y, ry_outer, ry_inner, 4
        )

        # We now call rfactor to make an intermediate function that
        # independently computes a histogram of each tile.
        intermediate = histogram.update().rfactor([(rx_outer, u), (ry_outer, v)])

        # We can now parallelize the intermediate over tiles.
        intermediate.compute_root().update().parallel(u).parallel(v)

        # We also reorder the tile indices outermost to give the
        # classic tiled traversal.
        intermediate.update().reorder(rx_inner, ry_inner, u, v)

        # Vectorize the initializations.
        intermediate.vectorize(x, 8)
        histogram.vectorize(x, 8)

        halide_result = histogram.realize([8])

        # See figures/lesson_18_hist_rfactor_tile.mp4 for a visualization of
        # what this does, below.

        # The equivalent Python is:
        c_intm = [[[0 for _ in range(8)] for _ in range(4)] for _ in range(4)]
        for vv in range(input.height() // 2):  # parallel
            for uu in range(input.width() // 2):  # parallel
                for r_yi in range(2):
                    for r_xi in range(2):
                        c_intm[vv][uu][input[uu * 2 + r_xi, vv * 2 + r_yi] // 32] += 1

        c_result = [0] * 8
        for xx in range(8):
            for r_y_outer in range(input.height() // 2):
                for r_x_outer in range(input.width() // 2):
                    c_result[xx] += c_intm[r_y_outer][r_x_outer][xx]

        # Check the answers agree:
        for xx in range(8):
            assert c_result[xx] == halide_result[xx], (
                f"halide_result({xx}) = {halide_result[xx]} instead of {c_result[xx]}"
            )

    print("Success!")
    return 0


if __name__ == "__main__":
    main()
