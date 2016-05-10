#!/usr/bin/python3

import halide as h

def test_rdom():
    x = h.Var("x")
    y = h.Var("y")

    diagonal = h.Func("diagonal")
    diagonal[x, y] = 1

    domain_width = 10
    domain_height = 10

    r = h.RDom(0, domain_width, 0, domain_height)
    r.where(r.x <= r.y)

    diagonal[r.x, r.y] = 2
    output = diagonal.realize(domain_width, domain_height)
    output = h.Image(h.Int(32), output)

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
