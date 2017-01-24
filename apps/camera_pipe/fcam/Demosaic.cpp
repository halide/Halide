#include <cmath>
#include <iostream>
#include <algorithm>
#include <map>
#include <vector>
#include <string.h>
// #ifdef FCAM_ARCH_ARM
// #include "Demosaic_ARM.h"
// #endif

#include "Demosaic.h"
// #include <FCam/Sensor.h>
// #include <FCam/Time.h>

namespace FCam {

// Make a linear luminance -> pixel value lookup table
void makeLUT(float contrast, int blackLevel, int whiteLevel, float gamma, unsigned char *lut) {
    unsigned short minRaw = 0 + blackLevel; //f.platform().minRawValue()+blackLevel;
    unsigned short maxRaw = whiteLevel; //f.platform().maxRawValue();

    for (int i = 0; i <= whiteLevel; i++) {
        lut[i] = 0;
    }

    float invRange = 1.0f/(maxRaw - minRaw);
    float b = 2 - powf(2.0f, contrast/100.0f);
    float a = 2 - 2*b;
    for (int i = minRaw+1; i <= maxRaw; i++) {
        // Get a linear luminance in the range 0-1
        float y = (i-minRaw)*invRange;
        // Gamma correct it
        y = powf(y, 1.0f/gamma);
        // Apply a piecewise quadratic contrast curve
        if (y > 0.5) {
            y = 1-y;
            y = a*y*y + b*y;
            y = 1-y;
        } else {
            y = a*y*y + b*y;
        }
        // Convert to 8 bit and save
        y = std::floor(y * 255 + 0.5f);
        if (y < 0) { y = 0; }
        if (y > 255) { y = 255; }
        lut[i] = (unsigned char)y;
    }
}

// From the Halide camera_pipe's color_correct
void makeColorMatrix(float colorMatrix[], float colorTemp) {
    float alpha = (1.0 / colorTemp - 1.0/3200) / (1.0/7000 - 1.0/3200);

    colorMatrix[0] = alpha*1.6697f     + (1-alpha)*2.2997f;
    colorMatrix[1] = alpha*-0.2693f    + (1-alpha)*-0.4478f;
    colorMatrix[2] = alpha*-0.4004f    + (1-alpha)*0.1706f;
    colorMatrix[3] = alpha*-42.4346f   + (1-alpha)*-39.0923f;

    colorMatrix[4] = alpha*-0.3576f    + (1-alpha)*-0.3826f;
    colorMatrix[5] = alpha*1.0615f     + (1-alpha)*1.5906f;
    colorMatrix[6] = alpha*1.5949f     + (1-alpha)*-0.2080f;
    colorMatrix[7] = alpha*-37.1158f   + (1-alpha)*-25.4311f;

    colorMatrix[8] = alpha*-0.2175f    + (1-alpha)*-0.0888f;
    colorMatrix[9] = alpha*-1.8751f    + (1-alpha)*-0.7344f;
    colorMatrix[10]= alpha*6.9640f     + (1-alpha)*2.2832f;
    colorMatrix[11]= alpha*-26.6970f   + (1-alpha)*-20.0826f;
}

// Some functions used by demosaic
inline short max(short a, short b) {return a>b ? a : b;}
inline short max(short a, short b, short c, short d) {return max(max(a, b), max(c, d));}
inline short min(short a, short b) {return a<b ? a : b;}

void demosaic(Halide::Runtime::Buffer<uint16_t> input, Halide::Runtime::Buffer<uint8_t> out, float colorTemp, float contrast, bool denoise, int blackLevel, int whiteLevel, float gamma) {
    const int BLOCK_WIDTH = 40;
    const int BLOCK_HEIGHT = 24;
    const int G = 0, GR = 0, R = 1, B = 2, GB = 3;

    int rawWidth = input.width();
    int rawHeight = input.height();
    int outWidth = rawWidth-32;
    int outHeight = rawHeight-48;
    outWidth = min(outWidth, out.width());
    outHeight = min(outHeight, out.height());
    outWidth /= BLOCK_WIDTH;
    outWidth *= BLOCK_WIDTH;
    outHeight /= BLOCK_HEIGHT;
    outHeight *= BLOCK_HEIGHT;

    // Prepare the lookup table
    unsigned char lut[whiteLevel+1];
    makeLUT(contrast, blackLevel, whiteLevel, gamma, lut);

    // Grab the color matrix
    float colorMatrix[12];
    makeColorMatrix(colorMatrix, colorTemp);

    //#pragma omp parallel for
    for (int by = 0; by < outHeight; by += BLOCK_HEIGHT) {
        for (int bx = 0; bx < outWidth; bx += BLOCK_WIDTH) {
            /*
              Stage 1: Load a block of input, treat it as 4-channel gr, r, b, gb
            */
            short inBlock[4][BLOCK_HEIGHT/2+4][BLOCK_WIDTH/2+4];

            for (int y = 0; y < BLOCK_HEIGHT/2+4; y++) {
                for (int x = 0; x < BLOCK_WIDTH/2+4; x++) {
                    inBlock[GR][y][x] = input(bx + 2*x,   by + 2*y);
                    inBlock[R][y][x] =  input(bx + 2*x+1, by + 2*y);
                    inBlock[B][y][x] =  input(bx + 2*x,   by + 2*y+1);
                    inBlock[GB][y][x] = input(bx + 2*x+1, by + 2*y+1);
                }
            }

            // linear luminance outputs
            short linear[3][4][BLOCK_HEIGHT/2+4][BLOCK_WIDTH/2+4];

            /*

            Stage 1.5: Suppress hot pixels

            gr[HERE] = min(gr[HERE], max(gr[UP], gr[LEFT], gr[RIGHT], gr[DOWN]));
            r[HERE]  = min(r[HERE], max(r[UP], r[LEFT], r[RIGHT], r[DOWN]));
            b[HERE]  = min(b[HERE], max(b[UP], b[LEFT], b[RIGHT], b[DOWN]));
            gb[HERE] = min(gb[HERE], max(gb[UP], gb[LEFT], gb[RIGHT], gb[DOWN]));

            */

            if (denoise) {
                for (int y = 1; y < BLOCK_HEIGHT/2+3; y++) {
                    for (int x = 1; x < BLOCK_WIDTH/2+3; x++) {
                        linear[G][GR][y][x] = min(inBlock[GR][y][x],
                                                  max(inBlock[GR][y-1][x],
                                                      inBlock[GR][y+1][x],
                                                      inBlock[GR][y][x+1],
                                                      inBlock[GR][y][x-1]));
                        linear[R][R][y][x] = min(inBlock[R][y][x],
                                                 max(inBlock[R][y-1][x],
                                                     inBlock[R][y+1][x],
                                                     inBlock[R][y][x+1],
                                                     inBlock[R][y][x-1]));
                        linear[B][B][y][x] = min(inBlock[B][y][x],
                                                 max(inBlock[B][y-1][x],
                                                     inBlock[B][y+1][x],
                                                     inBlock[B][y][x+1],
                                                     inBlock[B][y][x-1]));
                        linear[G][GB][y][x] = min(inBlock[GB][y][x],
                                                  max(inBlock[GB][y-1][x],
                                                      inBlock[GB][y+1][x],
                                                      inBlock[GB][y][x+1],
                                                      inBlock[GB][y][x-1]));
                    }
                }
            } else {
                for (int y = 1; y < BLOCK_HEIGHT/2+3; y++) {
                    for (int x = 1; x < BLOCK_WIDTH/2+3; x++) {
                        linear[G][GR][y][x] = inBlock[GR][y][x];
                        linear[R][R][y][x] = inBlock[R][y][x];
                        linear[B][B][y][x] = inBlock[B][y][x];
                        linear[G][GB][y][x] = inBlock[GB][y][x];
                    }
                }
            }


            /*
              2: Interpolate g at r

              gv_r = (gb[UP] + gb[HERE])/2;
              gvd_r = |gb[UP] - gb[HERE]|;

              gh_r = (gr[HERE] + gr[RIGHT])/2;
              ghd_r = |gr[HERE] - gr[RIGHT]|;

              g_r = ghd_r < gvd_r ? gh_r : gv_r;

              3: Interpolate g at b

              gv_b = (gr[DOWN] + gr[HERE])/2;
              gvd_b = |gr[DOWN] - gr[HERE]|;

              gh_b = (gb[LEFT] + gb[HERE])/2;
              ghd_b = |gb[LEFT] - gb[HERE]|;

              g_b = ghd_b < gvd_b ? gh_b : gv_b;

            */

            for (int y = 1; y < BLOCK_HEIGHT/2+3; y++) {
                for (int x = 1; x < BLOCK_WIDTH/2+3; x++) {
                    short gv_r = (linear[G][GB][y-1][x] + linear[G][GB][y][x])/2;
                    short gvd_r = abs(linear[G][GB][y-1][x] - linear[G][GB][y][x]);
                    short gh_r = (linear[G][GR][y][x] + linear[G][GR][y][x+1])/2;
                    short ghd_r = abs(linear[G][GR][y][x] - linear[G][GR][y][x+1]);
                    linear[G][R][y][x] = ghd_r < gvd_r ? gh_r : gv_r;

                    short gv_b = (linear[G][GR][y+1][x] + linear[G][GR][y][x])/2;
                    short gvd_b = abs(linear[G][GR][y+1][x] - linear[G][GR][y][x]);
                    short gh_b = (linear[G][GB][y][x] + linear[G][GB][y][x-1])/2;
                    short ghd_b = abs(linear[G][GB][y][x] - linear[G][GB][y][x-1]);
                    linear[G][B][y][x] = ghd_b < gvd_b ? gh_b : gv_b;
                }
            }

            /*
              4: Interpolate r at gr

              r_gr = (r[LEFT] + r[HERE])/2 + gr[HERE] - (g_r[LEFT] + g_r[HERE])/2;

              5: Interpolate b at gr

              b_gr = (b[UP] + b[HERE])/2 + gr[HERE] - (g_b[UP] + g_b[HERE])/2;

              6: Interpolate r at gb

              r_gb = (r[HERE] + r[DOWN])/2 + gb[HERE] - (g_r[HERE] + g_r[DOWN])/2;

              7: Interpolate b at gb

              b_gb = (b[HERE] + b[RIGHT])/2 + gb[HERE] - (g_b[HERE] + g_b[RIGHT])/2;
            */
            for (int y = 1; y < BLOCK_HEIGHT/2+3; y++) {
                for (int x = 1; x < BLOCK_WIDTH/2+3; x++) {
                    linear[R][GR][y][x] = ((linear[R][R][y][x-1] + linear[R][R][y][x])/2 +
                                           linear[G][GR][y][x] -
                                           (linear[G][R][y][x-1] + linear[G][R][y][x])/2);

                    linear[B][GR][y][x] = ((linear[B][B][y-1][x] + linear[B][B][y][x])/2 +
                                           linear[G][GR][y][x] -
                                           (linear[G][B][y-1][x] + linear[G][B][y][x])/2);

                    linear[R][GB][y][x] = ((linear[R][R][y][x] + linear[R][R][y+1][x])/2 +
                                           linear[G][GB][y][x] -
                                           (linear[G][R][y][x] + linear[G][R][y+1][x])/2);

                    linear[B][GB][y][x] = ((linear[B][B][y][x] + linear[B][B][y][x+1])/2 +
                                           linear[G][GB][y][x] -
                                           (linear[G][B][y][x] + linear[G][B][y][x+1])/2);

                }
            }


            /*

            8: Interpolate r at b

            rp_b = (r[DOWNLEFT] + r[HERE])/2 + g_b[HERE] - (g_r[DOWNLEFT] + g_r[HERE])/2;
            rn_b = (r[LEFT] + r[DOWN])/2 + g_b[HERE] - (g_r[LEFT] + g_r[DOWN])/2;
            rpd_b = (r[DOWNLEFT] - r[HERE]);
            rnd_b = (r[LEFT] - r[DOWN]);

            r_b = rpd_b < rnd_b ? rp_b : rn_b;

            9: Interpolate b at r

            bp_r = (b[UPRIGHT] + b[HERE])/2 + g_r[HERE] - (g_b[UPRIGHT] + g_b[HERE])/2;
            bn_r = (b[RIGHT] + b[UP])/2 + g_r[HERE] - (g_b[RIGHT] + g_b[UP])/2;
            bpd_r = |b[UPRIGHT] - b[HERE]|;
            bnd_r = |b[RIGHT] - b[UP]|;

            b_r = bpd_r < bnd_r ? bp_r : bn_r;

            */
            for (int y = 1; y < BLOCK_HEIGHT/2+3; y++) {
                for (int x = 1; x < BLOCK_WIDTH/2+3; x++) {
                    short rp_b = ((linear[R][R][y+1][x-1] + linear[R][R][y][x])/2 +
                                  linear[G][B][y][x] -
                                  (linear[G][R][y+1][x-1] + linear[G][R][y][x])/2);
                    short rpd_b = abs(linear[R][R][y+1][x-1] - linear[R][R][y][x]);

                    short rn_b = ((linear[R][R][y][x-1] + linear[R][R][y+1][x])/2 +
                                  linear[G][B][y][x] -
                                  (linear[G][R][y][x-1] + linear[G][R][y+1][x])/2);
                    short rnd_b = abs(linear[R][R][y][x-1] - linear[R][R][y+1][x]);

                    linear[R][B][y][x] = rpd_b < rnd_b ? rp_b : rn_b;

                    short bp_r = ((linear[B][B][y-1][x+1] + linear[B][B][y][x])/2 +
                                  linear[G][R][y][x] -
                                  (linear[G][B][y-1][x+1] + linear[G][B][y][x])/2);
                    short bpd_r = abs(linear[B][B][y-1][x+1] - linear[B][B][y][x]);

                    short bn_r = ((linear[B][B][y][x+1] + linear[B][B][y-1][x])/2 +
                                  linear[G][R][y][x] -
                                  (linear[G][B][y][x+1] + linear[G][B][y-1][x])/2);
                    short bnd_r = abs(linear[B][B][y][x+1] - linear[B][B][y-1][x]);

                    linear[B][R][y][x] = bpd_r < bnd_r ? bp_r : bn_r;
                }
            }

            /*
              10: Color matrix

              11: Gamma correct

            */

            float r, g, b;
            unsigned short ri, gi, bi;
            for (int y = 2; y < BLOCK_HEIGHT/2+2; y++) {
                for (int x = 2; x < BLOCK_WIDTH/2+2; x++) {

                    // Convert from sensor rgb to srgb
                    r = colorMatrix[0]*linear[R][GR][y][x] +
                        colorMatrix[1]*linear[G][GR][y][x] +
                        colorMatrix[2]*linear[B][GR][y][x] +
                        colorMatrix[3];

                    g = colorMatrix[4]*linear[R][GR][y][x] +
                        colorMatrix[5]*linear[G][GR][y][x] +
                        colorMatrix[6]*linear[B][GR][y][x] +
                        colorMatrix[7];

                    b = colorMatrix[8]*linear[R][GR][y][x] +
                        colorMatrix[9]*linear[G][GR][y][x] +
                        colorMatrix[10]*linear[B][GR][y][x] +
                        colorMatrix[11];

                    // Clamp
                    ri = r < 0 ? 0 : (r > whiteLevel ? whiteLevel : (unsigned short)(r+0.5f));
                    gi = g < 0 ? 0 : (g > whiteLevel ? whiteLevel : (unsigned short)(g+0.5f));
                    bi = b < 0 ? 0 : (b > whiteLevel ? whiteLevel : (unsigned short)(b+0.5f));

                    // Gamma correct and store
                    out(bx+(x-2)*2, by+(y-2)*2, 0) = lut[ri];
                    out(bx+(x-2)*2, by+(y-2)*2, 1) = lut[gi];
                    out(bx+(x-2)*2, by+(y-2)*2, 2) = lut[bi];

                    // Convert from sensor rgb to srgb
                    r = colorMatrix[0]*linear[R][R][y][x] +
                        colorMatrix[1]*linear[G][R][y][x] +
                        colorMatrix[2]*linear[B][R][y][x] +
                        colorMatrix[3];

                    g = colorMatrix[4]*linear[R][R][y][x] +
                        colorMatrix[5]*linear[G][R][y][x] +
                        colorMatrix[6]*linear[B][R][y][x] +
                        colorMatrix[7];

                    b = colorMatrix[8]*linear[R][R][y][x] +
                        colorMatrix[9]*linear[G][R][y][x] +
                        colorMatrix[10]*linear[B][R][y][x] +
                        colorMatrix[11];

                    // Clamp
                    ri = r < 0 ? 0 : (r > whiteLevel ? whiteLevel : (unsigned short)(r+0.5f));
                    gi = g < 0 ? 0 : (g > whiteLevel ? whiteLevel : (unsigned short)(g+0.5f));
                    bi = b < 0 ? 0 : (b > whiteLevel ? whiteLevel : (unsigned short)(b+0.5f));

                    // Gamma correct and store
                    out(bx+(x-2)*2+1, by+(y-2)*2, 0) = lut[ri];
                    out(bx+(x-2)*2+1, by+(y-2)*2, 1) = lut[gi];
                    out(bx+(x-2)*2+1, by+(y-2)*2, 2) = lut[bi];

                    // Convert from sensor rgb to srgb
                    r = colorMatrix[0]*linear[R][B][y][x] +
                        colorMatrix[1]*linear[G][B][y][x] +
                        colorMatrix[2]*linear[B][B][y][x] +
                        colorMatrix[3];

                    g = colorMatrix[4]*linear[R][B][y][x] +
                        colorMatrix[5]*linear[G][B][y][x] +
                        colorMatrix[6]*linear[B][B][y][x] +
                        colorMatrix[7];

                    b = colorMatrix[8]*linear[R][B][y][x] +
                        colorMatrix[9]*linear[G][B][y][x] +
                        colorMatrix[10]*linear[B][B][y][x] +
                        colorMatrix[11];

                    // Clamp
                    ri = r < 0 ? 0 : (r > whiteLevel ? whiteLevel : (unsigned short)(r+0.5f));
                    gi = g < 0 ? 0 : (g > whiteLevel ? whiteLevel : (unsigned short)(g+0.5f));
                    bi = b < 0 ? 0 : (b > whiteLevel ? whiteLevel : (unsigned short)(b+0.5f));

                    // Gamma correct and store
                    out(bx+(x-2)*2, by+(y-2)*2+1, 0) = lut[ri];
                    out(bx+(x-2)*2, by+(y-2)*2+1, 1) = lut[gi];
                    out(bx+(x-2)*2, by+(y-2)*2+1, 2) = lut[bi];

                    // Convert from sensor rgb to srgb
                    r = colorMatrix[0]*linear[R][GB][y][x] +
                        colorMatrix[1]*linear[G][GB][y][x] +
                        colorMatrix[2]*linear[B][GB][y][x] +
                        colorMatrix[3];

                    g = colorMatrix[4]*linear[R][GB][y][x] +
                        colorMatrix[5]*linear[G][GB][y][x] +
                        colorMatrix[6]*linear[B][GB][y][x] +
                        colorMatrix[7];

                    b = colorMatrix[8]*linear[R][GB][y][x] +
                        colorMatrix[9]*linear[G][GB][y][x] +
                        colorMatrix[10]*linear[B][GB][y][x] +
                        colorMatrix[11];

                    // Clamp
                    ri = r < 0 ? 0 : (r > whiteLevel ? whiteLevel : (unsigned short)(r+0.5f));
                    gi = g < 0 ? 0 : (g > whiteLevel ? whiteLevel : (unsigned short)(g+0.5f));
                    bi = b < 0 ? 0 : (b > whiteLevel ? whiteLevel : (unsigned short)(b+0.5f));

                    // Gamma correct and store
                    out(bx+(x-2)*2+1, by+(y-2)*2+1, 0) = lut[ri];
                    out(bx+(x-2)*2+1, by+(y-2)*2+1, 1) = lut[gi];
                    out(bx+(x-2)*2+1, by+(y-2)*2+1, 2) = lut[bi];
                }
            }
        }
    }
}

}
