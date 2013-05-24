	.file	"halide_halide_game_of_life"
	.section	.text.halide_set_custom_allocator,"axG",@progbits,halide_set_custom_allocator,comdat
	.weak	halide_set_custom_allocator
	.align	32, 0x90
	.type	halide_set_custom_allocator,@function
halide_set_custom_allocator:            # @halide_set_custom_allocator
	.cfi_startproc
# BB#0:
	naclcall	.L0$pb
.L0$pb:
	popl	%eax
.Ltmp0:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp0-.L0$pb), %eax
	movl	4(%esp), %ecx
	movl	halide_custom_malloc@GOT(%eax), %edx
	movl	%ecx, (%edx)
	movl	8(%esp), %ecx
	movl	halide_custom_free@GOT(%eax), %eax
	movl	%ecx, (%eax)
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp1:
	.size	halide_set_custom_allocator, .Ltmp1-halide_set_custom_allocator
	.cfi_endproc

	.section	.text.halide_malloc,"axG",@progbits,halide_malloc,comdat
	.weak	halide_malloc
	.align	32, 0x90
	.type	halide_malloc,@function
halide_malloc:                          # @halide_malloc
	.cfi_startproc
# BB#0:
	pushl	%ebx
.Ltmp4:
	.cfi_def_cfa_offset 8
	subl	$8, %esp
.Ltmp5:
	.cfi_def_cfa_offset 16
.Ltmp6:
	.cfi_offset %ebx, -8
	naclcall	.L1$pb
.L1$pb:
	popl	%ebx
.Ltmp7:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp7-.L1$pb), %ebx
	movl	halide_custom_malloc@GOT(%ebx), %eax
	movl	(%eax), %eax
	testl	%eax, %eax
	je	.LBB1_1
# BB#2:
	addl	$8, %esp
	popl	%ebx
	nacljmp	%eax
.LBB1_1:
	movl	16(%esp), %eax
	addl	$32, %eax
	movl	%eax, (%esp)
	naclcall	malloc@PLT
	leal	32(%eax), %ecx
	andl	$-32, %ecx
	movl	%eax, -4(%ecx)
	movl	%ecx, %eax
	addl	$8, %esp
	popl	%ebx
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp8:
	.size	halide_malloc, .Ltmp8-halide_malloc
	.cfi_endproc

	.section	.text.halide_free,"axG",@progbits,halide_free,comdat
	.weak	halide_free
	.align	32, 0x90
	.type	halide_free,@function
halide_free:                            # @halide_free
	.cfi_startproc
# BB#0:
	pushl	%ebx
.Ltmp11:
	.cfi_def_cfa_offset 8
	subl	$8, %esp
.Ltmp12:
	.cfi_def_cfa_offset 16
.Ltmp13:
	.cfi_offset %ebx, -8
	naclcall	.L2$pb
.L2$pb:
	popl	%ebx
.Ltmp14:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp14-.L2$pb), %ebx
	movl	halide_custom_free@GOT(%ebx), %eax
	movl	(%eax), %eax
	testl	%eax, %eax
	je	.LBB2_1
# BB#2:
	addl	$8, %esp
	popl	%ebx
	nacljmp	%eax
.LBB2_1:
	movl	16(%esp), %eax
	movl	-4(%eax), %eax
	movl	%eax, (%esp)
	naclcall	free@PLT
	addl	$8, %esp
	popl	%ebx
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp15:
	.size	halide_free, .Ltmp15-halide_free
	.cfi_endproc

	.section	.text.halide_start_clock,"axG",@progbits,halide_start_clock,comdat
	.weak	halide_start_clock
	.align	32, 0x90
	.type	halide_start_clock,@function
halide_start_clock:                     # @halide_start_clock
	.cfi_startproc
# BB#0:
	pushl	%ebx
.Ltmp18:
	.cfi_def_cfa_offset 8
	subl	$8, %esp
.Ltmp19:
	.cfi_def_cfa_offset 16
.Ltmp20:
	.cfi_offset %ebx, -8
	naclcall	.L3$pb
.L3$pb:
	popl	%ebx
.Ltmp21:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp21-.L3$pb), %ebx
	movl	halide_reference_clock@GOT(%ebx), %eax
	movl	%eax, (%esp)
	movl	$0, 4(%esp)
	naclcall	gettimeofday@PLT
	xorl	%eax, %eax
	addl	$8, %esp
	popl	%ebx
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp22:
	.size	halide_start_clock, .Ltmp22-halide_start_clock
	.cfi_endproc

	.section	.text.halide_current_time,"axG",@progbits,halide_current_time,comdat
	.weak	halide_current_time
	.align	32, 0x90
	.type	halide_current_time,@function
halide_current_time:                    # @halide_current_time
	.cfi_startproc
# BB#0:
	pushl	%ebx
.Ltmp26:
	.cfi_def_cfa_offset 8
	pushl	%esi
.Ltmp27:
	.cfi_def_cfa_offset 12
	subl	$20, %esp
.Ltmp28:
	.cfi_def_cfa_offset 32
.Ltmp29:
	.cfi_offset %esi, -12
.Ltmp30:
	.cfi_offset %ebx, -8
	naclcall	.L4$pb
.L4$pb:
	popl	%ebx
.Ltmp31:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp31-.L4$pb), %ebx
	leal	8(%esp), %eax
	movl	%eax, (%esp)
	movl	$0, 4(%esp)
	naclcall	gettimeofday@PLT
	movl	halide_reference_clock@GOT(%ebx), %ecx
	movl	8(%esp), %esi
	movl	12(%esp), %eax
	subl	4(%ecx), %eax
	movl	$274877907, %edx        # imm = 0x10624DD3
	imull	%edx
	movl	%edx, %eax
	shrl	$31, %eax
	sarl	$6, %edx
	leal	(%edx,%eax), %edx
	subl	(%ecx), %esi
	imull	$1000, %esi, %eax       # imm = 0x3E8
	addl	%edx, %eax
	addl	$20, %esp
	popl	%esi
	popl	%ebx
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp32:
	.size	halide_current_time, .Ltmp32-halide_current_time
	.cfi_endproc

	.section	.text.halide_printf,"axG",@progbits,halide_printf,comdat
	.weak	halide_printf
	.align	32, 0x90
	.type	halide_printf,@function
halide_printf:                          # @halide_printf
	.cfi_startproc
# BB#0:
	pushl	%ebx
.Ltmp35:
	.cfi_def_cfa_offset 8
	subl	$24, %esp
.Ltmp36:
	.cfi_def_cfa_offset 32
.Ltmp37:
	.cfi_offset %ebx, -8
	naclcall	.L5$pb
.L5$pb:
	popl	%ebx
.Ltmp38:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp38-.L5$pb), %ebx
	leal	36(%esp), %eax
	movl	%eax, 20(%esp)
	movl	stderr@GOT(%ebx), %ecx
	movl	(%ecx), %ecx
	movl	%eax, 8(%esp)
	movl	32(%esp), %eax
	movl	%eax, 4(%esp)
	movl	%ecx, (%esp)
	naclcall	vfprintf@PLT
	addl	$24, %esp
	popl	%ebx
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp39:
	.size	halide_printf, .Ltmp39-halide_printf
	.cfi_endproc

	.section	.text.halide_error,"axG",@progbits,halide_error,comdat
	.weak	halide_error
	.align	32, 0x90
	.type	halide_error,@function
halide_error:                           # @halide_error
	.cfi_startproc
# BB#0:
	pushl	%ebx
.Ltmp42:
	.cfi_def_cfa_offset 8
	subl	$8, %esp
.Ltmp43:
	.cfi_def_cfa_offset 16
.Ltmp44:
	.cfi_offset %ebx, -8
	naclcall	.L6$pb
.L6$pb:
	popl	%ebx
.Ltmp45:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp45-.L6$pb), %ebx
	movl	halide_error_handler@GOT(%ebx), %eax
	movl	(%eax), %eax
	testl	%eax, %eax
	je	.LBB6_1
# BB#2:
	addl	$8, %esp
	popl	%ebx
	nacljmp	%eax
.LBB6_1:
	movl	16(%esp), %eax
	movl	%eax, 4(%esp)
	leal	.L.str@GOTOFF(%ebx), %eax
	movl	%eax, (%esp)
	naclcall	halide_printf@PLT
	movl	$1, (%esp)
	naclcall	exit@PLT
	addl	$8, %esp
	popl	%ebx
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp46:
	.size	halide_error, .Ltmp46-halide_error
	.cfi_endproc

	.section	.text.halide_set_error_handler,"axG",@progbits,halide_set_error_handler,comdat
	.weak	halide_set_error_handler
	.align	32, 0x90
	.type	halide_set_error_handler,@function
halide_set_error_handler:               # @halide_set_error_handler
	.cfi_startproc
# BB#0:
	naclcall	.L7$pb
.L7$pb:
	popl	%eax
.Ltmp47:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp47-.L7$pb), %eax
	movl	4(%esp), %ecx
	movl	halide_error_handler@GOT(%eax), %eax
	movl	%ecx, (%eax)
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp48:
	.size	halide_set_error_handler, .Ltmp48-halide_set_error_handler
	.cfi_endproc

	.section	.text.halide_debug_to_file,"axG",@progbits,halide_debug_to_file,comdat
	.weak	halide_debug_to_file
	.align	32, 0x90
	.type	halide_debug_to_file,@function
halide_debug_to_file:                   # @halide_debug_to_file
	.cfi_startproc
# BB#0:
	pushl	%ebp
.Ltmp54:
	.cfi_def_cfa_offset 8
	pushl	%ebx
.Ltmp55:
	.cfi_def_cfa_offset 12
	pushl	%edi
.Ltmp56:
	.cfi_def_cfa_offset 16
	pushl	%esi
.Ltmp57:
	.cfi_def_cfa_offset 20
	subl	$284, %esp              # imm = 0x11C
.Ltmp58:
	.cfi_def_cfa_offset 304
.Ltmp59:
	.cfi_offset %esi, -20
.Ltmp60:
	.cfi_offset %edi, -16
.Ltmp61:
	.cfi_offset %ebx, -12
.Ltmp62:
	.cfi_offset %ebp, -8
	naclcall	.L8$pb
.L8$pb:
	popl	%ebx
.Ltmp63:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp63-.L8$pb), %ebx
	leal	.L.str1@GOTOFF(%ebx), %eax
	movl	%eax, 4(%esp)
	movl	304(%esp), %esi
	movl	%esi, (%esp)
	naclcall	fopen@PLT
	movl	%eax, 40(%esp)          # 4-byte Spill
	movl	$-1, %ecx
	testl	%eax, %eax
	je	.LBB8_22
# BB#1:
	movl	324(%esp), %eax
	movl	320(%esp), %edx
	movl	316(%esp), %ecx
	movl	312(%esp), %ebp
	movl	%esi, (%esp)
	movl	%eax, %esi
	movl	$46, 4(%esp)
	movl	%ecx, %eax
	imull	%ebp, %eax
	movl	%eax, 24(%esp)          # 4-byte Spill
	imull	%edx, %eax
	imull	%esi, %eax
	movl	%eax, 28(%esp)          # 4-byte Spill
	naclcall	strrchr@PLT
	movl	%eax, %edi
	testl	%edi, %edi
	je	.LBB8_19
# BB#2:
	movsbl	1(%edi), %eax
	movl	%eax, (%esp)
	naclcall	tolower@PLT
	cmpl	$116, %eax
	jne	.LBB8_19
# BB#3:
	movsbl	2(%edi), %eax
	movl	%eax, (%esp)
	naclcall	tolower@PLT
	cmpl	$105, %eax
	jne	.LBB8_19
# BB#4:
	movsbl	3(%edi), %eax
	movl	%eax, (%esp)
	naclcall	tolower@PLT
	cmpl	$102, %eax
	jne	.LBB8_19
# BB#5:
	movsbl	4(%edi), %eax
	movl	%eax, (%esp)
	naclcall	tolower@PLT
	testl	%eax, %eax
	je	.LBB8_8
# BB#6:
	movsbl	4(%edi), %eax
	movl	%eax, (%esp)
	naclcall	tolower@PLT
	cmpl	$102, %eax
	jne	.LBB8_19
# BB#7:                                 # %_ZN12_GLOBAL__N_118has_tiff_extensionEPKc.exit.i
	movsbl	5(%edi), %eax
	movl	%eax, (%esp)
	naclcall	tolower@PLT
	testl	%eax, %eax
	je	.LBB8_8
.LBB8_19:                               # %_ZN12_GLOBAL__N_118has_tiff_extensionEPKc.exit.thread.i
	movl	%ebp, 44(%esp)
	movl	316(%esp), %eax
	movl	%eax, 48(%esp)
	movl	320(%esp), %eax
	movl	%eax, 52(%esp)
	movl	%esi, 56(%esp)
	movl	328(%esp), %eax
	movl	%eax, 60(%esp)
	movl	40(%esp), %eax          # 4-byte Reload
	movl	%eax, 12(%esp)
	leal	44(%esp), %eax
	movl	%eax, (%esp)
	movl	$1, 8(%esp)
	movl	$20, 4(%esp)
	naclcall	fwrite@PLT
	movl	$-2, 32(%esp)           # 4-byte Folded Spill
	cmpl	$1, %eax
	jne	.LBB8_21
	jmp	.LBB8_20
