/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Trivial test harness for cg_test.ml.
 * Compile together with generated asm:
 *
 *  $ ./cg_test.native --> cg_test.bc 
 *  $ llc cg_test.bc --> cg_test.s
 *  $ gcc cg_test_runner.c cg_test.s -o cg_test.exe --> cg_test.exe
 *  $ ./cg_test.exe
 */
#include <stdio.h>
#include <stdint.h>
#include <assert.h>

extern void _im_main(char* b1, char* b2);

int main(int argc, char* argv[]) {
    uint32_t in = 12345;
    uint32_t out = 11111;

    printf("in: 0x%x, out: 0x%x\n", in, out);

    printf("running...\n");
    _im_main((char*)&in, (char*)&out);

    uint32_t reference = in + (uint32_t)0xDEADBEEF;
    printf("in: 0x%x, out: 0x%x; reference: 0x%x\n", in, out, reference);

    assert(out = reference);
}
