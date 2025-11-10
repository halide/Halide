#include "Halide.h"
#include <stdio.h>
#include <string.h>

using namespace Halide;

int main(int argc, char **argv) {
    // This test only runs on macOS with Metal support
    Target t = get_jit_target_from_environment();
    
    if (t.os != Target::OSX || !t.has_feature(Target::Metal)) {
        printf("[SKIP] This test only runs on macOS with Metal support\n");
        return 0;
    }

    // Test 1: Verify default behavior (source code embedded) works
    {
        printf("Test 1: Default behavior (source code)...\n");
        
        Func f;
        Var x, y, xi, yi;
        f(x, y) = x + y;
        f.gpu_tile(x, y, xi, yi, 8, 8);
        
        Buffer<int> result = f.realize({32, 32}, t);
        
        // Verify correctness
        for (int i = 0; i < 32; i++) {
            for (int j = 0; j < 32; j++) {
                int expected = i + j;
                if (result(i, j) != expected) {
                    printf("  ERROR: result(%d, %d) = %d, expected %d\n", 
                           i, j, result(i, j), expected);
                    return 1;
                }
            }
        }
        
        printf("  Default compilation succeeded\n");
    }
    
    // Test 2: Set Metal compiler and linker, verify runtime behavior
    {
        printf("Test 2: With Metal compiler and linker set...\n");
        
        // Set the Metal compiler and linker
        set_metal_compiler_and_linker("xcrun -sdk macosx metal", "xcrun -sdk macosx metallib");
        
        Func g;
        Var x, y, xi, yi;
        g(x, y) = x * y + 42;
        g.gpu_tile(x, y, xi, yi, 8, 8);
        
        Buffer<int> result = g.realize({32, 32}, t);
        
        // Verify correctness
        for (int i = 0; i < 32; i++) {
            for (int j = 0; j < 32; j++) {
                int expected = i * j + 42;
                if (result(i, j) != expected) {
                    printf("  ERROR: result(%d, %d) = %d, expected %d\n", 
                           i, j, result(i, j), expected);
                    return 1;
                }
            }
        }
        
        printf("  Compilation with Metal tools succeeded\n");
    }
       
    // Test 3: Set only compiler (incomplete - should fall back to source)
    {
        printf("Test 3: Incomplete configuration (compiler only)...\n");
        
        set_metal_compiler_and_linker("xcrun -sdk macosx metal", "");
        
        Func m;
        Var x, y, xi, yi;
        m(x, y) = x + y * 2;
        m.gpu_tile(x, y, xi, yi, 8, 8);
        
        Buffer<int> result = m.realize({32, 32}, t);
        
        // Verify correctness
        for (int i = 0; i < 32; i++) {
            for (int j = 0; j < 32; j++) {
                int expected = i + j * 2;
                if (result(i, j) != expected) {
                    printf("  ERROR: result(%d, %d) = %d, expected %d\n", 
                           i, j, result(i, j), expected);
                    return 1;
                }
            }
        }
        
        printf("  Compilation with incomplete config succeeded (expected fallback to source)\n");
    }
    
    printf("Success!\n");
    return 0;
}
