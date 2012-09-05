Summary
--------
What you probably want as a user of Halide is one of our [binary releases](http://github.com/halide/Halide/downloads).

Once you have that, check out [Getting Started](https://github.com/halide/Halide/wiki/Getting-Started).

Using Halide just requires including a single header and linking against a self-contained static library which includes our entire compiler. Given that, you can JIT or statically compile Halide programs without needing anything else.

Building the Halide compiler from source currently requires some occasionally hairy external dependencies. We're working on streamlining the process for major platforms (Mac & Ubuntu now, Windows to follow) as we speak. You're still very welcome to spelunk through the code, but again, you can do anything short of modify the compiler with one of the [binary releases](http://github.com/halide/Halide/downloads).

Be aware that this is very much a research compiler. There are plenty of rough edges, and a simple syntax error in one place might cause a segfault somewhere else. We're working hard to make it more user-friendly, and if you do have trouble please feel free to raise it on our [bug tracker](http://github.com/halide/Halide/issues), or on the [halide-dev list](https://lists.csail.mit.edu/mailman/listinfo/halide-dev).


Getting Started
----------------
### Getting Halide

Right now we support OS X >=10.7 and 64-bit Ubuntu Linux. Download the appropriate package below:

For Linux: https://github.com/downloads/halide/Halide/halide-20120831-linux.tgz  
For Mac: https://github.com/downloads/halide/Halide/halide-20120831-osx.tgz

These files contain the halide library (libHalide.a) and a header file (Halide.h). Unpack these in a scratch directory:

    $ tar xvzf halide-linux64.tgz 
    halide/
    halide/libHalide.a
    halide/Halide.h

If you would like to use CUDA (i.e. ptx architecture) on OS X you will need to install at minimum the CUDA toolkit and drivers from http://developer.nvidia.com/cuda/cuda-downloads. On Ubuntu you just need the default ubuntu nvidia drivers. There's no need to install anything from nvidia's site.

### Using Halide

Next we'll write a minimal program that uses Halide. Open an editor and enter the following code:

```cpp
#include "halide/Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    
    // Define a halide function representing a linear ramp
    Func f;
    Var x;
    
    f(x) = x*2;
    
    // JIT-compile and run the halide pipeline, evaluating f over the range [0, 9]
    Image<int> im = f.realize(10);
    
    // Check we got the output we expected
    for (int i = 0; i < 10; i++) {
        if (im(i) != i*2) {
            printf("Uh oh!, im(%d) should have been %d but instead it's %d\n",
                   i, i*2, im(i));
            return -1;
        }
    }
    
    printf("Success!\n");
    
    return 0;
}
```

Save it as `hello_halide.cpp`. This defines a very simple Halide pipeline, JIT compiles it, and runs it. To compile this:

    $ g++-4.6 -std=c++0x hello_halide.cpp -L halide -lHalide -ldl -lpthread -o hello_halide

Now run `./hello_halide`. If it prints "success", you're up and running!. 

To run this with CUDA, run `HL_TARGET=ptx ./hello_halide`. If halide has trouble finding libcuda, you can manually link it in by adding `-L /usr/local/cuda/lib -lcuda` or similar to the compilation command. If it prints "success" (as well as a bunch of JITed ptx assembly), you're up and running on the GPU!

### Debugging Halide code

Let's see what Halide's doing under the hood. Halide tracing and debugging is controlled using environment variables. Trying running: 

    $ HL_TRACE=1 ./hello_halide
    Time 0
    Realizing f0 over 0 10  
    Success!

Our function f was given the internal name "f0", and we realized it over the range [0, 10). We can give our function a more useful name by providing a name as the sole argument to the Func constructor (i.e. `Func f("my_function")` instead of `Func f`). Let's turn up the tracing level by one:

    $ HL_TRACE=2 ./hello_halide
    Time 0
    Realizing f0 over 0 10
    Storing f0 at 0 0
    Storing f0 at 1 2
    Storing f0 at 2 4
    Storing f0 at 3 6
    Storing f0 at 4 8
    Storing f0 at 5 10
    Storing f0 at 6 12
    Storing f0 at 7 14
    Storing f0 at 8 16
    Storing f0 at 9 18
    Success!

At tracing level 2 we also print every time we evaluate a halide function. The line `Storing f0 at 6 12` means that we evaluated f0 at location 6 and the result was 12. For less trivial pipelines you'll also see lines about loading from other functions, and when you use reductions, lines about initializing and updating certain sites in functions. 

### Where do I go from here

For many more simple examples, look through our tests folder at https://github.com/halide/Halide/tree/master/test/cpp. Good ones to look at at first are bounds, chunk, logical, fibonacci and convolution. These all use Halide as JIT compiler. In the apps folder: https://github.com/halide/Halide/tree/master/apps are a range of more complex image processing routines that use Halide as a static compiler. Check out wavelet, or local_laplacian.

### Compiling the Halide compiler

This is currently a bit challenging because of dependencies. Instructions are coming very soon, and we're working on automating it to make it much easier.

Resources
----------
- Check out [our first paper](http://people.csail.mit.edu/jrk/halide12).
- Subscribe to [halide-announce](https://lists.csail.mit.edu/mailman/listinfo/halide-announce) to hear about releases.
- Subscribe to [halide-dev](https://lists.csail.mit.edu/mailman/listinfo/halide-dev) to discuss technical issues.
