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
