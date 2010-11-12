define void @foo(<8 x i16>* %in, <4 x i16>* %out) {
        %a = load <8 x i16>* %in, align 16
        %b0 = extractelement <8 x i16> %a, i32 0 
        %b1 = extractelement <8 x i16> %a, i32 2 
        %b2 = extractelement <8 x i16> %a, i32 4 
        %b3 = extractelement <8 x i16> %a, i32 6 

        %c0 = extractelement <8 x i16> %a, i32 1 
        %c1 = extractelement <8 x i16> %a, i32 3 
        %c2 = extractelement <8 x i16> %a, i32 5 
        %c3 = extractelement <8 x i16> %a, i32 7 

        %b_0 = insertelement <4 x i16> <i16 0, i16 0, i16 0, i16 0>, i16 %b0, i32 0
        %b_1 = insertelement <4 x i16> %b_0, i16 %b1, i32 1
        %b_2 = insertelement <4 x i16> %b_1, i16 %b2, i32 2
        %b = insertelement <4 x i16> %b_2, i16 %b3, i32 3        

        %c_0 = insertelement <4 x i16> <i16 0, i16 0, i16 0, i16 0>, i16 %c0, i32 0
        %c_1 = insertelement <4 x i16> %c_0, i16 %c1, i32 1
        %c_2 = insertelement <4 x i16> %c_1, i16 %c2, i32 2
        %c = insertelement <4 x i16> %c_2, i16 %c3, i32 3        

        %d = add <4 x i16> %c, %b

        store <4 x i16> %d, <4 x i16>* %out, align 16

        ret void;
}

define void @bar(<8 x i16>* %in, <4 x i16>* %out) {
        %a = load <8 x i16>* %in, align 16

        %b = shufflevector <8 x i16> %a, <8 x i16> %a, <4 x i32> <i32 0, i32 2, i32 4, i32 6>

        %c = shufflevector <8 x i16> %a, <8 x i16> %a, <4 x i32> <i32 1, i32 3, i32 5, i32 7>
        
        %d = add <4 x i16> %c, %b

        store <4 x i16> %d, <4 x i16>* %out, align 16

        ret void;
}

define void @moo(float *%in00, float *%out) {
        ; load 11 elements
        %e00 = load float *%in00, align 4
        
        %in01 = getelementptr float *%in00, i16 1
        %e01 = load float *%in01, align 4

        %in02 = getelementptr float *%in00, i16 2
        %e02 = load float *%in02, align 4

        %in03 = getelementptr float *%in00, i16 3
        %e03 = load float *%in03, align 4

        %in04 = getelementptr float *%in00, i16 4
        %e04 = load float *%in04, align 4

        %in05 = getelementptr float *%in00, i16 5
        %e05 = load float *%in05, align 4

        %in06 = getelementptr float *%in00, i16 6
        %e06 = load float *%in06, align 4

        %in07 = getelementptr float *%in00, i16 7
        %e07 = load float *%in07, align 4

        %in08 = getelementptr float *%in00, i16 8
        %e08 = load float *%in08, align 4

        %in09 = getelementptr float *%in00, i16 9
        %e09 = load float *%in09, align 4        

        %in10 = getelementptr float *%in00, i16 9
        %e10 = load float *%in10, align 4        

        %in11 = getelementptr float *%in00, i16 9
        %e11 = load float *%in11, align 4        

        ; compute all products
        %p0_0 = fmul float %e00,  12.125
        %p0_1 = fmul float %e01,  2.125
        %p0_2 = fmul float %e02,  212.125
        %p0_3 = fmul float %e03,  512.125
        %p0_4 = fmul float %e04,  612.125
        %p0_5 = fmul float %e05,  812.125
        %p0_6 = fmul float %e06,  912.125
        %p0_7 = fmul float %e07,  812.125
        %p0_8 = fmul float %e08,  912.125

        %p1_0 = fmul float %e01,  12.125
        %p1_1 = fmul float %e02,  2.125
        %p1_2 = fmul float %e03,  212.125
        %p1_3 = fmul float %e04,  512.125
        %p1_4 = fmul float %e05,  612.125
        %p1_5 = fmul float %e06,  812.125
        %p1_6 = fmul float %e07,  912.125
        %p1_7 = fmul float %e08,  812.125
        %p1_8 = fmul float %e09,  912.125

        %p2_0 = fmul float %e02,  12.125
        %p2_1 = fmul float %e03,  2.125
        %p2_2 = fmul float %e04,  212.125
        %p2_3 = fmul float %e05,  512.125
        %p2_4 = fmul float %e06,  612.125
        %p2_5 = fmul float %e07,  812.125
        %p2_6 = fmul float %e08,  912.125
        %p2_7 = fmul float %e09,  812.125
        %p2_8 = fmul float %e10,  912.125

        %p3_0 = fmul float %e03,  12.125
        %p3_1 = fmul float %e04,  2.125
        %p3_2 = fmul float %e05,  212.125
        %p3_3 = fmul float %e06,  512.125
        %p3_4 = fmul float %e07,  612.125
        %p3_5 = fmul float %e08,  812.125
        %p3_6 = fmul float %e09,  912.125
        %p3_7 = fmul float %e10,  812.125
        %p3_8 = fmul float %e11,  912.125

        ; do all the sums
        %s0_1 = fadd float %p0_0, %p0_1
        %s0_2 = fadd float %s0_1, %p0_2
        %s0_3 = fadd float %s0_2, %p0_3
        %s0_4 = fadd float %s0_3, %p0_4
        %s0_5 = fadd float %s0_4, %p0_5
        %s0_6 = fadd float %s0_5, %p0_6
        %s0_7 = fadd float %s0_6, %p0_7
        %s0_8 = fadd float %s0_7, %p0_8

        %s1_1 = fadd float %p1_0, %p1_1
        %s1_2 = fadd float %s1_1, %p1_2
        %s1_3 = fadd float %s1_2, %p1_3
        %s1_4 = fadd float %s1_3, %p1_4
        %s1_5 = fadd float %s1_4, %p1_5
        %s1_6 = fadd float %s1_5, %p1_6
        %s1_7 = fadd float %s1_6, %p1_7
        %s1_8 = fadd float %s1_7, %p1_8

        %s2_1 = fadd float %p2_0, %p2_1
        %s2_2 = fadd float %s2_1, %p2_2
        %s2_3 = fadd float %s2_2, %p2_3
        %s2_4 = fadd float %s2_3, %p2_4
        %s2_5 = fadd float %s2_4, %p2_5
        %s2_6 = fadd float %s2_5, %p2_6
        %s2_7 = fadd float %s2_6, %p2_7
        %s2_8 = fadd float %s2_7, %p2_8

        %s3_1 = fadd float %p3_0, %p3_1
        %s3_2 = fadd float %s3_1, %p3_2
        %s3_3 = fadd float %s3_2, %p3_3
        %s3_4 = fadd float %s3_3, %p3_4
        %s3_5 = fadd float %s3_4, %p3_5
        %s3_6 = fadd float %s3_5, %p3_6
        %s3_7 = fadd float %s3_6, %p3_7
        %s3_8 = fadd float %s3_7, %p3_8

        store float %s0_8, float *%out, align 4

        %out_1 = getelementptr float *%out, i16 1
        store float %s1_8, float *%out_1, align 4

        %out_2 = getelementptr float *%out, i16 2
        store float %s2_8, float *%out_2, align 4

        %out_3 = getelementptr float *%out, i16 3
        store float %s3_8, float *%out_3, align 4

        ret void
}
