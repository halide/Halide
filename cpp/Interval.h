#ifndef INTERVAL_H
#define INTERVAL_H

inline int64_t add64(int64_t a, int64_t b, bool &overflow) {
    int64_t sum = a+b;
    overflow = (((a <= 0) && (b < 0) && (sum >= 0)) || 
                ((a >= 0) && (b > 0) && (sum <= 0)));
    return sum;
}

inline int64_t sub64(int64_t a, int64_t b, bool &overflow) {
    int64_t sub = a-b;
    overflow = (((a <= 0) && (b > 0) && (sub >= 0)) || 
                ((a >= 0) && (b < 0) && (sub <= 0)));    
    return sub;
}

inline int64_t mul64(int64_t x, int64_t y, bool &overflow) {
    int64_t big   = (int64_t(0x7fffffff) << 32) | int64_t(0xffffffff);
    int64_t small = (int64_t(0x80000000) << 32);
    if (x > 0) {
        if (y > 0) {
            overflow = x > (big / y);
        } else {
            overflow = y < (small / x);
        }
    } else {
        if (y > 0) {
            overflow = x < (small / y);
        } else if (x == 0) {
            overflow = false;
        } else {
            overflow = y < (big / x);
        }
    }
    return x*y;
}

// An interval object to do interval arithmetic with
struct Interval {

    // The largest negative number is always a valid min
    static const int64_t SMALLEST = 0x8000000000000000l;

    // The largest positive number is always a valid max
    static const int64_t BIGGEST = 0x7fffffffffffffffl;

    Interval() : _min(SMALLEST), _max(BIGGEST) {        
    }

    Interval(int64_t a, int64_t b) : _min(a), _max(b) {
    }
 
    Interval operator+(const Interval &b) const {
        Interval n(*this);
        n += b;
        return n;
    }

    Interval operator-(const Interval &b) const {
        Interval n(*this);
        n -= b;
        return n;
    }

    Interval operator*(const Interval &b) const {
        Interval n(*this);
        n *= b;
        return n;
    }

    Interval operator+(int64_t b) const {
        Interval n(*this);
        n += b;
        return n;
    }

    Interval operator-(int64_t b) const {
        Interval n(*this);
        n -= b;
        return n;
    }

    Interval operator*(int64_t b) const {
        Interval n(*this);
        n *= b;
        return n;
    }

    Interval operator-() const {
        Interval n(-_max, -_min);
        // if min is really small, -min may not be representable
        if (!bounded() || _min == SMALLEST) n.unbounded();        
        return n;
    }

    Interval &operator+=(const Interval &b) {
        if (!b.bounded() || !bounded()) {
            unbounded();
            return *this;
        }

        bool o1, o2;
        _min = add64(_min, b._min, o1);
        _max = add64(_max, b._max, o2);
        if (o1 || o2) unbounded();
        return *this;
    }

    Interval &operator-=(const Interval &b) {
        if (!b.bounded() || !bounded()) {
            unbounded();
            return *this;
        }

        bool o1, o2;
        _min = sub64(_min, b._max, o1);
        _max = sub64(_max, b._min, o2);
        if (o1 || o2) unbounded();
        return *this;
    }
    
    Interval &operator*=(const Interval &o) {
        if (!o.bounded() || !bounded()) {
            unbounded();
            return *this;
        }

        bool o1, o2, o3, o4;
        int64_t a = mul64(_min, o._min, o1);
        int64_t b = mul64(_min, o._max, o2);
        int64_t c = mul64(_max, o._min, o3);
        int64_t d = mul64(_max, o._max, o4);

        if (o1 || o2 || o3 || o4) {
            unbounded();
            return *this;
        }

        if (a < b && a < c && a < d) {
            _min = a;
        } else if (b < c && b < d) {
            _min = b;
        } else if (c < d) {
            _min = c;
        } else {
            _min = d;
        }

        if (a > b && a > c && a > d) {
            _max = a;
        } else if (b > c && b > d) {
            _max = b;
        } else if (c > d) {
            _max = c;
        } else {
            _max = d;
        }
        return *this;
    }


    Interval &operator+=(int64_t b) {
        if (!bounded()) {
            return *this;
        }

        bool o1, o2;
        _min = add64(_min, b, o1);
        _max = add64(_max, b, o2);
        if (o1 || o2) unbounded();
        return *this;
    }

    Interval &operator-=(int64_t b) {
        if (!bounded()) {
            return *this;
        }

        bool o1, o2;
        _min = sub64(_min, b, o1);
        _max = sub64(_max, b, o2);
        if (o1 || o2) unbounded();
        return *this;
    }
    
    Interval &operator*=(int64_t o) {
        if (!bounded()) {
            unbounded();
            return *this;
        }

        bool o1, o2;
        int64_t a = mul64(_min, o, o1);
        int64_t b = mul64(_max, o, o2);

        if (o1 || o2) {
            unbounded();
            return *this;
        }

        if (a < b) {
            _min = a;
            _max = b;
        } else {
            _min = b;
            _max = a;
        }
        return *this;
    }

    bool bounded() const {
        return !(_min == SMALLEST && _max == BIGGEST);
    }

