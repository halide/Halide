import halide as hl

x = hl.Var('x')
y = hl.Var('y')

@hl.generator("bitpy")
class BitGenerator:
    bit_input = hl.InputBuffer(hl.Bool(), 1)
    bit_constant = hl.InputScalar(hl.Bool())
    bit_output = hl.OutputBuffer(hl.Bool(), 1)

    def generate(self):
        self.bit_output[x] = self.bit_input[x] + self.bit_constant;

if __name__ == "__main__":
    hl.main()

