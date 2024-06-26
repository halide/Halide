import halide as hl


def test_multipass_constraints():
    input = hl.ImageParam(hl.Float(32), 2, "input")

    f = hl.Func("f")
    x = hl.Var("x")
    y = hl.Var("y")

    f[x, y] = input[x + 1, y + 1] + input[x - 1, y - 1]
    f[x, y] += 3.0
    f.update().vectorize(x, 4)

    o = f.output_buffer()

    # Now make some hard-to-resolve constraints
    input.dim(0).set_bounds(
        min=input.dim(1).min() - 5, extent=input.dim(1).extent() + o.dim(0).extent()
    )

    o.dim(0).set_bounds(
        min=0,
        extent=hl.select(
            o.dim(0).extent() < 22, o.dim(0).extent() + 1, o.dim(0).extent()
        ),
    )

    # Make a bounds query buffer
    query_buf = hl.Buffer.make_bounds_query(type=hl.Float(32), sizes=[7, 8])
    query_buf.set_min([2, 2])

    f.infer_input_bounds(query_buf)

    if (
        input.get().dim(0).min() != -4
        or input.get().dim(0).extent() != 34
        or input.get().dim(1).min() != 1
        or input.get().dim(1).extent() != 10
        or query_buf.dim(0).min() != 0
        or query_buf.dim(0).extent() != 24
        or query_buf.dim(1).min() != 2
        or query_buf.dim(1).extent() != 8
    ):

        print(
            "Constraints not correctly satisfied:\n",
            "in:",
            input.get().dim(0).min(),
            input.get().dim(0).extent(),
            input.get().dim(1).min(),
            input.get().dim(1).extent(),
            "out:",
            query_buf.dim(0).min(),
            query_buf.dim(0).extent(),
            query_buf.dim(1).min(),
            query_buf.dim(1).extent(),
        )
        assert False


if __name__ == "__main__":
    test_multipass_constraints()
