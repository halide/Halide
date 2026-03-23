import halide as hl

x = hl.Var("x")
y = hl.Var("y")
c = hl.Var("c")

# Note that this Generator explicitly inherits from hl.Generator;
# this isn't necessary (the decorator will inject the base class for you),
# but is allowed for better compatibility with static type checkers such
# as pytype. Thus, this is left here as a test to verify that this option
# remains functional.


@hl.generator(name="complexpy")
class ComplexPy(hl.Generator):
    vectorize = hl.GeneratorParam(True)
    extra_input_name = hl.GeneratorParam("")

    typed_buffer_input = hl.InputBuffer(hl.UInt(8), 3)
    untyped_buffer_input = hl.InputBuffer(None, 3)
    simple_input = hl.InputBuffer(None, 3)
    float_arg = hl.InputScalar(hl.Float(32))
    # We can use `int` as an alias for `hl.Int(32)` if we like
    int_arg = hl.InputScalar(int)

    simple_output = hl.OutputBuffer(hl.Float(32), 3)
    tuple_output = hl.OutputBuffer(None, 3)
    typed_buffer_output = hl.OutputBuffer(hl.Float(32), 3)
    untyped_buffer_output = hl.OutputBuffer(None, None)
    static_compiled_buffer_output = hl.OutputBuffer(hl.UInt(8), 3)
    scalar_output = hl.OutputScalar(hl.Float(32))

    def configure(self):
        g = self
        if len(g.extra_input_name):
            g.add_input(g.extra_input_name, hl.InputBuffer(hl.UInt(16), 3))
        g.add_output("extra_output", hl.OutputBuffer(hl.Float(64), 2))

    def generate(self):
        g = self

        g.simple_output[x, y, c] = hl.f32(g.simple_input[x, y, c])
        g.typed_buffer_output[x, y, c] = hl.f32(g.typed_buffer_input[x, y, c])
        g.untyped_buffer_output[x, y, c] = hl.cast(
            g.untyped_buffer_output.type(),
            g.untyped_buffer_input[x, y, c],
        )

        intermediate = hl.Func("intermediate")
        intermediate[x, y, c] = g.simple_input[x, y, c] * g.float_arg

        g.tuple_output[x, y, c] = (
            intermediate[x, y, c],
            intermediate[x, y, c] + g.int_arg,
        )

        # This should be compiled into the Generator product itself,
        # and not produce another input for the Stub or AOT filter.
        static_compiled_buffer = hl.Buffer(hl.UInt(8), [4, 4, 1])
        for xx in range(4):
            for yy in range(4):
                for cc in range(1):
                    static_compiled_buffer[xx, yy, cc] = xx + yy + cc + 42

        g.static_compiled_buffer_output[x, y, c] = static_compiled_buffer[x, y, c]

        extra_input = getattr(g, g.extra_input_name, None)
        if extra_input:
            g.extra_output[x, y] = hl.f64(extra_input[x, y, 0] + 1)
        else:
            g.extra_output[x, y] = hl.f64(0)

        g.scalar_output[()] = g.float_arg + g.int_arg

        (
            intermediate.compute_at(g.tuple_output, y)
            .specialize(g.vectorize)
            .vectorize(x, g.natural_vector_size(hl.Float(32)))
        )


if __name__ == "__main__":
    hl.main()
