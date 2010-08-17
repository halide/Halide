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

    // dst += src
    void add(Reg dst, Reg src) { 
        emit(0x48 | ((dst.num & 8) >> 1) | (src.num >> 3));
        emit(0x03);
        emit(0xC0 | ((dst.num & 7) << 3) | (src.num & 7));
    }

    void add(Reg dst, int n) {
        emit(0x48 | (dst.num >> 3));
        emit(0x81);
        emit(0xC0 | (dst.num & 7));
        emitConst(n);
    }

    void add(Reg dst, Mem src) {
        emit(0x48 | ((dst.num & 8) >> 1) | (src.reg.num >> 3));
        emit(0x03);
        if (src.offset) {
            emit(0x80 | ((dst.num & 7) << 3) | (src.reg.num & 7));
            if ((src.reg.num & 7) == 4) {
                emit(0x24);
            }
            emitConst(src.offset);
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
    
    void add(Mem dst, Reg src) {
        emit(0x48 | ((dst.reg.num & 8) >> 1) | (src.num >> 3));        
        emit(0x01);
        if (dst.offset) {
            emit(0x40 | ((dst.reg.num & 7) << 3) | (src.num & 7));
            emitConst(dst.offset);            
        } else if ((dst.reg.num & 7) == 5) {
            emit(0x40 | ((dst.reg.num & 7) << 3) | (src.num & 7));
            emit(0x00);
        } else {
            emit(0x00 | ((dst.reg.num & 7) << 3) | (src.num & 7));
            if ((dst.reg.num & 7) == 4) {
                emit(0x00);
                emit(0x00);
                emit(0x00);
                emit(0x00);
                emit(0x00);
                emit(0x00);
                emit(0x00);
                emit(0x24);
            }
        }
    }

protected:
    std::vector<unsigned char> _buffer;

    void emitConst(int x) {
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
