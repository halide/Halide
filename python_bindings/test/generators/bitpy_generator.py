import halide as hl

x = hl.Var('x')
y = hl.Var('y')

@hl.generator(name = "bitpy")
class BitGenerator:
    bit_input = hl.InputBuffer(hl.Bool(), 1)
    bit_constant = hl.InputScalar(hl.Bool())

    bit_output = hl.OutputBuffer(hl.Bool(), 1)

    def generate(self):
        g = self
        g.bit_output[x] = g.bit_input[x] | g.bit_constant;

if __name__ == "__main__":
    hl.main()

