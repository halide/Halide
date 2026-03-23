#include "Halide.h"
#include <stdio.h>

using namespace Halide;

Expr u8(Expr a) {
    return cast<uint8_t>(a);
}

/* Do n unrolled iterations of game of life on a torus */
Func gameOfLife(ImageParam input, int n) {
    Var x, y;
    Func in;
    if (n == 1) {
        in(x, y) = input(x, y);
    } else {
        in = gameOfLife(input, n - 1);
        in.compute_root();
    }

    Expr w = input.width(), h = input.height();
    Expr W = (x + w - 1) % w, E = (x + 1) % w, N = (y + h - 1) % h, S = (y + 1) % h;
    Expr livingNeighbors = (in(W, N) + in(x, N) +
                            in(E, N) + in(W, y) +
                            in(E, y) + in(W, S) +
                            in(x, S) + in(E, S));
    Expr alive = in(x, y) != 0;
    Func output;
    output(x, y) = select(livingNeighbors == 3 || (alive && livingNeighbors == 2), u8(1), u8(0));

    return output;
}

int main(int argc, char **argv) {

    Buffer<uint8_t> board1(32, 32), board2(32, 32), board3(32, 32);

    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            uint8_t val = ((rand() & 0xff) < 128) ? 1 : 0;
            board1(x, y) = val;
            board2(x, y) = val;
            board3(x, y) = val;
        }
    }

    ImageParam input(UInt(8), 2);

    {
        // Outer loop in C

        Func oneIteration = gameOfLife(input, 1);
        Func twoIterations = gameOfLife(input, 2);

        for (int i = 0; i < 10; i++) {
            input.set(board1);
            board1 = oneIteration.realize({32, 32});
            input.set(board1);
            board1 = oneIteration.realize({32, 32});
            input.set(board2);
            board2 = twoIterations.realize({32, 32});

            /*
            for (int y = 0; y < 32; y++) {
                for (int x = 0; x < 32; x++) {
                    printf(board1(x, y) ? "#" : " ");
                }
                printf("|");
                for (int x = 0; x < 32; x++) {
                    printf(board2(x, y) ? "#" : " ");
                }
                printf("\n");
            }
            */

            for (int y = 0; y < 32; y++) {
                for (int x = 0; x < 32; x++) {
                    if (board1(x, y) != board2(x, y)) {
                        printf("At timestep %d, boards one and two disagree at %d, %d: %d vs %d\n",
                               i, x, y, board1(x, y), board2(x, y));
                        return 1;
                    }
                }
            }
        }
    }

    {
        // Outer loop in Halide using a reduction
        Func life;

        // Initialize step
        Var x, y, z;
        life(x, y, z) = input(x, y);

        // Update step
        Expr w = input.width(), h = input.height();
        RDom t(0, w, 0, h, 0, 21);
        Expr lastT = (t.z + 1) % 2;
        Expr W = (t.x + w - 1) % w, E = (t.x + 1) % w, N = (t.y + h - 1) % h, S = (t.y + 1) % h;
        Expr alive = life(t.x, t.y, lastT) != u8(0);
        Expr livingNeighbors = (life(W, N, lastT) + life(t.x, N, lastT) +
                                life(E, N, lastT) + life(W, t.y, lastT) +
                                life(E, t.y, lastT) + life(W, S, lastT) +
                                life(t.x, S, lastT) + life(E, S, lastT));
        life(t.x, t.y, t.z % 2) = select(livingNeighbors == 3 || (alive && livingNeighbors == 2), u8(1), u8(0));
        life.compute_root();

        Func output;
        output(x, y) = life(x, y, 1);

        input.set(board3);
        output.realize(board3);

        /*
        for (int y = 0; y < 32; y++) {
            for (int x = 0; x < 32; x++) {
                printf(board1(x, y) ? "#" : " ");
            }
            printf("|");
            for (int x = 0; x < 32; x++) {
                printf(board3(x, y) ? "#" : " ");
            }
            printf("\n");
        }
        */

        for (int y = 0; y < 32; y++) {
            for (int x = 0; x < 32; x++) {
                if (board1(x, y) != board3(x, y)) {
                    printf("Boards one and three disagree at %d, %d: %d vs %d\n",
                           x, y, board1(x, y), board3(x, y));
                    return 1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
