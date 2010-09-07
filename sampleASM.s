.CODE
        add rax, rax    
        add rax, rcx
        add rax, rdx
        add rax, rbx
        add rax, rsp
        add rax, rbp
        add rax, rsi
        add rax, rdi
        add rax, r8
        add rax, r9
        add rax, r10
        add rax, r11
        add rax, r12
        add rax, r13
        add rax, r14
        add rax, r15
        add rax, rax    
        add rcx, rax    
        add rdx, rax    
        add rbx, rax    
        add rsp, rax    
        add rbp, rax    
        add rsi, rax    
        add rdi, rax    
        add r8, rax    
        add r9, rax    
        add r10, rax    
        add r11, rax    
        add r12, rax    
        add r13, rax    
        add r14, rax    
        add r15, rax
        add r15, r15
        add rax, 13             
        add r15, 13
        add r9, 1024
        add r9, 2147483647
        add rax, [r11]
        add rax, [r11-17]
        add rcx, [r11+17]
        add rdx, [r11+17]
        add rax, [rax]   
        add [rax+17], rax
        add [rax], rax      
        ret
        call rdx
        call qword ptr[rdx]
        call qword ptr[rdx+17]
        call qword ptr[rdx+1700000]    
        call r10
        call qword ptr[r10]
        call qword ptr[r10+17]
        call qword ptr[r10+1700000]    
        call r12
        call qword ptr[r12]
        call qword ptr[r12+17]
        call qword ptr[r12+1700000]    
        call rsp
        call qword ptr[rsp]
        call qword ptr[rsp+17]
        call qword ptr[rsp+1700000]    
        call rbp
        call qword ptr[rbp]
        call qword ptr[rbp+17]
        call qword ptr[rbp+1700000]    
        mov rcx, rdx
        mov rdx, rcx
        mov rcx, [rdx]
        mov rcx, [rdx+17]
        mov rcx, [rdx+1700000]
        mov r10, r11
        mov r10, [r11]
        mov r10, [r11+17]
        mov r10, [r11+1700000]
        mov rsp, r11
        mov rsp, [r11]
        mov rsp, [r11+17]
        mov rsp, [r11+1700000]
        mov r10, rsp
        mov r10, [rsp]
        mov r10, [rsp+17]
        mov r10, [rsp+1700000]
        mov rbp, r11
        mov rbp, [r11]
        mov rbp, [r11+17]
        mov rbp, [r11+1700000]
        mov r10, rbp
        mov r10, [rbp]
        mov r10, [rbp+17]
        mov r10, [rbp+1700000]    
bar:    jmp foo       
foo:    jmp bar   
gah:    jmp gah       
        addps xmm0, xmm1
        shufps xmm0, xmm1, 0
        addps xmm10, xmm11
        shufps xmm10, xmm11, 0    
    END