.LBB8_8:                                # %_ZN12_GLOBAL__N_118has_tiff_extensionEPKc.exit.thread1.i
	movw	$18761, 72(%esp)        # imm = 0x4949
	movw	$42, 74(%esp)
	movl	$8, 76(%esp)
	movw	$15, 80(%esp)
	movw	$256, 82(%esp)          # imm = 0x100
	movw	$4, 84(%esp)
	movl	$1, 86(%esp)
	movl	%ebp, 90(%esp)
	movw	$257, 94(%esp)          # imm = 0x101
	movw	$4, 96(%esp)
	movl	$1, 98(%esp)
	movl	316(%esp), %ebp
	movl	%ebp, 102(%esp)
	movw	$258, 106(%esp)         # imm = 0x102
	movw	$3, 108(%esp)
	movl	$1, 110(%esp)
	movl	332(%esp), %edx
	leal	(,%edx,8), %eax
	movw	%ax, 114(%esp)
	movl	320(%esp), %edx
	cmpl	$5, %edx
	setl	%al
	cmpl	$2, %esi
	sbbb	%cl, %cl
	movl	$1, %edi
	testb	%al, %cl
	cmovel	%edx, %edi
	movl	%edi, 36(%esp)          # 4-byte Spill
	cmovnel	%edx, %esi
	cmpl	$2, %esi
	setg	%al
	movzbl	%al, %eax
	incl	%eax
	movw	$259, 118(%esp)         # imm = 0x103
	movw	$3, 120(%esp)
	movl	$1, 122(%esp)
	movw	$1, 126(%esp)
	movw	$262, 130(%esp)         # imm = 0x106
	movw	$3, 132(%esp)
	movl	$1, 134(%esp)
	movw	%ax, 138(%esp)
	movw	$273, 142(%esp)         # imm = 0x111
	movw	$4, 144(%esp)
	movl	%esi, 146(%esp)
	movl	$210, 150(%esp)
	movw	$277, 154(%esp)         # imm = 0x115
	movw	$3, 156(%esp)
	movl	$1, 158(%esp)
	movw	%si, 162(%esp)
	movw	$278, 166(%esp)         # imm = 0x116
	movw	$4, 168(%esp)
	movl	$1, 170(%esp)
	movl	%ebp, 174(%esp)
	cmpl	$1, %esi
	jne	.LBB8_10
# BB#9:
	movl	28(%esp), %eax          # 4-byte Reload
	imull	332(%esp), %eax
	jmp	.LBB8_11
.LBB8_10:
	leal	210(,%esi,4), %eax
.LBB8_11:
	movw	$279, 178(%esp)         # imm = 0x117
	movw	$4, 180(%esp)
	movl	%esi, 182(%esp)
	movl	%eax, 186(%esp)
	movw	$282, 190(%esp)         # imm = 0x11A
	movw	$5, 192(%esp)
	movl	$1, 194(%esp)
	movl	$194, 198(%esp)
	movw	$283, 202(%esp)         # imm = 0x11B
	movw	$5, 204(%esp)
	movl	$1, 206(%esp)
	movl	$202, 210(%esp)
	movw	$284, 214(%esp)         # imm = 0x11C
	movw	$3, 216(%esp)
	movl	$1, 218(%esp)
	movw	$2, 222(%esp)
	movw	$296, 226(%esp)         # imm = 0x128
	movw	$3, 228(%esp)
	movl	$1, 230(%esp)
	movw	$1, 234(%esp)
	movw	$339, 238(%esp)         # imm = 0x153
	movw	$3, 240(%esp)
	movl	$1, 242(%esp)
	movl	328(%esp), %eax
	movw	_ZN12_GLOBAL__N_130pixel_type_to_tiff_sample_typeE@GOTOFF(%ebx,%eax,2), %ax
	movw	%ax, 246(%esp)
	movw	$-32539, 250(%esp)      # imm = 0xFFFFFFFFFFFF80E5
	movw	$4, 252(%esp)
	movl	$1, 254(%esp)
	movl	36(%esp), %eax          # 4-byte Reload
	movl	%eax, 258(%esp)
	movl	$0, 262(%esp)
	movl	$1, 266(%esp)
	movl	$1, 270(%esp)
	movl	$1, 274(%esp)
	movl	$1, 278(%esp)
	movl	40(%esp), %eax          # 4-byte Reload
	movl	%eax, 12(%esp)
	leal	72(%esp), %eax
	movl	%eax, (%esp)
	movl	$1, 8(%esp)
	movl	$210, 4(%esp)
	naclcall	fwrite@PLT
	movl	$-2, 32(%esp)           # 4-byte Folded Spill
	cmpl	$1, %eax
	jne	.LBB8_21
# BB#12:
	cmpl	$2, %esi
	movl	%esi, %ebp
	jl	.LBB8_20
# BB#13:                                # %.lr.ph.i
	leal	210(,%ebp,8), %eax
	movl	%eax, 68(%esp)
	movl	24(%esp), %edi          # 4-byte Reload
	imull	332(%esp), %edi
	imull	36(%esp), %edi          # 4-byte Folded Reload
	xorl	%esi, %esi
.LBB8_14:                               # =>This Inner Loop Header: Depth=1
	movl	40(%esp), %eax          # 4-byte Reload
	movl	%eax, 12(%esp)
	leal	68(%esp), %eax
	movl	%eax, (%esp)
	movl	$1, 8(%esp)
	movl	$4, 4(%esp)
	naclcall	fwrite@PLT
	cmpl	$1, %eax
	jne	.LBB8_21
# BB#15:                                #   in Loop: Header=BB8_14 Depth=1
	addl	%edi, 68(%esp)
	incl	%esi
	cmpl	%ebp, %esi
	jl	.LBB8_14
# BB#16:                                # %._crit_edge.i
	movl	36(%esp), %eax          # 4-byte Reload
	imull	24(%esp), %eax          # 4-byte Folded Reload
	movl	%eax, 64(%esp)
	xorl	%esi, %esi
	leal	64(%esp), %edi
.LBB8_17:                               # =>This Inner Loop Header: Depth=1
	cmpl	%ebp, %esi
	jge	.LBB8_20
# BB#18:                                #   in Loop: Header=BB8_17 Depth=1
	movl	40(%esp), %eax          # 4-byte Reload
	movl	%eax, 12(%esp)
	movl	%edi, (%esp)
	movl	$1, 8(%esp)
	movl	$4, 4(%esp)
	incl	%esi
	naclcall	fwrite@PLT
	cmpl	$1, %eax
	je	.LBB8_17
	jmp	.LBB8_21
.LBB8_20:                               # %.loopexit.i
	movl	40(%esp), %eax          # 4-byte Reload
	movl	%eax, 12(%esp)
	movl	28(%esp), %eax          # 4-byte Reload
	imull	332(%esp), %eax
	movl	%eax, 4(%esp)
	movl	308(%esp), %eax
	movl	%eax, (%esp)
	movl	$1, 8(%esp)
	naclcall	fwrite@PLT
	movl	$-1, %ecx
	xorl	%edx, %edx
	cmpl	$1, %eax
	cmovnel	%ecx, %edx
	movl	%edx, 32(%esp)          # 4-byte Spill
.LBB8_21:                               # %_ZN12_GLOBAL__N_124halide_write_debug_imageEPKcPhiiiiiiPFbPKvjPvES5_.exit
	movl	40(%esp), %eax          # 4-byte Reload
	movl	%eax, (%esp)
	naclcall	fclose@PLT
	movl	32(%esp), %ecx          # 4-byte Reload
.LBB8_22:
	movl	%ecx, %eax
	addl	$284, %esp              # imm = 0x11C
	popl	%esi
	popl	%edi
	popl	%ebx
	popl	%ebp
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp64:
	.size	halide_debug_to_file, .Ltmp64-halide_debug_to_file
	.cfi_endproc

	.section	.text.sqrt_f32,"axG",@progbits,sqrt_f32,comdat
	.weak	sqrt_f32
	.align	32, 0x90
	.type	sqrt_f32,@function
sqrt_f32:                               # @sqrt_f32
# BB#0:
	naclcall	.L9$pb
.L9$pb:
	popl	%eax
.Ltmp65:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp65-.L9$pb), %eax
	movl	sqrtf@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp66:
	.size	sqrt_f32, .Ltmp66-sqrt_f32

	.section	.text.sin_f32,"axG",@progbits,sin_f32,comdat
	.weak	sin_f32
	.align	32, 0x90
	.type	sin_f32,@function
sin_f32:                                # @sin_f32
# BB#0:
	naclcall	.L10$pb
.L10$pb:
	popl	%eax
.Ltmp67:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp67-.L10$pb), %eax
	movl	sinf@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp68:
	.size	sin_f32, .Ltmp68-sin_f32

	.section	.text.asin_f32,"axG",@progbits,asin_f32,comdat
	.weak	asin_f32
	.align	32, 0x90
	.type	asin_f32,@function
asin_f32:                               # @asin_f32
# BB#0:
	naclcall	.L11$pb
.L11$pb:
	popl	%eax
.Ltmp69:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp69-.L11$pb), %eax
	movl	asinf@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp70:
	.size	asin_f32, .Ltmp70-asin_f32

	.section	.text.cos_f32,"axG",@progbits,cos_f32,comdat
	.weak	cos_f32
	.align	32, 0x90
	.type	cos_f32,@function
cos_f32:                                # @cos_f32
# BB#0:
	naclcall	.L12$pb
.L12$pb:
	popl	%eax
.Ltmp71:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp71-.L12$pb), %eax
	movl	cosf@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp72:
	.size	cos_f32, .Ltmp72-cos_f32

	.section	.text.acos_f32,"axG",@progbits,acos_f32,comdat
	.weak	acos_f32
	.align	32, 0x90
	.type	acos_f32,@function
acos_f32:                               # @acos_f32
# BB#0:
	naclcall	.L13$pb
.L13$pb:
	popl	%eax
.Ltmp73:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp73-.L13$pb), %eax
	movl	acosf@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp74:
	.size	acos_f32, .Ltmp74-acos_f32

	.section	.text.tan_f32,"axG",@progbits,tan_f32,comdat
	.weak	tan_f32
	.align	32, 0x90
	.type	tan_f32,@function
tan_f32:                                # @tan_f32
# BB#0:
	naclcall	.L14$pb
.L14$pb:
	popl	%eax
.Ltmp75:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp75-.L14$pb), %eax
	movl	tanf@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp76:
	.size	tan_f32, .Ltmp76-tan_f32

	.section	.text.atan_f32,"axG",@progbits,atan_f32,comdat
	.weak	atan_f32
	.align	32, 0x90
	.type	atan_f32,@function
atan_f32:                               # @atan_f32
# BB#0:
	naclcall	.L15$pb
.L15$pb:
	popl	%eax
.Ltmp77:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp77-.L15$pb), %eax
	movl	atanf@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp78:
	.size	atan_f32, .Ltmp78-atan_f32

	.section	.text.sinh_f32,"axG",@progbits,sinh_f32,comdat
	.weak	sinh_f32
	.align	32, 0x90
	.type	sinh_f32,@function
sinh_f32:                               # @sinh_f32
# BB#0:
	naclcall	.L16$pb
.L16$pb:
	popl	%eax
.Ltmp79:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp79-.L16$pb), %eax
	movl	sinhf@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp80:
	.size	sinh_f32, .Ltmp80-sinh_f32

	.section	.text.asinh_f32,"axG",@progbits,asinh_f32,comdat
	.weak	asinh_f32
	.align	32, 0x90
	.type	asinh_f32,@function
asinh_f32:                              # @asinh_f32
# BB#0:
	naclcall	.L17$pb
.L17$pb:
	popl	%eax
.Ltmp81:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp81-.L17$pb), %eax
	movl	asinhf@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp82:
	.size	asinh_f32, .Ltmp82-asinh_f32

	.section	.text.cosh_f32,"axG",@progbits,cosh_f32,comdat
	.weak	cosh_f32
	.align	32, 0x90
	.type	cosh_f32,@function
cosh_f32:                               # @cosh_f32
# BB#0:
	naclcall	.L18$pb
.L18$pb:
	popl	%eax
.Ltmp83:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp83-.L18$pb), %eax
	movl	coshf@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp84:
	.size	cosh_f32, .Ltmp84-cosh_f32

	.section	.text.acosh_f32,"axG",@progbits,acosh_f32,comdat
	.weak	acosh_f32
	.align	32, 0x90
	.type	acosh_f32,@function
acosh_f32:                              # @acosh_f32
# BB#0:
	naclcall	.L19$pb
.L19$pb:
	popl	%eax
.Ltmp85:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp85-.L19$pb), %eax
	movl	acoshf@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp86:
	.size	acosh_f32, .Ltmp86-acosh_f32

	.section	.text.tanh_f32,"axG",@progbits,tanh_f32,comdat
	.weak	tanh_f32
	.align	32, 0x90
	.type	tanh_f32,@function
tanh_f32:                               # @tanh_f32
# BB#0:
	naclcall	.L20$pb
.L20$pb:
	popl	%eax
.Ltmp87:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp87-.L20$pb), %eax
	movl	tanhf@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp88:
	.size	tanh_f32, .Ltmp88-tanh_f32

	.section	.text.atanh_f32,"axG",@progbits,atanh_f32,comdat
	.weak	atanh_f32
	.align	32, 0x90
	.type	atanh_f32,@function
atanh_f32:                              # @atanh_f32
# BB#0:
	naclcall	.L21$pb
.L21$pb:
	popl	%eax
