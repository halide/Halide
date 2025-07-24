import halide as hl
import numpy as np


def test_rdom():
    x = hl.Var("x")
    y = hl.Var("y")

    diagonal = hl.Func("diagonal")
    diagonal[x, y] = 1

    domain_width = 10
    domain_height = 10

    r = hl.RDom([(0, domain_width), (0, domain_height)])
    r.where(r.x <= r.y)

    diagonal[r.x, r.y] += 2
    output = diagonal.realize([domain_width, domain_height])

    for iy in range(domain_height):
        for ix in range(domain_width):
            if ix <= iy:
                assert output[ix, iy] == 3
            else:
                assert output[ix, iy] == 1

    assert r.x.name() == r[0].name()
    assert r.y.name() == r[1].name()
    try:
        r[-1].name()
        raise Exception("underflowing index should raise KeyError")
    except KeyError:
        pass
    try:
        r[2].name()
        raise Exception("overflowing index should raise KeyError")
    except KeyError:
        pass
    try:
        r["foo"].name()
        raise Exception("bad index type should raise TypeError")
    except TypeError:
        pass

    return 0


def test_implicit_pure_definition():
    a = np.random.ranf((2, 3)).astype(np.float32)
    expected = a.sum(axis=1)

    ha = hl.Buffer(a, name="ha")
    da_cols = ha.dim(0).extent()

    x = hl.Var("x")
    k = hl.RDom([(0, da_cols)], name="k")

    hc = hl.Func("hc")
    # hc[x] = 0.0 # this is implicit
    hc[x] += ha[k, x]

    result = np.array(hc.realize([2]))
    assert np.allclose(result, expected)


if __name__ == "__main__":
    test_rdom()
    test_implicit_pure_definition()
