#include <vector>
#include <string>
#include <map>
#include <stdint.h>

#ifdef _MSC_VER
#include <windows.h>
#else //!_MSC_VER
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#define HAVE_SYSCONF
typedef unsigned long DWORD;
#endif

#include "base.h"

using namespace std;

class AsmX64 {
public:
    // 64-bit registers
    class Reg {
    public:
        Reg() : num(0) {}
        Reg(unsigned char x) : num(x) {}
        unsigned char num;
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
        SSEReg() : num(0) {}
        SSEReg(unsigned char x) : num(x) {}
        unsigned char num;
        bool operator==(const SSEReg &other) {
            return num == other.num;
        }
    };

    SSEReg xmm0, xmm1, xmm2, xmm3,
        xmm4, xmm5, xmm6, xmm7, 
        xmm8, xmm9, xmm10, xmm11, 
        xmm12, xmm13, xmm14, xmm15;

    class Mem {
    public:
        Mem(Reg a, int32_t o = 0) : reg(a), offset(o) {}
        Reg reg;
        int32_t offset;
    };

    AsmX64() : 
        rax(0), rcx(1), rdx(2), rbx(3), 
        rsp(4), rbp(5), rsi(6), rdi(7),
        r8(8), r9(9), r10(10), r11(11), 
        r12(12), r13(13), r14(14), r15(15),
        xmm0(0), xmm1(1), xmm2(2), xmm3(3),
        xmm4(4), xmm5(5), xmm6(6), xmm7(7), 
        xmm8(8), xmm9(9), xmm10(10), xmm11(11), 
        xmm12(12), xmm13(13), xmm14(14), xmm15(15), 
        data(NULL)
        {}

    std::vector<unsigned char> &buffer() {return _buffer;}

protected:
    // simple binary operations like add, sub, cmp
    void bop(Reg dst, Reg src, unsigned char op) {
        emit(0x48 | ((dst.num & 8) >> 1) | (src.num >> 3));
        emit(op);
        emit(0xC0 | ((dst.num & 7) << 3) | (src.num & 7));
    }

