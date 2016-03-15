
#define L2FETCH(ADDR, REG)   asm("	l2fetch(%0, %1)\n"	:: "r" (ADDR), "r" (REG)	);

#include "gaussian_asm.h"
//#include "dspcache.h"               // contains assembly for cache pre-fetching.
#include "hexagon_types.h"
#include "hexagon_protos.h"         // part of Q6 tools, contains intrinsics definitions
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* void gaussian(const unsigned char *imgSrc, unsigned int width, unsigned int height, */
/* 		unsigned int stride, unsigned char *imgDst, unsigned int dstStride) { */

/* 	unsigned int* intSrc0 = (unsigned int*) imgSrc; */
/* 	unsigned int* intSrc1 = intSrc0 + (stride / 4); */
/* 	unsigned int* intSrc2 = intSrc1 + (stride / 4); */

/* 	unsigned long result; */
/* 	unsigned long long one, two, three; */
/* 	unsigned char* dst = imgDst + dstStride + 1; */

/* 	unsigned long long colSumC, colSumN, colSum2, colSum3; */

/* 	unsigned int wRest = (width - 2) % 4; */
/* 	one = Q6_P_vzxtbh_R(*intSrc0++); */
/* 	two = Q6_P_vzxtbh_R(*intSrc1++); */
/* 	three = Q6_P_vzxtbh_R(*intSrc2++); */

/* 	colSumC = Q6_P_vaddh_PP(Q6_P_vaddh_PP(one, Q6_P_vaslh_PI(two, 1)), three); */

/* 	for (int j = 0; j < (height - 2); j++) { */


/* 		for (int i = 0; i < ((width - 2) / 4); i++) { */


/* 			one = Q6_P_vzxtbh_R(*intSrc0++); */
/* 			two = Q6_P_vzxtbh_R(*intSrc1++); */
/* 			three = Q6_P_vzxtbh_R(*intSrc2++); */

/* 			colSumN = Q6_P_vaddh_PP(Q6_P_vaddh_PP(one, Q6_P_vaslh_PI(two, 1)), */
/* 					three); */
/* 			colSum2 = Q6_P_valignb_PPI(colSumN, colSumC, 2); */
/* 			colSum3 = Q6_P_valignb_PPI(colSumN, colSumC, 4); */

/* 			colSum2 = Q6_P_vaddh_PP(Q6_P_vaslh_PI(colSum2, 1), colSumC); */
/* 			result = Q6_R_vtrunehb_P( */
/* 					Q6_P_vasrh_PI(Q6_P_vaddh_PP(colSum2, colSum3), 4)); */

/* 			memcpy(dst, &result, 4); */
/* 			dst += 4; */

/* 			colSumC = colSumN; */
/* 		} */

/* 		if (wRest) { */
/* 			one = Q6_P_vzxtbh_R(*intSrc0++); */
/* 			two = Q6_P_vzxtbh_R(*intSrc1++); */
/* 			three = Q6_P_vzxtbh_R(*intSrc2++); */

/* 			colSumN = Q6_P_vaddh_PP(Q6_P_vaddh_PP(one, Q6_P_vaslh_PI(two, 1)), */
/* 					three); */
/* 			colSum2 = Q6_P_valignb_PPI(colSumN, colSumC, 2); */
/* 			colSum3 = Q6_P_valignb_PPI(colSumN, colSumC, 4); */

/* 			colSum2 = Q6_P_vaddh_PP(Q6_P_vaslh_PI(colSum2, 1), colSumC); */
/* 			result = Q6_R_vtrunehb_P( */
/* 					Q6_P_vasrh_PI(Q6_P_vaddh_PP(colSum2, colSum3), 4)); */

/* 			switch (wRest) { */
/* 			case 3: */
/* 				memcpy(dst, &result, 3); */
/* 				dst += 3; */
/* 				*dst++ = (imgSrc[stride * (j) + (width - 1)] */
/* 						+ (imgSrc[stride * (j + 1) + (width - 1)] << 1) */
/* 						+ imgSrc[stride * (j + 2) + (width - 1)] */
/* 						+ (imgSrc[stride * (j) + (width - 1)] << 1) */
/* 						+ (imgSrc[stride * (j + 1) + (width - 1)] << 2) */
/* 						+ (imgSrc[stride * (j + 2) + (width - 1)] << 1) */
/* 						+ (imgSrc[stride * (j) + (width - 2)]) */
/* 						+ (imgSrc[stride * (j + 1) + (width - 2)] << 1) */
/* 						+ imgSrc[stride * (j + 2) + (width - 2)]) >> 4; */

/* 				*dst++ = ((imgSrc[(j + 1) * stride]) */
/* 						+ (imgSrc[(j + 2) * stride] << 1) */
/* 						+ (imgSrc[(j + 3) * stride]) */
/* 						+ (imgSrc[(j + 1) * stride] << 1) */
/* 						+ (imgSrc[(j + 2) * stride] << 2) */
/* 						+ (imgSrc[(j + 3) * stride] << 1) */
/* 						+ (imgSrc[(j + 1) * stride + 1]) */
/* 						+ (imgSrc[(j + 2) * stride + 1] << 1) */
/* 						+ (imgSrc[(j + 3) * stride + 1])) >> 4; */
/* 				colSumC = colSumN; */
/* 				break; */

/* 			case 2: */
/* 				memcpy(dst, &result, 2); */
/* 				dst += 2; */
/* 				*dst++ = (imgSrc[stride * (j) + (width - 1)] */
/* 						+ (imgSrc[stride * (j + 1) + (width - 1)] << 1) */
/* 						+ imgSrc[stride * (j + 2) + (width - 1)] */
/* 						+ (imgSrc[stride * (j) + (width - 1)] << 1) */
/* 						+ (imgSrc[stride * (j + 1) + (width - 1)] << 2) */
/* 						+ (imgSrc[stride * (j + 2) + (width - 1)] << 1) */
/* 						+ (imgSrc[stride * (j) + (width - 2)]) */
/* 						+ (imgSrc[stride * (j + 1) + (width - 2)] << 1) */
/* 						+ imgSrc[stride * (j + 2) + (width - 2)]) >> 4; */

/* 				*dst++ = ((imgSrc[(j + 1) * stride]) */
/* 						+ (imgSrc[(j + 2) * stride] << 1) */
/* 						+ (imgSrc[(j + 3) * stride]) */
/* 						+ (imgSrc[(j + 1) * stride] << 1) */
/* 						+ (imgSrc[(j + 2) * stride] << 2) */
/* 						+ (imgSrc[(j + 3) * stride] << 1) */
/* 						+ (imgSrc[(j + 1) * stride + 1]) */
/* 						+ (imgSrc[(j + 2) * stride + 1] << 1) */
/* 						+ (imgSrc[(j + 3) * stride + 1])) >> 4; */

/* 				colSumC = colSumN; */
/* 				break; */

/* 			case 1: */
/* 				memcpy(dst, &result, 1); */
/* 				dst += 1; */
/* 				*dst++ = (imgSrc[stride * (j) + (width - 1)] */
/* 						+ (imgSrc[stride * (j + 1) + (width - 1)] << 1) */
/* 						+ imgSrc[stride * (j + 2) + (width - 1)] */
/* 						+ (imgSrc[stride * (j) + (width - 1)] << 1) */
/* 						+ (imgSrc[stride * (j + 1) + (width - 1)] << 2) */
/* 						+ (imgSrc[stride * (j + 2) + (width - 1)] << 1) */
/* 						+ (imgSrc[stride * (j) + (width - 2)]) */
/* 						+ (imgSrc[stride * (j + 1) + (width - 2)] << 1) */
/* 						+ imgSrc[stride * (j + 2) + (width - 2)]) >> 4; */

/* 				*dst++ = ((imgSrc[(j + 1) * stride]) */
/* 						+ (imgSrc[(j + 2) * stride] << 1) */
/* 						+ (imgSrc[(j + 3) * stride]) */
/* 						+ (imgSrc[(j + 1) * stride] << 1) */
/* 						+ (imgSrc[(j + 2) * stride] << 2) */
/* 						+ (imgSrc[(j + 3) * stride] << 1) */
/* 						+ (imgSrc[(j + 1) * stride + 1]) */
/* 						+ (imgSrc[(j + 2) * stride + 1] << 1) */
/* 						+ (imgSrc[(j + 3) * stride + 1])) >> 4; */

/* 				colSumC = colSumN; */
/* 				break; */
/* 			} */

/* 		} */

/* 	} */

/* } */

/* void gaussian_top(const unsigned char *imgSrc, unsigned int width, */
/* 		unsigned int height, unsigned int stride, unsigned char *imgDst, */
/* 		unsigned int dstStride, fcvBorderType borderType, uint8_t borderValue) { */

/* 	const int h_1 = height - 1; */
/* 	const int h_2 = height - 2; */
/* 	const int w_2 = width - 2; */
/* 	const int w_1 = width - 1; */

/* 	const uint8_t* src0, *src1; */

/* 	// top-left corner */
/* 	src0 = imgSrc; */
/* 	src1 = src0 + stride; */
/* 	imgDst[0] = (uint8_t) ((src0[0] * 9 + src0[1] * 3 + src1[0] * 3 + src1[1]) */
/* 			>> 4); */

/* 	unsigned int* intSrc0 = (unsigned int*) imgSrc; */
/* 	unsigned int* intSrc1 = intSrc0 + (stride / 4); */

/* 	unsigned int* intSrc2 = (unsigned int*) imgSrc */
/* 			+ (height - 2) * (stride / 4); */
/* 	unsigned int* intSrc3 = intSrc2 + (stride / 4); */

/* 	unsigned long result; */
/* 	unsigned long result2; */
/* 	unsigned long long one, two, three, four; */
/* 	unsigned char* dst = imgDst + 1; */
/* 	unsigned char* dst1 = imgDst + (height - 1) * stride + 1; */

/* 	unsigned long long colSumC, colSumN, colSum2, colSum3; */
/* 	unsigned long long colSumC2, colSumN2, colSum2B, colSum3B; */

/* 	unsigned int wRest = (width - 2) % 4; */

/* 	// Top AND Bottom Row */
/* 	one = Q6_P_vzxtbh_R(*intSrc0++); */
/* 	two = Q6_P_vzxtbh_R(*intSrc1++); */
/* 	three = Q6_P_vzxtbh_R(*intSrc2++); */
/* 	four = Q6_P_vzxtbh_R(*intSrc3++); */

/* 	colSumC = Q6_P_vaddh_PP(Q6_P_vaddh_PP(one, Q6_P_vaslh_PI(one, 1)), two); */
/* 	colSumC2 = Q6_P_vaddh_PP(Q6_P_vaddh_PP(three, Q6_P_vaslh_PI(four, 1)), */
/* 			four); */

/* 	for (int i = 0; i < ((width - 2) / 4); i++) { */
/* 		one = Q6_P_vzxtbh_R(*intSrc0++); */
/* 		two = Q6_P_vzxtbh_R(*intSrc1++); */
/* 		three = Q6_P_vzxtbh_R(*intSrc2++); */
/* 		four = Q6_P_vzxtbh_R(*intSrc3++); */

/* 		colSumN = Q6_P_vaddh_PP(Q6_P_vaddh_PP(one, Q6_P_vaslh_PI(one, 1)), two); */
/* 		colSum2 = Q6_P_valignb_PPI(colSumN, colSumC, 2); */
/* 		colSum3 = Q6_P_valignb_PPI(colSumN, colSumC, 4); */

/* 		colSum2 = Q6_P_vaddh_PP(Q6_P_vaslh_PI(colSum2, 1), colSumC); */
/* 		result = Q6_R_vtrunehb_P( */
/* 				Q6_P_vasrh_PI(Q6_P_vaddh_PP(colSum2, colSum3), 4)); */

/* 		memcpy(dst, &result, 4); */
/* 		dst += 4; */

/* 		colSumN2 = Q6_P_vaddh_PP(Q6_P_vaddh_PP(three, Q6_P_vaslh_PI(four, 1)), */
/* 				four); */
/* 		colSum2B = Q6_P_valignb_PPI(colSumN2, colSumC2, 2); */
/* 		colSum3B = Q6_P_valignb_PPI(colSumN2, colSumC2, 4); */

/* 		colSum2B = Q6_P_vaddh_PP(Q6_P_vaslh_PI(colSum2B, 1), colSumC2); */
/* 		result2 = Q6_R_vtrunehb_P( */
/* 				Q6_P_vasrh_PI(Q6_P_vaddh_PP(colSum2B, colSum3B), 4)); */

/* 		memcpy(dst1, &result2, 4); */
/* 		dst1 += 4; */

/* 		colSumC2 = colSumN2; */
/* 		colSumC = colSumN; */
/* 	} */

/* 	if (wRest) { */
/* 		one = Q6_P_vzxtbh_R(*intSrc0++); */
/* 		two = Q6_P_vzxtbh_R(*intSrc1++); */
/* 		three = Q6_P_vzxtbh_R(*intSrc2++); */
/* 		four = Q6_P_vzxtbh_R(*intSrc3++); */

/* 		colSumN = Q6_P_vaddh_PP(Q6_P_vaddh_PP(one, Q6_P_vaslh_PI(one, 1)), two); */
/* 		colSumN2 = Q6_P_vaddh_PP(Q6_P_vaddh_PP(three, Q6_P_vaslh_PI(four, 1)), */
/* 				four); */
/* 		colSum2 = Q6_P_valignb_PPI(colSumN, colSumC, 2); */
/* 		colSum2B = Q6_P_valignb_PPI(colSumN2, colSumC2, 2); */
/* 		colSum3 = Q6_P_valignb_PPI(colSumN, colSumC, 4); */
/* 		colSum3B = Q6_P_valignb_PPI(colSumN2, colSumC2, 4); */
/* 		colSum2 = Q6_P_vaddh_PP(Q6_P_vaslh_PI(colSum2, 1), colSumC); */
/* 		result = Q6_R_vtrunehb_P( */
/* 				Q6_P_vasrh_PI(Q6_P_vaddh_PP(colSum2, colSum3), 4)); */
/* 		colSum2B = Q6_P_vaddh_PP(Q6_P_vaslh_PI(colSum2B, 1), colSumC2); */
/* 		result2 = Q6_R_vtrunehb_P( */
/* 				Q6_P_vasrh_PI(Q6_P_vaddh_PP(colSum2B, colSum3B), 4)); */

/* 		switch (wRest) { */
/* 		case 3: */
/* 			memcpy(dst, &result, 3); */
/* 			memcpy(dst1, &result2, 3); */
/* 			break; */
/* 		case 2: */
/* 			memcpy(dst, &result, 2); */
/* 			memcpy(dst1, &result2, 2); */
/* 			break; */
/* 		case 1: */
/* 			memcpy(dst, &result, 1); */
/* 			memcpy(dst1, &result2, 1); */
/* 			break; */
/* 		} */

/* 	} */

/* 	// top-right corner */
/* 	src0 = imgSrc + w_2; */
/* 	src1 = src0 + stride; */
/* 	imgDst[w_1] = (uint8_t) ((src0[0] * 3 + src0[1] * 9 + src1[0] + src1[1] * 3) */
/* 			>> 4); */

/* 	// Pixel (0, 1) */
/* 	imgDst[dstStride] = ((imgSrc[0]) + (imgSrc[stride] << 1) */
/* 			+ (imgSrc[2 * stride]) + (imgSrc[0] << 1) + (imgSrc[stride] << 2) */
/* 			+ (imgSrc[2 * stride] << 1) + (imgSrc[1]) */
/* 			+ (imgSrc[stride + 1] << 1) + (imgSrc[2 * stride + 1])) >> 4; */

/* 	// bottom-left corner */
/* 	src0 = imgSrc + stride * (h_2); */
/* 	src1 = src0 + stride; */
/* 	imgDst[dstStride * (h_1)] = (uint8_t) ((src0[0] * 3 + src0[1] + src1[0] * 9 */
/* 			+ src1[1] * 3) >> 4); */

/* 	// bottom-right corner */
/* 	src0 = imgSrc + stride * (h_2) + w_2; */
/* 	src1 = src0 + stride; */
/* 	imgDst[dstStride * (h_1) + w_1] = (uint8_t) ((src0[0] + src0[1] * 3 */
/* 			+ src1[0] * 3 + src1[1] * 9) >> 4); */
/* } */

void gaussian_hvx_top(const unsigned char *__restrict imgSrc, unsigned int width,
		unsigned int height, unsigned int stride,
		unsigned char *__restrict imgDst, unsigned int dstStride,
		unsigned int VLEN, fcvBorderType borderType, uint8_t borderValue) {


	const int h_1 = height - 1;
	const int h_2 = height - 2;
	const int w_2 = width - 2;
	const int w_1 = width - 1;

	const uint8_t* src0, *src1;

	HEXAGON_Vect32 const2 = 0x02020202;

	HVX_Vector sLine0, sLine1;
	HVX_Vector sX_1, sX0, sX1, sX2, sX0X1, sX0X1X1, sX_1X0;
	HVX_Vector sSumE, sSumO;
	HVX_VectorPair dVsumv0, dVsumv1;

	unsigned char *src = (unsigned char*) imgSrc + stride;
	unsigned char *dst = (unsigned char*) imgDst + 1;

	unsigned int wRest = (width - 2) % VLEN;

	HVX_Vector *iptr0 = (HVX_Vector *) (src - (stride));
	HVX_Vector *iptr1 = (HVX_Vector *) (src);
	HVX_Vector *optr = (HVX_Vector *) (dst);

	sLine0 = *iptr0++;
	sLine1 = *iptr1++;

	dVsumv0 = Q6_W_vcombine_VV(Q6_V_vzero(), Q6_V_vzero());
	dVsumv1 = Q6_Wh_vadd_VubVub(sLine0, sLine1);
	dVsumv1 = Q6_Wh_vmpyacc_WhVubRb(dVsumv1, sLine0, const2);

	for (int i = 0; i < (width - 2) / VLEN; i++) {

		sX_1 = Q6_V_vlalign_VVI(Q6_V_hi_W(dVsumv1), Q6_V_hi_W(dVsumv0), 2);

		sLine0 = *iptr0++;
		sLine1 = *iptr1++;

		dVsumv0 = dVsumv1;
		dVsumv1 = Q6_Wh_vadd_VubVub(sLine0, sLine1);
		dVsumv1 = Q6_Wh_vmpyacc_WhVubRb(dVsumv1, sLine0, const2);

		sX0 = Q6_V_lo_W(dVsumv0);
		sX1 = Q6_V_hi_W(dVsumv0);

		sX2 = Q6_V_valign_VVI(Q6_V_lo_W(dVsumv1), Q6_V_lo_W(dVsumv0), 2);

		sX_1X0 = Q6_Vh_vadd_VhVh(sX_1, sX0);
		sX0X1 = Q6_Vh_vadd_VhVh(sX0, sX1);
		sX0X1X1 = Q6_Vh_vadd_VhVh(sX0X1, sX1);

		sSumE = Q6_Vh_vadd_VhVh(sX_1X0, sX0X1);
		sSumO = Q6_Vh_vadd_VhVh(sX0X1X1, sX2);

                //		*optr++ = Q6_Vub_vasr_VhVhR_rnd_sat(sSumO, sSumE, 4);
		*optr++ = Q6_Vub_vasr_VhVhR_sat(sSumO, sSumE, 4);

	}

	if (wRest) {

		sX_1 = Q6_V_vlalign_VVI(Q6_V_hi_W(dVsumv1), Q6_V_hi_W(dVsumv0), 2);
		sX0 = Q6_V_lo_W(dVsumv1);
		sX1 = Q6_V_hi_W(dVsumv1);
		sX2 = Q6_V_valign_VVI(Q6_V_lo_W(dVsumv1), Q6_V_lo_W(dVsumv1), 2);

		sX_1X0 = Q6_Vh_vadd_VhVh(sX_1, sX0);
		sX0X1 = Q6_Vh_vadd_VhVh(sX0, sX1);
		sX0X1X1 = Q6_Vh_vadd_VhVh(sX0X1, sX1);

		sSumE = Q6_Vh_vadd_VhVh(sX_1X0, sX0X1);
		sSumO = Q6_Vh_vadd_VhVh(sX0X1X1, sX2);

                //		*optr = Q6_Vub_vasr_VhVhR_rnd_sat(sSumO, sSumE, 4);
		*optr = Q6_Vub_vasr_VhVhR_sat(sSumO, sSumE, 4);
	}

	// top-left corner
	src0 = imgSrc;
	src1 = src0 + stride;
	imgDst[0] = (uint8_t) ((src0[0] * 9 + src0[1] * 3 + src1[0] * 3 + src1[1])
			>> 4);

	// top-right corner
	src0 = imgSrc + w_2;
	src1 = src0 + stride;
	imgDst[w_1] = (uint8_t) ((src0[0] * 3 + src0[1] * 9 + src1[0] + src1[1] * 3)
			>> 4);

	// Pixel (0, 1)
	imgDst[dstStride] = ((imgSrc[0]) + (imgSrc[stride] << 1)
			+ (imgSrc[2 * stride]) + (imgSrc[0] << 1) + (imgSrc[stride] << 2)
			+ (imgSrc[2 * stride] << 1) + (imgSrc[1])
			+ (imgSrc[stride + 1] << 1) + (imgSrc[2 * stride + 1])) >> 4;

	unsigned char *src2 = (unsigned char*) imgSrc + (height - 1) * stride;
	unsigned char *dst2 = (unsigned char*) imgDst + (height - 1) * dstStride;

	HVX_Vector *iptr3 = (HVX_Vector *) (src2 - (stride));
	HVX_Vector *iptr4 = (HVX_Vector *) (src2);
	HVX_Vector *optr2 = (HVX_Vector *) (dst2);

	sLine0 = *iptr3++;
	sLine1 = *iptr4++;

	dVsumv0 = Q6_W_vcombine_VV(Q6_V_vzero(), Q6_V_vzero());
	dVsumv1 = Q6_Wh_vadd_VubVub(sLine0, sLine1);
	dVsumv1 = Q6_Wh_vmpyacc_WhVubRb(dVsumv1, sLine1, const2);

	for (int i = 0; i < (width - 2) / VLEN; i++) {

		sX_1 = Q6_V_vlalign_VVI(Q6_V_hi_W(dVsumv1), Q6_V_hi_W(dVsumv0), 2);

		sLine0 = *iptr3++;
		sLine1 = *iptr4++;

		dVsumv0 = dVsumv1;
		dVsumv1 = Q6_Wh_vadd_VubVub(sLine0, sLine1);
		dVsumv1 = Q6_Wh_vmpyacc_WhVubRb(dVsumv1, sLine1, const2);

		sX0 = Q6_V_lo_W(dVsumv0);
		sX1 = Q6_V_hi_W(dVsumv0);

		sX2 = Q6_V_valign_VVI(Q6_V_lo_W(dVsumv1), Q6_V_lo_W(dVsumv0), 2);

		sX_1X0 = Q6_Vh_vadd_VhVh(sX_1, sX0);
		sX0X1 = Q6_Vh_vadd_VhVh(sX0, sX1);
		sX0X1X1 = Q6_Vh_vadd_VhVh(sX0X1, sX1);

		sSumE = Q6_Vh_vadd_VhVh(sX_1X0, sX0X1);
		sSumO = Q6_Vh_vadd_VhVh(sX0X1X1, sX2);

                //		*optr2++ = Q6_Vub_vasr_VhVhR_rnd_sat(sSumO, sSumE, 4);
		*optr2++ = Q6_Vub_vasr_VhVhR_sat(sSumO, sSumE, 4);

	}

	if (wRest) {

		sX_1 = Q6_V_vlalign_VVI(Q6_V_hi_W(dVsumv1), Q6_V_hi_W(dVsumv0), 2);
		sX0 = Q6_V_lo_W(dVsumv1);
		sX1 = Q6_V_hi_W(dVsumv1);
		sX2 = Q6_V_valign_VVI(Q6_V_lo_W(dVsumv1), Q6_V_lo_W(dVsumv1), 2);

		sX_1X0 = Q6_Vh_vadd_VhVh(sX_1, sX0);
		sX0X1 = Q6_Vh_vadd_VhVh(sX0, sX1);
		sX0X1X1 = Q6_Vh_vadd_VhVh(sX0X1, sX1);

		sSumE = Q6_Vh_vadd_VhVh(sX_1X0, sX0X1);
		sSumO = Q6_Vh_vadd_VhVh(sX0X1X1, sX2);

                //		*optr2 = Q6_Vub_vasr_VhVhR_rnd_sat(sSumO, sSumE, 4);
		*optr2 = Q6_Vub_vasr_VhVhR_sat(sSumO, sSumE, 4);
	}

	// bottom-left corner
	src0 = imgSrc + stride * (h_2);
	src1 = src0 + stride;
	imgDst[dstStride * (h_1)] = (uint8_t) ((src0[0] * 3 + src0[1] + src1[0] * 9
			+ src1[1] * 3) >> 4);

	// bottom-right corner
	src0 = imgSrc + stride * (h_2) + w_2;
	src1 = src0 + stride;
	imgDst[dstStride * (h_1) + w_1] = (uint8_t) ((src0[0] + src0[1] * 3
			+ src1[0] * 3 + src1[1] * 9) >> 4);


}

void gaussian_hvx(const unsigned char *__restrict imgSrc, unsigned int width,
		unsigned int height, unsigned int stride,
		unsigned char *__restrict imgDst, unsigned int dstStride,
		unsigned int VLEN) {

	HEXAGON_Vect32 const2 = 0x02020202;

	HVX_Vector sLine0, sLine1, sLine2;
	HVX_Vector sX_1, sX0, sX1, sX2, sX0X1, sX0X1X1, sX_1X0;
	HVX_Vector sSumE, sSumO;
	HVX_VectorPair dVsumv0, dVsumv1;

	unsigned char *src = (unsigned char*) imgSrc + stride;
	unsigned char *dst = (unsigned char*) imgDst + dstStride;

	unsigned int wRest = (width - 2) % VLEN;

	for (int j = 0; j < (height-2); j++) {

		HVX_Vector *iptr0 = (HVX_Vector *) (src - (stride));
		HVX_Vector *iptr1 = (HVX_Vector *) (src);
		HVX_Vector *iptr2 = (HVX_Vector *) (src + (stride));
		HVX_Vector *optr = (HVX_Vector *) (dst);

		sLine0 = *iptr0++;
		sLine1 = *iptr1++;
		sLine2 = *iptr2++;

		dVsumv0 = Q6_W_vcombine_VV(Q6_V_vzero(), Q6_V_vzero());
		dVsumv1 = Q6_Wh_vadd_VubVub(sLine0, sLine2);
		dVsumv1 = Q6_Wh_vmpyacc_WhVubRb(dVsumv1, sLine1, const2);

		for (int i = 0; i < (width - 2) / VLEN; i++) {

			sX_1 = Q6_V_vlalign_VVI(Q6_V_hi_W(dVsumv1), Q6_V_hi_W(dVsumv0), 2);

			sLine0 = *iptr0++;
			sLine1 = *iptr1++;
			sLine2 = *iptr2++;

			dVsumv0 = dVsumv1;
			dVsumv1 = Q6_Wh_vadd_VubVub(sLine0, sLine2);
			dVsumv1 = Q6_Wh_vmpyacc_WhVubRb(dVsumv1, sLine1, const2);

			sX0 = Q6_V_lo_W(dVsumv0);
			sX1 = Q6_V_hi_W(dVsumv0);

			sX2 = Q6_V_valign_VVI(Q6_V_lo_W(dVsumv1), Q6_V_lo_W(dVsumv0), 2);

			sX_1X0 = Q6_Vh_vadd_VhVh(sX_1, sX0);
			sX0X1 = Q6_Vh_vadd_VhVh(sX0, sX1);
			sX0X1X1 = Q6_Vh_vadd_VhVh(sX0X1, sX1);

			sSumE = Q6_Vh_vadd_VhVh(sX_1X0, sX0X1);
			sSumO = Q6_Vh_vadd_VhVh(sX0X1X1, sX2);

                        //			*optr++ = Q6_Vub_vasr_VhVhR_rnd_sat(sSumO, sSumE, 4);
			*optr++ = Q6_Vub_vasr_VhVhR_sat(sSumO, sSumE, 4);

		}

		if (wRest) {

			sX_1 = Q6_V_vlalign_VVI(Q6_V_hi_W(dVsumv1), Q6_V_hi_W(dVsumv0), 2);
			sX0 = Q6_V_lo_W(dVsumv1);
			sX1 = Q6_V_hi_W(dVsumv1);
			sX2 = Q6_V_valign_VVI(Q6_V_lo_W(dVsumv1), Q6_V_lo_W(dVsumv1), 2);

			sX_1X0 = Q6_Vh_vadd_VhVh(sX_1, sX0);
			sX0X1 = Q6_Vh_vadd_VhVh(sX0, sX1);
			sX0X1X1 = Q6_Vh_vadd_VhVh(sX0X1, sX1);

			sSumE = Q6_Vh_vadd_VhVh(sX_1X0, sX0X1);
			sSumO = Q6_Vh_vadd_VhVh(sX0X1X1, sX2);

                        //			*optr = Q6_Vub_vasr_VhVhR_rnd_sat(sSumO, sSumE, 4);
			*optr = Q6_Vub_vasr_VhVhR_sat(sSumO, sSumE, 4);

		}

		*dst = ((imgSrc[(j) * stride]) + (imgSrc[(j + 1) * stride] << 1)
				+ (imgSrc[(j + 2) * stride]) + (imgSrc[(j) * stride] << 1)
				+ (imgSrc[(j + 1) * stride] << 2)
				+ (imgSrc[(j + 2) * stride] << 1) + (imgSrc[(j) * stride + 1])
				+ (imgSrc[(j + 1) * stride + 1] << 1)
				+ (imgSrc[(j + 2) * stride + 1])) >> 4;





		dst += (width - 1);

		*dst++ = (imgSrc[stride * (j) + (width - 1)]
				+ (imgSrc[stride * (j + 1) + (width - 1)] << 1)
				+ imgSrc[stride * (j + 2) + (width - 1)]
				+ (imgSrc[stride * (j) + (width - 1)] << 1)
				+ (imgSrc[stride * (j + 1) + (width - 1)] << 2)
				+ (imgSrc[stride * (j + 2) + (width - 1)] << 1)
				+ (imgSrc[stride * (j) + (width - 2)])
				+ (imgSrc[stride * (j + 1) + (width - 2)] << 1)
				+ imgSrc[stride * (j + 2) + (width - 2)])>> 4;



		src += stride;
	}

}


