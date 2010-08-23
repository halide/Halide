#include <vector>
#include <string>
#include <map>
#include <stdint.h>

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
        xmm12(12), xmm13(13), xmm14(14), xmm15(15)
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
    // dst += src
    void add(Reg dst, Reg src) { 
        bop(dst, src, 0x03);
    }

    void add(Reg dst, int32_t n) {
        bop(dst, n, 0x05, 0x00);
    }

    void add(Reg dst, std::string name) {
        if (bindings.find(name) != bindings.end()) {
            add(dst, bindings[name]);
        } else {
            add(dst, 0xefbeadde);
        }
        bindingSites[name].push_back(_buffer.size()-4);
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
        bindingSites[name].push_back(_buffer.size()-4);
    }

    void sub(Reg dst, Mem src) {
        bop(dst, src, 0x2B);
    }
    
    void sub(Mem dst, Reg src) {
        bop(dst, src, 0x29);
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
        bindingSites[name].push_back(_buffer.size()-4);
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
        bindingSites[name].push_back(_buffer.size()-4);
    }

    void bor(Reg dst, Mem src) {
        bop(dst, src, 0x0B);
    }
    
    void bor(Mem dst, Reg src) {
        bop(dst, src, 0x09);
    }


    // bitwise xor (dst ^= src)
    void xor(Reg dst, Reg src) { 
        bop(dst, src, 0x31);
    }

    void xor(Reg dst, int32_t n) {
        bop(dst, n, 0x35, 0x06);
    }

    void xor(Reg dst, std::string name) {
        if (bindings.find(name) != bindings.end()) {
            xor(dst, bindings[name]);
        } else {
            xor(dst, 0xefbeadde);
        }
        bindingSites[name].push_back(_buffer.size()-4);
    }

    void xor(Reg dst, Mem src) {
        bop(dst, src, 0x33);
    }
    
    void xor(Mem dst, Reg src) {
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
        bindingSites[name].push_back(_buffer.size()-4);
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
                emit(mem.offset);
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

    /*
    void mov(Reg dst, Mem src) {
    }
    
    void mov(Mem dst, Reg src) {
    }

    void mov(Reg dst, int64_t n) {
    }

    void mov(Reg dst, int64_t *ptr) {
    }

    void mov(int64_t *ptr, Reg src) {
    }

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

    void movss(SSEReg dst, SSEReg src) {
    }

    void movss(SSEReg dst, float *src) {
    }

    void movss(float *dst, SSEReg src) {
    }

    void movss(Mem dst, SSEReg src) {
    }

    void movss(SSEReg dst, Mem src) {
    }

    void addss(SSEReg dst, SSEReg src) {
    }

    void subss(SSEReg dst, SSEReg src) {
        
    }

    void label(const std::string &name) {
        bind(name, _buffer.size());
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

protected:

    void emitRelBinding(const std::string &name) {
        int32_t dstOffset = 0xefbeadde;
        if (bindings.find(name) != bindings.end())
            dstOffset = bindings[name]-_buffer.size()-4;
        emitInt32(dstOffset);        
        relBindingSites[name].push_back(_buffer.size()-4);        
    }

    void emitBinding(const std::string &name) {
        int32_t dstOffset = 0xefbeadde;
        if (bindings.find(name) != bindings.end())
            dstOffset = bindings[name];
        emitInt32(dstOffset);        
        bindingSites[name].push_back(_buffer.size()-4);        
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
};
