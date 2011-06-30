void ptr_test(void** inval, void** outval) {
    long* in = (long*)inval[1];
    long* out = (long*)outval[1];

    out[0] = in[0];
}
