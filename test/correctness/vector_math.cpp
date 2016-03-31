        Func f10;
        f10(x, y) = (input(x, y)) / cast<A>(Expr(c));
        f10.vectorize(x, lanes);
        Image<A> im10 = f10.realize(W, H);

        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                A correct = divide(input(x, y), (A)c);

                if (!close_enough(im10(x, y), correct)) {
                    printf("im10(%d, %d) = %f/%d = %f instead of %f\n", x, y,
                           (double)(input(x, y)), c,
                           (double)(im10(x, y)),
                           (double)(correct));
                    printf("Error when dividing by %d\n", c);
                    return false;
                }
            }
        }
    }

    // Interleave
    if (verbose) printf("Interleaving store\n");
    Func f11;
    f11(x, y) = select((x%2)==0, input(x/2, y), input(x/2, y+1));
    f11.vectorize(x, lanes);
    Image<A> im11 = f11.realize(W, H);

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            A correct = ((x%2)==0) ? input(x/2, y) : input(x/2, y+1);
            if (im11(x, y) != correct) {
                printf("im11(%d, %d) = %f instead of %f\n", x, y, (double)(im11(x, y)), (double)(correct));
                return false;
            }
        }
    }

    // Reverse
    if (verbose) printf("Reversing\n");
    Func f12;
    f12(x, y) = input(W-1-x, H-1-y);
    f12.vectorize(x, lanes);
    Image<A> im12 = f12.realize(W, H);

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            A correct = input(W-1-x, H-1-y);
            if (im12(x, y) != correct) {
                printf("im12(%d, %d) = %f instead of %f\n", x, y, (double)(im12(x, y)), (double)(correct));
                return false;
            }
        }
    }

    // Unaligned load with known shift
    if (verbose) printf("Unaligned load\n");
    Func f13;
    f13(x, y) = input(x+3, y);
    f13.vectorize(x, lanes);
    Image<A> im13 = f13.realize(W, H);

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            A correct = input(x+3, y);
            if (im13(x, y) != correct) {
                printf("im13(%d, %d) = %f instead of %f\n", x, y, (double)(im13(x, y)), (double)(correct));
            }
        }
    }

    // Absolute value
    if (!type_of<A>().is_uint()) {
        if (verbose) printf("Absolute value\n");
        Func f14;
        f14(x, y) = cast<A>(abs(input(x, y)));
        Image<A> im14 = f14.realize(W, H);

        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                A correct = input(x, y);
                if (correct <= 0) correct = -correct;
                if (im14(x, y) != correct) {
                    printf("im14(%d, %d) = %f instead of %f\n", x, y, (double)(im14(x, y)), (double)(correct));
                }
            }
        }
    }

    // pmaddwd
    if (type_of<A>() == Int(16)) {
        if (verbose) printf("pmaddwd\n");
        Func f15, f16;
        f15(x, y) = cast<int>(input(x, y)) * input(x, y+2) + cast<int>(input(x, y+1)) * input(x, y+3);
        f16(x, y) = cast<int>(input(x, y)) * input(x, y+2) - cast<int>(input(x, y+1)) * input(x, y+3);
        f15.vectorize(x, lanes);
        f16.vectorize(x, lanes);
        Image<int32_t> im15 = f15.realize(W, H);
        Image<int32_t> im16 = f16.realize(W, H);
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                int correct15 = input(x, y)*input(x, y+2) + input(x, y+1)*input(x, y+3);
                int correct16 = input(x, y)*input(x, y+2) - input(x, y+1)*input(x, y+3);
                if (im15(x, y) != correct15) {
                    printf("im15(%d, %d) = %d instead of %d\n", x, y, im15(x, y), correct15);
                }
                if (im16(x, y) != correct16) {
                    printf("im16(%d, %d) = %d instead of %d\n", x, y, im16(x, y), correct16);
                }
            }
        }
    }

    // Fast exp, log, and pow
    if (type_of<A>() == Float(32)) {
        if (verbose) printf("Fast transcendentals\n");
        Func f15, f16, f17, f18, f19, f20;
        Expr a = input(x, y) * 0.5f;
        Expr b = input((x+1)%W, y) * 0.5f;
        f15(x, y) = log(a);
        f16(x, y) = exp(b);
        f17(x, y) = pow(a, b/16.0f);
        f18(x, y) = fast_log(a);
        f19(x, y) = fast_exp(b);
        f20(x, y) = fast_pow(a, b/16.0f);
        Image<float> im15 = f15.realize(W, H);
        Image<float> im16 = f16.realize(W, H);
        Image<float> im17 = f17.realize(W, H);
        Image<float> im18 = f18.realize(W, H);
        Image<float> im19 = f19.realize(W, H);
        Image<float> im20 = f20.realize(W, H);

        int worst_log_mantissa = 0;
        int worst_exp_mantissa = 0;
        int worst_pow_mantissa = 0;
        int worst_fast_log_mantissa = 0;
        int worst_fast_exp_mantissa = 0;
        int worst_fast_pow_mantissa = 0;

        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                float a = input(x, y) * 0.5f;
                float b = input((x+1)%W, y) * 0.5f;
                float correct_log = logf(a);
                float correct_exp = expf(b);
                float correct_pow = powf(a, b/16.0f);

                int correct_log_mantissa = mantissa(correct_log);
                int correct_exp_mantissa = mantissa(correct_exp);
                int correct_pow_mantissa = mantissa(correct_pow);

                int log_mantissa = mantissa(im15(x, y));
                int exp_mantissa = mantissa(im16(x, y));
                int pow_mantissa = mantissa(im17(x, y));

                int fast_log_mantissa = mantissa(im18(x, y));
                int fast_exp_mantissa = mantissa(im19(x, y));
                int fast_pow_mantissa = mantissa(im20(x, y));

                int log_mantissa_error = abs(log_mantissa - correct_log_mantissa);
                int exp_mantissa_error = abs(exp_mantissa - correct_exp_mantissa);
                int pow_mantissa_error = abs(pow_mantissa - correct_pow_mantissa);
                int fast_log_mantissa_error = abs(fast_log_mantissa - correct_log_mantissa);
                int fast_exp_mantissa_error = abs(fast_exp_mantissa - correct_exp_mantissa);
                int fast_pow_mantissa_error = abs(fast_pow_mantissa - correct_pow_mantissa);

                worst_log_mantissa = std::max(worst_log_mantissa, log_mantissa_error);
                worst_exp_mantissa = std::max(worst_exp_mantissa, exp_mantissa_error);

                if (a >= 0) {
                    worst_pow_mantissa = std::max(worst_pow_mantissa, pow_mantissa_error);
                }

                if (is_finite(correct_log)) {
                    worst_fast_log_mantissa = std::max(worst_fast_log_mantissa, fast_log_mantissa_error);
                }

                if (is_finite(correct_exp)) {
                    worst_fast_exp_mantissa = std::max(worst_fast_exp_mantissa, fast_exp_mantissa_error);
                }

                if (is_finite(correct_pow) && a > 0) {
                    worst_fast_pow_mantissa = std::max(worst_fast_pow_mantissa, fast_pow_mantissa_error);
                }

                if (log_mantissa_error > 8) {
                    printf("log(%f) = %1.10f instead of %1.10f (mantissa: %d vs %d)\n",
                           a, im15(x, y), correct_log, correct_log_mantissa, log_mantissa);
                }
                if (exp_mantissa_error > 32) {
                    // Actually good to the last 2 bits of the mantissa with sse4.1 / avx
                    printf("exp(%f) = %1.10f instead of %1.10f (mantissa: %d vs %d)\n",
                           b, im16(x, y), correct_exp, correct_exp_mantissa, exp_mantissa);
                }
                if (a >= 0 && pow_mantissa_error > 64) {
                    printf("pow(%f, %f) = %1.10f instead of %1.10f (mantissa: %d vs %d)\n",
                           a, b/16.0f, im17(x, y), correct_pow, correct_pow_mantissa, pow_mantissa);
                }
                if (is_finite(correct_log) && fast_log_mantissa_error > 64) {
                    printf("fast_log(%f) = %1.10f instead of %1.10f (mantissa: %d vs %d)\n",
                           a, im18(x, y), correct_log, correct_log_mantissa, fast_log_mantissa);
                }
                if (is_finite(correct_exp) && fast_exp_mantissa_error > 64) {
                    printf("fast_exp(%f) = %1.10f instead of %1.10f (mantissa: %d vs %d)\n",
                           b, im19(x, y), correct_exp, correct_exp_mantissa, fast_exp_mantissa);
                }
                if (a >= 0 && is_finite(correct_pow) && fast_pow_mantissa_error > 128) {
                    printf("fast_pow(%f, %f) = %1.10f instead of %1.10f (mantissa: %d vs %d)\n",
                           a, b/16.0f, im20(x, y), correct_pow, correct_pow_mantissa, fast_pow_mantissa);
                }
            }
        }

        /*
        printf("log mantissa error: %d\n", worst_log_mantissa);
        printf("exp mantissa error: %d\n", worst_exp_mantissa);
        printf("pow mantissa error: %d\n", worst_pow_mantissa);
        printf("fast_log mantissa error: %d\n", worst_fast_log_mantissa);
        printf("fast_exp mantissa error: %d\n", worst_fast_exp_mantissa);
        printf("fast_pow mantissa error: %d\n", worst_fast_pow_mantissa);
        */
    }

    // Lerp (where the weight is the same type as the values)
    if (verbose) printf("Lerp\n");
    Func f21;
    Expr weight = input(x+2, y);
    Type t = type_of<A>();
    if (t.is_float()) {
        weight = clamp(weight, cast<A>(0), cast<A>(1));
    } else if (t.is_int()) {
        weight = cast(UInt(t.bits(), t.lanes()), max(0, weight));
    }
    f21(x, y) = lerp(input(x, y), input(x+1, y), weight);
    Image<A> im21 = f21.realize(W, H);

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            double a = (double)(input(x, y));
            double b = (double)(input(x+1, y));
            double w = (double)(input(x+2, y));
            if (w < 0) w = 0;
            if (!t.is_float()) {
                uint64_t divisor = 1;
                divisor <<= t.bits();
                divisor -= 1;
                w /= divisor;
            }
            w = std::min(std::max(w, 0.0), 1.0);

            double lerped = (a*(1.0-w) + b*w);
            if (!t.is_float()) {
                lerped = floor(lerped + 0.5);
            }
            A correct = (A)(lerped);
            if (im21(x, y) != correct) {
                printf("lerp(%f, %f, %f) = %f instead of %f\n", a, b, w, (double)(im21(x, y)), (double)(correct));
                return false;
            }
        }
    }

    // Absolute difference
    if (verbose) printf("Absolute difference\n");
    Func f22;
    f22(x, y) = absd(input(x, y), input(x+1, y));
    f22.vectorize(x, lanes);
    Image<typename with_unsigned<A>::type> im22 = f22.realize(W, H);

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            typename with_unsigned<A>::type correct = absd((double)input(x, y), (double)input(x+1, y));
            if (im22(x, y) != correct) {
                printf("im22(%d, %d) = %f instead of %f\n", x, y, (double)(im3(x, y)), (double)(correct));
                return false;
            }
        }
    }

    return true;
}

int main(int argc, char **argv) {

    bool ok = true;

    // Only native vector widths - llvm doesn't handle others well
    ok = ok && test<float>(4);
    ok = ok && test<float>(8);
    ok = ok && test<double>(2);
    ok = ok && test<uint8_t>(16);
    ok = ok && test<int8_t>(16);
    ok = ok && test<uint16_t>(8);
    ok = ok && test<int16_t>(8);
    ok = ok && test<uint32_t>(4);
    ok = ok && test<int32_t>(4);

    if (!ok) return -1;
    printf("Success!\n");
    return 0;
}
