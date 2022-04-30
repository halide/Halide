import halide as hl

x = hl.Var('x')
y = hl.Var('y')
c = hl.Var('c')

@hl.generator(name = "complexpy")
class ComplexPy:
    def GeneratorParams(self) -> type:
        class MyGeneratorParams:
            vectorize = True

        return MyGeneratorParams

    def Inputs(self, gp:GeneratorParams) -> type:
        class MyInputs:
            typed_buffer_input = hl.InputBuffer(hl.UInt(8), 3)
            untyped_buffer_input = hl.InputBuffer(None, 3)
            simple_input = hl.InputBuffer(None, 3)
            float_arg = hl.InputScalar(hl.Float(32))
            int_arg = hl.InputScalar(hl.Int(32))
            extra_input = hl.InputBuffer(hl.UInt(16), 3)

        return MyInputs

    def Outputs(self, gp:GeneratorParams) -> type:
        class MyOutputs:
            simple_output = hl.OutputBuffer(hl.Float(32), 3)
            tuple_output = hl.OutputBuffer(None, 3)
            typed_buffer_output = hl.OutputBuffer(hl.Float(32), 3)
            untyped_buffer_output = hl.OutputBuffer(None, 3)
            static_compiled_buffer_output = hl.OutputBuffer(hl.UInt(8), 3)
            scalar_output = hl.OutputScalar(hl.Float(32))
            extra_output = hl.OutputBuffer(hl.Float(64), 2)

        return MyOutputs

    # Just an intermediate Func we need to share between generate() and schedule()
    intermediate = hl.Func()

    def generate(self, gp, i, o):
        o.simple_output[x, y, c] = hl.f32(i.simple_input[x, y, c])
        o.typed_buffer_output[x, y, c] = hl.f32(i.typed_buffer_input[x, y, c])
        o.untyped_buffer_output[x, y, c] = hl.cast(o.untyped_buffer_output.output_type(), i.untyped_buffer_input[x, y, c])

        self.intermediate[x, y, c] = i.simple_input[x, y, c] * i.float_arg

        o.tuple_output[x, y, c] = (self.intermediate[x, y, c], self.intermediate[x, y, c] + i.int_arg)

        # This should be compiled into the Generator product itself,
        # and not produce another input for the Stub or AOT filter.
        static_compiled_buffer = hl.Buffer(hl.UInt(8), [4, 4, 1])
        for xx in range(4):
            for yy in range(4):
                for cc in range(1):
                    static_compiled_buffer[xx, yy, cc] = xx + yy + cc + 42

        o.static_compiled_buffer_output[x, y, c] = static_compiled_buffer[x, y, c]
        o.extra_output[x, y] = hl.f64(i.extra_input[x, y, 0] + 1)

        o.scalar_output[()] = i.float_arg + i.int_arg

    def schedule(self, gp, i, o):
        self.intermediate.compute_at(o.tuple_output, y);
        self.intermediate.specialize(gp.vectorize).vectorize(x, self.natural_vector_size(hl.Float(32)));

if __name__ == "__main__":
    hl.main()

