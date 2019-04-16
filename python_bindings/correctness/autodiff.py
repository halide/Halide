import halide as hl

def test_autodiff():
	x = hl.Var('x')
	b = hl.Buffer(hl.Float(32), [3])
	b[0] = 1.0
	b[1] = 2.0
	b[2] = 3.0
	f, g, h = hl.Func('f'), hl.Func('g'), hl.Func('h')
	f[x] = b[x]
	g[x] = f[x] * 5.0
	r = hl.RDom([(0, 3)])
	h[()] = 0.0
	h[()] += g[r.x]

	d = hl.propagate_adjoints(h)
	d_f = d[f]
	d_f_buf = d_f.realize(3)
	assert(d_f_buf[0] == 5.0)
	assert(d_f_buf[1] == 5.0)
	assert(d_f_buf[2] == 5.0)
	d_b = d[b]
	d_b_buf = d_b.realize(3)
	assert(d_b_buf[0] == 5.0)
	assert(d_b_buf[1] == 5.0)
	assert(d_b_buf[2] == 5.0)

if __name__ == "__main__":
	test_autodiff()