// #ifdef FCAM_ARCH_ARM
#include "Demosaic_ARM.h"
#ifdef __arm__
#include <arm_neon.h>
#endif

namespace FCam {

// Make a linear luminance -> pixel value lookup table
extern void makeLUT(float contrast, int blackLevel, int whiteLevel, float gamma, unsigned char *lut);
extern void makeColorMatrix(float colorMatrix[], float colorTemp);

// Some functions used by demosaic
inline short max(short a, short b) {return a>b ? a : b;}
inline short max(short a, short b, short c, short d) {return max(max(a, b), max(c, d));}
inline short min(short a, short b) {return a<b ? a : b;}

void demosaic_ARM(Halide::Runtime::Buffer<uint16_t> input, Halide::Runtime::Buffer<uint8_t> out, float colorTemp, float contrast, bool denoise, int blackLevel, int whiteLevel, float gamma) {

#ifdef __arm__ // only build on arm
    const int BLOCK_WIDTH  = 40;
    const int BLOCK_HEIGHT = 24;

    // Image input = src.image();

#if 0
    // Check we're the right bayer pattern. If not crop and continue.
    switch ((int)src.platform().bayerPattern()) {
    case GRBG:
        break;
    case RGGB:
        input = input.subImage(1, 0, Size(input.width()-2, input.height()));
        break;
    case BGGR:
        input = input.subImage(0, 1, Size(input.width(), input.height()-2));
        break;
    case GBRG:
        input = input.subImage(1, 1, Size(input.width()-2, input.height()-2));
    default:
        error(Event::DemosaicError, "Can't demosaic from a non-bayer sensor\n");
        return Image();
    }
#endif
    int rawWidth = out.width();
    int rawHeight = out.height();

    const int VEC_WIDTH = ((BLOCK_WIDTH + 8)/8);
    const int VEC_HEIGHT = ((BLOCK_HEIGHT + 8)/2);

#if 0
    int rawPixelsPerRow = input.bytesPerRow()/2 ; // Assumes bytesPerRow is even
#else
    int rawPixelsPerRow = input.stride(1); // Assumes bytesPerRow is even
#endif

    int outWidth = rawWidth;
    int outHeight = rawHeight;
    outWidth = min(outWidth, out.width());
    outHeight = min(outHeight, out.height());
    outWidth /= BLOCK_WIDTH;
    outWidth *= BLOCK_WIDTH;
    outHeight /= BLOCK_HEIGHT;
    outHeight *= BLOCK_HEIGHT;

#if 0
    // Check we're the right size, if not, crop center
    if (((input.width() - 8) != (unsigned)outWidth) ||
        ((input.height() - 8) != (unsigned)outHeight)) {
        int offX = (input.width() - 8 - outWidth)/2;
        int offY = (input.height() - 8 - outHeight)/2;
        offX -= offX&1;
        offY -= offY&1;

        if (offX || offY) {
            input = input.subImage(offX, offY, Size(outWidth+8, outHeight+8));
        }
    }
#endif

    // Time startTime = Time::now();

    // Prepare the color matrix in S8.8 fixed point
    float colorMatrix_f[12];

#if 0
    // Check if there's a custom color matrix
    if (src.shot().colorMatrix().size() == 12) {
        for (int i = 0; i < 12; i++) {
            colorMatrix_f[i] = src.shot().colorMatrix()[i];
        }
    } else {
        // Otherwise use the platform version
        src.platform().rawToRGBColorMatrix(src.shot().whiteBalance, colorMatrix_f);
    }
#else
    makeColorMatrix(colorMatrix_f, colorTemp);
#endif

    int16x4_t colorMatrix[3];
    for (int i = 0; i < 3; i++) {
        int16_t val = (int16_t)(colorMatrix_f[i*4+0] * 256 + 0.5);
        colorMatrix[i] = vld1_lane_s16(&val, colorMatrix[i], 0);
        val = (int16_t)(colorMatrix_f[i*4+1] * 256 + 0.5);
        colorMatrix[i] = vld1_lane_s16(&val, colorMatrix[i], 1);
        val = (int16_t)(colorMatrix_f[i*4+2] * 256 + 0.5);
        colorMatrix[i] = vld1_lane_s16(&val, colorMatrix[i], 2);
        val = (int16_t)(colorMatrix_f[i*4+3] * 256 + 0.5);
        colorMatrix[i] = vld1_lane_s16(&val, colorMatrix[i], 3);
    }

    // A buffer to store data after demosiac and color correction
    // but before gamma correction
    __attribute__((aligned(16))) uint16_t out16[BLOCK_WIDTH*BLOCK_HEIGHT*3];

    // Various color channels. Only 4 of them are defined before
    // demosaic, all of them are defined after demosiac
    __attribute__((aligned(16))) int16_t scratch[VEC_WIDTH*VEC_HEIGHT*4*12];

#define R_R_OFF  (VEC_WIDTH*VEC_HEIGHT*4*0)
#define R_GR_OFF (VEC_WIDTH*VEC_HEIGHT*4*1)
#define R_GB_OFF (VEC_WIDTH*VEC_HEIGHT*4*2)
#define R_B_OFF  (VEC_WIDTH*VEC_HEIGHT*4*3)

#define G_R_OFF  (VEC_WIDTH*VEC_HEIGHT*4*4)
#define G_GR_OFF (VEC_WIDTH*VEC_HEIGHT*4*5)
#define G_GB_OFF (VEC_WIDTH*VEC_HEIGHT*4*6)
#define G_B_OFF  (VEC_WIDTH*VEC_HEIGHT*4*7)

#define B_R_OFF  (VEC_WIDTH*VEC_HEIGHT*4*8)
#define B_GR_OFF (VEC_WIDTH*VEC_HEIGHT*4*9)
#define B_GB_OFF (VEC_WIDTH*VEC_HEIGHT*4*10)
#define B_B_OFF  (VEC_WIDTH*VEC_HEIGHT*4*11)

#define R_R(i)  (scratch+(i)+R_R_OFF)
#define R_GR(i) (scratch+(i)+R_GR_OFF)
#define R_GB(i) (scratch+(i)+R_GB_OFF)
#define R_B(i)  (scratch+(i)+R_B_OFF)

#define G_R(i)  (scratch+(i)+G_R_OFF)
#define G_GR(i) (scratch+(i)+G_GR_OFF)
#define G_GB(i) (scratch+(i)+G_GB_OFF)
#define G_B(i)  (scratch+(i)+G_B_OFF)

#define B_R(i)  (scratch+(i)+B_R_OFF)
#define B_GR(i) (scratch+(i)+B_GR_OFF)
#define B_GB(i) (scratch+(i)+B_GB_OFF)
#define B_B(i)  (scratch+(i)+B_B_OFF)

    // Reuse some of the output scratch area for the noisy inputs
#define G_GR_NOISY B_GR
#define B_B_NOISY  G_B
#define R_R_NOISY  G_R
#define G_GB_NOISY B_GB

    // Prepare the lookup table
    unsigned char lut[whiteLevel+1];
    makeLUT(contrast, blackLevel, whiteLevel, gamma, lut);

    // For each block in the input
#if 0
    for (int by = 0; by < rawHeight-8-BLOCK_HEIGHT+1; by += BLOCK_HEIGHT) {
#else
    for (int by = 0; by < outHeight; by += BLOCK_HEIGHT) {
#endif
        const short *__restrict__ blockPtr = (const short *)&input(0,by);
        unsigned char *__restrict__ outBlockPtr = &out(0, by);
#if 0
        for (int bx = 0; bx < rawWidth-8-BLOCK_WIDTH+1; bx += BLOCK_WIDTH) {
#else
        for (int bx = 0; bx < outWidth; bx += BLOCK_WIDTH) {
#endif
            // Stage 1) Demux a block of input into L1
            if (1) {
                register const int16_t *__restrict__ rawPtr = blockPtr;
                register const int16_t *__restrict__ rawPtr2 = blockPtr + rawPixelsPerRow;

                register const int rawJump = rawPixelsPerRow*2 - VEC_WIDTH*8;

                register int16_t *__restrict__ g_gr_ptr = denoise ? G_GR_NOISY(0) : G_GR(0);
                register int16_t *__restrict__ r_r_ptr  = denoise ? R_R_NOISY(0)  : R_R(0);
                register int16_t *__restrict__ b_b_ptr  = denoise ? B_B_NOISY(0)  : B_B(0);
                register int16_t *__restrict__ g_gb_ptr = denoise ? G_GB_NOISY(0) : G_GB(0);

                for (int y = 0; y < VEC_HEIGHT; y++) {
                    for (int x = 0; x < VEC_WIDTH/2; x++) {
                        asm volatile("# Stage 1) Demux\n");

                        // The below needs to be volatile, but
                        // it's not clear why (if it's not, it
                        // gets optimized out entirely)
                        asm volatile(
                            "vld2.16  {d6-d9}, [%[rawPtr]]! \n\t"
                            "vld2.16  {d10-d13}, [%[rawPtr2]]! \n\t"
                            "vst1.16  {d6-d7}, [%[g_gr_ptr]]! \n\t"
                            "vst1.16  {d8-d9}, [%[r_r_ptr]]! \n\t"
                            "vst1.16  {d10-d11}, [%[b_b_ptr]]! \n\t"
                            "vst1.16  {d12-d13}, [%[g_gb_ptr]]! \n\t" :
                            [rawPtr]"+r"(rawPtr),
                            [rawPtr2]"+r"(rawPtr2),
                            [g_gr_ptr]"+r"(g_gr_ptr),
                            [r_r_ptr]"+r"(r_r_ptr),
                            [b_b_ptr]"+r"(b_b_ptr),
                            [g_gb_ptr]"+r"(g_gb_ptr) ::
                            "d6", "d7", "d8", "d9", "d10", "d11", "d12", "d13", "memory");

                    }

                    rawPtr += rawJump;
                    rawPtr2 += rawJump;
                }
            }

            // Stage 1.5) Denoise sensor input (noisy pixel supression)

            // A pixel can't be brighter than its brightest neighbor

            if (denoise) {
                register int16_t *__restrict__ ptr_in = nullptr;
                register int16_t *__restrict__ ptr_out = nullptr;
                asm volatile("#Stage 1.5: Denoise\n\t");
                for (int b=0; b<4; b++) {
                    if (b==0) { ptr_in = G_GR_NOISY(0); }
                    if (b==1) { ptr_in = R_R_NOISY(0); }
                    if (b==2) { ptr_in = B_B_NOISY(0); }
                    if (b==3) { ptr_in = G_GB_NOISY(0); }
                    if (b==0) { ptr_out = G_GR(0); }
                    if (b==1) { ptr_out = R_R(0); }
                    if (b==2) { ptr_out = B_B(0); }
                    if (b==3) { ptr_out = G_GB(0); }

                    // write the top block pixels who aren't being denoised
                    for (int x = 0; x < (BLOCK_WIDTH+8); x+=8) {
                        int16x8_t in = vld1q_s16(ptr_in);
                        vst1q_s16(ptr_out, in);
                        ptr_in+=8;
                        ptr_out+=8;
                    }

                    for (int y = 1; y < VEC_HEIGHT - 1; y++) {
                        for (int x = 0; x < VEC_WIDTH/2; x++) {
                            int16x8_t here  = vld1q_s16(ptr_in);
                            int16x8_t above = vld1q_s16(ptr_in + VEC_WIDTH*4);
                            int16x8_t under = vld1q_s16(ptr_in - VEC_WIDTH*4);
                            int16x8_t right = vld1q_s16(ptr_in + 1);
                            int16x8_t left  = vld1q_s16(ptr_in - 1);
                            int16x8_t max;

                            // find the max of the neighbors
                            max = vmaxq_s16(left, right);
                            max = vmaxq_s16(above, max);
                            max = vmaxq_s16(under, max);

                            // clamp here to be less than the max.
                            here  = vminq_s16(max, here);

                            vst1q_s16(ptr_out, here);
                            ptr_in += 8;
                            ptr_out += 8;
                        }
                    }

                    // write the bottom block pixels who aren't being denoised
                    for (int x = 0; x < (BLOCK_WIDTH+8); x+=8) {
                        int16x8_t in = vld1q_s16(ptr_in);
                        vst1q_s16(ptr_out, in);
                        ptr_in+=8;
                        ptr_out+=8;
                    }
                }
            }

            // Stage 2 and 3) Do horizontal and vertical
            // interpolation of green, as well as picking the
            // output for green
            /*
              gv_r = (gb[UP] + gb[HERE])/2;
              gvd_r = (gb[UP] - gb[HERE]);
              gh_r = (gr[HERE] + gr[RIGHT])/2;
              ghd_r = (gr[HERE] - gr[RIGHT]);
              g_r = ghd_r < gvd_r ? gh_r : gv_r;

              gv_b = (gr[DOWN] + gr[HERE])/2;
              gvd_b = (gr[DOWN] - gr[HERE]);
              gh_b = (gb[LEFT] + gb[HERE])/2;
              ghd_b = (gb[LEFT] - gb[HERE]);
              g_b = ghd_b < gvd_b ? gh_b : gv_b;
            */
            if (1) {

                int i = VEC_WIDTH*4;

                register int16_t *g_gb_up_ptr = G_GB(i) - VEC_WIDTH*4;
                register int16_t *g_gb_here_ptr = G_GB(i);
                register int16_t *g_gb_left_ptr = G_GB(i) - 1;
                register int16_t *g_gr_down_ptr = G_GR(i) + VEC_WIDTH*4;
                register int16_t *g_gr_here_ptr = G_GR(i);
                register int16_t *g_gr_right_ptr = G_GR(i) + 1;
                register int16_t *g_r_ptr = G_R(i);
                register int16_t *g_b_ptr = G_B(i);

                for (int y = 1; y < VEC_HEIGHT-1; y++) {
                    for (int x = 0; x < VEC_WIDTH/2; x++) {

                        asm volatile("#Stage 2) Green interpolation\n");

                        // Load the inputs

                        int16x8_t gb_up = vld1q_s16(g_gb_up_ptr);
                        g_gb_up_ptr+=8;
                        int16x8_t gb_here = vld1q_s16(g_gb_here_ptr);
                        g_gb_here_ptr+=8;
                        int16x8_t gb_left = vld1q_s16(g_gb_left_ptr); // unaligned
                        g_gb_left_ptr+=8;
                        int16x8_t gr_down = vld1q_s16(g_gr_down_ptr);
                        g_gr_down_ptr+=8;
                        int16x8_t gr_here = vld1q_s16(g_gr_here_ptr);
                        g_gr_here_ptr+=8;
                        int16x8_t gr_right = vld1q_s16(g_gr_right_ptr); // unaligned
                        g_gr_right_ptr+=8;

                        //I couldn't get this assembly to work, and I don't know which
                        // of the three blocks of assembly is wrong
                        // This asm should load the inputs
                        /*
                        asm volatile(
                        "vld1.16        {d16-d17}, [%[gb_up]]!\n\t"
                        "vld1.16        {d18-d19}, [%[gb_here]]!\n\t"
                        "vld1.16        {d20-d21}, [%[gb_left]]!\n\t"
                        "vld1.16        {d22-d23}, [%[gr_down]]!\n\t"
                        "vld1.16        {d24-d25}, [%[gr_here]]!\n\t"
                        "vld1.16        {d26-d27}, [%[gr_right]]!\n\t"
                        :
                        [gb_up]"+r"(g_gb_up_ptr),
                        [gb_here]"+r"(g_gb_here_ptr),
                        [gb_left]"+r"(g_gb_left_ptr),
                        [gr_down]"+r"(g_gr_down_ptr),
                        [gr_here]"+r"(g_gr_here_ptr),
                        [gr_right]"+r"(g_gr_right_ptr) ::
                        //"d16","d17","d18","d19","d20","d21","d22","d23","d24","d25","d26","d27",
                        "q8","q9","q10","q11","q12","q13");

                        //q8 - gb_up
                        //q9 - gb_here
                        //q10 - gb_left
                        //q11 - gr_down
                        //q12 - gr_here
                        //q13 - gr_right
                        */

                        // Do the processing
                        int16x8_t gv_r  = vhaddq_s16(gb_up, gb_here);
                        int16x8_t gvd_r = vabdq_s16(gb_up, gb_here);
                        int16x8_t gh_r  = vhaddq_s16(gr_right, gr_here);
                        int16x8_t ghd_r = vabdq_s16(gr_here, gr_right);
                        int16x8_t g_r = vbslq_s16(vcltq_s16(ghd_r, gvd_r), gh_r, gv_r);

                        int16x8_t gv_b  = vhaddq_s16(gr_down, gr_here);
                        int16x8_t gvd_b = vabdq_s16(gr_down, gr_here);
                        int16x8_t gh_b  = vhaddq_s16(gb_left, gb_here);
                        int16x8_t ghd_b = vabdq_s16(gb_left, gb_here);
                        int16x8_t g_b = vbslq_s16(vcltq_s16(ghd_b, gvd_b), gh_b, gv_b);

                        //this asm should do the above selection/interpolation
                        /*
                        asm volatile(
                        "vabd.s16       q0, q12, q13\n\t" //ghd_r
                        "vabd.s16       q1, q8, q9\n\t" //gvd_r
                        "vabd.s16       q2, q10, q9\n\t" //ghd_b
                        "vabd.s16       q3, q11, q12\n\t" //gvd_b
                        "vcgt.s16       q1, q0, q1\n\t" //select ghd_r or gvd_r
                        "vcgt.s16       q2, q2, q3\n\t" //select gvd_b or ghd_b
                        "vhadd.s16      q8, q8, q9\n\t" //gv_r
                        "vhadd.s16      q11, q11, q12\n\t" //gv_b
                        "vhadd.s16      q12, q12, q13\n\t" //gh_r
                        "vhadd.s16      q9, q9, q10\n\t" //gh_b
                        "vbsl           q1, q12, q8\n\t" //g_r
                        "vbsl           q2, q9, q11\n\t" //g_b
                         ::: "q0","q1","q2","q3","q8","q9","q10","q11","q12","q13");
                        */

                        //this should save the output
                        /*
                        asm volatile(
                        "vst1.16        {d2-d3}, [%[g_r]]!\n\t"
                        "vst1.16        {d4-d5}, [%[g_b]]!\n\t" :
                        [g_r]"+r"(g_r_ptr),[g_b]"+r"(g_b_ptr)
                        :: "memory");
                        */

                        // Save the outputs
                        vst1q_s16(g_r_ptr, g_r);
                        g_r_ptr+=8;
                        vst1q_s16(g_b_ptr, g_b);
                        g_b_ptr+=8;
                    }
                }
            }
            asm volatile("#End of stage 2 (green interpolation)\n");
            // Stages 4-9

            if (1) {

                /*
                  r_gr = (r[LEFT] + r[HERE])/2 + gr[HERE] - (g_r[LEFT] + g_r[HERE])/2;
                  b_gr = (b[UP] + b[HERE])/2 + gr[HERE] - (g_b[UP] + g_b[HERE])/2;
                  r_gb = (r[HERE] + r[DOWN])/2 + gb[HERE] - (g_r[HERE] + g_r[DOWN])/2;
                  b_gb = (b[HERE] + b[RIGHT])/2 + gb[HERE] - (g_b[HERE] + g_b[RIGHT])/2;

                  rp_b = (r[DOWNLEFT] + r[HERE])/2 + g_b[HERE] - (g_r[DOWNLEFT] + g_r[HERE])/2;
                  rn_b = (r[LEFT] + r[DOWN])/2 + g_b[HERE] - (g_r[LEFT] + g_r[DOWN])/2;
                  rpd_b = (r[DOWNLEFT] - r[HERE]);
                  rnd_b = (r[LEFT] - r[DOWN]);
                  r_b = rpd_b < rnd_b ? rp_b : rn_b;

                  bp_r = (b[UPRIGHT] + b[HERE])/2 + g_r[HERE] - (g_b[UPRIGHT] + g_b[HERE])/2;
                  bn_r = (b[RIGHT] + b[UP])/2 + g_r[HERE] - (g_b[RIGHT] + g_b[UP])/2;
                  bpd_r = (b[UPRIGHT] - b[HERE]);
                  bnd_r = (b[RIGHT] - b[UP]);
                  b_r = bpd_r < bnd_r ? bp_r : bn_r;
                */

                int i = 2*VEC_WIDTH*4;

                for (int y = 2; y < VEC_HEIGHT-2; y++) {
                    for (int x = 0; x < VEC_WIDTH; x++) {

                        asm volatile("#Stage 4) r/b interpolation\n");

                        // Load the inputs
                        int16x4_t r_here       = vld1_s16(R_R(i));
                        int16x4_t r_left       = vld1_s16(R_R(i) - 1);
                        int16x4_t r_down       = vld1_s16(R_R(i) + VEC_WIDTH*4);

                        int16x4_t g_r_left     = vld1_s16(G_R(i) - 1);
                        int16x4_t g_r_here     = vld1_s16(G_R(i));
                        int16x4_t g_r_down     = vld1_s16(G_R(i) + VEC_WIDTH*4);

                        int16x4_t b_up         = vld1_s16(B_B(i) - VEC_WIDTH*4);
                        int16x4_t b_here       = vld1_s16(B_B(i));
                        int16x4_t b_right      = vld1_s16(B_B(i) + 1);

                        int16x4_t g_b_up       = vld1_s16(G_B(i) - VEC_WIDTH*4);
                        int16x4_t g_b_here     = vld1_s16(G_B(i));
                        int16x4_t g_b_right    = vld1_s16(G_B(i) + 1);

                        // Do the processing
                        int16x4_t gr_here      = vld1_s16(G_GR(i));
                        int16x4_t gb_here      = vld1_s16(G_GB(i));

                        {
                            // red at green
                            int16x4_t r_gr  = vadd_s16(vhadd_s16(r_left, r_here),
                                                       vsub_s16(gr_here,
                                                                vhadd_s16(g_r_left, g_r_here)));
                            int16x4_t r_gb  = vadd_s16(vhadd_s16(r_here, r_down),
                                                       vsub_s16(gb_here,
                                                                vhadd_s16(g_r_down, g_r_here)));
                            vst1_s16(R_GR(i), r_gr);
                            vst1_s16(R_GB(i), r_gb);
                        }

                        {
                            // red at blue
                            int16x4_t r_downleft   = vld1_s16(R_R(i) + VEC_WIDTH*4 - 1);
                            int16x4_t g_r_downleft = vld1_s16(G_R(i) + VEC_WIDTH*4 - 1);

                            int16x4_t rp_b  = vadd_s16(vhadd_s16(r_downleft, r_here),
                                                       vsub_s16(g_b_here,
                                                                vhadd_s16(g_r_downleft, g_r_here)));
                            int16x4_t rn_b  = vadd_s16(vhadd_s16(r_left, r_down),
                                                       vsub_s16(g_b_here,
                                                                vhadd_s16(g_r_left, g_r_down)));
                            int16x4_t rpd_b = vabd_s16(r_downleft, r_here);
                            int16x4_t rnd_b = vabd_s16(r_left, r_down);
                            int16x4_t r_b   = vbsl_s16(vclt_s16(rpd_b, rnd_b), rp_b, rn_b);
                            vst1_s16(R_B(i), r_b);
                        }

                        {
                            // blue at green
                            int16x4_t b_gr  = vadd_s16(vhadd_s16(b_up, b_here),
                                                       vsub_s16(gr_here,
                                                                vhadd_s16(g_b_up, g_b_here)));
                            int16x4_t b_gb  = vadd_s16(vhadd_s16(b_here, b_right),
                                                       vsub_s16(gb_here,
                                                                vhadd_s16(g_b_right, g_b_here)));
                            vst1_s16(B_GR(i), b_gr);
                            vst1_s16(B_GB(i), b_gb);
                        }

                        {
                            // blue at red
                            int16x4_t b_upright    = vld1_s16(B_B(i) - VEC_WIDTH*4 + 1);
                            int16x4_t g_b_upright  = vld1_s16(G_B(i) - VEC_WIDTH*4 + 1);

                            int16x4_t bp_r  = vadd_s16(vhadd_s16(b_upright, b_here),
                                                       vsub_s16(g_r_here,
                                                                vhadd_s16(g_b_upright, g_b_here)));
                            int16x4_t bn_r  = vadd_s16(vhadd_s16(b_right, b_up),
                                                       vsub_s16(g_r_here,
                                                                vhadd_s16(g_b_right, g_b_up)));
                            int16x4_t bpd_r = vabd_s16(b_upright, b_here);
                            int16x4_t bnd_r = vabd_s16(b_right, b_up);
                            int16x4_t b_r   = vbsl_s16(vclt_s16(bpd_r, bnd_r), bp_r, bn_r);
                            vst1_s16(B_R(i), b_r);
                        }

                        // Advance the index
                        i += 4;
                    }
                }
                asm volatile("#End of stage 4 - what_ever\n\t");
            }

            // Stage 10)
            if (1) {
                // Color-correct and save the results into a 16-bit buffer for gamma correction

                asm volatile("#Stage 10) Color Correction\n");

                uint16_t *__restrict__ out16Ptr = out16;

                int i = 2*VEC_WIDTH*4;

                const uint16x4_t bound = vdup_n_u16(whiteLevel);

                for (int y = 2; y < VEC_HEIGHT-2; y++) {

                    // skip the first vec in each row

                    int16x4x2_t r0 = vzip_s16(vld1_s16(R_GR(i)), vld1_s16(R_R(i)));
                    int16x4x2_t g0 = vzip_s16(vld1_s16(G_GR(i)), vld1_s16(G_R(i)));
                    int16x4x2_t b0 = vzip_s16(vld1_s16(B_GR(i)), vld1_s16(B_R(i)));
                    i += 4;

                    for (int x = 1; x < VEC_WIDTH; x++) {

                        int16x4x2_t r1 = vzip_s16(vld1_s16(R_GR(i)), vld1_s16(R_R(i)));
                        int16x4x2_t g1 = vzip_s16(vld1_s16(G_GR(i)), vld1_s16(G_R(i)));
                        int16x4x2_t b1 = vzip_s16(vld1_s16(B_GR(i)), vld1_s16(B_R(i)));

                        // do the color matrix
                        int32x4_t rout = vmovl_s16(vdup_lane_s16(colorMatrix[0], 3));
                        rout = vmlal_lane_s16(rout, r0.val[1], colorMatrix[0], 0);
                        rout = vmlal_lane_s16(rout, g0.val[1], colorMatrix[0], 1);
                        rout = vmlal_lane_s16(rout, b0.val[1], colorMatrix[0], 2);

                        int32x4_t gout = vmovl_s16(vdup_lane_s16(colorMatrix[1], 3));
                        gout = vmlal_lane_s16(gout, r0.val[1], colorMatrix[1], 0);
                        gout = vmlal_lane_s16(gout, g0.val[1], colorMatrix[1], 1);
                        gout = vmlal_lane_s16(gout, b0.val[1], colorMatrix[1], 2);

                        int32x4_t bout = vmovl_s16(vdup_lane_s16(colorMatrix[2], 3));
                        bout = vmlal_lane_s16(bout, r0.val[1], colorMatrix[2], 0);
                        bout = vmlal_lane_s16(bout, g0.val[1], colorMatrix[2], 1);
                        bout = vmlal_lane_s16(bout, b0.val[1], colorMatrix[2], 2);

                        uint16x4x3_t col16;
                        col16.val[0] = vqrshrun_n_s32(rout, 8);
                        col16.val[1] = vqrshrun_n_s32(gout, 8);
                        col16.val[2] = vqrshrun_n_s32(bout, 8);
                        col16.val[0] = vmin_u16(col16.val[0], bound);
                        col16.val[1] = vmin_u16(col16.val[1], bound);
                        col16.val[2] = vmin_u16(col16.val[2], bound);
                        vst3_u16(out16Ptr, col16);
                        out16Ptr += 12;

                        rout = vmovl_s16(vdup_lane_s16(colorMatrix[0], 3));
                        rout = vmlal_lane_s16(rout, r1.val[0], colorMatrix[0], 0);
                        rout = vmlal_lane_s16(rout, g1.val[0], colorMatrix[0], 1);
                        rout = vmlal_lane_s16(rout, b1.val[0], colorMatrix[0], 2);

                        gout = vmovl_s16(vdup_lane_s16(colorMatrix[1], 3));
                        gout = vmlal_lane_s16(gout, r1.val[0], colorMatrix[1], 0);
                        gout = vmlal_lane_s16(gout, g1.val[0], colorMatrix[1], 1);
                        gout = vmlal_lane_s16(gout, b1.val[0], colorMatrix[1], 2);

                        bout = vmovl_s16(vdup_lane_s16(colorMatrix[2], 3));
                        bout = vmlal_lane_s16(bout, r1.val[0], colorMatrix[2], 0);
                        bout = vmlal_lane_s16(bout, g1.val[0], colorMatrix[2], 1);
                        bout = vmlal_lane_s16(bout, b1.val[0], colorMatrix[2], 2);

                        col16.val[0] = vqrshrun_n_s32(rout, 8);
                        col16.val[1] = vqrshrun_n_s32(gout, 8);
                        col16.val[2] = vqrshrun_n_s32(bout, 8);
                        col16.val[0] = vmin_u16(col16.val[0], bound);
                        col16.val[1] = vmin_u16(col16.val[1], bound);
                        col16.val[2] = vmin_u16(col16.val[2], bound);
                        vst3_u16(out16Ptr, col16);
                        out16Ptr += 12;

                        r0 = r1;
                        g0 = g1;
                        b0 = b1;

                        i += 4;
                    }

                    // jump back
                    i -= VEC_WIDTH*4;

                    r0 = vzip_s16(vld1_s16(R_B(i)), vld1_s16(R_GB(i)));
                    g0 = vzip_s16(vld1_s16(G_B(i)), vld1_s16(G_GB(i)));
                    b0 = vzip_s16(vld1_s16(B_B(i)), vld1_s16(B_GB(i)));
                    i += 4;

                    for (int x = 1; x < VEC_WIDTH; x++) {
                        int16x4x2_t r1 = vzip_s16(vld1_s16(R_B(i)), vld1_s16(R_GB(i)));
                        int16x4x2_t g1 = vzip_s16(vld1_s16(G_B(i)), vld1_s16(G_GB(i)));
                        int16x4x2_t b1 = vzip_s16(vld1_s16(B_B(i)), vld1_s16(B_GB(i)));

                        // do the color matrix
                        int32x4_t rout = vmovl_s16(vdup_lane_s16(colorMatrix[0], 3));
                        rout = vmlal_lane_s16(rout, r0.val[1], colorMatrix[0], 0);
                        rout = vmlal_lane_s16(rout, g0.val[1], colorMatrix[0], 1);
                        rout = vmlal_lane_s16(rout, b0.val[1], colorMatrix[0], 2);

                        int32x4_t gout = vmovl_s16(vdup_lane_s16(colorMatrix[1], 3));
                        gout = vmlal_lane_s16(gout, r0.val[1], colorMatrix[1], 0);
                        gout = vmlal_lane_s16(gout, g0.val[1], colorMatrix[1], 1);
                        gout = vmlal_lane_s16(gout, b0.val[1], colorMatrix[1], 2);

                        int32x4_t bout = vmovl_s16(vdup_lane_s16(colorMatrix[2], 3));
                        bout = vmlal_lane_s16(bout, r0.val[1], colorMatrix[2], 0);
                        bout = vmlal_lane_s16(bout, g0.val[1], colorMatrix[2], 1);
                        bout = vmlal_lane_s16(bout, b0.val[1], colorMatrix[2], 2);

                        uint16x4x3_t col16;
                        col16.val[0] = vqrshrun_n_s32(rout, 8);
                        col16.val[1] = vqrshrun_n_s32(gout, 8);
                        col16.val[2] = vqrshrun_n_s32(bout, 8);
                        col16.val[0] = vmin_u16(col16.val[0], bound);
                        col16.val[1] = vmin_u16(col16.val[1], bound);
                        col16.val[2] = vmin_u16(col16.val[2], bound);
                        vst3_u16(out16Ptr, col16);
                        out16Ptr += 12;

                        rout = vmovl_s16(vdup_lane_s16(colorMatrix[0], 3));
                        rout = vmlal_lane_s16(rout, r1.val[0], colorMatrix[0], 0);
                        rout = vmlal_lane_s16(rout, g1.val[0], colorMatrix[0], 1);
                        rout = vmlal_lane_s16(rout, b1.val[0], colorMatrix[0], 2);

                        gout = vmovl_s16(vdup_lane_s16(colorMatrix[1], 3));
                        gout = vmlal_lane_s16(gout, r1.val[0], colorMatrix[1], 0);
                        gout = vmlal_lane_s16(gout, g1.val[0], colorMatrix[1], 1);
                        gout = vmlal_lane_s16(gout, b1.val[0], colorMatrix[1], 2);

                        bout = vmovl_s16(vdup_lane_s16(colorMatrix[2], 3));
                        bout = vmlal_lane_s16(bout, r1.val[0], colorMatrix[2], 0);
                        bout = vmlal_lane_s16(bout, g1.val[0], colorMatrix[2], 1);
                        bout = vmlal_lane_s16(bout, b1.val[0], colorMatrix[2], 2);

                        col16.val[0] = vqrshrun_n_s32(rout, 8);
                        col16.val[1] = vqrshrun_n_s32(gout, 8);
                        col16.val[2] = vqrshrun_n_s32(bout, 8);
                        col16.val[0] = vmin_u16(col16.val[0], bound);
                        col16.val[1] = vmin_u16(col16.val[1], bound);
                        col16.val[2] = vmin_u16(col16.val[2], bound);
                        vst3_u16(out16Ptr, col16);
                        out16Ptr += 12;

                        r0 = r1;
                        g0 = g1;
                        b0 = b1;

                        i += 4;
                    }
                }
                asm volatile("#End of stage 10) - color correction\n\t");
            }


            if (1) {

                asm volatile("#Gamma Correction\n");
                // Gamma correction (on the CPU, not the NEON)
                const uint16_t *__restrict__ out16Ptr = out16;

                for (int y = 0; y < BLOCK_HEIGHT; y++) {
                    unsigned int *__restrict__ outPtr32 = (unsigned int *)(outBlockPtr + y * outWidth * 3);
                    for (int x = 0; x < (BLOCK_WIDTH*3)/4; x++) {
                        unsigned val = ((lut[out16Ptr[0]] << 0) |
                                        (lut[out16Ptr[1]] << 8) |
                                        (lut[out16Ptr[2]] << 16) |
                                        (lut[out16Ptr[3]] << 24));
                        *outPtr32++ = val;
                        out16Ptr += 4;
                        // *outPtr++ = lut[*out16Ptr++];
                    }
                }
                asm volatile("#end of Gamma Correction\n");

                /*
                const uint16_t * __restrict__ out16Ptr = out16;
                for (int y = 0; y < BLOCK_HEIGHT; y++) {
                    unsigned char * __restrict__ outPtr = (outBlockPtr + y * outWidth * 3);
                    for (int x = 0; x < (BLOCK_WIDTH*3); x++) {
                        *outPtr++ = lut[*out16Ptr++];
                    }
                }
                */

            }


            blockPtr += BLOCK_WIDTH;
            outBlockPtr += BLOCK_WIDTH*3;
        }
    }
#endif //__arm__
}

}


// #endif
