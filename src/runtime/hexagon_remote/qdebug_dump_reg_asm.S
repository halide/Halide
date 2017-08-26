/**=============================================================================

 @file
 qdebug_dump_reg_asm.h

 @brief
 An assembly file illustrating some of the ways the QD macros may be used to
 display or process register contents.

 Copyright (c) 2017 Qualcomm Technologies Incorporated.
 All Rights Reserved Qualcomm Proprietary

 Export of this technology or software is regulated by the U.S.
 Government. Diversion contrary to U.S. law prohibited.

 All ideas, data and information contained in or disclosed by
 this document are confidential and proprietary information of
 Qualcomm Technologies Incorporated and all rights therein are expressly reserved.
 By accepting this material the recipient agrees that this material
 and the information contained therein are held in confidence and in
 trust and will not be used, copied, reproduced in whole or in part,
 nor its contents revealed in any manner to others without the express
 written permission of Qualcomm Technologies Incorporated.

 =============================================================================**/

#include "qdebug_asm.h"

	.text
   .p2align 4                         
   .globl test           
   .type       test, @function
test:
	{ r0 = add(r0,r0) }
	{ JUMPR R31 }

   .size       test, .-test
 
   .text
   .p2align 4                         
   .globl qdebug_dump_reg_asm           
   .type       qdebug_dump_reg_asm, @function
qdebug_dump_reg_asm:
   {
   ALLOCFRAME(#(8*8 + 2*8)) // r + p
   }
   QD print_u8 V_all
   {
   DEALLOC_RETURN
   }
   .size       qdebug_dump_reg_asm, .-qdebug_dump_reg_asm