    void bop(Reg dst, int32_t n, unsigned char raxop, unsigned char op) {
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

public:
    ~AsmX64() {
        if (data) delete[] data;
    }

    // dst += src
    void add(Reg dst, Reg src) { 
        bop(dst, src, 0x03);
    }

    // TODO: this should type check or assert that its operands are <= 32 bits
    void add(Reg dst, int32_t n) {
        bop(dst, n, 0x05, 0x00);
    }

    void add(Reg dst, std::string name) {
        if (bindings.find(name) != bindings.end()) {
            add(dst, bindings[name]);
        } else {
            add(dst, 0xefbeadde);
        }
        bindingSites[name].push_back(bufSize()-4);
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

    void sub(Reg dst, int32_t n) {
        bop(dst, n, 0x2D, 0x05);
    }

    void sub(Reg dst, std::string name) {
        if (bindings.find(name) != bindings.end()) {
            sub(dst, bindings[name]);
        } else {
            sub(dst, 0xefbeadde);
        }
        bindingSites[name].push_back(bufSize()-4);
    }

    void sub(Reg dst, Mem src) {
        bop(dst, src, 0x2B);
    }
    
    void sub(Mem dst, Reg src) {
        bop(dst, src, 0x29);
    }

    void imul(Reg dst, Reg src) {
        emit(0x48 | ((dst.num & 8) >> 1) | (src.num >> 3));
        emit(0x0F);
        emit(0xAF);
        emit(0xC0 | ((dst.num & 7) << 3) | (src.num & 7));        
    }

    void imul(Reg dst, Mem src) {
        emit(0x48 | ((dst.num & 8) >> 1) | (src.reg.num >> 3));
        emit(0x0F);
        emit(0xAF);
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

    void imul(Reg dst, int32_t n) {
        emit(0x48 | ((dst.num & 8) >> 1) | (dst.num >> 3));
        if (n >= -128 && n <= 127) {
            emit(0x6B);
            emit(0xC0 | ((dst.num & 7) << 3) | (dst.num & 7));        
            emit((int8_t)n);
        } else {
            emit(0x69);
            emit(0xC0 | ((dst.num & 7) << 3) | (dst.num & 7));        
            emitInt32(n);
        }
    }

    // bitwise and (dst &= src)
    void band(Reg dst, Reg src) { 
        bop(dst, src, 0x21);
    }

    void band(Reg dst, int32_t n) {
        bop(dst, n, 0x25, 0x04);
    }

    void band(Reg dst, std::string name) {
        if (bindings.find(name) != bindings.end()) {
            band(dst, bindings[name]);
        } else {
            band(dst, 0xefbeadde);
        }
        bindingSites[name].push_back(bufSize()-4);
    }

    void band(Reg dst, Mem src) {
        bop(dst, src, 0x23);
    }
    
    void band(Mem dst, Reg src) {
        bop(dst, src, 0x21);
    }

    // bitwise or (dst |= src)
    void bor(Reg dst, Reg src) { 
        bop(dst, src, 0x0B);
    }

    void bor(Reg dst, int32_t n) {
        bop(dst, n, 0x0D, 0x01);
    }

    void bor(Reg dst, std::string name) {
        if (bindings.find(name) != bindings.end()) {
            bor(dst, bindings[name]);
        } else {
            bor(dst, 0xefbeadde);
        }
        bindingSites[name].push_back(bufSize()-4);
    }

    void bor(Reg dst, Mem src) {
        bop(dst, src, 0x0B);
    }
    
    void bor(Mem dst, Reg src) {
        bop(dst, src, 0x09);
    }


    // bitwise xor (dst ^= src)
    void bxor(Reg dst, Reg src) { 
        bop(dst, src, 0x31);
    }

    void bxor(Reg dst, int32_t n) {
        bop(dst, n, 0x35, 0x06);
    }

    void bxor(Reg dst, std::string name) {
        if (bindings.find(name) != bindings.end()) {
            bxor(dst, bindings[name]);
        } else {
            bxor(dst, 0xefbeadde);
        }
        bindingSites[name].push_back(bufSize()-4);
    }

    void bxor(Reg dst, Mem src) {
        bop(dst, src, 0x33);
    }
    
    void bxor(Mem dst, Reg src) {
        bop(dst, src, 0x31);
    }

    // comparison
    void cmp(Reg dst, Reg src) { 
        bop(dst, src, 0x3B);
    }

    void cmp(Reg dst, int32_t n) {
        bop(dst, n, 0x3D, 0x07);
    }

    void cmp(Reg dst, std::string name) {
        if (bindings.find(name) != bindings.end()) {
            cmp(dst, bindings[name]);
        } else {
            cmp(dst, 0xefbeadde);
        }
        bindingSites[name].push_back(bufSize()-4);
    }

    void cmp(Reg dst, Mem src) {
        bop(dst, src, 0x3B);
    }
    
    void cmp(Mem dst, Reg src) {
        bop(dst, src, 0x39);
    }

    // return
    void ret() {
        emit(0xC3);
    }

    // call
    void call(Reg reg) {
        if (reg.num & 8) {
            emit(0x41);
        }
        emit(0xFF);
        emit(0xD0 | (reg.num & 7));
    }

    void call(Mem mem) {
        if (mem.reg.num & 8) {
            emit(0x41);
        }
        emit(0xFF);

        if (mem.offset || (mem.reg.num & 7) == 5) {
            if (mem.offset >= -128 && mem.offset <= 127) {
                emit(0x50 | (mem.reg.num & 7));
                if ((mem.reg.num & 7) == 4) {
                    emit(0x24);
                }
                emit((unsigned char)mem.offset);
            } else {
                emit(0x90 | (mem.reg.num & 7));
                if ((mem.reg.num & 7) == 4) {
                    emit(0x24);
                }
                emitInt32(mem.offset);            
            }
        } else {
            emit(0x10 | (mem.reg.num & 7));
            if ((mem.reg.num & 7) == 4) {
                emit(0x24);
            }
        }
    }

    // mov
    void mov(Reg dst, Reg src) {
        bop(dst, src, 0x8B);
    }
    
    void mov(Reg dst, Mem src) {
        bop(dst, src, 0x8B);
    }
    
    void mov(Mem dst, Reg src) {
        bop(dst, src, 0x89);
    } 

    void mov(Reg dst, int64_t n) {
        emit(0x48 | (dst.num >> 3));
        emit(0xB8 | (dst.num & 7));
        emitInt32(n & 0xffffffff);        
        emitInt32((n>>32));
    }

    void mov(Reg dst, int32_t n) {
        emit(0x48 | (dst.num >> 3));
        emit(0xC7);
        emit(0xC0 | (dst.num & 7));
        emitInt32(n);
    } 

    void mov(Reg dst, void *addr) {
        mov(dst, (int64_t)addr);
    }

    void mov(Reg dst, float n) {
        union {
            float f;
            int i;
        } tmp;
        tmp.f = n;
        mov(dst, tmp.i);
    } 

    /*
    ... conditional moves
    */

    // near jump
    void jmp(const std::string &name) {
        emit(0xE9);
        emitRelBinding(name);
    }

    // jump if equal
    void jeq(const std::string &name) {
        emit(0x0F);
        emit(0x84);
        emitRelBinding(name);
    }

    // jump if not equal
    void jne(const std::string &name) {
        emit(0x0F);
        emit(0x85);
        emitRelBinding(name);
    }

    // jump if <=
    void jle(const std::string &name) {
        emit(0x0F);
        emit(0x8E);
        emitRelBinding(name);
    }

    // jump if >=
    void jge(const std::string &name) {
        emit(0x0F);
        emit(0x8D);
        emitRelBinding(name);
    }

    // jump if <
    void jl(const std::string &name) {
        emit(0x0F);
        emit(0x8C);
        emitRelBinding(name);
    }

    // jump if >
    void jg(const std::string &name) {
        emit(0x0F);
        emit(0x8F);
        emitRelBinding(name);
    }

    /*
    
    // shift left
    void sal(Reg reg, uint8_t val) {
         
    }

    // shift right (with sign extension)
    void sar(Reg reg, uint8_t val) {
        
    }

    // rotate bits left
    void rol(Reg reg, uint8_t val) {
        
    }

    // rotate bits right
    void ror(Reg reg, uint8_t val) {
        
    }

    // bit search left and right
    void bsl(Reg dst, Mem src) {
    }

    void bsl(Reg dst, Reg src) {
    }

    void bsr(Reg dst, Mem src) {
    }

    void bsr(Reg dst, Reg src) {
    }

    // endianness swap
    void bswap(Red reg) {
    }

    // swap 
    void xchg(Reg dst, Mem src) {
    }

    void xchg(Reg dst, Reg src) {
        
    }

    void xchg(Mem dst, Reg src) {
        xchg(src, dst);
    }

    */

    // SSE instructions

    void bop(SSEReg dst, SSEReg src, unsigned char op) {
        if (dst.num > 7 || src.num > 7) {
            emit(0x40 | ((dst.num & 8) >> 1) | (src.num >> 3));
        }
        emit(0x0F);
        emit(op);
        emit(0xC0 | ((dst.num & 7) << 3) | (src.num & 7));
    }

    void bop(SSEReg dst, Mem src, unsigned char op) {
        if (dst.num > 7 || src.reg.num > 7) {
            emit(0x40 | ((dst.num & 8) >> 1) | (src.reg.num >> 3));
        }
        emit(0x0F);
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


    void bop(SSEReg dst, Reg src, unsigned char op) {
        emit(0x48 | ((dst.num & 8) >> 1) | (src.num >> 3));
        emit(0x0F);
        emit(op);
        emit(0xC0 | ((dst.num & 7) << 3) | (src.num & 7));
    }

    void bop(Mem dst, SSEReg src, unsigned char op) {
        if (dst.reg.num > 7 || src.num > 7) {
            emit(0x40 | ((src.num & 8) >> 1) | (dst.reg.num >> 3));
        }
        emit(0x0F);
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

    void movss(SSEReg dst, SSEReg src) {
        emit(0xF3);
        bop(dst, src, 0x10);
    }

    void movss(Mem dst, SSEReg src) {
        emit(0xF3);
        bop(dst, src, 0x11);
    }

    void movss(SSEReg dst, Mem src) {
        emit(0xF3);
        bop(dst, src, 0x10);
    }

    void movntss(Mem dst, SSEReg src) {
        emit(0xF3);
        bop(dst, src, 0x2B);
    }

    void movntps(Mem dst, SSEReg src) {
        bop(dst, src, 0x2B);
    }

    void movaps(Mem dst, SSEReg src) {
        bop(dst, src, 0x29);
    }

    void movaps(SSEReg dst, Mem src) {
        bop(dst, src, 0x28);
    }

    void movaps(SSEReg dst, SSEReg src) {
        bop(dst, src, 0x28);
    }

    void movups(Mem dst, SSEReg src) {
        bop(dst, src, 0x11);
    }

    void movups(SSEReg dst, Mem src) {
        bop(dst, src, 0x10);
    }

    void movups(SSEReg dst, SSEReg src) {
        bop(dst, src, 0x10);
    }

    void addss(SSEReg dst, SSEReg src) {
        emit(0xF3);
        bop(dst, src, 0x58);
    }

    void subss(SSEReg dst, SSEReg src) {
        emit(0xF3);
        bop(dst, src, 0x5C);        
    }

    void mulss(SSEReg dst, SSEReg src) {
        emit(0xF3);
        bop(dst, src, 0x59);
    }

    void divss(SSEReg dst, SSEReg src) {
        emit(0xF3);
        bop(dst, src, 0x5E);
    }

    void cmpeqss(SSEReg dst, SSEReg src) {
        emit(0xF3);
        bop(dst, src, 0xC2);
        emit(0x00);
    }

    void cmpltss(SSEReg dst, SSEReg src) {
        emit(0xF3);
        bop(dst, src, 0xC2);
        emit(0x01);
    }
    
    void cmpless(SSEReg dst, SSEReg src) {
        emit(0xF3);
        bop(dst, src, 0xC2);
        emit(0x02);
    }

    void cmpneqss(SSEReg dst, SSEReg src) {
        emit(0xF3);
        bop(dst, src, 0xC2);
        emit(0x04);
    }

    void cmpnltss(SSEReg dst, SSEReg src) {
        emit(0xF3);
        bop(dst, src, 0xC2);
        emit(0x05);
    }

    void cmpnless(SSEReg dst, SSEReg src) {
        emit(0xF3);
        bop(dst, src, 0xC2);
        emit(0x06);
    }

    void addps(SSEReg dst, SSEReg src) {
        bop(dst, src, 0x58);
    }

    void subps(SSEReg dst, SSEReg src) {
        bop(dst, src, 0x5C);        
    }

    void mulps(SSEReg dst, SSEReg src) {
        bop(dst, src, 0x59);
    }

    void divps(SSEReg dst, SSEReg src) {
        bop(dst, src, 0x5E);
    }

    void cmpeqps(SSEReg dst, SSEReg src) {
        bop(dst, src, 0xC2);
        emit(0x00);
    }

    void cmpltps(SSEReg dst, SSEReg src) {
        bop(dst, src, 0xC2);
        emit(0x01);
    }
    
    void cmpleps(SSEReg dst, SSEReg src) {
        bop(dst, src, 0xC2);
        emit(0x02);
    }

    void cmpneqps(SSEReg dst, SSEReg src) {
        bop(dst, src, 0xC2);
        emit(0x04);
    }

    void cmpnltps(SSEReg dst, SSEReg src) {
        bop(dst, src, 0xC2);
        emit(0x05);
    }

    void cmpnleps(SSEReg dst, SSEReg src) {
        bop(dst, src, 0xC2);
        emit(0x06);
    }

    void bandps(SSEReg dst, SSEReg src) {
        bop(dst, src, 0x54);
    }

    void bandnps(SSEReg dst, SSEReg src) {
        bop(dst, src, 0x55);
    }

    void borps(SSEReg dst, SSEReg src) {
        bop(dst, src, 0x56);
    }

    void bxorps(SSEReg dst, SSEReg src) {
        bop(dst, src, 0x57);
    }

    void cvtsi2ss(SSEReg dst, Reg src) {
        emit(0xF3);
        bop(dst, src, 0x2A);
    }

    void punpckldq(SSEReg dst, SSEReg src) {
        emit(0x66);
        bop(dst, src, 0x62);
    }

    void punpcklqdq(SSEReg dst, SSEReg src) {
        emit(0x66);
        bop(dst, src, 0x6C);        
    }

    void punpckldq(SSEReg dst, Mem src) {
        emit(0x66);
        bop(dst, src, 0x62);
    }

    void punpcklqdq(SSEReg dst, Mem src) {
        emit(0x66);
        bop(dst, src, 0x6C);        
    }

    void shufps(SSEReg dst, SSEReg src, 
                uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
        bop(dst, src, 0xC6);
        emit((a&3) | ((b&3)<<2) | ((c&3)<<4) | ((d&3)<<6));
    }

    void popNonVolatiles() {
        mov(rbx, Mem(rsp, 0xD8));
        mov(rbp, Mem(rsp, 0xD0));
        mov(rdi, Mem(rsp, 0xC8));
        mov(rsi, Mem(rsp, 0xC0));
        mov(r12, Mem(rsp, 0xB8));
        mov(r13, Mem(rsp, 0xB0));
        mov(r14, Mem(rsp, 0xA8));
        mov(r15, Mem(rsp, 0xA0));
        
        movups(xmm6, Mem(rsp, 0x90));
        movups(xmm7, Mem(rsp, 0x80));
        movups(xmm8, Mem(rsp, 0x70));
        movups(xmm9, Mem(rsp, 0x60));
        movups(xmm10, Mem(rsp, 0x50));
        movups(xmm11, Mem(rsp, 0x40));
        movups(xmm12, Mem(rsp, 0x30));
        movups(xmm13, Mem(rsp, 0x20));
        movups(xmm14, Mem(rsp, 0x10));
        movups(xmm15, Mem(rsp, 0x00));
        add(rsp, 0xE0);
    }

    void pushNonVolatiles() {
        sub(rsp, 0xE0);
        mov(Mem(rsp, 0xD8), rbx);
        mov(Mem(rsp, 0xD0), rbp);
        mov(Mem(rsp, 0xC8), rdi);
        mov(Mem(rsp, 0xC0), rsi);
        mov(Mem(rsp, 0xB8), r12);
        mov(Mem(rsp, 0xB0), r13);
        mov(Mem(rsp, 0xA8), r14);
        mov(Mem(rsp, 0xA0), r15);
        movups(Mem(rsp, 0x90), xmm6);
        movups(Mem(rsp, 0x80), xmm7);
        movups(Mem(rsp, 0x70), xmm8);
        movups(Mem(rsp, 0x60), xmm9);
        movups(Mem(rsp, 0x50), xmm10);
        movups(Mem(rsp, 0x40), xmm11);
        movups(Mem(rsp, 0x30), xmm12);
        movups(Mem(rsp, 0x20), xmm13);
        movups(Mem(rsp, 0x10), xmm14);
        movups(Mem(rsp, 0x00), xmm15);
    }

    // Add a mark for intel's static binary analyzer (iaca). Code that
    // includes these won't run. Use savecoff to generate an obj file
    // to pass to the analyzer.
    void iacaStart() {
        emit(0x65);
        emit(0xC6);
        emit(0x04);
        emit(0x25);
        emit(0x6F);
        emit(0x00);
        emit(0x00);
        emit(0x00);
        emit(0x6F);
    }

    void iacaEnd() {
        emit(0x65);
        emit(0xC6);
        emit(0x04);
        emit(0x25);
        emit(0xDE);
        emit(0x00);
        emit(0x00);
        emit(0x00);
        emit(0xDE);
    }

    void label(const std::string &name) {
        bind(name, bufSize());
    }

    // bind a string to a value   
    void bind(const std::string &name, int32_t val) {
        bindings[name] = val;
        std::vector<uint32_t> &sites = bindingSites[name];

        for (size_t i = 0; i < sites.size(); i++) {
            _buffer[sites[i]] = val & 0xff;
            _buffer[sites[i]+1] = (val >> 8) & 0xff;
            _buffer[sites[i]+2] = (val >> 16) & 0xff;
            _buffer[sites[i]+3] = (val >> 24) & 0xff;
        }

        sites = relBindingSites[name];
        for (size_t i = 0; i < sites.size(); i++) {
            int32_t v = val - (sites[i]+4);
            _buffer[sites[i]] = v & 0xff;
            _buffer[sites[i]+1] = (v >> 8) & 0xff;
            _buffer[sites[i]+2] = (v >> 16) & 0xff;
            _buffer[sites[i]+3] = (v >> 24) & 0xff;            
        }

        bindings[name] = val;
    }
    
    static void makePagesExecutable(void *base, size_t size)
    {
#ifdef _MSC_VER
        // Convince windows that the buffer is safe to execute (normally
        // it refuses to do so for security reasons)
        DWORD out;
        VirtualProtect(base, size, PAGE_EXECUTE_READWRITE, &out);
#else //!_MSC_VER
        // Page protection on Mac OS X/Darwin, via http://blog.gmane.org/gmane.comp.gnu.lightning.general/month=20100201
        // and https://llvm.org/svn/llvm-project/compiler-rt/trunk/lib/enable_execute_stack.c
#if __APPLE__
        /* On Darwin, pagesize is always 4096 bytes */
        const uintptr_t pageSize = 4096;
#elif !defined(HAVE_SYSCONF)
        #error "HAVE_SYSCONF not defined! See enable_execute_stack.c"
#else
        const uintptr_t pageSize = sysconf(_SC_PAGESIZE);
#endif /* __APPLE__ */
        uintptr_t p = (uintptr_t)base;
    	const uintptr_t pageAlignMask = ~(pageSize-1);
        unsigned char* startPage = (unsigned char*)(p & pageAlignMask);
        unsigned char* endPage = (unsigned char*)((p+size+pageSize) & pageAlignMask);
        size_t length = endPage - startPage;
    	(void) mprotect((void *)startPage, length, PROT_EXEC | PROT_READ | PROT_WRITE);
#endif //!_MSC_VER
    }

    // run the function with no arguments and no return value
    void run() {
        makePagesExecutable( (void *)&(_buffer[0]), _buffer.size() );
        
        // Cast the buffer to a function pointer of the appropriate type and call it
        void (*func)(void) = (void (*)(void))(&(_buffer[0]));
        /*
        Platform calling conventions
        Scratch registers:
            Win64:
                - RAX, RCX, RDX
                - R8-R11
                - ST(0)-ST(7) // doesn't matter?
                - XMM0-XMM5
                - YMM0-YMM5
                - YMM6H-YMM15H
            Mac/Linux AMD64:
                - RAX, RCX, RDX
                - RSI, RDI
                - R8-R11
                - ST(0)-ST(7) // doesn't matter?
                - XMM0-XMM15
                - YMM0-YMM15
        Callee-save:
            Win64:
                - RBX, RBP
                - RSI, RDI
                - R12-R15
                - XMM6-XMM15
            Mac/Linux AMD64:
                - RBX, RBP
                - R12-R15
        
        Conclusion: Win64 callees will be strictly more conservative than AMD64
        callers require. This should be safe, assuming no args.
        */
        func();
    }

    
    void saveCOFF(const char *filename) {
        FILE *f = fopen(filename, "w");
        unsigned short coffHeader[10] = {0x8664,  // machine
                                         1,     // sections
                                         0, 0,  // date stamp
                                         10*2 + 8 + 8*4, 0, // pointer to symbol table
                                         1, 0,  // entries in symbol table
                                         0,     // optional header size
                                         0};    // characteristics

        // The symbol table
        struct {
            char name[8];
            unsigned long value;
            short sectionNumber;
            unsigned short type;
            unsigned char storageClass;
            unsigned char auxiliary;
        } symbols;

        // The section table
        unsigned char sectionName[8] = {'.', 't', 'e', 'x', 't', 0, 0, 0};
        unsigned int sectionHeader[8] = {0, // physical address
                                         0, // virtual address
                                         bufSize(), // size of data
                                         10*2 + 8 + 8*4 + 18 + 4, // pointer to raw data
                                         0, // relocation table
                                         0, // line numbers
                                         0, // relocation entries and line number entries
                                         0x60500020}; // flags
        
        memset(&symbols, 0, sizeof(symbols));
        symbols.name[0] = 'f';
        symbols.name[1] = 'u';
        symbols.name[2] = 'n';
        symbols.name[3] = 'c';
        symbols.value = 0;
        symbols.sectionNumber = 1;
        symbols.type = 32;
        symbols.storageClass = 2;
        symbols.auxiliary = 0;
        
        fwrite(coffHeader, 2, 10, f);
        fwrite(sectionName, 1, 8, f);
        fwrite(sectionHeader, 4, 8, f);
        fwrite(&symbols, 18, 1, f);
        int stringTableSize = 0;
        fwrite(&stringTableSize, 4, 1, f);
        fwrite(&(_buffer[0]), 1, _buffer.size(), f);
        fclose(f);
    }

    void saveELF(const char *filename) {
        FILE *f = fopen(filename, "w");
        
        
        struct elf64_hdr {
            uint8_t	ident[16];	/* ELF "magic number" */
            uint16_t type;
            uint16_t machine;
            uint32_t version;
            uint64_t entry;		/* Entry point virtual address */
            uint64_t phoff;		/* Program header table file offset */
            uint64_t shoff;		/* Section header table file offset */
            uint32_t flags;
            uint16_t ehsize;
            uint16_t phentsize;
            uint16_t phnum;
            uint16_t shentsize;
            uint16_t shnum;
            uint16_t shstrndx;
        } header;

        // N.B 17 chars long. Normal string algos will not work.
        const char *strtab_contents = "\0.shstrtab\0.text\0";

        memset(header.ident, 0, 16);
        header.ident[0] = 0x7f;
        header.ident[1] = 'E';
        header.ident[2] = 'L';
        header.ident[3] = 'F';
        header.ident[4] = 2;
        header.ident[5] = 1;
        header.ident[6] = 1;
        header.type = 1;
        header.machine = 62;
        header.version = 1;
        header.entry = 0;
        header.phoff = 0;
        header.shoff = 64 + _buffer.size() + 17;
        header.flags = 0;
        header.ehsize = 64;
        header.phentsize = 0;
        header.phnum = 0;
        header.shentsize = 64;
        header.shnum = 3;
        header.shstrndx = 1;

        struct elf64_shdr {
            uint32_t name;		/* Section name, index in string tbl */
            uint32_t type;		/* Type of section */
            uint64_t flags;		/* Miscellaneous section attributes */
            uint64_t addr;		/* Section virtual addr at execution */
            uint64_t offset;	/* Section file offset */
            uint64_t size;		/* Size of section in bytes */
            uint32_t link;		/* Index of another section */
            uint32_t info;		/* Additional section information */
            uint64_t addralign;	/* Section alignment */
            uint64_t entsize;	/* Entry size if section holds table */
        } zero, strtab, code;

        memset(&zero, 0, sizeof(zero));

        strtab.name = 1;
        strtab.type = 3;
        strtab.flags = 0;
        strtab.addr = 0;
        strtab.offset = sizeof(header) + _buffer.size();
        strtab.size = 17;
        strtab.link = 0;
        strtab.info = 0;
        strtab.addralign = 1;
        strtab.entsize = 0;

        code.name = 11; 
        code.type = 1;
        code.flags = 6;
        code.addr = 0;
        code.offset = sizeof(header);
        code.size = _buffer.size();
        code.link = 0;
        code.info = 0;
        code.addralign = 4;
        code.entsize = 0;

        fwrite(&header, 1, 64, f);
        fwrite(&(_buffer[0]), 1, _buffer.size(), f);
        fwrite(strtab_contents, 1, 17, f);
        fwrite(&zero, 1, 64, f);
        fwrite(&strtab, 1, 64, f);
        fwrite(&code, 1, 64, f);
        fclose(f);
    }

    // Add some anonymous data to a data section that lives with this
    // object. Returns a pointer to it.
    template<typename T>
    void *addData(T x) {
        if (data == NULL) {
            data = new uint8_t[4096];
            dataSize = 0;
        }
        assert(dataSize + sizeof(T) <= 4096, 
               "Can't use more than 4096 bytes for constants for now.\n");
        T *ptr = (T*)(data + dataSize);
        dataSize += sizeof(T);
        *ptr = x;        
        return ptr;
    }

protected:

    uint32_t bufSize() {
        return (uint32_t)_buffer.size();
    }

    void emitRelBinding(const std::string &name) {
        int32_t dstOffset = 0xefbeadde;
        if (bindings.find(name) != bindings.end())
            dstOffset = bindings[name]-bufSize()-4;
        emitInt32(dstOffset);        
        relBindingSites[name].push_back(bufSize()-4);        
    }

    void emitBinding(const std::string &name) {
        int32_t dstOffset = 0xefbeadde;
        if (bindings.find(name) != bindings.end())
            dstOffset = bindings[name];
        emitInt32(dstOffset);        
        bindingSites[name].push_back(bufSize()-4);        
    }



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

    // sites in the code that are unresolved 
    // each site holds a 4-byte constant
    std::map<std::string, std::vector<uint32_t> > bindingSites;

    // Instead of pasting a 4-byte constant here, we should paste that
    // constant minus the location immediately following the
    // site. This is useful for jmp instructions.
    std::map<std::string, std::vector<uint32_t> > relBindingSites;
    std::map<std::string, int32_t> bindings;

    // A data page (4096 bytes) to use for constants. Initialized on first use.
    unsigned char *data;
    // How much of it us used?
    int dataSize;
};