.Ltmp89:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp89-.L21$pb), %eax
	movl	atanhf@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp90:
	.size	atanh_f32, .Ltmp90-atanh_f32

	.section	.text.hypot_f32,"axG",@progbits,hypot_f32,comdat
	.weak	hypot_f32
	.align	32, 0x90
	.type	hypot_f32,@function
hypot_f32:                              # @hypot_f32
# BB#0:
	naclcall	.L22$pb
.L22$pb:
	popl	%eax
.Ltmp91:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp91-.L22$pb), %eax
	movl	hypotf@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp92:
	.size	hypot_f32, .Ltmp92-hypot_f32

	.section	.text.exp_f32,"axG",@progbits,exp_f32,comdat
	.weak	exp_f32
	.align	32, 0x90
	.type	exp_f32,@function
exp_f32:                                # @exp_f32
# BB#0:
	naclcall	.L23$pb
.L23$pb:
	popl	%eax
.Ltmp93:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp93-.L23$pb), %eax
	movl	expf@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp94:
	.size	exp_f32, .Ltmp94-exp_f32

	.section	.text.log_f32,"axG",@progbits,log_f32,comdat
	.weak	log_f32
	.align	32, 0x90
	.type	log_f32,@function
log_f32:                                # @log_f32
# BB#0:
	naclcall	.L24$pb
.L24$pb:
	popl	%eax
.Ltmp95:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp95-.L24$pb), %eax
	movl	logf@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp96:
	.size	log_f32, .Ltmp96-log_f32

	.section	.text.pow_f32,"axG",@progbits,pow_f32,comdat
	.weak	pow_f32
	.align	32, 0x90
	.type	pow_f32,@function
pow_f32:                                # @pow_f32
# BB#0:
	naclcall	.L25$pb
.L25$pb:
	popl	%eax
.Ltmp97:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp97-.L25$pb), %eax
	movl	powf@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp98:
	.size	pow_f32, .Ltmp98-pow_f32

	.section	.text.floor_f32,"axG",@progbits,floor_f32,comdat
	.weak	floor_f32
	.align	32, 0x90
	.type	floor_f32,@function
floor_f32:                              # @floor_f32
# BB#0:
	pushl	%eax
	movss	8(%esp), %xmm0
	roundss	$1, %xmm0, %xmm0
	movss	%xmm0, (%esp)
	flds	(%esp)
	popl	%eax
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp99:
	.size	floor_f32, .Ltmp99-floor_f32

	.section	.text.ceil_f32,"axG",@progbits,ceil_f32,comdat
	.weak	ceil_f32
	.align	32, 0x90
	.type	ceil_f32,@function
ceil_f32:                               # @ceil_f32
# BB#0:
	pushl	%eax
	movss	8(%esp), %xmm0
	roundss	$2, %xmm0, %xmm0
	movss	%xmm0, (%esp)
	flds	(%esp)
	popl	%eax
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp100:
	.size	ceil_f32, .Ltmp100-ceil_f32

	.section	.text.round_f32,"axG",@progbits,round_f32,comdat
	.weak	round_f32
	.align	32, 0x90
	.type	round_f32,@function
round_f32:                              # @round_f32
# BB#0:
	naclcall	.L28$pb
.L28$pb:
	popl	%eax
.Ltmp101:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp101-.L28$pb), %eax
	movl	roundf@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp102:
	.size	round_f32, .Ltmp102-round_f32

	.section	.text.sqrt_f64,"axG",@progbits,sqrt_f64,comdat
	.weak	sqrt_f64
	.align	32, 0x90
	.type	sqrt_f64,@function
sqrt_f64:                               # @sqrt_f64
# BB#0:
	naclcall	.L29$pb
.L29$pb:
	popl	%eax
.Ltmp103:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp103-.L29$pb), %eax
	movl	sqrt@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp104:
	.size	sqrt_f64, .Ltmp104-sqrt_f64

	.section	.text.sin_f64,"axG",@progbits,sin_f64,comdat
	.weak	sin_f64
	.align	32, 0x90
	.type	sin_f64,@function
sin_f64:                                # @sin_f64
# BB#0:
	naclcall	.L30$pb
.L30$pb:
	popl	%eax
.Ltmp105:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp105-.L30$pb), %eax
	movl	sin@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp106:
	.size	sin_f64, .Ltmp106-sin_f64

	.section	.text.asin_f64,"axG",@progbits,asin_f64,comdat
	.weak	asin_f64
	.align	32, 0x90
	.type	asin_f64,@function
asin_f64:                               # @asin_f64
# BB#0:
	naclcall	.L31$pb
.L31$pb:
	popl	%eax
.Ltmp107:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp107-.L31$pb), %eax
	movl	asin@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp108:
	.size	asin_f64, .Ltmp108-asin_f64

	.section	.text.cos_f64,"axG",@progbits,cos_f64,comdat
	.weak	cos_f64
	.align	32, 0x90
	.type	cos_f64,@function
cos_f64:                                # @cos_f64
# BB#0:
	naclcall	.L32$pb
.L32$pb:
	popl	%eax
.Ltmp109:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp109-.L32$pb), %eax
	movl	cos@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp110:
	.size	cos_f64, .Ltmp110-cos_f64

	.section	.text.acos_f64,"axG",@progbits,acos_f64,comdat
	.weak	acos_f64
	.align	32, 0x90
	.type	acos_f64,@function
acos_f64:                               # @acos_f64
# BB#0:
	naclcall	.L33$pb
.L33$pb:
	popl	%eax
.Ltmp111:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp111-.L33$pb), %eax
	movl	acos@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp112:
	.size	acos_f64, .Ltmp112-acos_f64

	.section	.text.tan_f64,"axG",@progbits,tan_f64,comdat
	.weak	tan_f64
	.align	32, 0x90
	.type	tan_f64,@function
tan_f64:                                # @tan_f64
# BB#0:
	naclcall	.L34$pb
.L34$pb:
	popl	%eax
.Ltmp113:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp113-.L34$pb), %eax
	movl	tan@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp114:
	.size	tan_f64, .Ltmp114-tan_f64

	.section	.text.atan_f64,"axG",@progbits,atan_f64,comdat
	.weak	atan_f64
	.align	32, 0x90
	.type	atan_f64,@function
atan_f64:                               # @atan_f64
# BB#0:
	naclcall	.L35$pb
.L35$pb:
	popl	%eax
.Ltmp115:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp115-.L35$pb), %eax
	movl	atan@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp116:
	.size	atan_f64, .Ltmp116-atan_f64

	.section	.text.sinh_f64,"axG",@progbits,sinh_f64,comdat
	.weak	sinh_f64
	.align	32, 0x90
	.type	sinh_f64,@function
sinh_f64:                               # @sinh_f64
# BB#0:
	naclcall	.L36$pb
.L36$pb:
	popl	%eax
.Ltmp117:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp117-.L36$pb), %eax
	movl	sinh@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp118:
	.size	sinh_f64, .Ltmp118-sinh_f64

	.section	.text.asinh_f64,"axG",@progbits,asinh_f64,comdat
	.weak	asinh_f64
	.align	32, 0x90
	.type	asinh_f64,@function
asinh_f64:                              # @asinh_f64
# BB#0:
	naclcall	.L37$pb
.L37$pb:
	popl	%eax
.Ltmp119:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp119-.L37$pb), %eax
	movl	asinh@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp120:
	.size	asinh_f64, .Ltmp120-asinh_f64

	.section	.text.cosh_f64,"axG",@progbits,cosh_f64,comdat
	.weak	cosh_f64
	.align	32, 0x90
	.type	cosh_f64,@function
cosh_f64:                               # @cosh_f64
# BB#0:
	naclcall	.L38$pb
.L38$pb:
	popl	%eax
.Ltmp121:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp121-.L38$pb), %eax
	movl	cosh@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp122:
	.size	cosh_f64, .Ltmp122-cosh_f64

	.section	.text.acosh_f64,"axG",@progbits,acosh_f64,comdat
	.weak	acosh_f64
	.align	32, 0x90
	.type	acosh_f64,@function
acosh_f64:                              # @acosh_f64
# BB#0:
	naclcall	.L39$pb
.L39$pb:
	popl	%eax
.Ltmp123:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp123-.L39$pb), %eax
	movl	acosh@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp124:
	.size	acosh_f64, .Ltmp124-acosh_f64

	.section	.text.tanh_f64,"axG",@progbits,tanh_f64,comdat
	.weak	tanh_f64
	.align	32, 0x90
	.type	tanh_f64,@function
tanh_f64:                               # @tanh_f64
# BB#0:
	naclcall	.L40$pb
.L40$pb:
	popl	%eax
.Ltmp125:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp125-.L40$pb), %eax
	movl	tanh@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp126:
	.size	tanh_f64, .Ltmp126-tanh_f64

	.section	.text.atanh_f64,"axG",@progbits,atanh_f64,comdat
	.weak	atanh_f64
	.align	32, 0x90
	.type	atanh_f64,@function
atanh_f64:                              # @atanh_f64
# BB#0:
	naclcall	.L41$pb
.L41$pb:
	popl	%eax
.Ltmp127:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp127-.L41$pb), %eax
	movl	atanh@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp128:
	.size	atanh_f64, .Ltmp128-atanh_f64

	.section	.text.hypot_f64,"axG",@progbits,hypot_f64,comdat
	.weak	hypot_f64
	.align	32, 0x90
	.type	hypot_f64,@function
hypot_f64:                              # @hypot_f64
# BB#0:
	naclcall	.L42$pb
.L42$pb:
	popl	%eax
.Ltmp129:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp129-.L42$pb), %eax
	movl	hypot@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp130:
	.size	hypot_f64, .Ltmp130-hypot_f64

	.section	.text.exp_f64,"axG",@progbits,exp_f64,comdat
	.weak	exp_f64
	.align	32, 0x90
	.type	exp_f64,@function
exp_f64:                                # @exp_f64
# BB#0:
	naclcall	.L43$pb
.L43$pb:
	popl	%eax
.Ltmp131:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp131-.L43$pb), %eax
	movl	exp@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp132:
	.size	exp_f64, .Ltmp132-exp_f64

	.section	.text.log_f64,"axG",@progbits,log_f64,comdat
	.weak	log_f64
	.align	32, 0x90
	.type	log_f64,@function
log_f64:                                # @log_f64
# BB#0:
	naclcall	.L44$pb
.L44$pb:
	popl	%eax
.Ltmp133:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp133-.L44$pb), %eax
	movl	log@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp134:
	.size	log_f64, .Ltmp134-log_f64

	.section	.text.pow_f64,"axG",@progbits,pow_f64,comdat
	.weak	pow_f64
	.align	32, 0x90
	.type	pow_f64,@function
pow_f64:                                # @pow_f64
# BB#0:
	naclcall	.L45$pb
.L45$pb:
	popl	%eax
.Ltmp135:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp135-.L45$pb), %eax
	movl	pow@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp136:
	.size	pow_f64, .Ltmp136-pow_f64

	.section	.text.floor_f64,"axG",@progbits,floor_f64,comdat
	.weak	floor_f64
	.align	32, 0x90
	.type	floor_f64,@function
floor_f64:                              # @floor_f64
# BB#0:
	subl	$12, %esp
	movsd	16(%esp), %xmm0
	roundsd	$1, %xmm0, %xmm0
	movsd	%xmm0, (%esp)
	fldl	(%esp)
	addl	$12, %esp
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp137:
	.size	floor_f64, .Ltmp137-floor_f64

	.section	.text.ceil_f64,"axG",@progbits,ceil_f64,comdat
	.weak	ceil_f64
	.align	32, 0x90
	.type	ceil_f64,@function
ceil_f64:                               # @ceil_f64
# BB#0:
	subl	$12, %esp
	movsd	16(%esp), %xmm0
	roundsd	$2, %xmm0, %xmm0
	movsd	%xmm0, (%esp)
	fldl	(%esp)
	addl	$12, %esp
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp138:
	.size	ceil_f64, .Ltmp138-ceil_f64

	.section	.text.round_f64,"axG",@progbits,round_f64,comdat
	.weak	round_f64
	.align	32, 0x90
	.type	round_f64,@function
round_f64:                              # @round_f64
# BB#0:
	naclcall	.L48$pb
.L48$pb:
	popl	%eax
.Ltmp139:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp139-.L48$pb), %eax
	movl	round@GOT(%eax), %eax
	nacljmp	%eax
	.align	32, 0x90
.Ltmp140:
	.size	round_f64, .Ltmp140-round_f64

	.section	.rodata.cst4,"aM",@progbits,4
	.align	4
.LCPI49_0:
	.long	2139095039              # float 3.4028235E+38
	.section	.text.maxval_f32,"axG",@progbits,maxval_f32,comdat
	.weak	maxval_f32
	.align	32, 0x90
	.type	maxval_f32,@function
maxval_f32:                             # @maxval_f32
# BB#0:
	naclcall	.L49$pb
.L49$pb:
	popl	%eax
.Ltmp141:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp141-.L49$pb), %eax
	flds	.LCPI49_0@GOTOFF(%eax)
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp142:
	.size	maxval_f32, .Ltmp142-maxval_f32

	.section	.rodata.cst4,"aM",@progbits,4
	.align	4
.LCPI50_0:
	.long	4286578687              # float -3.4028235E+38
	.section	.text.minval_f32,"axG",@progbits,minval_f32,comdat
	.weak	minval_f32
	.align	32, 0x90
	.type	minval_f32,@function
