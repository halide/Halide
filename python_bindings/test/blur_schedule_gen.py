import halide as hl

def main():
    hl.load_plugin("autoschedule_adams2019")

    x, y = hl.Var("x"), hl.Var("y")

    input = hl.Func("input")
    input[x,y] = 42.7
    input_uint16 = hl.Func("input_uint16")

    input_uint16[x,y] = hl.u16(input[x,y])
    ci = input_uint16

    blur_x = hl.Func("blur_x")
    blur_y = hl.Func("blur_y")

    blur_x[x,y] = (ci[x,y]+ci[x+1,y]+ci[x+2,y])/3
    blur_y[x,y] = hl.cast(hl.UInt(8), (blur_x[x,y]+blur_x[x,y+1]+blur_x[x,y+2])/3)

    # schedule
    #xi, yi = hl.Var("xi"), hl.Var("yi")
    #blur_y.tile(x, y, xi, yi, 8, 4).parallel(y).vectorize(xi, 8)
    #blur_x.compute_at(blur_y, x).vectorize(x, 8)

    blur_y.print_loop_nest()
    
    input.set_estimate(x, 0, 1536)
    input.set_estimate(y, 0, 2560)
    blur_x.set_estimate(x, 0, 1536)
    blur_x.set_estimate(y, 0, 2560)
    blur_y.set_estimate(x, 0, 1536)
    blur_y.set_estimate(y, 0, 2560)

    p = hl.Pipeline(blur_y)
    target = hl.Target('x86-64-linux-no_runtime')
    # Only first parameter is used (number of cores on CPU)
    params = hl.MachineParams(32, 16777216, 40);
    result = p.auto_schedule('Adams2019', target, params)
    
    print("Loop nest!")
    blur_y.print_loop_nest()

    print('Schedule:')
    print(result.schedule_source)
    print('Python Schedule:')
    print(result.python_schedule_source)

    # applying the schedule
    #print("Applying Python Schedule...")
    #p.apply_python_schedule(target)

    print("JIT Compiling...")
    p.compile_jit() # compile
    buf = p.realize(1536, 2560) # compute and get the buffer
    print(buf)
    print('Done!')

if __name__ == '__main__':
    main()
