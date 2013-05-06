#define NO_MOD (256*256*256)

#define idx(x,y,yMod)  (((y)%yMod)*(W) + x + programIndex)
static void
horizontal(uniform int W,
           uniform int inYMod, uniform int outYMod,
           uniform const float in[],
           uniform float out[],
           uniform int x, uniform int y)
{
    out[idx(x,y,outYMod)] = in[idx(x-1,y,inYMod)] + in[idx(x,y,inYMod)] + in[idx(x+1,y,inYMod)]; 
}

static void
vertical(uniform int W,
         uniform int inYMod, uniform int outYMod,
         uniform const float in[],
         uniform float out[],
         uniform int x, uniform int y)
{
    out[idx(x,y,outYMod)] = in[idx(x,y-2,inYMod)] + in[idx(x,y-1,inYMod)] + in[idx(x,y,inYMod)]; 
}

export void
unfused_scalar(uniform int W, uniform int H,
             uniform const float input[],
             uniform float tmp[],
             uniform float output[]) {

    for (uniform int y = 4; y < H - 4; y++) {
        for (uniform int x = 4; x < W - 4; x += programCount) {
            horizontal(W, NO_MOD, NO_MOD, input, tmp, x, y);
        }
    }
    for (uniform int y = 4; y < H - 4; y++) {
        for (uniform int x = 4; x < W - 4; x += programCount) {
            vertical(W, NO_MOD, NO_MOD, tmp, output, x, y);
        }
    }
}

export void
fused_scalar(uniform int W, uniform int H,
             uniform const float input[],
             uniform float tmp[],
             uniform float output[]) {

    for (uniform int y = 4; y < H - 4; y++) {
        for (uniform int x = 4; x < W - 4; x += programCount) {
            horizontal(W, NO_MOD, NO_MOD, input, tmp, x, y);
            vertical(W, NO_MOD, NO_MOD, tmp, output, x, y);
        }
    }
}

export void
fused_memory_reuse(uniform int W, uniform int H,
                   uniform const float input[],
                   uniform float tmp[],
                   uniform float output[]) {

    for (uniform int y = 4; y < H - 4; y++) {
        for (uniform int x = 4; x < W - 4; x += programCount) {
            horizontal(W, NO_MOD, 4, input, tmp, x, y);
            vertical(W, 4, NO_MOD, tmp, output, x, y);
        }
    }
}