minval_f32:                             # @minval_f32
# BB#0:
	naclcall	.L50$pb
.L50$pb:
	popl	%eax
.Ltmp143:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp143-.L50$pb), %eax
	flds	.LCPI50_0@GOTOFF(%eax)
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp144:
	.size	minval_f32, .Ltmp144-minval_f32

	.section	.rodata.cst8,"aM",@progbits,8
	.align	8
.LCPI51_0:
	.quad	9218868437227405311     # double 1.797693134862316E+308
	.section	.text.maxval_f64,"axG",@progbits,maxval_f64,comdat
	.weak	maxval_f64
	.align	32, 0x90
	.type	maxval_f64,@function
maxval_f64:                             # @maxval_f64
# BB#0:
	naclcall	.L51$pb
.L51$pb:
	popl	%eax
.Ltmp145:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp145-.L51$pb), %eax
	fldl	.LCPI51_0@GOTOFF(%eax)
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp146:
	.size	maxval_f64, .Ltmp146-maxval_f64

	.section	.rodata.cst8,"aM",@progbits,8
	.align	8
.LCPI52_0:
	.quad	-4503599627370497       # double -1.797693134862316E+308
	.section	.text.minval_f64,"axG",@progbits,minval_f64,comdat
	.weak	minval_f64
	.align	32, 0x90
	.type	minval_f64,@function
minval_f64:                             # @minval_f64
# BB#0:
	naclcall	.L52$pb
.L52$pb:
	popl	%eax
.Ltmp147:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp147-.L52$pb), %eax
	fldl	.LCPI52_0@GOTOFF(%eax)
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp148:
	.size	minval_f64, .Ltmp148-minval_f64

	.section	.text.maxval_u8,"axG",@progbits,maxval_u8,comdat
	.weak	maxval_u8
	.align	32, 0x90
	.type	maxval_u8,@function
maxval_u8:                              # @maxval_u8
# BB#0:
	movl	$255, %eax
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp149:
	.size	maxval_u8, .Ltmp149-maxval_u8

	.section	.text.minval_u8,"axG",@progbits,minval_u8,comdat
	.weak	minval_u8
	.align	32, 0x90
	.type	minval_u8,@function
minval_u8:                              # @minval_u8
# BB#0:
	xorl	%eax, %eax
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp150:
	.size	minval_u8, .Ltmp150-minval_u8

	.section	.text.maxval_u16,"axG",@progbits,maxval_u16,comdat
	.weak	maxval_u16
	.align	32, 0x90
	.type	maxval_u16,@function
maxval_u16:                             # @maxval_u16
# BB#0:
	movl	$65535, %eax            # imm = 0xFFFF
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp151:
	.size	maxval_u16, .Ltmp151-maxval_u16

	.section	.text.minval_u16,"axG",@progbits,minval_u16,comdat
	.weak	minval_u16
	.align	32, 0x90
	.type	minval_u16,@function
minval_u16:                             # @minval_u16
# BB#0:
	xorl	%eax, %eax
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp152:
	.size	minval_u16, .Ltmp152-minval_u16

	.section	.text.maxval_u32,"axG",@progbits,maxval_u32,comdat
	.weak	maxval_u32
	.align	32, 0x90
	.type	maxval_u32,@function
maxval_u32:                             # @maxval_u32
# BB#0:
	movl	$-1, %eax
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp153:
	.size	maxval_u32, .Ltmp153-maxval_u32

	.section	.text.minval_u32,"axG",@progbits,minval_u32,comdat
	.weak	minval_u32
	.align	32, 0x90
	.type	minval_u32,@function
minval_u32:                             # @minval_u32
# BB#0:
	xorl	%eax, %eax
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp154:
	.size	minval_u32, .Ltmp154-minval_u32

	.section	.text.maxval_u64,"axG",@progbits,maxval_u64,comdat
	.weak	maxval_u64
	.align	32, 0x90
	.type	maxval_u64,@function
maxval_u64:                             # @maxval_u64
# BB#0:
	movl	$-1, %eax
	movl	$-1, %edx
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp155:
	.size	maxval_u64, .Ltmp155-maxval_u64

	.section	.text.minval_u64,"axG",@progbits,minval_u64,comdat
	.weak	minval_u64
	.align	32, 0x90
	.type	minval_u64,@function
minval_u64:                             # @minval_u64
# BB#0:
	xorl	%eax, %eax
	xorl	%edx, %edx
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp156:
	.size	minval_u64, .Ltmp156-minval_u64

	.section	.text.maxval_s8,"axG",@progbits,maxval_s8,comdat
	.weak	maxval_s8
	.align	32, 0x90
	.type	maxval_s8,@function
maxval_s8:                              # @maxval_s8
# BB#0:
	movl	$127, %eax
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp157:
	.size	maxval_s8, .Ltmp157-maxval_s8

	.section	.text.minval_s8,"axG",@progbits,minval_s8,comdat
	.weak	minval_s8
	.align	32, 0x90
	.type	minval_s8,@function
minval_s8:                              # @minval_s8
# BB#0:
	movl	$-128, %eax
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp158:
	.size	minval_s8, .Ltmp158-minval_s8

	.section	.text.maxval_s16,"axG",@progbits,maxval_s16,comdat
	.weak	maxval_s16
	.align	32, 0x90
	.type	maxval_s16,@function
maxval_s16:                             # @maxval_s16
# BB#0:
	movl	$32767, %eax            # imm = 0x7FFF
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp159:
	.size	maxval_s16, .Ltmp159-maxval_s16

	.section	.text.minval_s16,"axG",@progbits,minval_s16,comdat
	.weak	minval_s16
	.align	32, 0x90
	.type	minval_s16,@function
minval_s16:                             # @minval_s16
# BB#0:
	movl	$-32768, %eax           # imm = 0xFFFFFFFFFFFF8000
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp160:
	.size	minval_s16, .Ltmp160-minval_s16

	.section	.text.maxval_s32,"axG",@progbits,maxval_s32,comdat
	.weak	maxval_s32
	.align	32, 0x90
	.type	maxval_s32,@function
maxval_s32:                             # @maxval_s32
# BB#0:
	movl	$2147483647, %eax       # imm = 0x7FFFFFFF
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp161:
	.size	maxval_s32, .Ltmp161-maxval_s32

	.section	.text.minval_s32,"axG",@progbits,minval_s32,comdat
	.weak	minval_s32
	.align	32, 0x90
	.type	minval_s32,@function
minval_s32:                             # @minval_s32
# BB#0:
	movl	$-2147483648, %eax      # imm = 0xFFFFFFFF80000000
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp162:
	.size	minval_s32, .Ltmp162-minval_s32

	.section	.text.maxval_s64,"axG",@progbits,maxval_s64,comdat
	.weak	maxval_s64
	.align	32, 0x90
	.type	maxval_s64,@function
maxval_s64:                             # @maxval_s64
# BB#0:
	movl	$-1, %eax
	movl	$2147483647, %edx       # imm = 0x7FFFFFFF
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp163:
	.size	maxval_s64, .Ltmp163-maxval_s64

	.section	.text.minval_s64,"axG",@progbits,minval_s64,comdat
	.weak	minval_s64
	.align	32, 0x90
	.type	minval_s64,@function
minval_s64:                             # @minval_s64
# BB#0:
	xorl	%eax, %eax
	movl	$-2147483648, %edx      # imm = 0xFFFFFFFF80000000
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp164:
	.size	minval_s64, .Ltmp164-minval_s64

	.section	.text.abs_i8,"axG",@progbits,abs_i8,comdat
	.weak	abs_i8
	.align	32, 0x90
	.type	abs_i8,@function
abs_i8:                                 # @abs_i8
# BB#0:
	movb	4(%esp), %al
	movb	%al, %cl
	sarb	$7, %cl
	addb	%cl, %al
	xorb	%cl, %al
	movsbl	%al, %eax
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp165:
	.size	abs_i8, .Ltmp165-abs_i8

	.section	.text.abs_i16,"axG",@progbits,abs_i16,comdat
	.weak	abs_i16
	.align	32, 0x90
	.type	abs_i16,@function
abs_i16:                                # @abs_i16
# BB#0:
	movw	4(%esp), %ax
	movw	%ax, %cx
	negw	%cx
	cmovlw	%ax, %cx
	movswl	%cx, %eax
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp166:
	.size	abs_i16, .Ltmp166-abs_i16

	.section	.text.abs_i32,"axG",@progbits,abs_i32,comdat
	.weak	abs_i32
	.align	32, 0x90
	.type	abs_i32,@function
abs_i32:                                # @abs_i32
# BB#0:
	movl	4(%esp), %ecx
	movl	%ecx, %eax
	negl	%eax
	cmovll	%ecx, %eax
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp167:
	.size	abs_i32, .Ltmp167-abs_i32

	.section	.text.abs_i64,"axG",@progbits,abs_i64,comdat
	.weak	abs_i64
	.align	32, 0x90
	.type	abs_i64,@function
abs_i64:                                # @abs_i64
# BB#0:
	movl	8(%esp), %edx
	movl	%edx, %ecx
	sarl	$31, %ecx
	movl	4(%esp), %eax
	addl	%ecx, %eax
	adcl	%ecx, %edx
	xorl	%ecx, %edx
	xorl	%ecx, %eax
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp168:
	.size	abs_i64, .Ltmp168-abs_i64

	.section	.rodata.cst16,"aM",@progbits,16
	.align	16
.LCPI73_0:
	.long	2147483647              # float nan
	.long	2147483647              # float nan
	.long	2147483647              # float nan
	.long	2147483647              # float nan
	.section	.text.abs_f32,"axG",@progbits,abs_f32,comdat
	.weak	abs_f32
	.align	32, 0x90
	.type	abs_f32,@function
abs_f32:                                # @abs_f32
# BB#0:
	pushl	%eax
	naclcall	.L73$pb
.L73$pb:
	popl	%eax
.Ltmp169:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp169-.L73$pb), %eax
	movss	8(%esp), %xmm0
	andps	.LCPI73_0@GOTOFF(%eax), %xmm0
	movss	%xmm0, (%esp)
	flds	(%esp)
	popl	%eax
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp170:
	.size	abs_f32, .Ltmp170-abs_f32

	.section	.rodata.cst16,"aM",@progbits,16
	.align	16
.LCPI74_0:
	.quad	9223372036854775807     # double nan
	.quad	9223372036854775807     # double nan
	.section	.text.abs_f64,"axG",@progbits,abs_f64,comdat
	.weak	abs_f64
	.align	32, 0x90
	.type	abs_f64,@function
abs_f64:                                # @abs_f64
# BB#0:
	subl	$12, %esp
	naclcall	.L74$pb
.L74$pb:
	popl	%eax
.Ltmp171:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp171-.L74$pb), %eax
	movsd	16(%esp), %xmm0
	andpd	.LCPI74_0@GOTOFF(%eax), %xmm0
	movsd	%xmm0, (%esp)
	fldl	(%esp)
	addl	$12, %esp
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp172:
	.size	abs_f64, .Ltmp172-abs_f64

	.section	.text.halide_shutdown_thread_pool,"axG",@progbits,halide_shutdown_thread_pool,comdat
	.weak	halide_shutdown_thread_pool
	.align	32, 0x90
	.type	halide_shutdown_thread_pool,@function
halide_shutdown_thread_pool:            # @halide_shutdown_thread_pool
	.cfi_startproc
# BB#0:
	pushl	%ebp
.Ltmp178:
	.cfi_def_cfa_offset 8
	pushl	%ebx
.Ltmp179:
	.cfi_def_cfa_offset 12
	pushl	%edi
.Ltmp180:
	.cfi_def_cfa_offset 16
	pushl	%esi
.Ltmp181:
	.cfi_def_cfa_offset 20
	subl	$28, %esp
.Ltmp182:
	.cfi_def_cfa_offset 48
.Ltmp183:
	.cfi_offset %esi, -20
.Ltmp184:
	.cfi_offset %edi, -16
.Ltmp185:
	.cfi_offset %ebx, -12
.Ltmp186:
	.cfi_offset %ebp, -8
	naclcall	.L75$pb
.L75$pb:
	popl	%ebx
.Ltmp187:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp187-.L75$pb), %ebx
	movl	halide_thread_pool_initialized@GOT(%ebx), %eax
	movl	%eax, 20(%esp)          # 4-byte Spill
	cmpb	$0, (%eax)
	je	.LBB75_5
# BB#1:
	movl	halide_work_queue@GOT(%ebx), %esi
	leal	1572872(%esi), %edi
	movl	%edi, 16(%esp)          # 4-byte Spill
	movl	%edi, (%esp)
	naclcall	pthread_mutex_lock@PLT
	movb	$1, 1573220(%esi)
	leal	1572912(%esi), %eax
	movl	%eax, 12(%esp)          # 4-byte Spill
	movl	%eax, (%esp)
	naclcall	pthread_cond_broadcast@PLT
	movl	%edi, (%esp)
	naclcall	pthread_mutex_unlock@PLT
	movl	halide_threads@GOT(%ebx), %ebp
	movl	(%ebp), %eax
	decl	%eax
	testl	%eax, %eax
	jle	.LBB75_4
# BB#2:
	xorl	%edi, %edi
	.align	16, 0x90
.LBB75_3:                               # %.lr.ph
                                        # =>This Inner Loop Header: Depth=1
	movl	1572960(%esi,%edi,4), %eax
	leal	24(%esp), %ecx
	movl	%ecx, 4(%esp)
	movl	%eax, (%esp)
	naclcall	pthread_join@PLT
	incl	%edi
	movl	(%ebp), %eax
	decl	%eax
	cmpl	%eax, %edi
	jl	.LBB75_3
