	.version 2.0
	.target compute_10, map_f64_to_f32




.shared .b8__nothing[];

.entry kernel (.param .b64 __param_1, .param .b64 __param_2) // @kernel
{
	.reg .b32 %r<2>;
	.reg .b64 %rd<5>;
	.reg .f32 %f<1>;
// BB#0:                                // %entry
	ld.param.u64	%rd0, [__param_1];
	ld.global.f32	%f0, [%rd0];
	cvt.rzi.u32.f32	%r0, %f0;
	cvt.s64.s32	%rd1, %r0;
	shl.b64	%rd2, %rd1, 2;
	ld.param.u64	%rd3, [__param_2];
	add.u64	%rd4, %rd3, %rd2;
	mov.u32	%r1, 1;
	st.global.u32	[%rd4], %r1;
	exit;
}

