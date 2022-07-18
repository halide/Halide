import halide as hl

x = hl.Var('x')
y = hl.Var('y')

@hl.generator(name = "simplepy")
class SimplePy:
    offset = hl.GeneratorParam(0)

    buffer_input = hl.InputBuffer(hl.UInt(8), 2)
    float_arg = hl.InputScalar(hl.Float(32))

    simple_output = hl.OutputBuffer(hl.Float(32), 2)

    def generate(self):
        g = self

        g.simple_output[x, y] = hl.f32(g.offset + g.buffer_input[x, y]) + g.float_arg
        g.simple_output.compute_root()

if __name__ == "__main__":
    hl.main()