.LBB75_4:                               # %._crit_edge
	movl	16(%esp), %eax          # 4-byte Reload
	movl	%eax, (%esp)
	naclcall	pthread_mutex_destroy@PLT
	movl	12(%esp), %eax          # 4-byte Reload
	movl	%eax, (%esp)
	naclcall	pthread_cond_destroy@PLT
	movl	20(%esp), %eax          # 4-byte Reload
	movb	$0, (%eax)
.LBB75_5:
	addl	$28, %esp
	popl	%esi
	popl	%edi
	popl	%ebx
	popl	%ebp
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp188:
	.size	halide_shutdown_thread_pool, .Ltmp188-halide_shutdown_thread_pool
	.cfi_endproc

	.section	.text.halide_set_custom_do_task,"axG",@progbits,halide_set_custom_do_task,comdat
	.weak	halide_set_custom_do_task
	.align	32, 0x90
	.type	halide_set_custom_do_task,@function
halide_set_custom_do_task:              # @halide_set_custom_do_task
	.cfi_startproc
# BB#0:
	naclcall	.L76$pb
.L76$pb:
	popl	%eax
.Ltmp189:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp189-.L76$pb), %eax
	movl	4(%esp), %ecx
	movl	halide_custom_do_task@GOT(%eax), %eax
	movl	%ecx, (%eax)
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp190:
	.size	halide_set_custom_do_task, .Ltmp190-halide_set_custom_do_task
	.cfi_endproc

	.section	.text.halide_set_custom_do_par_for,"axG",@progbits,halide_set_custom_do_par_for,comdat
	.weak	halide_set_custom_do_par_for
	.align	32, 0x90
	.type	halide_set_custom_do_par_for,@function
halide_set_custom_do_par_for:           # @halide_set_custom_do_par_for
	.cfi_startproc
# BB#0:
	naclcall	.L77$pb
.L77$pb:
	popl	%eax
.Ltmp191:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp191-.L77$pb), %eax
	movl	4(%esp), %ecx
	movl	halide_custom_do_par_for@GOT(%eax), %eax
	movl	%ecx, (%eax)
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp192:
	.size	halide_set_custom_do_par_for, .Ltmp192-halide_set_custom_do_par_for
	.cfi_endproc

	.section	.text.halide_do_task,"axG",@progbits,halide_do_task,comdat
	.weak	halide_do_task
	.align	32, 0x90
	.type	halide_do_task,@function
halide_do_task:                         # @halide_do_task
	.cfi_startproc
# BB#0:
	pushl	%ebx
.Ltmp195:
	.cfi_def_cfa_offset 8
	subl	$8, %esp
.Ltmp196:
	.cfi_def_cfa_offset 16
.Ltmp197:
	.cfi_offset %ebx, -8
	naclcall	.L78$pb
.L78$pb:
	popl	%ebx
.Ltmp198:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp198-.L78$pb), %ebx
	movl	halide_custom_do_task@GOT(%ebx), %eax
	movl	(%eax), %eax
	testl	%eax, %eax
	je	.LBB78_1
# BB#2:
	addl	$8, %esp
	popl	%ebx
	nacljmp	%eax
.LBB78_1:
	movl	24(%esp), %edx
	movl	20(%esp), %ecx
	movl	16(%esp), %eax
	movl	%edx, 4(%esp)
	movl	%ecx, (%esp)
	naclcall	%eax
	addl	$8, %esp
	popl	%ebx
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp199:
	.size	halide_do_task, .Ltmp199-halide_do_task
	.cfi_endproc

	.section	.text.halide_worker_thread,"axG",@progbits,halide_worker_thread,comdat
	.weak	halide_worker_thread
	.align	32, 0x90
	.type	halide_worker_thread,@function
halide_worker_thread:                   # @halide_worker_thread
	.cfi_startproc
# BB#0:
	pushl	%ebp
.Ltmp205:
	.cfi_def_cfa_offset 8
	pushl	%ebx
.Ltmp206:
	.cfi_def_cfa_offset 12
	pushl	%edi
.Ltmp207:
	.cfi_def_cfa_offset 16
	pushl	%esi
.Ltmp208:
	.cfi_def_cfa_offset 20
	subl	$44, %esp
.Ltmp209:
	.cfi_def_cfa_offset 64
.Ltmp210:
	.cfi_offset %esi, -20
.Ltmp211:
	.cfi_offset %edi, -16
.Ltmp212:
	.cfi_offset %ebx, -12
.Ltmp213:
	.cfi_offset %ebp, -8
	naclcall	.L79$pb
.L79$pb:
	popl	%ebx
.Ltmp214:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp214-.L79$pb), %ebx
	movl	$1572872, %eax          # imm = 0x180008
	addl	halide_work_queue@GOT(%ebx), %eax
	movl	%eax, (%esp)
	naclcall	pthread_mutex_lock@PLT
	movl	64(%esp), %edx
	testl	%edx, %edx
	je	.LBB79_11
# BB#1:                                 # %.backedge.preheader
	movl	4(%edx), %eax
	movl	16(%eax), %ecx
	cmpl	(%edx), %ecx
	jne	.LBB79_7
# BB#2:
	movl	halide_work_queue@GOT(%ebx), %edi
	movl	$1572872, %eax          # imm = 0x180008
	addl	halide_work_queue@GOT(%ebx), %eax
	movl	%eax, 28(%esp)          # 4-byte Spill
	.align	16, 0x90
.LBB79_3:                               # %.lr.ph4
                                        # =>This Inner Loop Header: Depth=1
	cmpb	$0, 1573220(%edi)
	jne	.LBB79_10
# BB#4:                                 #   in Loop: Header=BB79_3 Depth=1
	movl	1572864(%edi), %eax
	cmpl	1572868(%edi), %eax
	jne	.LBB79_19
# BB#5:                                 #   in Loop: Header=BB79_3 Depth=1
	leal	1572872(%edi), %esi
	movl	%esi, 4(%esp)
	leal	1572912(%edi), %eax
	movl	%eax, (%esp)
	naclcall	pthread_cond_wait@PLT
	movl	%esi, (%esp)
	jmp	.LBB79_6
	.align	16, 0x90
.LBB79_19:                              #   in Loop: Header=BB79_3 Depth=1
	imull	$24, %eax, %esi
	movl	4(%edi,%esi), %ebp
	cmpl	8(%edi,%esi), %ebp
	jne	.LBB79_21
# BB#20:                                #   in Loop: Header=BB79_3 Depth=1
	leal	1(%eax), %ecx
	sarl	$31, %ecx
	shrl	$16, %ecx
	leal	1(%eax,%ecx), %ecx
	andl	$-65536, %ecx           # imm = 0xFFFFFFFFFFFF0000
	negl	%ecx
	leal	1(%eax,%ecx), %eax
	movl	%eax, 1572864(%edi)
	movl	$0, 16(%edi,%esi)
	leal	1572872(%edi), %eax
	movl	%eax, (%esp)
	jmp	.LBB79_6
	.align	16, 0x90
.LBB79_21:                              #   in Loop: Header=BB79_3 Depth=1
	leal	4(%edi,%esi), %eax
	movl	(%edi,%esi), %ecx
	movl	%ecx, 40(%esp)          # 4-byte Spill
	movl	12(%edi,%esi), %ecx
	movl	%ecx, 36(%esp)          # 4-byte Spill
	leal	1(%ebp), %ecx
	movl	%ecx, (%eax)
	incl	20(%edi,%esi)
	leal	1572872(%edi), %eax
	movl	%eax, 32(%esp)          # 4-byte Spill
	movl	%eax, (%esp)
	naclcall	pthread_mutex_unlock@PLT
	movl	36(%esp), %eax          # 4-byte Reload
	movl	%eax, 8(%esp)
	movl	%ebp, 4(%esp)
	movl	40(%esp), %eax          # 4-byte Reload
	movl	%eax, (%esp)
	naclcall	halide_do_task@PLT
	movl	32(%esp), %ebp          # 4-byte Reload
	movl	%ebp, (%esp)
	naclcall	pthread_mutex_lock@PLT
	decl	20(%edi,%esi)
	movl	%ebp, (%esp)
.LBB79_6:                               #   in Loop: Header=BB79_3 Depth=1
	naclcall	pthread_mutex_unlock@PLT
	movl	28(%esp), %eax          # 4-byte Reload
	movl	%eax, (%esp)
	naclcall	pthread_mutex_lock@PLT
	movl	64(%esp), %edx
	movl	4(%edx), %eax
	movl	16(%eax), %ecx
	cmpl	(%edx), %ecx
	je	.LBB79_3
.LBB79_7:                               # %.us-lcssa
	cmpl	$0, 20(%eax)
	movl	64(%esp), %edi
	je	.LBB79_10
# BB#8:
	movl	$1572872, %esi          # imm = 0x180008
	addl	halide_work_queue@GOT(%ebx), %esi
	.align	16, 0x90
.LBB79_9:                               # %.lr.ph
                                        # =>This Inner Loop Header: Depth=1
	movl	%esi, (%esp)
	naclcall	pthread_mutex_unlock@PLT
	movl	%esi, (%esp)
	naclcall	pthread_mutex_lock@PLT
	movl	4(%edi), %eax
	cmpl	$0, 20(%eax)
	jne	.LBB79_9
	jmp	.LBB79_10
.LBB79_11:                              # %.backedge.us.preheader
	movl	halide_work_queue@GOT(%ebx), %esi
	jmp	.LBB79_12
	.align	16, 0x90
.LBB79_16:                              #   in Loop: Header=BB79_12 Depth=1
	naclcall	pthread_mutex_unlock@PLT
	leal	1572872(%esi), %eax
	movl	%eax, (%esp)
	naclcall	pthread_mutex_lock@PLT
.LBB79_12:                              # %.backedge.us.preheader
                                        # =>This Inner Loop Header: Depth=1
	cmpb	$0, 1573220(%esi)
	jne	.LBB79_10
# BB#13:                                # %.lr.ph1
                                        #   in Loop: Header=BB79_12 Depth=1
	movl	1572864(%esi), %eax
	cmpl	1572868(%esi), %eax
	jne	.LBB79_14
# BB#18:                                #   in Loop: Header=BB79_12 Depth=1
	leal	1572872(%esi), %edi
	movl	%edi, 4(%esp)
	leal	1572912(%esi), %eax
	movl	%eax, (%esp)
	naclcall	pthread_cond_wait@PLT
	movl	%edi, (%esp)
	jmp	.LBB79_16
.LBB79_14:                              #   in Loop: Header=BB79_12 Depth=1
	imull	$24, %eax, %edi
	movl	4(%esi,%edi), %ebp
	cmpl	8(%esi,%edi), %ebp
	jne	.LBB79_15
# BB#17:                                #   in Loop: Header=BB79_12 Depth=1
	leal	1(%eax), %ecx
	sarl	$31, %ecx
	shrl	$16, %ecx
	leal	1(%eax,%ecx), %ecx
	andl	$-65536, %ecx           # imm = 0xFFFFFFFFFFFF0000
	negl	%ecx
	leal	1(%eax,%ecx), %eax
	movl	%eax, 1572864(%esi)
	movl	$0, 16(%esi,%edi)
	leal	1572872(%esi), %eax
	movl	%eax, (%esp)
	jmp	.LBB79_16
.LBB79_15:                              #   in Loop: Header=BB79_12 Depth=1
	leal	4(%esi,%edi), %eax
	movl	(%esi,%edi), %ecx
	movl	%ecx, 40(%esp)          # 4-byte Spill
	movl	12(%esi,%edi), %ecx
	movl	%ecx, 36(%esp)          # 4-byte Spill
	leal	1(%ebp), %ecx
	movl	%ecx, (%eax)
	incl	20(%esi,%edi)
	leal	1572872(%esi), %eax
	movl	%eax, 32(%esp)          # 4-byte Spill
	movl	%eax, (%esp)
	naclcall	pthread_mutex_unlock@PLT
	movl	36(%esp), %eax          # 4-byte Reload
	movl	%eax, 8(%esp)
	movl	%ebp, 4(%esp)
	movl	40(%esp), %eax          # 4-byte Reload
	movl	%eax, (%esp)
	naclcall	halide_do_task@PLT
	movl	32(%esp), %ebp          # 4-byte Reload
	movl	%ebp, (%esp)
	naclcall	pthread_mutex_lock@PLT
	decl	20(%esi,%edi)
	movl	%ebp, (%esp)
	jmp	.LBB79_16
.LBB79_10:                              # %.loopexit
	movl	$1572872, %eax          # imm = 0x180008
	addl	halide_work_queue@GOT(%ebx), %eax
	movl	%eax, (%esp)
	naclcall	pthread_mutex_unlock@PLT
	xorl	%eax, %eax
	addl	$44, %esp
	popl	%esi
	popl	%edi
	popl	%ebx
	popl	%ebp
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp215:
	.size	halide_worker_thread, .Ltmp215-halide_worker_thread
	.cfi_endproc

	.section	.text.halide_do_par_for,"axG",@progbits,halide_do_par_for,comdat
	.weak	halide_do_par_for
	.align	32, 0x90
	.type	halide_do_par_for,@function
halide_do_par_for:                      # @halide_do_par_for
	.cfi_startproc
# BB#0:
	pushl	%ebp
