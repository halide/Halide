#include <FImage.h>
#include <sys/time.h>

using namespace FImage;

int main(int argc, char **argv) {

    Func fib("fib", Int(32));
    Var x;
    fib(x) = select(x > 1, fib(x-1) + fib(x-2), 1);

    Image<int32_t> out = fib.realize(1024);

    for (int i = 2; i < 1024; i++) {
        if (out(i) != out(i-1) + out(i-2)) {
            printf("Failed!\n");
            for (int i = 0; i < 1024; i++) {
                printf("%d\n", out(i));
            }
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}
