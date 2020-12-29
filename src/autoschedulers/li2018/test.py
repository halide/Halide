import halide as hl

def main():
    hl.load_plugin("autoschedule_li2018")

    x = hl.Var('x')
    f_in = hl.Func('in')
    f_in[x] = hl.f32(x) # Cast to float 32
    f_0 = hl.Func('f_0')
    f_0[x] = 2 * f_in[x]
    f_1 = hl.Func('f_1')
    f_1[x] = hl.sin(f_0[x])
    f_2 = hl.Func('f_2')
    f_2[x] = f_1[x] * f_1[x]

    # Setup
    f_2.set_estimate(x, 0, 1000)
    p = hl.Pipeline(f_2)
    target = hl.Target()
    # Only first parameter is used (number of cores on CPU)
    params = hl.MachineParams(32, 0, 0);
    result = p.auto_schedule('Li2018', target, params)
    print('Schedule:')
    print(result.schedule_source)

    p.compile_jit() # compile
    buf = p.realize(1000) # compute and get the buffer

if __name__ == '__main__':
    main()