.Ltmp221:
	.cfi_def_cfa_offset 8
	pushl	%ebx
.Ltmp222:
	.cfi_def_cfa_offset 12
	pushl	%edi
.Ltmp223:
	.cfi_def_cfa_offset 16
	pushl	%esi
.Ltmp224:
	.cfi_def_cfa_offset 20
	subl	$44, %esp
.Ltmp225:
	.cfi_def_cfa_offset 64
.Ltmp226:
	.cfi_offset %esi, -20
.Ltmp227:
	.cfi_offset %edi, -16
.Ltmp228:
	.cfi_offset %ebx, -12
.Ltmp229:
	.cfi_offset %ebp, -8
	naclcall	.L80$pb
.L80$pb:
	popl	%ebx
.Ltmp230:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp230-.L80$pb), %ebx
	movl	halide_custom_do_par_for@GOT(%ebx), %eax
	movl	(%eax), %eax
	movl	76(%esp), %ecx
	movl	72(%esp), %edx
	movl	68(%esp), %esi
	movl	64(%esp), %edi
	testl	%eax, %eax
	je	.LBB80_2
# BB#1:
	movl	%ecx, 12(%esp)
	movl	%edx, 8(%esp)
	movl	%esi, 4(%esp)
	movl	%edi, (%esp)
	naclcall	%eax
	jmp	.LBB80_12
.LBB80_2:
	movl	halide_thread_pool_initialized@GOT(%ebx), %eax
	movl	%eax, 28(%esp)          # 4-byte Spill
	cmpb	$0, (%eax)
	jne	.LBB80_11
# BB#3:
	movl	halide_work_queue@GOT(%ebx), %ebp
	movb	$0, 1573220(%ebp)
	leal	1572872(%ebp), %eax
	movl	%eax, (%esp)
	movl	$0, 4(%esp)
	naclcall	pthread_mutex_init@PLT
	leal	1572912(%ebp), %eax
	movl	%eax, (%esp)
	movl	$0, 4(%esp)
	naclcall	pthread_cond_init@PLT
	movl	$0, 1572868(%ebp)
	movl	$0, 1572864(%ebp)
	movl	$1, 1573216(%ebp)
	leal	.L.str2@GOTOFF(%ebx), %eax
	movl	%eax, (%esp)
	naclcall	getenv@PLT
	movl	halide_threads@GOT(%ebx), %esi
	movl	$2, (%esi)
	testl	%eax, %eax
	je	.LBB80_5
# BB#4:
	movl	%eax, (%esp)
	naclcall	atoi@PLT
	movl	%eax, (%esi)
	jmp	.LBB80_6
.LBB80_5:
	leal	.L.str3@GOTOFF(%ebx), %eax
	movl	%eax, (%esp)
	movl	$2, 4(%esp)
	naclcall	halide_printf@PLT
	movl	(%esi), %eax
.LBB80_6:
	cmpl	$65, %eax
	jl	.LBB80_8
# BB#7:                                 # %.preheader.thread
	movl	$64, (%esi)
	xorl	%edi, %edi
	jmp	.LBB80_9
.LBB80_8:                               # %.preheader
	decl	%eax
	xorl	%edi, %edi
	testl	%eax, %eax
	jle	.LBB80_10
	.align	16, 0x90
.LBB80_9:                               # %.lr.ph
                                        # =>This Inner Loop Header: Depth=1
	movl	halide_worker_thread@GOT(%ebx), %eax
	movl	%eax, 8(%esp)
	leal	1572960(%ebp,%edi,4), %eax
	movl	%eax, (%esp)
	movl	$0, 12(%esp)
	movl	$0, 4(%esp)
	naclcall	pthread_create@PLT
	incl	%edi
	movl	(%esi), %eax
	decl	%eax
	cmpl	%eax, %edi
	jl	.LBB80_9
.LBB80_10:                              # %._crit_edge
	movl	28(%esp), %eax          # 4-byte Reload
	movb	$1, (%eax)
.LBB80_11:
	movl	halide_work_queue@GOT(%ebx), %esi
	leal	1572872(%esi), %ebp
	movl	%ebp, (%esp)
	naclcall	pthread_mutex_lock@PLT
	movl	1573216(%esi), %eax
	leal	2(%eax), %edx
	leal	1(%eax), %ecx
	testl	%eax, %eax
	cmovnel	%ecx, %edx
	movl	%edx, 1573216(%esi)
	cmovel	%ecx, %eax
	imull	$24, 1572868(%esi), %ecx
	movl	64(%esp), %edx
	movl	%edx, (%esi,%ecx)
	movl	68(%esp), %edi
	movl	%edi, 4(%esi,%ecx)
	movl	72(%esp), %edx
	addl	%edi, %edx
	movl	%edx, 8(%esi,%ecx)
	movl	76(%esp), %edx
	movl	%edx, 12(%esi,%ecx)
	movl	%eax, 16(%esi,%ecx)
	movl	$0, 20(%esi,%ecx)
	movl	1572868(%esi), %ecx
	movl	%eax, 32(%esp)
	imull	$24, %ecx, %eax
	addl	%esi, %eax
	movl	%eax, 36(%esp)
	leal	1(%ecx), %eax
	sarl	$31, %eax
	shrl	$16, %eax
	leal	1(%ecx,%eax), %eax
	andl	$-65536, %eax           # imm = 0xFFFFFFFFFFFF0000
	negl	%eax
	leal	1(%ecx,%eax), %eax
	movl	%eax, 1572868(%esi)
	movl	%ebp, (%esp)
	naclcall	pthread_mutex_unlock@PLT
	addl	$1572912, %esi          # imm = 0x180030
	movl	%esi, (%esp)
	naclcall	pthread_cond_broadcast@PLT
	leal	32(%esp), %eax
	movl	%eax, (%esp)
	naclcall	halide_worker_thread@PLT
.LBB80_12:
	addl	$44, %esp
	popl	%esi
	popl	%edi
	popl	%ebx
	popl	%ebp
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp231:
	.size	halide_do_par_for, .Ltmp231-halide_do_par_for
	.cfi_endproc

	.section	.text.halide_copy_to_host,"axG",@progbits,halide_copy_to_host,comdat
	.weak	halide_copy_to_host
	.align	32, 0x90
	.type	halide_copy_to_host,@function
halide_copy_to_host:                    # @halide_copy_to_host
	.cfi_startproc
# BB#0:
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp232:
	.size	halide_copy_to_host, .Ltmp232-halide_copy_to_host
	.cfi_endproc

	.section	.text.packsswbx16,"axG",@progbits,packsswbx16,comdat
	.weak	packsswbx16
	.align	32, 0x90
	.type	packsswbx16,@function
packsswbx16:                            # @packsswbx16
# BB#0:
	packsswb	%xmm1, %xmm0
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp233:
	.size	packsswbx16, .Ltmp233-packsswbx16

	.section	.text.packuswbx16,"axG",@progbits,packuswbx16,comdat
	.weak	packuswbx16
	.align	32, 0x90
	.type	packuswbx16,@function
packuswbx16:                            # @packuswbx16
# BB#0:
	packuswb	%xmm1, %xmm0
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp234:
	.size	packuswbx16, .Ltmp234-packuswbx16

	.section	.text.packssdwx8,"axG",@progbits,packssdwx8,comdat
	.weak	packssdwx8
	.align	32, 0x90
	.type	packssdwx8,@function
packssdwx8:                             # @packssdwx8
# BB#0:
	packssdw	%xmm1, %xmm0
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp235:
	.size	packssdwx8, .Ltmp235-packssdwx8

	.section	.text.abs_i8x16,"axG",@progbits,abs_i8x16,comdat
	.weak	abs_i8x16
	.align	32, 0x90
	.type	abs_i8x16,@function
abs_i8x16:                              # @abs_i8x16
	.cfi_startproc
# BB#0:
	pabsb	%xmm0, %xmm0
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp236:
	.size	abs_i8x16, .Ltmp236-abs_i8x16
	.cfi_endproc

	.section	.text.abs_i16x8,"axG",@progbits,abs_i16x8,comdat
	.weak	abs_i16x8
	.align	32, 0x90
	.type	abs_i16x8,@function
abs_i16x8:                              # @abs_i16x8
	.cfi_startproc
# BB#0:
	pabsw	%xmm0, %xmm0
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp237:
	.size	abs_i16x8, .Ltmp237-abs_i16x8
	.cfi_endproc

	.section	.text.abs_i32x4,"axG",@progbits,abs_i32x4,comdat
	.weak	abs_i32x4
	.align	32, 0x90
	.type	abs_i32x4,@function
abs_i32x4:                              # @abs_i32x4
	.cfi_startproc
# BB#0:
	pabsd	%xmm0, %xmm0
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp238:
	.size	abs_i32x4, .Ltmp238-abs_i32x4
	.cfi_endproc

	.section	.text.sqrt_f32x4,"axG",@progbits,sqrt_f32x4,comdat
	.weak	sqrt_f32x4
	.align	32, 0x90
	.type	sqrt_f32x4,@function
sqrt_f32x4:                             # @sqrt_f32x4
	.cfi_startproc
# BB#0:
	sqrtps	%xmm0, %xmm0
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp239:
	.size	sqrt_f32x4, .Ltmp239-sqrt_f32x4
	.cfi_endproc

	.section	.text.sqrt_f64x2,"axG",@progbits,sqrt_f64x2,comdat
	.weak	sqrt_f64x2
	.align	32, 0x90
	.type	sqrt_f64x2,@function
sqrt_f64x2:                             # @sqrt_f64x2
	.cfi_startproc
# BB#0:
	sqrtpd	%xmm0, %xmm0
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp240:
	.size	sqrt_f64x2, .Ltmp240-sqrt_f64x2
	.cfi_endproc

	.section	.rodata.cst16,"aM",@progbits,16
	.align	16
.LCPI90_0:
	.long	2147483647              # 0x7fffffff
	.long	2147483647              # 0x7fffffff
	.long	2147483647              # 0x7fffffff
	.long	2147483647              # 0x7fffffff
	.section	.text.abs_f32x4,"axG",@progbits,abs_f32x4,comdat
	.weak	abs_f32x4
	.align	32, 0x90
	.type	abs_f32x4,@function
abs_f32x4:                              # @abs_f32x4
	.cfi_startproc
# BB#0:
	naclcall	.L90$pb
.L90$pb:
	popl	%eax
.Ltmp241:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp241-.L90$pb), %eax
	andps	.LCPI90_0@GOTOFF(%eax), %xmm0
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp242:
	.size	abs_f32x4, .Ltmp242-abs_f32x4
	.cfi_endproc

	.section	.rodata.cst16,"aM",@progbits,16
	.align	16
.LCPI91_0:
	.long	4294967295              # 0xffffffff
	.long	2147483647              # 0x7fffffff
	.long	4294967295              # 0xffffffff
	.long	2147483647              # 0x7fffffff
	.section	.text.abs_f64x2,"axG",@progbits,abs_f64x2,comdat
	.weak	abs_f64x2
	.align	32, 0x90
	.type	abs_f64x2,@function
abs_f64x2:                              # @abs_f64x2
	.cfi_startproc
# BB#0:
	naclcall	.L91$pb
.L91$pb:
	popl	%eax
.Ltmp243:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp243-.L91$pb), %eax
	andps	.LCPI91_0@GOTOFF(%eax), %xmm0
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp244:
	.size	abs_f64x2, .Ltmp244-abs_f64x2
	.cfi_endproc

	.text
	.globl	halide_game_of_life
	.align	32, 0x90
	.type	halide_game_of_life,@function
halide_game_of_life:                    # @halide_game_of_life
	.cfi_startproc
# BB#0:                                 # %entry
	pushl	%ebp
.Ltmp248:
	.cfi_def_cfa_offset 8
.Ltmp249:
	.cfi_offset %ebp, -8
	movl	%esp, %ebp
.Ltmp250:
	.cfi_def_cfa_register %ebp
	pushl	%ebx
	pushl	%edi
	pushl	%esi
	subl	$44, %esp
.Ltmp251:
	.cfi_offset %esi, -20
.Ltmp252:
	.cfi_offset %edi, -16
.Ltmp253:
	.cfi_offset %ebx, -12
	naclcall	.L92$pb
.L92$pb:
	popl	%ebx
.Ltmp254:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp254-.L92$pb), %ebx
	movl	8(%ebp), %edi
	cmpl	$1, 28(%edi)
	jne	.LBB92_1
# BB#4:                                 # %after_assert
	movl	12(%ebp), %eax
	cmpl	$1, 28(%eax)
	jne	.LBB92_5
# BB#6:                                 # %after_assert28
	movl	48(%eax), %esi
	movl	48(%edi), %edx
	leal	1(%edx), %eax
	cmpl	%esi, %eax
	jg	.LBB92_8
# BB#7:                                 # %after_assert28
	movl	12(%ebp), %eax
	movl	16(%eax), %ecx
	movl	%ecx, -36(%ebp)         # 4-byte Spill
	movl	$1, %eax
	subl	16(%edi), %eax
	leal	15(%ecx), %ecx
	movl	%ecx, -24(%ebp)         # 4-byte Spill
	andl	$-16, %ecx
	movl	%ecx, -40(%ebp)         # 4-byte Spill
	addl	%ecx, %eax
	addl	%esi, %eax
	cmpl	%edx, %eax
	jg	.LBB92_8
