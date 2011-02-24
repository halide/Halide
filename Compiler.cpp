#include "Compiler.h"

void Compiler::compile(FImage *im) {

    compilePrologue();

    // Compile a chunk of code that just runs the definitions in order
    for (int i = 0; i < (int)im->definitions.size(); i++) {
        compileDefinition(im, i);
    }
    
    compileEpilogue();
}
