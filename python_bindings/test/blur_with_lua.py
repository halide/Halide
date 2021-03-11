import halide as hl

def main():
    hl.load_plugin("autoschedule_adams2019")

    x, y = hl.Var("x"), hl.Var("y")

    input = hl.Func("input")
    input[x,y] = hl.f32(42.7)
    input_uint16 = hl.Func("input_uint16")

    input_uint16[x,y] = hl.u16(input[x,y])
    ci = input_uint16

    blur_x = hl.Func("blur_x")
    blur_y = hl.Func("blur_y")

    blur_x[x,y] = (ci[x,y]+ci[x+1,y]+ci[x+2,y])/3
    blur_y[x,y] = hl.cast(hl.UInt(8), (blur_x[x,y]+blur_x[x,y+1]+blur_x[x,y+2])/3)

    p = hl.Pipeline(blur_y)
    target = hl.Target('x86-64-linux-no_runtime')

    print("Loop nest!")
    blur_y.print_loop_nest()

    # applying the schedule
    print("Applying Lua Schedule...")
    p.apply_lua_schedule(target)
    print("Loop nest!")
    blur_y.print_loop_nest()

    print("JIT Compiling...")
    p.compile_jit() # compile
    buf = p.realize(1536, 2560) # compute and get the buffer
    print(buf)
    print('Done!')

if __name__ == '__main__':
    main()