# BB#9:                                 # %after_assert30
	movl	%esi, -28(%ebp)         # 4-byte Spill
	movl	%edx, -20(%ebp)         # 4-byte Spill
	movl	12(%ebp), %edx
	movl	44(%edx), %esi
	movl	%esi, -16(%ebp)         # 4-byte Spill
	movl	44(%edi), %ecx
	leal	1(%ecx), %eax
	cmpl	%esi, %eax
	movl	%edx, %esi
	jg	.LBB92_11
# BB#10:                                # %after_assert30
	movl	12(%esi), %eax
	movl	%eax, -44(%ebp)         # 4-byte Spill
	movl	$1, %edx
	movl	8(%ebp), %edi
	subl	12(%edi), %edx
	leal	3(%eax), %eax
	movl	%eax, %edi
	andl	$-4, %edi
	addl	%edi, %edx
	addl	-16(%ebp), %edx         # 4-byte Folded Reload
	cmpl	%ecx, %edx
	movl	%esi, %edx
	jg	.LBB92_11
# BB#12:                                # %after_assert34
	movl	%ecx, -32(%ebp)         # 4-byte Spill
	movl	8(%ebp), %esi
	movl	-40(%ebp), %ecx         # 4-byte Reload
	cmpl	-36(%ebp), %ecx         # 4-byte Folded Reload
	jle	.LBB92_14
# BB#13:                                # %assert_failed35
	subl	$16, %esp
	leal	__unnamed_1@GOTOFF(%ebx), %eax
	jmp	.LBB92_2
.LBB92_1:                               # %assert_failed
	subl	$16, %esp
	leal	__unnamed_2@GOTOFF(%ebx), %eax
	jmp	.LBB92_2
.LBB92_5:                               # %assert_failed27
	subl	$16, %esp
	leal	__unnamed_3@GOTOFF(%ebx), %eax
	jmp	.LBB92_2
.LBB92_8:                               # %assert_failed29
	subl	$16, %esp
	leal	__unnamed_4@GOTOFF(%ebx), %eax
	jmp	.LBB92_2
.LBB92_11:                              # %assert_failed31
	subl	$16, %esp
	leal	__unnamed_5@GOTOFF(%ebx), %eax
.LBB92_2:                               # %assert_failed
	movl	%eax, (%esp)
	naclcall	halide_error@PLT
.LBB92_3:                               # %assert_failed
	leal	-12(%ebp), %esp
	popl	%esi
	popl	%edi
	popl	%ebx
	popl	%ebp
	popl	%ecx
	nacljmp	%ecx
.LBB92_14:                              # %after_assert36
	cmpl	-44(%ebp), %edi         # 4-byte Folded Reload
	jle	.LBB92_16
# BB#15:                                # %assert_failed37
	subl	$16, %esp
	leal	__unnamed_6@GOTOFF(%ebx), %eax
	jmp	.LBB92_2
.LBB92_16:                              # %after_assert38
	movl	32(%edx), %ecx
	movl	8(%edx), %edx
	movl	%edx, -36(%ebp)         # 4-byte Spill
	movl	32(%esi), %edx
	movl	%edx, -44(%ebp)         # 4-byte Spill
	movl	8(%esi), %edx
	movl	%edx, -40(%ebp)         # 4-byte Spill
	movl	%esp, %edi
	leal	-48(%edi), %esi
	movl	%esi, %esp
	movl	-16(%ebp), %edx         # 4-byte Reload
	movl	%edx, -48(%edi)
	movl	-28(%ebp), %edx         # 4-byte Reload
	movl	%edx, -44(%edi)
	movl	%ecx, -40(%edi)
	sarl	$2, %eax
	movl	%eax, -36(%edi)
	movl	-32(%ebp), %eax         # 4-byte Reload
	movl	%eax, -32(%edi)
	movl	-20(%ebp), %eax         # 4-byte Reload
	movl	%eax, -28(%edi)
	movl	-44(%ebp), %eax         # 4-byte Reload
	movl	%eax, -24(%edi)
	movl	-40(%ebp), %eax         # 4-byte Reload
	movl	%eax, -20(%edi)
	movl	-36(%ebp), %eax         # 4-byte Reload
	movl	%eax, -16(%edi)
	subl	$16, %esp
	movl	%esi, 12(%esp)
	movl	-24(%ebp), %eax         # 4-byte Reload
	sarl	$4, %eax
	movl	%eax, 8(%esp)
	leal	par_for_f3.v1.v1@GOTOFF(%ebx), %eax
	movl	%eax, (%esp)
	movl	$0, 4(%esp)
	naclcall	halide_do_par_for@PLT
	jmp	.LBB92_3
	.align	32, 0x90
.Ltmp255:
	.size	halide_game_of_life, .Ltmp255-halide_game_of_life
	.cfi_endproc

	.section	.rodata.cst16,"aM",@progbits,16
	.align	16
.LCPI93_0:
	.long	1                       # 0x1
	.long	1                       # 0x1
	.long	1                       # 0x1
	.long	1                       # 0x1
.LCPI93_1:
	.long	3                       # 0x3
	.long	3                       # 0x3
	.long	3                       # 0x3
	.long	3                       # 0x3
.LCPI93_2:
	.long	2                       # 0x2
	.long	2                       # 0x2
	.long	2                       # 0x2
	.long	2                       # 0x2
.LCPI93_3:
	.long	65280                   # 0xff00
	.long	65280                   # 0xff00
	.long	65280                   # 0xff00
	.long	65280                   # 0xff00
.LCPI93_4:
	.long	16711680                # 0xff0000
	.long	16711680                # 0xff0000
	.long	16711680                # 0xff0000
	.long	16711680                # 0xff0000
.LCPI93_5:
	.long	255                     # 0xff
	.long	255                     # 0xff
	.long	255                     # 0xff
	.long	255                     # 0xff
.LCPI93_6:
	.long	4278190080              # 0xff000000
	.long	4278190080              # 0xff000000
	.long	4278190080              # 0xff000000
	.long	4278190080              # 0xff000000
	.text
	.align	32, 0x90
	.type	par_for_f3.v1.v1,@function
par_for_f3.v1.v1:                       # @par_for_f3.v1.v1
# BB#0:                                 # %entry
	pushl	%ebp
	pushl	%ebx
	pushl	%edi
	pushl	%esi
	subl	$364, %esp              # imm = 0x16C
	naclcall	.L93$pb
.L93$pb:
	popl	%eax
.Ltmp256:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp256-.L93$pb), %eax
	movl	388(%esp), %ecx
	movl	12(%ecx), %edx
	testl	%edx, %edx
	jle	.LBB93_5
# BB#1:
	movl	24(%ecx), %esi
	movl	%esi, 24(%esp)          # 4-byte Spill
	movl	20(%ecx), %edi
	imull	%esi, %edi
	addl	16(%ecx), %edi
	movl	%edi, 20(%esp)          # 4-byte Spill
	movl	384(%esp), %esi
	shll	$4, %esi
	movl	%esi, 16(%esp)          # 4-byte Spill
	movl	32(%ecx), %ebp
	movl	28(%ecx), %ebx
	movl	8(%ecx), %esi
	movl	%esi, 12(%esp)          # 4-byte Spill
	movl	%ecx, %esi
	movl	(%esi), %ecx
	movl	%ecx, 96(%esp)          # 4-byte Spill
	movl	4(%esi), %ecx
	movl	%ecx, 8(%esp)           # 4-byte Spill
	xorl	%ecx, %ecx
	movdqa	.LCPI93_0@GOTOFF(%eax), %xmm2
	movdqa	%xmm2, 336(%esp)        # 16-byte Spill
	movaps	.LCPI93_1@GOTOFF(%eax), %xmm0
	movaps	%xmm0, 272(%esp)        # 16-byte Spill
	movaps	.LCPI93_2@GOTOFF(%eax), %xmm0
	movaps	%xmm0, 256(%esp)        # 16-byte Spill
	movdqa	.LCPI93_3@GOTOFF(%eax), %xmm0
	movdqa	%xmm0, 32(%esp)         # 16-byte Spill
	movaps	.LCPI93_4@GOTOFF(%eax), %xmm1
	movaps	%xmm1, 80(%esp)         # 16-byte Spill
	movaps	.LCPI93_5@GOTOFF(%eax), %xmm1
	movaps	%xmm1, 64(%esp)         # 16-byte Spill
	movdqa	.LCPI93_6@GOTOFF(%eax), %xmm1
	movdqa	%xmm1, 48(%esp)         # 16-byte Spill
	.align	16, 0x90
.LBB93_4:                               # %f3.v0.v0_loop.preheader.us
                                        # =>This Loop Header: Depth=1
                                        #     Child Loop BB93_2 Depth 2
	movl	%ecx, 28(%esp)          # 4-byte Spill
	movl	16(%esp), %eax          # 4-byte Reload
	leal	(%ecx,%eax), %eax
	movl	%eax, 320(%esp)         # 4-byte Spill
	movl	%eax, %ecx
	imull	12(%esp), %ecx          # 4-byte Folded Reload
	movl	%ecx, 108(%esp)         # 4-byte Spill
	movl	8(%esp), %ecx           # 4-byte Reload
	leal	1(%ecx,%eax), %eax
	movl	24(%esp), %esi          # 4-byte Reload
	imull	%esi, %eax
	movl	20(%esp), %edi          # 4-byte Reload
	subl	%edi, %eax
	movl	%eax, 104(%esp)         # 4-byte Spill
	movl	320(%esp), %eax         # 4-byte Reload
	leal	(%eax,%ecx), %eax
	imull	%esi, %eax
	subl	%edi, %eax
	movl	%eax, 100(%esp)         # 4-byte Spill
	movl	320(%esp), %eax         # 4-byte Reload
	leal	-1(%ecx,%eax), %eax
	imull	%esi, %eax
	subl	%edi, %eax
	xorl	%edi, %edi
	.align	16, 0x90
