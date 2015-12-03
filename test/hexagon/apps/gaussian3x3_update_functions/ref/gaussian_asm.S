

 .file       "gaussian_asm.S"

#define Gaussian3x3_CALLEE_SAVED_REG_SIZE    7 * 8
#define Gaussian3x3_CALLEE_SAVED_REG_OFFSET  0
#define Gaussian3x3_STACK_SIZE               (Gaussian3x3_CALLEE_SAVED_REG_OFFSET + Gaussian3x3_CALLEE_SAVED_REG_SIZE)

   .text
   .p2align 4						   // ensures 16-byte alignment of first packet
   .globl gaussian3x3						   // makes function have global scope
   .type       gaussian3x3, @function
gaussian3x3:
   {
	 P0 = CMP.GT(R1,#2)				   // width>2
	 P1 = CMP.GT(R2,#2)				   // height>2
	 R8 = #4
	 M0 = R3						   // M0 = srcStride -- used to increment src pointer
   }{
	 P0 = AND(P0,P1)				   // if !((width>2)&&(height>2))
	 IF !P0.new JUMPR:nt R31		   // then return
   }{
	 ALLOCFRAME(#Gaussian3x3_STACK_SIZE)	   // prepare the stack
	 P0 = CMP.GT(R1,#4)				   // if srcWidth > 4
	 R8 -= ASL(R3,#1)				   // 4 - 2*srcStride
   }{
	 MEMD(R29+#(5*8)) = R27:26		   // save callee-saved registers onto stack
	 MEMD(R29+#(6*8)) = R31:30		   // save callee-saved registers onto stack
	 R26 = SUB(R3,R1)				   // srcStride - srcWidth
	 M1 = R8						   // M1 = 4-2*srcStride -- used to increment src pointer.
   }{
	 MEMD(R29+#(2*8)) = R21:20		   // save callee-saved registers onto stack
	 MEMD(R29+#(3*8)) = R23:22		   // save callee-saved registers onto stack
	 R30 = ASR(R1,#2)
	 R31 = ADD(R2,#0)				   // h = srcHeight -- used to loop over rows
   }{
	 MEMD(R29+#(4*8)) = R25:24		   // save callee-saved registers onto stack
	 MEMD(R29+#(0*8)) = R17:16		   // save callee-saved registers onto stack
	 R25 = ADD(R4,R5)				   // dstImg pointer for writing.
//	 R22 = insert(R1,#16,#16)		   // R23:22 is source value used in L2FETCH instruction to specify box prefetch size.
   }{
	 R30 = ADD(R30,#-1)				   // w4 = srcWidth/4 - 1  -- used to loop through pixels in a row (we are calculating them in groups of 4 pixels)
	 R23 = #0
	 R22.L = #6
	 R24 = ADD(R0, R3)
   }{
	 R28 = neg(R3)
   }{
	 MEMD(R29+#(1*8)) = R19:18		   // save callee-saved registers onto stack
	 R27 = SUB(R5,R1)				   // dstStride - srcWidth -- used to increment dstImg pointer at the end of outer loop
	 R23 = insert(R3, #16,#0)		   // R23:22 = [clear (63:48) | srcStride (47:32) | srcWidth (31:16) | srcHeight (15:0)] -- used for L2FETCH
	 LOOP1(.gaussian3x3_OUTER_LOOP,R31)
   }


   .falign
.gaussian3x3_OUTER_LOOP:
   {								   // call inner loop w4 times, and then process last group of columns, and increment pointers accordingly.
									   // process pixels in groups of 4, then increment pointers accordingly.
	// L2FETCH(R0,R23:22)				   // fetch the rows in srcImg that will be used in this outer loop iteration as well as the next one.
	 R15 = ADD(R25,#31)				   // dst + 31
	 R14 = ADD(R25,R1)
	 R1 = R0
   }{
	 R5:4 = MEMUBH(R0++M0)			   // load src0
	 R15 = AND(R15,#-32)			   // left edge of first full cache line in dst row
   }{
	 R7:6 = MEMUBH(R0++M0)			   // load src1
	 R14 = SUB(R14,R15)				   // number of bytes in dst row to right of first cache line edge
   }{
	 R5:4 += ASL(R7:6,#1)			   // src0 + 2*src1
	 R7:6 = MEMUBH(R0++M1)			   // load src2
	 P2 = CMP.GT(R14,#31)			   // at least 1 full cache line in dst row?
	 R14 = ASR(R14,#5)				   // number of full cache lines in dst row
   }{
	 R9:8 = VADDH(R5:4,R7:6)		   // col3:0
	 IF (!P2) JUMP .gaussian3x3_skip_dczeroa // skip dczeroa loop
   }

   /* {
	LOOP0(.down2_dczeroa, R14)		   // loop (number of cache lines)
   }

   .falign
.down2_dczeroa:
   {
	// DCZEROA(R15)					   // allocate cache line
	 //R15 = ADD(R15,#32)				   // advance to next cache line
   }:endloop0
   */

   .falign
.gaussian3x3_skip_dczeroa:
   {
	 IF (!P0) JUMP .gaussian3x3_OUTER_LOOP_part3 // if !(width > 4) then skip inner loop, and outer loop part
	 R20 = R25
	 P3=SP2LOOP0(.gaussian3x3_INNER_LOOP,R30)
   }

   .falign
.gaussian3x3_INNER_LOOP:
   {
	 R5:4 = MEMUBH(R0++M0)			   // load src0
	 R7:6 = VALIGNB(R9:8,R11:10,#2)	   // col4:1
	 R14 = R11						   // col3:2
	 R17 = VTRUNEHB(R13:12)			   // truncate to 8-bit
   }{
	 R7:6 = MEMUBH(R0++M0)			   // load src1
	 R11:10 += ASL(R7:6,#1)			   // col3:0 + 2*col4:1
	 R15 = R8						   // R15:14 = col5:2
	 R3:2 = LSR(R17:16,#24)			   // align output
   }{
	 R5:4 += ASL(R7:6,#1)			   // src0 + 2*src1
	 R7:6 = MEMUBH(R0++M1)			   // load src2
	 R11:10 = VADDH(R11:10,R15:14)	   // sum3:0
	 IF P3 MEMW(R25++#4) = R2		   // Write output to dstImg
   }{
	 R11:10 = R9:8					   // new col3:0
	 R9:8 = VADDH(R5:4,R7:6)		   // col7:4
	 R13:12 = VASRH(R11:10,#4)		   // sum3:0 >> 4
	 R16 = R17
   }:endloop0


   .falign
.gaussian3x3_OUTER_LOOP_part2:
   {
	 R7:6 = VALIGNB(R9:8,R11:10,#2)	   // col4:1
	 R14 = R11						   // col3:2
	 R17 = VTRUNEHB(R13:12)			   // truncate to 8-bit
   }{
	 R11:10 += ASL(R7:6,#1)			   // col3:0 + 2*col4:1
	 R15 = R8						   // R15:14 = col5:2
	 R3:2 = LSR(R17:16,#24)			   // align output
   }{
	 R5:4 += ASL(R7:6,#1)			   // src0 + 2*src1
	 R11:10 = VADDH(R11:10,R15:14)	   // sum3:0
	 IF P3 MEMW(R25++#4) = R2		   // Write output to dstImg
   }{
	 R11:10 = R9:8					   // new col3:0
	 R9:8 = VADDH(R5:4,R7:6)		   // col7:4
	 R13:12 = VASRH(R11:10,#4)		   // sum3:0 >> 4
	 R16 = R17
   }

   .falign
.gaussian3x3_OUTER_LOOP_part3:
   {
	 IF (!P0) R11:10 = R9:8
   }{
	 R7:6 = VALIGNB(R9:8,R11:10,#2)	   // col4:1
	 R14 = R11						   // col3:2
	 R17 = VTRUNEHB(R13:12)			   // truncate to 8-bit
   }{
	 R11:10 += ASL(R7:6,#1)			   // col3:0 + 2*col4:1
	 R15 = R8						   // R15:14 = col5:2
	 R3:2 = LSR(R17:16,#24)			   // align output
   }{
	 R11:10 = VADDH(R11:10,R15:14)	   // sum3:0
	 IF P0 MEMW(R25++#4) = R2		   // Write output to dstImg only if w4>1
   }{
	 R13:12 = VASRH(R11:10,#4)		   // sum3:0 >> 4
	 R16 = R17
   }{
	 R17 = VTRUNEHB(R13:12)
	 R24 = ADD(R24, #-2)
	 R12 = ADD(R24, #-1)			   // truncate to 8-bit
   }{
	 R3:2 = LSR(R17:16,#24)
	 R5 = MEMUB(R24++M0)
	 R7 = MEMUB(R12++M0) 			    // Align output
   }{
	 MEMW(R25) = R2					   // Write output to dstImg
	 R21 = ADD(R25, #3)
	 R25 += ADD(R27,#4)
	 R0 = ADD(R0,R26)
   }{
	 R6 = MEMUB(R24++M0)
	 R9 = MEMUB(R12++M0)
	 R5:4 = vzxtbh(R5)
	 R3:2 = vzxtbh(R7)
   }{
	 R10 = MEMUB(R24)
	 R11 = MEMUB(R12)
	 R7:6 = vzxtbh(R6)
	 R9:8 = vzxtbh(R9)
   }{
	 R13:12 = vzxtbh(R10)
	 R11:10 = vzxtbh(R11)
   }{
	 R7:6 = ASL(R7:6, #1)
	 R5:4 += ASL(R3:2, #1)
   }{
	 R13:12 += ASL(R11:10, #1)
	 R7:6 += ASL(R9:8, #2)
   }{
	 R13:12 = VADDH(R13:12, R11:10)
	 R5:4 = VADDH(R5:4, R3:2)

	 R17 = ADD(R1, #1)
   }{
	 R3 = MEMUB(R1++M0)
	 R7:6 += ASL(R9:8, #1)
	 R5:4 = VADDH(R5:4, R13:12)
   }{
	 R3:2 = vzxtbh(R3)
	 R5:4 = VADDH(R7:6, R5:4)
   }{
	 R13 = MEMUB(R17++M0)
	 R5:4 = VASRH(R5:4,#4)
   }{
	 R13:12 = vzxtbh(R13)
	 MEMB(R21) = R4
	 R24 = ADD(R24, R28)
   }{
	 R6 = MEMUB(R1++M0)
	 R9 = MEMUB(R17++M0)
	 R24 = ADD(R24, #2)
   }{
	 R10 = MEMUB(R1)
	 R11 = MEMUB(R17)
	 R7:6 = vzxtbh(R6)
	 R9:8 = vzxtbh(R9)
   }{
	 R5:4 = vzxtbh(R10)
	 R11:10 = vzxtbh(R11)
   }{
	 R9:8 = ASL(R9:8, #1)
	 R3:2 += ASL(R3:2, #1)
   }{
	 R5:4 += ASL(R5:4, #1)
	 R13:12 = VADDH(R13:12, R3:2)
   }{
	 R11:10 = VADDH(R5:4, R11:10)
	 R9:8 += ASL(R7:6, #2)
   }{
	 R9:8 += ASL(R7:6, #1)
	 R13:12 = VADDH(R13:12, R11:10)
   }{
	 R13:12 = VADDH(R13:12, R9:8)
   }{
	 R13:12 = VASRH(R13:12,#4)

   }{
	 MEMB(R20) = R12
   }:endloop1

   {
	 R17:16 = MEMD(R29+#(0*8))		   // restore callee-saved registers
	 R19:18 = MEMD(R29+#(1*8))		   // restore callee-saved registers
   }{
	 R21:20 = MEMD(R29+#(2*8))		   // restore callee-saved registers
	 R23:22 = MEMD(R29+#(3*8))		   // restore callee-saved registers
   }{
	 R25:24 = MEMD(R29+#(4*8))		   // restore callee-saved registers
	 R31:30 = MEMD(R29+#(6*8))		   // restore callee-saved registers
   }{
	 R27:26 = MEMD(R29+#(5*8))		   // restore callee-saved registers
	 DEALLOC_RETURN					   // return
   }
   .size       gaussian3x3, .-gaussian3x3




#define Gaussian3x3_HVX_CALLEE_SAVED_REG_SIZE    4 * 8
#define Gaussian3x3_HVX_CALLEE_SAVED_REG_OFFSET  0
#define Gaussian3x3_HVX_STACK_SIZE               (Gaussian3x3_CALLEE_SAVED_REG_OFFSET + Gaussian3x3_HVX_CALLEE_SAVED_REG_SIZE)
#define Gaussian3x3_HVX_VLEN_OFFSET              (0)

   .text
   .p2align 4						   // ensures 16-byte alignment of first packet
   .globl gaussian3x3_hvx					   // makes function have global scope
   .type  gaussian3x3_hvx, @function
gaussian3x3_hvx:
   {
      ALLOCFRAME(#Gaussian3x3_HVX_STACK_SIZE) // Allocates stack space for callee saved registers
	  R15 = MEMW(R29)				          // R15 = VLEN
	  R0 = ADD(R0, R3)				          // src1 = src + srcStride
	  R4 = ADD(R4, R5)			              // dst = dst + dstStride
   }{
   	  P0 = CMP.EQ(R15, #128)                  // Checks if VLEN is 128 byte vector mode
   	  P1 = CMP.EQ(R15, #64)                   // Checks if VLEN is 64 byte vector mode
   	  R6 = LSR(R1, #7)                        // R6 is Width / VLEN (for 128 byte mode)
   	  R7 = LSR(R1, #6)                        // R7 is Width/VLEN (for 64 byte mode)
   }{
	  MEMD(R29+#(0*8)) = R17:16               // Save callee saved register
	  R6 = SUB(R0,R3)				          // R6 = src0 = src1 - srcStride
	  IF P1 R12 = R7                          // If 128 byte mode sets R12 to be correct Width/VLEN
	  IF P0 R12 = R6                          // If 64 byte mode sets R12 to be correct Width/VLEN
   }{
	  R10 = ##0x02020202                      // R10 = 2 each byte constant for multiplication
   	  P0 = CMP.EQ(R12, #1)                    // IF R12 = 1 aka width is 128 or 64 will be used to skip a write out in main loop
   	  R9 = ADD(R0,R3)                         // R9 = src1 + srcStride
   }{
   	 MEMD(R29+#(2*8)) = R21:20                // Save callee saved register
   	 MEMD(R29+#(3*8)) = R23:22                // Save callee saved register
   	 R2 = ADD(R2, #-2)                        // R2 = height - 2, if multi-threading comment this out
   	 R7 = #4                                  // R7 = 4, constant for division later
   }{
     R5 = MPYI(R3, #2)                        // R5 = stride * 2 used for borders later
     MEMD(R29+#(1*8)) = R19:18                // Save callee saved register
   	 LOOP1(.gaussian3x3_hvx_OuterLoop, R2)    // Loop outer loop x height - 2 for single-threaded or x height for multi-threaded
   }

  .falign
 .gaussian3x3_hvx_OuterLoop:
 {
   R13 = R6                                   // R13 = R6 = src line0
   R8 = R0                                    // R8 = R0 = src line 1
   R1 = R4                                    // R1 = current dst
   P3 = SP3LOOP0(.gaussian3x3_hvx_InnerLoop, R12)  // LOOP by width/VLEN this loop handles an output of size width each iteration
 }

   .falign
.gaussian3x3_hvx_InnerLoop:
   {
	 V0 = VMEM(R6++#1)				   //[1] // V0 = line 0
	 V6 = VLALIGN(V5,V3,#2)			   //[1] // V6 = last halfword of V3 followed by all halfwords in V5 except the last one (offsets it)
	 V8.h = VADD(V6.h,V2.h)			   //[2] // V8 = V6 (previous vlalign) + V2 (previous col sum low)
	 V9.h  = VADD(V2.h,V3.h)		   //[2]  // V9 = V2 col sum low + V3 col sum high
   }{
	 V3:2 = V5:4					   //[1] // dVsumv1 = dVsumv0
	 V7 = VALIGN(V4,V2,#2)			   //[2] // V7 = all halfwords in V2 except the first one with the last halfword from the first halfword in V4 (offsets it)
	 V10.h = VADD(V9.h,V3.h)		   //[2] // V10 = V9 + V3 col sum high (row 2 * 2)
          
   }{
	 V1.tmp = VMEM(R9++#1)			   //[1] // V1 = line 2
	 V5:4.h = VADD(V0.ub,V1.ub)		   //[1] // V5:4 halfwords = widening add of line 0 + line 2 bytes
	 V14.ub = VASR(V12.h,V13.h, R7):sat //[3] // V15 = shifts right halfwords of V12 = (sSumO) and V13 = (sSumE) by 4 and interleaves them as bytes in V14
   }{
   	 IF P3 VMEM(R4++#1) = V14           // IF has loop 3 times or more writes V14 out to putput
   	 V12.h = VADD(V10.h,V7.h)           // V12 = V10 + V7
   	 V13.h = VADD(V8.h,V9.h)            // V13 = V8 + V9
   }{
	 V1.tmp = VMEM(R0++#1)			   //[1] // V1 = src line 1
	 V5:4.h+= VMPY(V1.ub,R10.b)		   //[1] // V5:4 = line 0 + line 2 + 2 * line 1
   }:endloop0

  // Now finishes the output of the current loop that finished but did not output

   {
	 V6 = VLALIGN(V5,V3,#2)			   //[1]
	 V8.h = VADD(V6.h,V2.h)			   //[2]
	 V9.h  = VADD(V2.h,V3.h)		   //[2]
   }{
	 V3:2 = V5:4					   //[1]
	 V7 = VALIGN(V4,V2,#2)			   //[2]
	 V10.h = VADD(V9.h,V3.h)		   //[2]
   }{
	 V14.ub = VASR(V12.h, V13.h, R7):sat //[3]
	 IF P3 VMEM(R4++#1) = V14.new	   //[3]
	 V12.h = VADD(V10.h,V7.h)		   //[2]
	 V13.h = VADD(V8.h,V9.h)		   //[2]
   }
   //====== epilogue ======
   {
	 V8.h = VADD(V6.h,V2.h)			   //[2]
	 V9.h  = VADD(V2.h,V3.h)		   //[2]
   }{
	 V7 = VALIGN(V4,V2,#2)			   //[2]
	 V10.h = VADD(V9.h,V3.h)		   //[2]
   }{
	 V14.ub = VASR(V12.h,V13.h,R7):sat //[3]
     IF (!P0) VMEM(R4++#1) =V14.new			   //[3]
	 V12.h = VADD(V10.h,V7.h)		   //[2]
	 V13.h = VADD(V8.h,V9.h)		   //[2]
   }{
	 V14.ub = VASR(V12.h,V13.h,R7):sat //[3]
	 VMEM(R4++#1) = V14.new
   }{
	 R2 = MEMUB(R13++#1)               // R2 = src0[0] for border left col
	 R7 = MEMUB(R8++#1)                // R7 = src1[0] for border left col
	 R11 = ADD(R8, R3)                 // R11 = address of src2
   }{
	 R10 = MEMUB(R11++#1)              // R10 = src2[0]
	 R14 = MEMUB(R13)                  // R14 = src0[1]
	 R17:16 = vzxtbh(R2)               // R17:16 = R2 with bytes widened to halfwords
	 R19:18 = vzxtbh(R7)               // R19:18 = R7 with bytes widened to halfwords
   }{
	 R14 = MEMUB(R8)                   // R14 = src1[1]
	 R15 = MEMUB(R11)                  // R15 = src2[1]
	 R21:20 = vzxtbh(R10)              // R21:20 = R10 with bytes widened to halfwords
	 R23:22 = vzxtbh(R14)              // R23:22 = R14 with bytes widened to halfwords
   }{
	 R17:16 += ASL(R17:16, #1)         // R17:16 = src0[0] * 3
	 R19:18 = ASL(R19:18, #1)          // R19:18 = src1[0] * 2
	 R7 = ADD(R3, #-3)                 // R7 = stride - 3
   }{
	 R19:18 += ASL(R19:18, #1)         // R19:18 = (src1[0] * 2) * 3
	 R21:20 += ASL(R21:20, #1)         // R21:20 = src2[0] * 3
   }{
	 R17:16 = VADDH(R17:16, R19:18)    // R17:16 = (src0[0] * 3) + (src1[0]*6)
	 R13 = ADD(R7, R13)                // R13 = address of src0[srcWidth-2]
	 R8 = ADD(R8, R7)                  // R8 = address of src1[srcWidth-2]
	 R11 = ADD(R11, R7)                // R11 = address of src2[srcWidth-2]
   }{
	 R17:16 = VADDH(R17:16, R21:20)    // R17:16 = (src0[0] * 3) + (src1[0]*6)  + (src2[0]*3)
	 R19:18 = vzxtbh(R14)              // R19:18 = R14 with bytes widened to halfwords
	 R2 = MEMUB(R13++#1)               // R2 = src0[srcWidth-2]
	 R7 = MEMUB(R8++#1)                // R7 = src1[srcWidth-2]
   }{
	 R19:18 = ASL(R19:18, #1)          // R18:18 = src1[1] * 2
	 R21:20 = vzxtbh(R15)              // R21:20 =  R15 with bytes widened to halfwords
	 R10 = MEMUB(R11++#1)              // R10 = src2[srcWidth-2]
	 R14 = MEMUB(R13)                  // R14 = src0[srcWidth-1]
   }{
	 R17:16 = VADDH(R17:16, R23:22)    // R17:16 = (src0[0] * 3) + (src1[0]*6)  + (src2[0]*3) + (src0[1])
	 R23:22 = vzxtbh(R10)              // R23:22 = R10 with bytes widened to halfwords
   }{
     R17:16 = VADDH(R17:16, R19:18)    // R17:16 = (src0[0] * 3) + (src1[0]*6)  + (src2[0]*3) + (src0[1]) + (src1[1] * 2)
     R19:18 = vzxtbh(R2)               // R19:18 = R2 with bytes widened to halfwords
   }{
	 R17:16 = VADDH(R17:16, R21:20)    // R17:16 = (src0[0] * 3) + (src1[0]*6)  + (src2[0]*3) + (src0[1]) + (src1[1] * 2) + src2[1]
	 R21:20 = vzxtbh(R7)               // R21:20 = R7 with bytes widened to halfwords
	 R15 = MEMUB(R11)                  // R15 = src2[srcWidth-1]
   }{
	 R17:16 = VASRH(R17:16,#4)         // R17:16 = R17:16 / 16
   }{
	 MEMB(R1) = R16                    // Outputs R16 to pixel (0, y)
	 R17:16 = vzxtbh(R14)              // R17:16 = R14 with bytes widened to halfwords
	 R14 = MEMUB(R8)                   // R14 = src1[srcWidth-1]
   }{
	 R21:20 = ASL(R21:20, #1)          // R21:20 = src1[srcWidth-2] * 2
	 R17:16 += ASL(R17:16, #1)         // R17:16 = src0[srcWidth-1] * 3
   }{
	 R19:18 = VADDH(R19:18, R21:20)    // R19:18 = src0[srcWidth-2] + (src1[srcWidth-2] * 2)
   }{
	 R19:18 = VADDH(R19:18, R23:22)    // R19:18 = src0[srcWidth-2] + (src1[srcWidth-2] * 2) + (src2[srcWidth-2])
	 R21:20 = vzxtbh(R14)              // R21:20 = R14 with bytes widened to halfwords
   }{
	 R21:20 = ASL(R21:20, #1)          // R21:20 = src1[srcWidth-1] * 2
	 R23:22 = vzxtbh(R15)              // R23:22 = R15 bytes widened to halfwords
   }{
     R21:20 += ASL(R21:20, #1)         // R21:20 = (src1[srcWidth-1] * 2)*3
	 R19:18 = VADDH(R19:18, R17:16)    // R19:18 = src0[srcWidth-2] + (src1[srcWidth-2] * 2) + (src2[srcWidth-2]) + (src0[srcWidth-1] * 3)
   }{
     R23:22 += ASL(R23:22, #1)         // R23:22 = src2[srcWidth-1] * 3
     R19:18 = VADDH(R19:18, R21:20)    // R19:18 = src0[srcWidth-2] + (src1[srcWidth-2] * 2) + (src2[srcWidth-2]) + (src0[srcWidth-1] * 3) + (src1[srcWidth-1] * 6)
     R1 = ADD(R1, R3)                  // R1 = dst(0, y) + stride -> dst(0, y+1)
   }{
	 R19:18 = VADDH(R19:18, R23:22)    // R19:18 = src0[srcWidth-2] + (src1[srcWidth-2] * 2) + (src2[srcWidth-2]) + (src0[srcWidth-1] * 3) + (src1[srcWidth-1] * 6) + (src2[srcWidth-1]*3)
	 R1 = ADD(R1, #-1)                 // R1 = dst(0, y+1) - 1 -> dst(dstWidth-1, y)
   }{
	 R19:18 = VASRH(R19:18,#4)         // R19:18 = R19:18 / 16
   }{
	 MEMB(R1) = R18                    // output R18 to dst(dstWidth-1, y)
   	 R10 = ##0x02020202                // Reinitialize constants for next inner loop
   	 R7 = #4
   }:endloop1

   {
   	R23:22 = MEMD(R29+#(3*8))
   }
   {
     R21:20 = MEMD(R29+#(2*8))
     R17:16 = MEMD(R29+#(0*8))		   // restore callee-saved registers
   }{
	 R19:18 = MEMD(R29+#(1*8))		   // restore callee-saved registers
	 DEALLOC_RETURN					   // return
   }
   .size      .gaussian3x3_hvx, ..gaussian3x3_hvx

#define Gaussian3x3_HVX_CALLEE_SAVED_REG_SIZE    4 * 8
#define Gaussian3x3_HVX_CALLEE_SAVED_REG_OFFSET  0
#define Gaussian3x3_HVX_STACK_SIZE               (Gaussian3x3_CALLEE_SAVED_REG_OFFSET + Gaussian3x3_HVX_CALLEE_SAVED_REG_SIZE)
#define Gaussian3x3_HVX_VLEN_OFFSET              (0)

   .text
   .p2align 4						   // ensures 16-byte alignment of first packet
   .globl gaussian3x3_hvx_borders					   // makes function have global scope
   .type  gaussian3x3_hvx_borders, @function
gaussian3x3_hvx_borders:
   {
      ALLOCFRAME(#Gaussian3x3_HVX_STACK_SIZE)
          R15 = MEMW(R29)				          // R15 = VLEN
          R0 = ADD(R0, R3)                        // R0 = src + stride
          R2 = ADD(R2, #-2)				          // R2 = srcHeight - 2 used to get to bottom borders
   }{
   	  P0 = CMP.EQ(R15, #64)                   // P0 checks if 64 byte vector mode
   	  R12 = LSR(R1, #7)                       // R12 = (srcWidth/VLEN) for 128 byte vector mode (default?)
   	  R6 = SUB(R0,R3)				          // R6 = src0 = src1 - srcStride
          R11 = LSR(R1, #6)                       // R11 = (srcWidth/VLEN) for 64 byte vector mode
   }{
          MEMD(R29+#(0*8)) = R17:16               // Saves callee saved register
          R9 = ADD(R0,R3)                         // R9 = src2 = src1 + stride
          IF P0 R12 = R11                         // sets R12 to correct inner loop count if64 byte vector mode
   }{
          R10 = ##0x02020202                      // R10 = 2's each byte for multiplication
   	  P0 = CMP.EQ(R12, #1)                    // If R12 is 1 will skip one write out later
          R7 = #4                                 // R7 = 4 constant for division
   }{
   	  MEMD(R29+#(2*8)) = R21:20               // Saves callee saved registers
   	  MEMD(R29+#(3*8)) = R23:22               // Saves collee saved registers
   	  R21 = ADD(R5, #-1)                      // R21 = dstStride - 1
   	  R20 = R4                                // R20 = dst(0, 0)
   }{                   // R5
      MEMD(R29+#(1*8)) = R19:18               // Saves callee saved register
      R18 = R6                                // R18 = src0
   	  R22 = MPYI(R3, R2)                      // R22 = srcStride * 2
   	  R23 = R12                               // R23 = inner loop count
   }
        {
    P3 = SP3LOOP0(.gaussian3x3_hvx_borders_InnerLoop, R12)
}

//Same loop as in gaussian3x3_hvx main loop but repeats src 0 (weights it as double)
   .falign
.gaussian3x3_hvx_borders_InnerLoop:
   {
	 V0 = VMEM(R6)				   //[1]
	 V6 = VLALIGN(V5,V3,#2)			   //[1]
	 V8.h = VADD(V6.h,V2.h)			   //[2]
	 V9.h  = VADD(V2.h,V3.h)		   //[2]
   }{
	 V3:2 = V5:4					   //[1]
	 V7 = VALIGN(V4,V2,#2)			   //[2]
	 V10.h = VADD(V9.h,V3.h)		   //[2]
   }{
	 V1.tmp = VMEM(R0++#1)			   //[1]
	 V5:4.h = VADD(V0.ub,V1.ub)		   //[1]
	 V14.ub = VASR(V12.h,V13.h, R7):sat //[3]
   }{
   	 IF P3 VMEM(R4++#1) = V14
   	 V12.h = VADD(V10.h,V7.h)
   	 V13.h = VADD(V8.h,V9.h)
   }{
	 V1.tmp = VMEM(R6++#1)			   //[1]
	 V5:4.h+= VMPY(V1.ub,R10.b)		   //[1]
   }:endloop0

   {
	 V6 = VLALIGN(V5,V3,#2)			   //[1]
	 V8.h = VADD(V6.h,V2.h)			   //[2]
	 V9.h  = VADD(V2.h,V3.h)		   //[2]
   }{
	 V3:2 = V5:4					   //[1]
	 V7 = VALIGN(V4,V2,#2)			   //[2]
	 V10.h = VADD(V9.h,V3.h)		   //[2]
   }{
	 V14.ub = VASR(V12.h, V13.h, R7):sat // [3]
	 IF P3 VMEM(R4++#1) = V14.new	     // [3]
	 V12.h = VADD(V10.h,V7.h)		   //  [2]
	 V13.h = VADD(V8.h,V9.h)		   //  [2]
   }
   //====== epilogue ======
   {
	 V8.h = VADD(V6.h,V2.h)			   // [2]
	 V9.h  = VADD(V2.h,V3.h)		   // [2]
   }{
	 V7 = VALIGN(V4,V2,#2)			   // [2]
	 V10.h = VADD(V9.h,V3.h)		   // [2]
   }{
	 V14.ub = VASR(V12.h,V13.h,R7):sat // [3]
     IF (!P0) VMEM(R4++#1) = V14.new   // [3]
	 V12.h = VADD(V10.h,V7.h)		   // [2]
	 V13.h = VADD(V8.h,V9.h)		   // [2]
   }{
	 V14.ub = VASR(V12.h,V13.h,R7):sat //[3]
	 VMEM(R4++#1) = V14.new
   }


//Handles dst output (0, 0) and dst(dstWidth-1, 0). Repeats edge similar to code in gaussian3x3_hvx left and right borders
     .falign
.down2_hvx_borders_corners:
   {
	 R8 = ADD(R18, R3)
	 M0 = R21
	 R17 = ADD(R3, #-3)
	 R19 = R6
   }{
	 R5 = MEMUB(R18++#1)
	 R3 = MEMUB(R8++#1)
   }{
	 R6 = MEMUB(R18)
	 R9 = MEMUB(R8)
	 R5:4 = vzxtbh(R5)
	 R3:2 = vzxtbh(R3)
   }{
	 R13:12 = vzxtbh(R6)
	 R11:10 = vzxtbh(R9)
   }{
	 R5:4 += ASL(R5:4, #3)
	 R13:12 += ASL(R13:12, #1)
   }{
	 R3:2 += ASL(R3:2, #1)
	 R5:4 = VADDH(R5:4, R13:12)
   }{
	 R5:4 = VADDH(R5:4, R3:2)
	 R18 = ADD(R18, R17)
   }{
	 R5:4 = VADDH(R5:4, R11:10)
	 R6 = MEMUB(R18++#1)
	 R8 = ADD(R8, R17)
   }{
	 R9 = MEMUB(R8++#1)
	 R7:6 = vzxtbh(R6)
	 R5:4 = VASRH(R5:4,#4)
   }{
	 R11:10 = vzxtbh(R9)
	 R5 = MEMUB(R18)
	 MEMB(R20++M0) = R4
   }{
	 R3 = MEMUB(R8)
	 R5:4 = vzxtbh(R5)
   }{
	 R3:2 = vzxtbh(R3)
	 R5:4 += ASL(R5:4, #3)
   }{
	 R7:6 += ASL(R7:6, #1)
	 R3:2 += ASL(R3:2, #1)
	 R17 = ADD(R17, #3)
   }{
	 R5:4 = VADDH(R5:4, R7:6)
	 R6 = ADD(R19, R22)
   }{
	 R5:4 = VADDH(R5:4, R11:10)
	 R0 = SUB(R6, R17)
   }{
	 R5:4 = VADDH(R5:4, R3:2)
	 R18 = R0
   }{
	 R5:4 = VASRH(R5:4,#4)
	 R10 = ##0x02020202
   }{
	 MEMB(R20++#1) = R4
	 R7 = #4
   }{
     R4 = ADD(R20, R22)
   }{
   	 R20 = R4
   	 P3 = SP3LOOP0(.gaussian3x3_hvx_bottom_InnerLoop, R23)
   }

// Handles output to bottom line border. Similar to top line inner loop. Weights bottom src line double
    .falign
.gaussian3x3_hvx_bottom_InnerLoop:
   {
	 V0 = VMEM(R6)				   //[1]
	 V6 = VLALIGN(V5,V3,#2)			   //[1]
	 V8.h = VADD(V6.h,V2.h)			   //[2]
	 V9.h  = VADD(V2.h,V3.h)		   //[2]
   }{
	 V3:2 = V5:4					   //[1]
	 V7 = VALIGN(V4,V2,#2)			   //[2]
	 V10.h = VADD(V9.h,V3.h)		   //[2]
   }{
	 V1.tmp = VMEM(R0++#1)			   //[1]
	 V5:4.h = VADD(V0.ub,V1.ub)		   //[1]
	 V14.ub = VASR(V12.h,V13.h, R7):sat //[3]
   }{
   	 IF P3 VMEM(R4++#1) = V14
   	 V12.h = VADD(V10.h,V7.h)
   	 V13.h = VADD(V8.h,V9.h)
   }{
	 V1.tmp = VMEM(R6++#1)			   //[1]
	 V5:4.h+= VMPY(V1.ub,R10.b)		   //[1]
   }:endloop0

   {
	 V6 = VLALIGN(V5,V3,#2)			   //[1]
	 V8.h = VADD(V6.h,V2.h)			   //[2]
	 V9.h  = VADD(V2.h,V3.h)		   //[2]
   }{
	 V3:2 = V5:4					   //[1]
	 V7 = VALIGN(V4,V2,#2)			   //[2]
	 V10.h = VADD(V9.h,V3.h)		   //[2]
   }{
	 V14.ub = VASR(V12.h, V13.h, R7):sat // [3]
	 IF P3 VMEM(R4++#1) = V14.new	     // [3]
	 V12.h = VADD(V10.h,V7.h)		   //  [2]
	 V13.h = VADD(V8.h,V9.h)		   //  [2]
   }
   //====== epilogue ======
   {
	 V8.h = VADD(V6.h,V2.h)			   // [2]
	 V9.h  = VADD(V2.h,V3.h)		   // [2]
   }{
	 V7 = VALIGN(V4,V2,#2)			   // [2]
	 V10.h = VADD(V9.h,V3.h)		   // [2]
   }{
	 V14.ub = VASR(V12.h,V13.h,R7):sat // [3]
     IF (!P0) VMEM(R4++#1) = V14.new   // [3]
	 V12.h = VADD(V10.h,V7.h)		   // [2]
	 V13.h = VADD(V8.h,V9.h)		   // [2]
   }{
	 V14.ub = VASR(V12.h,V13.h,R7):sat //[3]
	 VMEM(R4++#1) = V14.new
   }


 // Handles dst output (0, dstHeight-1) and dst(dstWidth-1, dstHeight-1). Repeats edge similar to code in gaussian3x3_hvx left and right borders
    .falign
.down2_hvx_bottom_corners:
   {
	 R8 = ADD(R18, R17)
	 M0 = R21
	 R17 = ADD(R17, #-3)
   }{
	 R5 = MEMUB(R18++#1)
	 R3 = MEMUB(R8++#1)
   }{
	 R6 = MEMUB(R18)
	 R9 = MEMUB(R8)
	 R5:4 = vzxtbh(R5)
	 R3:2 = vzxtbh(R3)
   }{
	 R13:12 = vzxtbh(R6)
	 R11:10 = vzxtbh(R9)
   }{
	 R5:4 += ASL(R5:4, #1)
	 R3:2 += ASL(R3:2, #3)
   }{
	 R11:10 += ASL(R11:10, #1)
	 R5:4 = VADDH(R5:4, R13:12)
   }{
	 R5:4 = VADDH(R5:4, R3:2)
	 R18 = ADD(R18, R17)
   }{
	 R5:4 = VADDH(R5:4, R11:10)
	 R6 = MEMUB(R18++#1)
	 R8 = ADD(R8, R17)
   }{
	 R9 = MEMUB(R8++#1)
	 R7:6 = vzxtbh(R6)
	 R5:4 = VASRH(R5:4,#4)
   }{
	 R11:10 = vzxtbh(R9)
	 R5 = MEMUB(R18)
	 MEMB(R20++M0) = R4
   }{
	 R3 = MEMUB(R8)
	 R5:4 = vzxtbh(R5)
   }{
	 R3:2 = vzxtbh(R3)
	 R5:4 += ASL(R5:4, #1)
   }{
	 R11:10 += ASL(R11:10, #1)
	 R3:2 += ASL(R3:2, #3)
   }{
	 R5:4 = VADDH(R5:4, R7:6)
   }{
	 R5:4 = VADDH(R5:4, R11:10)
   }{
	 R5:4 = VADDH(R5:4, R3:2)
   }{
	 R5:4 = VASRH(R5:4,#4)
   }{
	 MEMB(R20) = R4
   }

   {
     R21:20 = MEMD(R29+#(2*8))
     R17:16 = MEMD(R29+#(0*8))		   // restore callee-saved registers
   }{
	 R19:18 = MEMD(R29+#(1*8))		   // restore callee-saved registers
         R23:22 = MEMD(R29+#(3*8))
   }{
	 DEALLOC_RETURN					   // return
   }
   .size      .gaussian3x3_hvx_borders, ..gaussian3x3_hvx_borders



#define Gaussian3x3_Borders_CALLEE_SAVED_REG_SIZE    7 * 8
#define Gaussian3x3_Borders_CALLEE_SAVED_REG_OFFSET  0
#define Gaussian3x3_Borders_STACK_SIZE               (Gaussian3x3_CALLEE_SAVED_REG_OFFSET + Gaussian3x3_CALLEE_SAVED_REG_SIZE)

   .text
   .p2align 4						   // ensures 16-byte alignment of first packet
   .globl gaussian3x3_borders					   // makes function have global scope
   .type       gaussian3x3_borders, @function
gaussian3x3_borders:
   {
	 P0 = CMP.GT(R1,#2)				   // width>2
	 P1 = CMP.GT(R2,#2)				   // height>2
	 R8 = #4
	 M0 = R3						   // M0 = srcStride -- used to increment src pointer
   }{
	 P0 = AND(P0,P1)				   // if !((width>2)&&(height>2))
	 IF !P0.new JUMPR:nt R31		   // then return
	 R27 = ADD(R2, #-2)
   }{
	 ALLOCFRAME(#Gaussian3x3_Borders_STACK_SIZE) // prepare the stack
	 P0 = CMP.GT(R1,#4)				   // if srcWidth > 4
	 R8 = SUB(R8, R3)				   // 4 - 1*srcStride
   }{
	 MEMD(R29+#(5*8)) = R27:26		   // save callee-saved registers onto stack
	 MEMD(R29+#(6*8)) = R31:30		   // save callee-saved registers onto stack			   // srcStride - srcWidth
	 M1 = R8						   // M1 = 4-2*srcStride -- used to increment src pointer.
   }{
	 MEMD(R29+#(2*8)) = R21:20		   // save callee-saved registers onto stack
	 MEMD(R29+#(3*8)) = R23:22		   // save callee-saved registers onto stack
	 R30 = ASR(R1,#2)
	 R8 = ADD(R0, R3)
									   // h = srcHeight -- used to loop over rows
   }{
	 MEMD(R29+#(4*8)) = R25:24		   // save callee-saved registers onto stack
	 MEMD(R29+#(0*8)) = R17:16		   // save callee-saved registers onto stack
	 R25 = R4						   // dstImg pointer for writing.
	 R22 = insert(R1,#16,#16)		   // R23:22 is source value used in L2FETCH instruction to specify box prefetch size.
   }{
	 R30 = ADD(R30,#-1)
	 R27:26 = mpy(R27,R5)			   // w4 = srcWidth/4 - 1  -- used to loop through pixels in a row (we are calculating them in groups of 4 pixels)
	 R23 = #0
	 R22.L = #6						   // This is the height of the box that will be prefetched.
   }{
	 MEMD(R29+#(1*8)) = R19:18		   // save callee-saved registers onto stack			                   // dstStride - srcWidth -- used to increment dstImg pointer at the end of outer loop
	 R23 = insert(R3, #16,#0)
	 R24 = SUB(R26, R5)				   // R23:22 = [clear (63:48) | srcStride (47:32) | srcWidth (31:16) | srcHeight (15:0)] -- used for L2FETCH
	 R18 = R0						   //LOOP1(.down2_top_OUTER_LOOP,#1)
   }{
	 R19 = R3
	 R20 = R4
	 R21 = ADD(R5, #-1)
   }


   .falign
.gaussian3x3_borders_OUTER_LOOP:
   {								   // call inner loop w4 times, and then process last group of columns, and increment pointers accordingly.
									   // process pixels in groups of 4, then increment pointers accordingly.
	 L2FETCH(R0,R23:22)				   // fetch the rows in srcImg that will be used in this outer loop iteration as well as the next one.
	 R15 = ADD(R25,#31)				   // dst + 31
	 R14 = ADD(R25,R1)				   // dst + dstWidth
   }{
	 R5:4 = MEMUBH(R0)				   // load src0
	 R15 = AND(R15,#-32)			   // left edge of first full cache line in dst row
   }{
	 R7:6 = MEMUBH(R0++M0)			   // load src1
	 R14 = SUB(R14,R15)				   // number of bytes in dst row to right of first cache line edge
   }{
	 R5:4 += ASL(R7:6,#1)			   // src0 + 2*src1
	 R7:6 = MEMUBH(R0++M1)			   // load src2

	 P2 = CMP.GT(R14,#31)			   // at least 1 full cache line in dst row?
	 R14 = ASR(R14, #5)				   // number of full cache lines in dst row
   }{
	 R9:8 = VADDH(R5:4,R7:6)
	 JUMP .gaussian3x3_borders_skip_dczeroa
	 //IF (!P2) JUMP .gaussian3x3_borders_skip_dczeroa // skip dczeroa loop
   }//{
	 //LOOP0(.down2_top_dczeroa, R14)	   // loop (number of cache lines)
  // }

   /*.falign
.down2_top_dczeroa:
   {
	 DCZEROA(R15)					   // allocate cache line
	 R15 = ADD(R15,#32)				   // advance to next cache line
   }:endloop0
   /*/

   .falign
.gaussian3x3_borders_skip_dczeroa:
   {
	 IF (!P0) JUMP .gaussian3x3_borders_OUTER_LOOP_part3 // if !(width > 4) then skip inner loop, and outer loop part 2
	 P3=SP2LOOP0(.gaussian3x3_borders_INNER_LOOP,R30)
   }

   .falign
.gaussian3x3_borders_INNER_LOOP:
   {
	 R5:4 = MEMUBH(R0)				   // load src0
	 R7:6 = VALIGNB(R9:8,R11:10,#2)	   // col4:1
	 R14 = R11						   // col3:2
	 R17 = VTRUNEHB(R13:12)			   // truncate to 8-bit
   }{
	 R7:6 = MEMUBH(R0++M0)			   // load src1
	 R11:10 += ASL(R7:6,#1)			   // col3:0 + 2*col4:1
	 R15 = R8						   // R15:14 = col5:2
	 R3:2 = LSR(R17:16,#24)			   // align output
   }{
	 R5:4 += ASL(R7:6,#1)			   // src0 + 2*src1
	 R7:6 = MEMUBH(R0++M1)			   // load src2
	 R11:10 = VADDH(R11:10,R15:14)	   // sum3:0
	 IF P3 MEMW(R25++#4) = R2		   // Write output to dstImg
   }{
	 R11:10 = R9:8					   // new col3:0
	 R9:8 = VADDH(R5:4,R7:6)		   // col7:4
	 R13:12 = VASRH(R11:10,#4)		   // sum3:0 >> 4
	 R16 = R17
   }:endloop0

   .falign
.gaussian3x3_borders_LOOP_part2:
   {
	 R7:6 = VALIGNB(R9:8,R11:10,#2)	   // col4:1
	 R14 = R11						   // col3:2
	 R17 = VTRUNEHB(R13:12)			   // truncate to 8-bit
   }{
	 R11:10 += ASL(R7:6,#1)			   // col3:0 + 2*col4:1
	 R15 = R8						   // R15:14 = col5:2
	 R3:2 = LSR(R17:16,#24)			   // align output
   }{
	 R5:4 += ASL(R7:6,#1)			   // src0 + 2*src1
	 R11:10 = VADDH(R11:10,R15:14)	   // sum3:0
	 IF P3 MEMW(R25++#4) = R2		   // Write output to dstImg
   }{
	 R11:10 = R9:8					   // new col3:0
	 R9:8 = VADDH(R5:4,R7:6)		   // col7:4
	 R13:12 = VASRH(R11:10,#4)		   // sum3:0 >> 4
	 R16 = R17
   }

   .falign
.gaussian3x3_borders_OUTER_LOOP_part3:
   {
	 IF (!P0) R11:10 = R9:8
   }{
	 R7:6 = VALIGNB(R9:8,R11:10,#2)	   // col4:1
	 R14 = R11						   // col3:2
	 R17 = VTRUNEHB(R13:12)			   // truncate to 8-bit
   }{
	 R11:10 += ASL(R7:6,#1)			   // col3:0 + 2*col4:1
	 R15 = R8						   // R15:14 = col5:2
	 R3:2 = LSR(R17:16,#24)			   // align output
   }{
	 R11:10 = VADDH(R11:10,R15:14)	   // sum3:0
	 IF P0 MEMW(R25++#4) = R2		   // Write output to dstImg only if w4>1
   }{
	 R13:12 = VASRH(R11:10,#4)		   // sum3:0 >> 4
	 R16 = R17
   }{
	 R17 = VTRUNEHB(R13:12)			   // truncate to 8-bit
   }{
	 R3:2 = LSR(R17:16,#24)			   // align output
   }{
	 MEMW(R25) = R2					   // Write output to dstImg
	 R25 += ADD(R26,#4)
	 R0 = ADD(R0, R24)
   }{
	 R27 = R25
   }

   .falign
.gaussian3x3_borders_OUTER_LOOP2:
   {								   // call inner loop w4 times, and then process last group of columns, and increment pointers accordingly.							   // process pixels in groups of 4, then increment pointers accordingly.
	 R5:4 = MEMUBH(R0++M0)			   // load src0
	 R15 = AND(R15,#-32)			   // left edge of first full cache line in dst row
   }{
	 R7:6 = MEMUBH(R0)				   // load src1
	 R14 = SUB(R14,R15)				   // number of bytes in dst row to right of first cache line edge
   }{
	 R5:4 += ASL(R7:6,#1)			   // src0 + 2*src1
	 R7:6 = MEMUBH(R0++M1)			   // load src2

	 P2 = CMP.GT(R14,#31)			   // at least 1 full cache line in dst row?
	 R14 = ASR(R14, #5)				   // number of full cache lines in dst row
   }{
	 R9:8 = VADDH(R5:4,R7:6)
	 JUMP .gaussian3x3_borders_skip_dczeroa2
	 //IF (!P2) JUMP .gaussian3x3_borders_skip_dczeroa2 // skip dczeroa loop
   }//{
	 //LOOP0(.down2_top_dczeroa2, R14)   // loop (number of cache lines)
   //}

  /*.falign
.down2_top_dczeroa2:
   {
	 DCZEROA(R15)					   // allocate cache line
	 R15 = ADD(R15,#32)				   // advance to next cache line
   }:endloop0
*/
   .falign
.gaussian3x3_borders_skip_dczeroa2:
   {
	 IF (!P0) JUMP .gaussian3x3_borders_OUTER_LOOP_part32
	 P3=SP2LOOP0(.gaussian3x3_borders_INNER_LOOP2,R30)
   }

   .falign
.gaussian3x3_borders_INNER_LOOP2:
   {
	 R5:4 = MEMUBH(R0++M0)			   // load src0
	 R7:6 = VALIGNB(R9:8,R11:10,#2)	   // col4:1
	 R14 = R11						   // col3:2
	 R17 = VTRUNEHB(R13:12)			   // truncate to 8-bit
   }{
	 R7:6 = MEMUBH(R0)				   // load src1
	 R11:10 += ASL(R7:6,#1)			   // col3:0 + 2*col4:1
	 R15 = R8						   // R15:14 = col5:2
	 R3:2 = LSR(R17:16,#24)			   // align output
   }{
	 R5:4 += ASL(R7:6,#1)			   // src0 + 2*src1
	 R7:6 = MEMUBH(R0++M1)			   // load src2
	 R11:10 = VADDH(R11:10,R15:14)	   // sum3:0
	 IF P3 MEMW(R25++#4) = R2		   // Write output to dstImg
   }{
	 R11:10 = R9:8					   // new col3:0
	 R9:8 = VADDH(R5:4,R7:6)		   // col7:4
	 R13:12 = VASRH(R11:10,#4)		   // sum3:0 >> 4
	 R16 = R17
   }:endloop0

   .falign
.gaussian3x3_borders_OUTER_LOOP_part22:
   {
	 R7:6 = VALIGNB(R9:8,R11:10,#2)	   // col4:1
	 R14 = R11						   // col3:2
	 R17 = VTRUNEHB(R13:12)			   // truncate to 8-bit
   }{
	 R11:10 += ASL(R7:6,#1)			   // col3:0 + 2*col4:1
	 R15 = R8						   // R15:14 = col5:2
	 R3:2 = LSR(R17:16,#24)			   // align output
   }{
	 R5:4 += ASL(R7:6,#1)			   // src0 + 2*src1
	 R11:10 = VADDH(R11:10,R15:14)	   // sum3:0
	 IF P3 MEMW(R25++#4) = R2		   // Write output to dstImg
   }{
	 R11:10 = R9:8					   // new col3:0
	 R9:8 = VADDH(R5:4,R7:6)		   // col7:4
	 R13:12 = VASRH(R11:10,#4)		   // sum3:0 >> 4
	 R16 = R17
   }

   .falign
.gaussian3x3_borders_OUTER_LOOP_part32:
   {
	 IF (!P0) R11:10 = R9:8
   }{
	 R7:6 = VALIGNB(R9:8,R11:10,#2)	   // col4:1
	 R14 = R11						   // col3:2
	 R17 = VTRUNEHB(R13:12)			   // truncate to 8-bit
   }{
	 R11:10 += ASL(R7:6,#1)			   // col3:0 + 2*col4:1
	 R15 = R8						   // R15:14 = col5:2
	 R3:2 = LSR(R17:16,#24)			   // align output
   }{
	 R11:10 = VADDH(R11:10,R15:14)	   // sum3:0
	 IF P0 MEMW(R25++#4) = R2		   // Write output to dstImg only if w4>1
   }{
	 R13:12 = VASRH(R11:10,#4)		   // sum3:0 >> 4
	 R16 = R17
   }{
	 R17 = VTRUNEHB(R13:12)			   // truncate to 8-bit
   }{
	 R3:2 = LSR(R17:16,#24)			   // align output
   }{
	 MEMW(R25) = R2					   // Write output to dstImg
   }

   .falign
.gaussian3x3_borders_corners:
   {
	 R0 = R18
	 R8 = ADD(R18, R19)
	 M0 = R21
	 R17 = ADD(R19, #-3)
   }{
	 R24 = ADD(R24, R19)
   }{
	 R5 = MEMUB(R18++#1)
	 R3 = MEMUB(R8++#1)
   }{
	 R6 = MEMUB(R18)
	 R9 = MEMUB(R8)
	 R5:4 = vzxtbh(R5)
	 R3:2 = vzxtbh(R3)
   }{
	 R13:12 = vzxtbh(R6)
	 R11:10 = vzxtbh(R9)
   }{
	 R5:4 += ASL(R5:4, #3)
	 R13:12 += ASL(R13:12, #1)
   }{
	 R3:2 += ASL(R3:2, #1)
	 R5:4 = VADDH(R5:4, R13:12)
   }{
	 R5:4 = VADDH(R5:4, R3:2)
	 R18 = ADD(R18, R17)
   }{
	 R5:4 = VADDH(R5:4, R11:10)
	 R6 = MEMUB(R18++#1)
	 R8 = ADD(R18, R19)
   }{
	 R9 = MEMUB(R8++#1)
	 R7:6 = vzxtbh(R6)
	 R5:4 = VASRH(R5:4,#4)
   }{
	 R11:10 = vzxtbh(R9)
	 R5 = MEMUB(R18)
	 MEMB(R20++M0) = R4
   }{
	 R3 = MEMUB(R8)
	 R5:4 = vzxtbh(R5)
   }{
	 R3:2 = vzxtbh(R3)
	 R5:4 += ASL(R5:4, #3)
   }{
	 R7:6 += ASL(R7:6, #1)
	 R3:2 += ASL(R3:2, #1)
   }{
	 R5:4 = VADDH(R5:4, R7:6)
	 R18 = ADD(R0, R24)
   }{
	 R5:4 = VADDH(R5:4, R11:10)
	 R7 = MEMUB(R18++#1)
	 R8 = ADD(R18, R19)
   }{
	 R3 = MEMUB(R8++#1)
	 R7:6 = vzxtbh(R7)
	 R5:4 = VADDH(R5:4, R3:2)
   }{
	 R3:2 = vzxtbh(R3)
	 R5:4 = VASRH(R5:4,#4)
   }{
	 R7:6 += ASL(R7:6, #1)
	 MEMB(R20) = R4
	 R3:2 += ASL(R3:2, #3)
   }{
	 R5 = MEMUB(R18)
	 R4 = MEMUB(R8)
	 R7:6 = VADDH(R7:6, R3:2)
   }{
	 R5:4 = vzxtbh(R5)
	 R11:10 = vzxtbh(R4)
	 R18 = ADD(R18, R17)
   }{
	 R11:10 += ASL(R11:10, #1)
	 R7:6 = VADDH(R7:6, R5:4)
	 R8 = ADD(R18, R19)
   }{
	 R5 = MEMUB(R18++#1)
	 R9 = MEMUB(R8++#1)
	 R7:6 = VADDH(R7:6, R11:10)
   }{
	 R5:4 = vzxtbh(R5)
	 R7:6 = VASRH(R7:6,#4)
   }{
	 R3 = MEMUB(R18)
	 R13:12 = vzxtbh(R9)
	 MEMB(R27++M0) = R6
   }{
	 R10 = MEMUB(R8)
	 R3:2 = vzxtbh(R3)
	 R13:12 += ASL(R13:12, #1)
   }{
	 R3:2 += ASL(R3:2, #1)
	 R11:10 = vzxtbh(R10)
   }{
	 R13:12 = VADDH(R13:12, R5:4)
	 R11:10 += ASL(R11:10, #3)
   }{
	 R13:12 = VADDH(R13:12, R3:2)
   }{
	 R13:12 = VADDH(R13:12, R11:10)
   }{
	 R13:12 = VASRH(R13:12,#4)
   }{
	 MEMB(R27) = R12
   }

   // Epilogue

   {
	 R17:16 = MEMD(R29+#(0*8))		   // restore callee-saved registers
	 R19:18 = MEMD(R29+#(1*8))		   // restore callee-saved registers
   }{
	 R21:20 = MEMD(R29+#(2*8))		   // restore callee-saved registers
	 R23:22 = MEMD(R29+#(3*8))		   // restore callee-saved registers
   }{
	 R25:24 = MEMD(R29+#(4*8))		   // restore callee-saved registers
	 R31:30 = MEMD(R29+#(6*8))		   // restore callee-saved registers
   }{
	 R27:26 = MEMD(R29+#(5*8))		   // restore callee-saved registers
	 DEALLOC_RETURN					   // return
   }
   .size       gaussian3x3_borders, .-gaussian3x3_borders