    int64_t min() const {
        return _min;
    }

    int64_t max() const {
        return _max;
    }

    void setBounds(int64_t a, int64_t b) {
        _min = a;
        _max = b;
    }

    bool constant() const {
        return _min == _max;
    }

private:

    void unbounded() {
        _min = SMALLEST;
        _max = BIGGEST;
    }

    int64_t _min;
    int64_t _max;


};




// An interval with a modulus and remainder
struct SteppedInterval {

    SteppedInterval() : i(), rem(0), mod(1) {        
    }

    SteppedInterval(int64_t a, int64_t b, int64_t rem, int64_t mod) :
        i(a, b), rem(rem), mod(mod) {
    }
 

    SteppedInterval operator+(const SteppedInterval &b) const {
        SteppedInterval n(*this);
        n += b;
        return n;
    }

    SteppedInterval operator-(const SteppedInterval &b) const {
        SteppedInterval n(*this);
        n -= b;
        return n;
    }

    SteppedInterval operator*(const SteppedInterval &b) const {
        SteppedInterval n(*this);
        n *= b;
        return n;
    }

    SteppedInterval operator+(int64_t b) const {
        SteppedInterval n(*this);
        n += b;
        return n;
    }

    SteppedInterval operator-(int64_t b) const {
        SteppedInterval n(*this);
        n -= b;
        return n;
    }

    SteppedInterval operator*(int64_t b) const {
        SteppedInterval n(*this);
        n *= b;
        return n;
    }

    SteppedInterval operator-() const {        
        SteppedInterval n;
        n.i = -i;
        n.mod = mod;
        n.rem = mod-rem;
        return n;
    }

    SteppedInterval &operator+=(const SteppedInterval &b) {
        if (b.constant()) return (*this) += b.min();
        if (constant()) return (*this) = (b + i.min());

        i += b.i;

        bool o;
        mod = gcd(mod, b.mod);
        rem = add64(rem, b.rem, o) % mod;
        if (o) invalidate();
        while (rem < 0) rem += mod;
        return *this;
    }

    SteppedInterval &operator+=(int64_t b) {
        i += b;

        bool o;
        rem = add64(rem, b, o) % mod;
        if (o) invalidate();
        while (rem < 0) rem += mod;
        return *this;
    }

    SteppedInterval &operator-=(const SteppedInterval &b) {
        if (b.constant()) return (*this) -= b.min();
        if (constant()) return (*this) = -(b - i.min());

        i -= b.i;

        bool o;
        mod = gcd(mod, b.mod);
        rem = sub64(rem, b.rem, o) % mod;
        if (o) invalidate();
        while (rem < 0) rem += mod;
        return *this;
    }

    SteppedInterval &operator-=(int64_t b) {
        i -= b;

        bool o;
        rem = sub64(rem, b, o) % mod;
        if (o) invalidate();
        while (rem < 0) rem += mod;
        return *this;
    }
    
    SteppedInterval &operator*=(const SteppedInterval &b) {
        if (b.constant()) return (*this) *= b.min();
        if (constant()) return (*this) = b * i.min();

        i *= b.i;
        
        bool o;
        mod = gcd(mod, b.mod);
        rem = mul64(rem, b.rem, o) % mod;
        if (o) invalidate();
        while (rem < 0) rem += mod;
        return *this;
        
    }

    SteppedInterval &operator*=(int64_t b) {
        i *= b;
        
        bool o;
        mod = mul64(mod, b, o);
        rem = mul64(rem, b, o);
        if (o) invalidate();
        return *this;
        
    }
    
    int64_t modulus() const {
        return mod;
    }

    int64_t remainder() const {
        return rem;
    }

    int64_t min() const {
        return i.min();
    }

    int64_t max() const {
        return i.max();
    }

    bool bounded() const {
        // there's always a valid modulus and remainder
        return i.bounded();
    }

    void setBounds(int64_t a, int64_t b) {
        i.setBounds(a, b);
    }

    void setCongruence(int64_t r, int64_t m) {
        rem = r;
        mod = m;
    }

    bool constant() const {
        return min() == max();
    }

private:

    void invalidate() {
        rem = 0;
        mod = 1;
    }

    int64_t gcd(int64_t a, int64_t b) {
        int64_t tmp;
        while (b) {
            tmp = b;
            b = a % b;
            a = tmp;
        }
        return a;
    }

    Interval i;
    int64_t rem;
    int64_t mod;

};

Interval abs(const Interval &x);
SteppedInterval abs(const SteppedInterval &x);

inline Interval operator+(int64_t b, const Interval &a) {
    return a+b;
}

inline SteppedInterval operator+(int64_t b, const SteppedInterval &a) {
    return a+b;
}

inline Interval operator*(int64_t b, const Interval &a) {
    return a*b;
}

inline SteppedInterval operator*(int64_t b, const SteppedInterval &a) {
    return a*b;
}

inline Interval operator-(int64_t b, const Interval &a) {
    return -(a-b);
}

inline SteppedInterval operator-(int64_t b, const SteppedInterval &a) {
    return -(a-b);
}

#endif
