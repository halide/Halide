import halide as hl

def main():
    hl.load_plugin("autoschedule_adams2019")

    x, y = hl.Var("x"), hl.Var("y")
                                                                                 
    producer, consumer = hl.Func("producer"), hl.Func("consumer")                       
                                                                                 
      # The first stage will be some simple pointwise math similar               
      # to our familiar gradient function. The value at position x,              
      # y is the sqrt of product of x and y.                                     
    producer[x, y] = hl.sqrt(x * y)                                            
                                                                                 
      # Now we'll add a second stage which adds together multiple                
      # points in the first stage.                                               
    consumer[x, y] = (producer[x, y] +                                         
                      producer[x, y + 1] +                                     
                      producer[x + 1, y] +                                     
                      producer[x + 1, y + 1])                                  
                                                                                 
      # We'll turn on tracing for both functions.                                
      # consumer.trace_stores()                                                    
      # producer.trace_stores()                                                    
                                                                                 
    consumer.print_loop_nest()
    
    producer.set_estimate(x, 0, 1536)
    producer.set_estimate(y, 0, 2560)
    consumer.set_estimate(x, 0, 1536)
    consumer.set_estimate(y, 0, 2560)

    p = hl.Pipeline(consumer)
    target = hl.Target('x86-64-linux-no_runtime')
    # Only first parameter is used (number of cores on CPU)
    params = hl.MachineParams(32, 16777216, 40);
    result = p.auto_schedule('Adams2019', target, params)
    
    print("Loop nest!")
    consumer.print_loop_nest()

    print('Schedule:')
    print(result.schedule_source)
    print('Lua Schedule:')
    print(result.lua_schedule_source)
    print('Python Schedule:')
    print(result.python_schedule_source)

    print("JIT Compiling...")
    p.compile_jit() # compile
    buf = p.realize(1536, 2560) # compute and get the buffer
    print(buf)
    print('Done!')

if __name__ == '__main__':
    main()
