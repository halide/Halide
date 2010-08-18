#include <vector>

class AsmX64 {
public:
    // 64-bit registers
    class Reg {
    public:
        Reg(int x) : num(x) {}
        int num;
        bool operator==(const Reg &other) {
            return num == other.num;
        }
    };

    Reg rax, rcx, rdx, rbx, 
        rsp, rbp, rsi, rdi,
        r8, r9, r10, r11, 
        r12, r13, r14, r15;

    // SSE 128-bit registers
    class SSEReg {
    public:
        SSEReg(int x) : num(x) {}
        int num;
        bool operator==(const Reg &other) {
            return num == other.num;
        }
    };

    SSEReg xmm0, xmm1, xmm2, xmm3,
        xmm4, xmm5, xmm6, xmm7, 
        xmm8, xmm9, xmm10, xmm11, 
        xmm12, xmm13, xmm14, xmm15;

    class Mem {
    public:
        Mem(Reg a, int o = 0) : reg(a), offset(o) {}
        Reg reg;
        int offset;
    };

    AsmX64() : 
        rax(0), rcx(1), rdx(2), rbx(3), 
        rsp(4), rbp(5), rsi(6), rdi(7),
        r8(8), r9(9), r10(10), r11(11), 
        r12(12), r13(13), r14(14), r15(15),
        xmm0(0), xmm1(1), xmm2(2), xmm3(3),
        xmm4(4), xmm5(5), xmm6(6), xmm7(7), 
        xmm8(8), xmm9(9), xmm10(10), xmm11(11), 
        xmm12(12), xmm13(13), xmm14(14), xmm15(15)
        {}

    const std::vector<unsigned char> buffer() {return _buffer;}

    // simple binary operations like add, sub, cmp
    void bop(Reg dst, Reg src, unsigned char op) {
        emit(0x48 | ((dst.num & 8) >> 1) | (src.num >> 3));
        emit(op);
        emit(0xC0 | ((dst.num & 7) << 3) | (src.num & 7));
    }

    void bop(Reg dst, int n, unsigned char raxop, unsigned char op) {
        emit(0x48 | (dst.num >> 3));
        if (dst == rax) {
            emit(raxop);
            emitInt32(n);
        } else {
            if (n >= -128 && n <= 127) {
                emit(0x83);
                emit(0xC0 | (op << 3) | (dst.num & 7));
                emit((char)n);
            } else {
                emit(0x81);
                emit(0xC0 | (op << 3) | (dst.num & 7));
                emitInt32(n);
            }
        }

    }

    void bop(Reg dst, Mem src, unsigned char op) {
        emit(0x48 | ((dst.num & 8) >> 1) | (src.reg.num >> 3));
        emit(op);
        if (src.offset) {
            emit(0x80 | ((dst.num & 7) << 3) | (src.reg.num & 7));
            if ((src.reg.num & 7) == 4) {
                emit(0x24);
            }
            emitInt32(src.offset);
        } else if ((src.reg.num & 7) == 5) {
            emit(0x40 | ((dst.num & 7) << 3) | (src.reg.num & 7));
            emit(0x00);
        } else {
            emit(0x00 | ((dst.num & 7) << 3) | (src.reg.num & 7));
            if ((src.reg.num & 7) == 4) {
                emit(0x24);
            }
        }         
    }

    void bop(Mem dst, Reg src, unsigned char op) {
        emit(0x48 | ((src.num & 8) >> 1) | (dst.reg.num >> 3));        
        emit(op);
        if (dst.offset) {
            emit(0x80 | ((src.num & 7) << 3) | (dst.reg.num & 7));
            if ((dst.reg.num & 7) == 4) {
                emit(0x24);
            }
            emitInt32(dst.offset);            
        } else if ((dst.reg.num & 7) == 5) {
            emit(0x40 | ((src.num & 7) << 3) | (dst.reg.num & 7));
            emit(0x00);
        } else {
            emit(0x00 | ((src.num & 7) << 3) | (dst.reg.num & 7));
            if ((dst.reg.num & 7) == 4) {
                emit(0x24);
            }
        }
    }

    // dst += src
    void add(Reg dst, Reg src) { 
        bop(dst, src, 0x03);
    }

    void add(Reg dst, int n) {
        bop(dst, n, 0x05, 0x00);
    }

    void add(Reg dst, Mem src) {
        bop(dst, src, 0x03);
    }
    
    void add(Mem dst, Reg src) {
        bop(dst, src, 0x01);
    }

    // dst -= src
    void sub(Reg dst, Reg src) { 
        bop(dst, src, 0x2B);
    }

    void sub(Reg dst, int n) {
        bop(dst, n, 0x2D, 0x05);
    }

    void sub(Reg dst, Mem src) {
        bop(dst, src, 0x2B);
    }
    
    void sub(Mem dst, Reg src) {
        bop(dst, src, 0x29);
    }

    // comparison
    void cmp(Reg dst, Reg src) { 
        bop(dst, src, 0x3B);
    }

    void cmp(Reg dst, int n) {
        bop(dst, n, 0x3D, 0x07);
    }

    void cmp(Reg dst, Mem src) {
        bop(dst, src, 0x3B);
    }
    
    void cmp(Mem dst, Reg src) {
        bop(dst, src, 0x39);
    }

protected:
    std::vector<unsigned char> _buffer;

    void emitInt32(int x) {
        unsigned int y = (unsigned int)x;
        emit(y & 0xff);
        emit((y>>8) & 0xff);
        emit((y>>16) & 0xff);
        emit((y>>24) & 0xff);
    }

    virtual void emit(unsigned char x) {
        _buffer.push_back(x);
    }
        
};
