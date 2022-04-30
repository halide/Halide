import halide as hl

x = hl.Var('x')
y = hl.Var('y')

@hl.generator(name = "bitpy")
class BitGenerator:
    class Inputs:
        bit_input = hl.InputBuffer(hl.Bool(), 1)
        bit_constant = hl.InputScalar(hl.Bool())

    class Outputs:
        bit_output = hl.OutputBuffer(hl.Bool(), 1)

    def generate(self, gp, i, o):
        o.bit_output[x] = i.bit_input[x] + i.bit_constant;

if __name__ == "__main__":
    hl.main()