.LBB93_2:                               # %f3.v0.v0_loop.us
                                        #   Parent Loop BB93_4 Depth=1
                                        # =>  This Inner Loop Header: Depth=2
	movl	96(%esp), %ecx          # 4-byte Reload
	leal	(%ecx,%edi,4), %esi
	movl	%ebp, %ecx
	movl	104(%esp), %ebp         # 4-byte Reload
	leal	(%ebp,%esi), %ebp
	movdqu	(%ebx,%ebp,4), %xmm0
	movdqa	%xmm0, 240(%esp)        # 16-byte Spill
	psrld	$8, %xmm0
	pand	%xmm2, %xmm0
	movdqa	%xmm0, 160(%esp)        # 16-byte Spill
	movdqa	%xmm0, %xmm1
	movdqu	4(%ebx,%ebp,4), %xmm0
	movdqa	%xmm0, 320(%esp)        # 16-byte Spill
	psrld	$8, %xmm0
	pand	%xmm2, %xmm0
	movdqa	%xmm0, 144(%esp)        # 16-byte Spill
	movdqa	272(%esp), %xmm3        # 16-byte Reload
	psubd	%xmm0, %xmm3
	psubd	%xmm1, %xmm3
	movdqu	-4(%ebx,%ebp,4), %xmm4
	movdqa	%xmm4, 304(%esp)        # 16-byte Spill
	psrld	$8, %xmm4
	pand	%xmm2, %xmm4
	psubd	%xmm4, %xmm3
	movl	100(%esp), %ebp         # 4-byte Reload
	leal	(%ebp,%esi), %ebp
	movdqu	4(%ebx,%ebp,4), %xmm6
	movdqa	%xmm6, 288(%esp)        # 16-byte Spill
	psrld	$8, %xmm6
	pand	%xmm2, %xmm6
	psubd	%xmm6, %xmm3
	movdqu	-4(%ebx,%ebp,4), %xmm7
	movdqa	%xmm7, 224(%esp)        # 16-byte Spill
	psrld	$8, %xmm7
	pand	%xmm2, %xmm7
	psubd	%xmm7, %xmm3
	addl	%eax, %esi
	movdqu	4(%ebx,%esi,4), %xmm5
	movdqa	%xmm5, 208(%esp)        # 16-byte Spill
	psrld	$8, %xmm5
	pand	%xmm2, %xmm5
	psubd	%xmm5, %xmm3
	movdqu	(%ebx,%esi,4), %xmm0
	movdqa	%xmm0, 192(%esp)        # 16-byte Spill
	psrld	$8, %xmm0
	pand	%xmm2, %xmm0
	psubd	%xmm0, %xmm3
	movdqu	-4(%ebx,%esi,4), %xmm1
	movdqa	%xmm1, 176(%esp)        # 16-byte Spill
	psrld	$8, %xmm1
	pand	%xmm2, %xmm1
	pcmpeqd	%xmm1, %xmm3
	movdqa	256(%esp), %xmm2        # 16-byte Reload
	psubd	144(%esp), %xmm2        # 16-byte Folded Reload
	psubd	160(%esp), %xmm2        # 16-byte Folded Reload
	psubd	%xmm4, %xmm2
	psubd	%xmm6, %xmm2
	psubd	%xmm7, %xmm2
	psubd	%xmm5, %xmm2
	psubd	%xmm0, %xmm2
	pcmpeqd	%xmm1, %xmm2
	movdqu	(%ebx,%ebp,4), %xmm0
	movdqa	%xmm0, 160(%esp)        # 16-byte Spill
	movl	%ecx, %ebp
	psrld	$8, %xmm0
	pand	336(%esp), %xmm0        # 16-byte Folded Reload
	pxor	%xmm1, %xmm1
	pcmpeqd	%xmm1, %xmm0
	pandn	%xmm2, %xmm0
	por	%xmm3, %xmm0
	pxor	%xmm1, %xmm1
	blendvps	32(%esp), %xmm1 # 16-byte Folded Reload
	movaps	%xmm1, 144(%esp)        # 16-byte Spill
	movdqa	240(%esp), %xmm0        # 16-byte Reload
	psrld	$16, %xmm0
	pand	336(%esp), %xmm0        # 16-byte Folded Reload
	movdqa	%xmm0, 128(%esp)        # 16-byte Spill
	movdqa	320(%esp), %xmm1        # 16-byte Reload
	psrld	$16, %xmm1
	pand	336(%esp), %xmm1        # 16-byte Folded Reload
	movdqa	%xmm1, 112(%esp)        # 16-byte Spill
	movdqa	272(%esp), %xmm4        # 16-byte Reload
	psubd	%xmm1, %xmm4
	psubd	%xmm0, %xmm4
	movdqa	304(%esp), %xmm7        # 16-byte Reload
	psrld	$16, %xmm7
	pand	336(%esp), %xmm7        # 16-byte Folded Reload
	psubd	%xmm7, %xmm4
	movdqa	288(%esp), %xmm5        # 16-byte Reload
	psrld	$16, %xmm5
	pand	336(%esp), %xmm5        # 16-byte Folded Reload
	psubd	%xmm5, %xmm4
	movdqa	224(%esp), %xmm3        # 16-byte Reload
	psrld	$16, %xmm3
	pand	336(%esp), %xmm3        # 16-byte Folded Reload
	psubd	%xmm3, %xmm4
	movdqa	208(%esp), %xmm1        # 16-byte Reload
	psrld	$16, %xmm1
	pand	336(%esp), %xmm1        # 16-byte Folded Reload
	psubd	%xmm1, %xmm4
	movdqa	192(%esp), %xmm2        # 16-byte Reload
	psrld	$16, %xmm2
	pand	336(%esp), %xmm2        # 16-byte Folded Reload
	psubd	%xmm2, %xmm4
	movdqa	176(%esp), %xmm0        # 16-byte Reload
	psrld	$16, %xmm0
	pand	336(%esp), %xmm0        # 16-byte Folded Reload
	pcmpeqd	%xmm0, %xmm4
	movdqa	256(%esp), %xmm6        # 16-byte Reload
	psubd	112(%esp), %xmm6        # 16-byte Folded Reload
	psubd	128(%esp), %xmm6        # 16-byte Folded Reload
	psubd	%xmm7, %xmm6
	psubd	%xmm5, %xmm6
	movaps	80(%esp), %xmm5         # 16-byte Reload
	psubd	%xmm3, %xmm6
	movdqa	272(%esp), %xmm3        # 16-byte Reload
	psubd	%xmm1, %xmm6
	psubd	%xmm2, %xmm6
	movdqa	336(%esp), %xmm2        # 16-byte Reload
	pcmpeqd	%xmm0, %xmm6
	movdqa	160(%esp), %xmm0        # 16-byte Reload
	psrld	$16, %xmm0
	pand	%xmm2, %xmm0
	pxor	%xmm1, %xmm1
	pcmpeqd	%xmm1, %xmm0
	pandn	%xmm6, %xmm0
	por	%xmm4, %xmm0
	pxor	%xmm1, %xmm1
	blendvps	%xmm5, %xmm1
	paddd	144(%esp), %xmm1        # 16-byte Folded Reload
	movdqa	%xmm1, 144(%esp)        # 16-byte Spill
	movdqa	240(%esp), %xmm5        # 16-byte Reload
	pand	%xmm2, %xmm5
	movdqa	320(%esp), %xmm0        # 16-byte Reload
	pand	%xmm2, %xmm0
	movdqa	%xmm0, 320(%esp)        # 16-byte Spill
	movdqa	%xmm3, %xmm4
	movdqa	256(%esp), %xmm1        # 16-byte Reload
	psubd	%xmm0, %xmm4
	psubd	%xmm5, %xmm4
	movdqa	%xmm5, %xmm0
	movdqa	304(%esp), %xmm3        # 16-byte Reload
	pand	%xmm2, %xmm3
	movdqa	%xmm3, 304(%esp)        # 16-byte Spill
	psubd	%xmm3, %xmm4
	movdqa	288(%esp), %xmm3        # 16-byte Reload
	pand	%xmm2, %xmm3
	movdqa	%xmm3, 288(%esp)        # 16-byte Spill
	psubd	%xmm3, %xmm4
	movdqa	224(%esp), %xmm5        # 16-byte Reload
	pand	%xmm2, %xmm5
	psubd	%xmm5, %xmm4
	movdqa	208(%esp), %xmm6        # 16-byte Reload
	pand	%xmm2, %xmm6
	psubd	%xmm6, %xmm4
	movdqa	192(%esp), %xmm7        # 16-byte Reload
	pand	%xmm2, %xmm7
	psubd	%xmm7, %xmm4
	movdqa	176(%esp), %xmm3        # 16-byte Reload
	pand	%xmm2, %xmm3
	pcmpeqd	%xmm3, %xmm4
	psubd	320(%esp), %xmm1        # 16-byte Folded Reload
	psubd	%xmm0, %xmm1
	psubd	304(%esp), %xmm1        # 16-byte Folded Reload
	psubd	288(%esp), %xmm1        # 16-byte Folded Reload
	psubd	%xmm5, %xmm1
	psubd	%xmm6, %xmm1
	psubd	%xmm7, %xmm1
	pcmpeqd	%xmm3, %xmm1
	movdqa	160(%esp), %xmm0        # 16-byte Reload
	pand	%xmm2, %xmm0
	pxor	%xmm3, %xmm3
	pcmpeqd	%xmm3, %xmm0
	movaps	64(%esp), %xmm3         # 16-byte Reload
	pandn	%xmm1, %xmm0
	por	%xmm4, %xmm0
	pxor	%xmm1, %xmm1
	blendvps	%xmm3, %xmm1
	movdqa	48(%esp), %xmm0         # 16-byte Reload
	paddd	144(%esp), %xmm1        # 16-byte Folded Reload
	paddd	%xmm0, %xmm1
	movl	108(%esp), %ecx         # 4-byte Reload
	leal	(%ecx,%edi,4), %esi
	movdqu	%xmm1, (%ebp,%esi,4)
	incl	%edi
	cmpl	%edx, %edi
	jne	.LBB93_2
# BB#3:                                 # %f3.v0.v0_after_loop.us
                                        #   in Loop: Header=BB93_4 Depth=1
	movl	28(%esp), %ecx          # 4-byte Reload
	incl	%ecx
	cmpl	$16, %ecx
	jne	.LBB93_4
.LBB93_5:                               # %f3.v1.v10_after_loop
	addl	$364, %esp              # imm = 0x16C
	popl	%esi
	popl	%edi
	popl	%ebx
	popl	%ebp
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp257:
	.size	par_for_f3.v1.v1, .Ltmp257-par_for_f3.v1.v1

	.globl	halide_game_of_life_jit_wrapper
	.align	32, 0x90
	.type	halide_game_of_life_jit_wrapper,@function
halide_game_of_life_jit_wrapper:        # @halide_game_of_life_jit_wrapper
	.cfi_startproc
# BB#0:                                 # %entry
	pushl	%ebx
.Ltmp260:
	.cfi_def_cfa_offset 8
	subl	$8, %esp
.Ltmp261:
	.cfi_def_cfa_offset 16
.Ltmp262:
	.cfi_offset %ebx, -8
	naclcall	.L94$pb
.L94$pb:
	popl	%ebx
.Ltmp263:
	addl	$_GLOBAL_OFFSET_TABLE_+(.Ltmp263-.L94$pb), %ebx
	movl	16(%esp), %ecx
	movl	(%ecx), %eax
	movl	4(%ecx), %ecx
	movl	%ecx, 4(%esp)
	movl	%eax, (%esp)
	naclcall	halide_game_of_life@PLT
	addl	$8, %esp
	popl	%ebx
	popl	%ecx
	nacljmp	%ecx
	.align	32, 0x90
.Ltmp264:
	.size	halide_game_of_life_jit_wrapper, .Ltmp264-halide_game_of_life_jit_wrapper
	.cfi_endproc

	.type	halide_custom_malloc,@object # @halide_custom_malloc
	.section	.bss.halide_custom_malloc,"aGw",@nobits,halide_custom_malloc,comdat
	.weak	halide_custom_malloc
	.align	4
halide_custom_malloc:
	.long	0
	.size	halide_custom_malloc, 4

	.type	halide_custom_free,@object # @halide_custom_free
	.section	.bss.halide_custom_free,"aGw",@nobits,halide_custom_free,comdat
	.weak	halide_custom_free
	.align	4
halide_custom_free:
	.long	0
	.size	halide_custom_free, 4

	.type	halide_reference_clock,@object # @halide_reference_clock
	.section	.bss.halide_reference_clock,"aGw",@nobits,halide_reference_clock,comdat
	.weak	halide_reference_clock
	.align	4
halide_reference_clock:
	.zero	8
	.size	halide_reference_clock, 8

	.type	halide_error_handler,@object # @halide_error_handler
	.section	.bss.halide_error_handler,"aGw",@nobits,halide_error_handler,comdat
	.weak	halide_error_handler
	.align	4
halide_error_handler:
	.long	0
	.size	halide_error_handler, 4

	.type	.L.str,@object          # @.str
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str:
	.asciz	 "Error: %s\n"
	.size	.L.str, 11

	.type	.L.str1,@object         # @.str1
.L.str1:
	.asciz	 "wb"
	.size	.L.str1, 3

	.type	halide_work_queue,@object # @halide_work_queue
	.section	.bss.halide_work_queue,"aGw",@nobits,halide_work_queue,comdat
	.weak	halide_work_queue
	.align	4
halide_work_queue:
	.zero	1573224
	.size	halide_work_queue, 1573224

	.type	halide_threads,@object  # @halide_threads
	.section	.bss.halide_threads,"aGw",@nobits,halide_threads,comdat
	.weak	halide_threads
	.align	4
halide_threads:
	.long	0                       # 0x0
	.size	halide_threads, 4

	.type	halide_thread_pool_initialized,@object # @halide_thread_pool_initialized
	.section	.bss.halide_thread_pool_initialized,"aGw",@nobits,halide_thread_pool_initialized,comdat
	.weak	halide_thread_pool_initialized
halide_thread_pool_initialized:
	.byte	0                       # 0x0
	.size	halide_thread_pool_initialized, 1

	.type	halide_custom_do_task,@object # @halide_custom_do_task
	.section	.bss.halide_custom_do_task,"aGw",@nobits,halide_custom_do_task,comdat
	.weak	halide_custom_do_task
	.align	4
halide_custom_do_task:
	.long	0
	.size	halide_custom_do_task, 4

	.type	halide_custom_do_par_for,@object # @halide_custom_do_par_for
	.section	.bss.halide_custom_do_par_for,"aGw",@nobits,halide_custom_do_par_for,comdat
	.weak	halide_custom_do_par_for
	.align	4
halide_custom_do_par_for:
	.long	0
	.size	halide_custom_do_par_for, 4

	.type	.L.str2,@object         # @.str2
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str2:
	.asciz	 "HL_NUMTHREADS"
	.size	.L.str2, 14

	.type	.L.str3,@object         # @.str3
.L.str3:
	.asciz	 "HL_NUMTHREADS not defined. Defaulting to %d threads.\n"
	.size	.L.str3, 54

	.type	_ZN12_GLOBAL__N_130pixel_type_to_tiff_sample_typeE,@object # @_ZN12_GLOBAL__N_130pixel_type_to_tiff_sample_typeE
	.section	.rodata,"a",@progbits
	.align	2
_ZN12_GLOBAL__N_130pixel_type_to_tiff_sample_typeE:
	.short	3                       # 0x3
	.short	3                       # 0x3
	.short	1                       # 0x1
	.short	2                       # 0x2
	.short	1                       # 0x1
	.short	2                       # 0x2
	.short	1                       # 0x1
	.short	2                       # 0x2
	.short	1                       # 0x1
	.short	2                       # 0x2
	.size	_ZN12_GLOBAL__N_130pixel_type_to_tiff_sample_typeE, 20

	.type	__unnamed_2,@object     # @0
	.align	16
__unnamed_2:
	.asciz	 "Static constraint violated: p0.stride.0 == 1"
	.size	__unnamed_2, 45

	.type	__unnamed_3,@object     # @1
	.align	16
__unnamed_3:
	.asciz	 "Static constraint violated: f3.stride.0 == 1"
	.size	__unnamed_3, 45

	.type	__unnamed_4,@object     # @2
	.align	16
__unnamed_4:
	.asciz	 "p0 is accessed out of bounds in dimension 1"
	.size	__unnamed_4, 44

	.type	__unnamed_5,@object     # @3
	.align	16
__unnamed_5:
	.asciz	 "p0 is accessed out of bounds in dimension 0"
	.size	__unnamed_5, 44

	.type	__unnamed_1,@object     # @4
	.align	16
__unnamed_1:
	.asciz	 "f3 is accessed out of bounds in dimension 1"
	.size	__unnamed_1, 44

	.type	__unnamed_6,@object     # @5
	.align	16
__unnamed_6:
	.asciz	 "f3 is accessed out of bounds in dimension 0"
	.size	__unnamed_6, 44


	.section	".note.GNU-stack","",@progbits
