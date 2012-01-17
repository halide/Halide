#include "Halide.h"

using namespace Halide;

Expr u8(Expr a) {
    return cast<uint8_t>(a);
}

/* Do n unrolled iterations of game of life on a torus */
Func gameOfLife(UniformImage input, int n) {
    Var x, y;
    Func in;
    if (n == 1) {
        in(x, y) = input(x, y);
    } else {
        in = gameOfLife(input, n-1);
        in.root();
    }

    Expr w = input.width(), h = input.height();
    Expr W = (x+w-1) % w, E = (x+1) % w, N = (y+h-1) % h, S = (y+1) % h;
    Expr livingNeighbors = (in(W, N) + in(x, N) +
                            in(E, N) + in(W, y) + 
                            in(E, y) + in(W, S) +
                            in(x, S) + in(E, S));    
    Expr alive = in(x, y) != u8(0);
    Func output;
    output(x, y) = select(livingNeighbors == u8(3) || (alive && livingNeighbors == u8(2)), u8(1), u8(0));    

    return output;
}

int main(int argc, char **argv) {

    Image<uint8_t> board1(32, 32), board2(32, 32), board3(32, 32);
    
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            uint8_t val = ((rand() & 0xff) < 128) ? 1 : 0;
            board1(x, y) = val;
            board2(x, y) = val;
            board3(x, y) = val;
        }
    }
    
    UniformImage input(UInt(8), 2);

    {
        // Outer loop in C

        Func oneIteration = gameOfLife(input, 1);
        Func twoIterations = gameOfLife(input, 2);
        oneIteration.compileJIT();
        twoIterations.compileJIT();
        
        for (int i = 0; i < 10; i++) {
            input = board1;
            board1 = oneIteration.realize(32, 32);
            input = board1;
            board1 = oneIteration.realize(32, 32);
            input = board2;
            board2 = twoIterations.realize(32, 32);
            
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
            
            for (int y = 0; y < 32; y++) {
                for (int x = 0; x < 32; x++) {
                    if (board1(x, y) != board2(x, y)) {
                        printf("At timestep %d, boards one and two disagree at %d, %d: %d vs %d\n", 
                               i, x, y, board1(x, y), board2(x, y));
                        return -1;
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
        RVar t(0, 21);
        Expr lastT = (t+1)%2;
        Expr w = input.width(), h = input.height();
        Expr W = (x+w-1) % w, E = (x+1) % w, N = (y+h-1) % h, S = (y+1) % h;
        Expr alive = life(x, y, lastT) != u8(0);
        Expr livingNeighbors = (life(W, N, lastT) + life(x, N, lastT) +
                                life(E, N, lastT) + life(W, y, lastT) + 
                                life(E, y, lastT) + life(W, S, lastT) +
                                life(x, S, lastT) + life(E, S, lastT));            
        life(x, y, t%2) = select(livingNeighbors == u8(3) || (alive && livingNeighbors == u8(2)), u8(1), u8(0));    
        
        Func output;
        output(x, y) = life(x, y, 1);        

        // The update step of life needs to have t outermost to be
        // correct. Schedules can change meaning for reductions!  (but
        // only reductions). This is why we say Halide is really only
        // for feed-forward pipelines.
        life.update().transpose(t, y);

        input = board3;
        output.realize(board3);

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
        
        for (int y = 0; y < 32; y++) {
            for (int x = 0; x < 32; x++) {
                if (board1(x, y) != board3(x, y)) {
                    printf("Boards one and three disagree at %d, %d: %d vs %d\n", 
                           x, y, board1(x, y), board2(x, y));
                    return -1;
                }
            }
        }        
    }

    printf("Success!");
    return 0;

}
