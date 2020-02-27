import halide as hl

def test_atomics():
    x = hl.Var('x')
    im = hl.Func('im')
    f = hl.Func('f')
    im[x] = (x * x) % 5
    r = hl.RDom([(0, 100)])
    f[x] = 0
    f[hl.Expr(im[r])] += 1
    f.compute_root().update().atomic().parallel(r)
    target = hl.get_jit_target_from_environment()
    print("HL_JIT_TARGET is ", target)
    target2 = hl.get_target_from_environment()
    print("HL_TARGET is ", target2)
    b = f.realize(5)
    print("realize done ", target)

    ref = [0, 0, 0, 0, 0]
    for i in range(100):
        idx = (i * i) % 5
        ref[idx] += 1
    for i in range(5):
        assert(b[i] == ref[i])

if __name__ == "__main__":
    test_atomics()
