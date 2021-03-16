#!/usr/bin/python3

# Halide tutorial lesson 4

# This lesson demonstrates how to follow what Halide is doing at runtime.

# This lesson can be built by invoking the command:
#    make test_tutorial_lesson_04_debugging_2
# in a shell with the current directory at python_bindings/

import halide as hl


def main():
    hl.load_plugin("autoschedule_adams2019")

    gradient = hl.Func("gradient")
    x, y = hl.Var("x"), hl.Var("y")

    # We'll define our gradient function as before.
    gradient[x, y] = x + y

    print("Evaluating gradient")
    gradient.set_estimate(x, 0, 1000)
    gradient.set_estimate(y, 0, 1000)
    p = hl.Pipeline(gradient)
    target = hl.Target('x86-64-linux-no_runtime')
    # Only first parameter is used (number of cores on CPU)
    params = hl.MachineParams(32, 16777216, 40);
    result = p.auto_schedule('Adams2019', target, params)
    print("Loop nest!")
    gradient.print_loop_nest()

    print('Schedule:')
    print(result.schedule_source)
    print('Lua Schedule:')
    print(result.lua_schedule_source)

    # applying the schedule
    #print("Applying Python Schedule...")
    #p.apply_python_schedule(target)

    print("JIT Compiling...")
    p.compile_jit() # compile
    output = gradient.realize(1000, 1000)
    print(output)
    print("Success!")
    return 0


if __name__ == "__main__":
    main()

