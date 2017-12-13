import halide

def test_rdom():
    x = halide.Var("x")
    y = halide.Var("y")

    diagonal = halide.Func("diagonal")
    diagonal[x, y] = 1

    domain_width = 10
    domain_height = 10

    r = halide.RDom(0, domain_width, 0, domain_height)
    r.where(r.x <= r.y)

    diagonal[r.x, r.y] = 2
    output = diagonal.realize(domain_width, domain_height)
    
    for iy in range(domain_height):
        for ix in range(domain_width):
            if ix <= iy:
                assert output(ix, iy) == 2
            else:
                assert output(ix, iy) == 1

    print("Success!")
    return 0

if __name__ == "__main__":
    test_rdom()
