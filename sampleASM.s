.DATA
testData REAL4 1.75
.CODE
        mov rdx, 12h
        mov rdx, 12345678h
        mov rdx, 1122334455667788h
        mov rcx, 12h 
        mov rcx, 12345678h
        mov rcx, 1234567812345678h
        mov rsp, 12h 
        mov rsp, 12345678h
        mov rsp, 1234567812345678h
        mov rbp, 12h 
        mov rbp, 12345678h
        mov rbp, 1234567812345678h
        mov r8, 12h 
        mov r8, 12345678h
        mov r8, 1234567812345678h
        mov r12, 12h 
        mov r12, 12345678h
        mov r12, 1234567812345678h

        movss xmm0, xmm1
        movss xmm0, xmm2
        movss xmm0, xmm3
        subss xmm0, xmm1
        addss xmm0, xmm2
        addss xmm0, xmm3
        movss xmm0, xmm4
        movss xmm0, xmm5
        movss xmm0, xmm6
        movss xmm0, xmm7
        movss xmm0, xmm8
        movss xmm0, xmm9
        movss xmm0, xmm10
        movss xmm0, xmm11
        movss xmm0, xmm12
        movss xmm0, xmm13    
        movss xmm0, xmm14
        movss xmm0, xmm15    
        movss xmm1, xmm0
        movss xmm2, xmm0
        movss xmm3, xmm0
        movss xmm4, xmm0
        movss xmm5, xmm0
        movss xmm6, xmm0
        movss xmm7, xmm0
        movss xmm8, xmm0
        movss xmm9, xmm0
        movss xmm10, xmm0
        movss xmm11, xmm0
        movss xmm12, xmm0
        movss xmm13, xmm0    
        movss xmm14, xmm0
        movss xmm15, xmm0    
        movss xmm1, xmm15
        movss xmm2, xmm15
        movss xmm3, xmm15
        movss xmm4, xmm15
        movss xmm5, xmm15
        movss xmm6, xmm15
        movss xmm7, xmm15
        movss xmm8, xmm15
        movss xmm9, xmm15
        movss xmm10, xmm15
        movss xmm11, xmm15
        movss xmm12, xmm15
        movss xmm13, xmm15   
        movss xmm14, xmm15
        movss xmm15, xmm15    
    
        movss dword ptr [rax+17], xmm1 
        movss dword ptr [rax+1700000], xmm2
        movss xmm0, dword ptr [rax+17]
        movss xmm0, dword ptr [rax+1700000]
        movss xmm0, dword ptr [rcx+1700000]
        movss xmm0, dword ptr [rbp+1700000]
        movss xmm0, dword ptr [rsp+1700000]
        movss xmm0, dword ptr [r11+1700000]
        movss xmm9, dword ptr [rax+17]
        movss xmm9, dword ptr [rax+1700000]
        movss xmm9, dword ptr [rcx+1700000]
        movss xmm9, dword ptr [rbp+1700000]
        movss xmm9, dword ptr [rsp+1700000]
        movss xmm9, dword ptr [r11+1700000]
        movss dword ptr [rax+17], xmm0
        movss dword ptr [rax+1700000], xmm0
        movss dword ptr [rcx+1700000], xmm0
        movss dword ptr [rbp+1700000], xmm0
        movss dword ptr [rsp+1700000], xmm0
        movss dword ptr [r11+1700000], xmm0


        imul r8, r9, 32
        imul r8, r9
        imul r8, [r9]

        movups xmm0, dword ptr[rbp+17]
        movups xmm1, dword ptr[rbp+17]
        movups xmm2, dword ptr[rbp+17]
        movups xmm3, dword ptr[rbp+17]
        movups xmm0, dword ptr[rax+17]
        movups xmm0, dword ptr[rcx+17]
        movups xmm0, dword ptr[rdx+17]
        movups xmm0, dword ptr[rbx+17]
        movups xmm8, dword ptr[rbp+17]
        movups xmm9, dword ptr[rbp+17]
        movups xmm10, dword ptr[rbp+17]
        movups xmm11, dword ptr[rbp+17]
        movups xmm0, dword ptr[r8+17]
        movups xmm0, dword ptr[r9+17]
        movups xmm0, dword ptr[r10+17]
        movups xmm0, dword ptr[r11+17]                
        movups dword ptr[rbp+17], xmm0
        movups xmm0, xmm12

        addss xmm3, xmm7
        subss xmm3, xmm7
        divss xmm3, xmm7
        mulss xmm3, xmm7

        cmpeqss xmm3, xmm13
        cmpltss xmm3, xmm13
        cmpless xmm3, xmm13
        cmpneqss xmm3, xmm13
        cmpnltss xmm3, xmm13
        cmpnless xmm3, xmm13
        cmpnless xmm3, xmm14
        cmpnless xmm4, xmm13

        andps xmm3, xmm7
        orps xmm3, xmm7
        xorps xmm3, xmm7
        andnps xmm3, xmm7

        punpckldq xmm3, xmm7
        punpcklqdq xmm3, xmm7
        punpckldq xmm3, xmm14
        punpcklqdq xmm3, xmm14
        punpckldq xmm15, xmm14
        punpcklqdq xmm15, xmm14
        punpckldq xmm15, xmm3
        punpcklqdq xmm15, xmm3
    
        movntss dword ptr [rdx], xmm1
        movntss dword ptr [rdx], xmm2
        movntss dword ptr [rdx], xmm3    

    
        END
