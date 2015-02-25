
#include "Halide.h"
#include <stdio.h>
#include <stdint.h>
#include <string>

using namespace Halide;

int local_popcount(uint32_t v) {
  int count = 0;
  while (v) {
    if (v & 1) ++count;
    v >>= 1;
  }
  return count;
}

int local_count_trailing_zeros(uint32_t v) {
  for (int b = 0; b < 32; ++b) {
    if (v & (1 << b))
      // found a set bit
      return b;
  }
  return 0;
}

int local_count_leading_zeros(uint32_t v) {
  for (int b = 31; b >= 0; --b) {
    if (v & (1 << b))
      // found a set bit
      return 31 - b;
  }
  return 0;
}

std::string as_bits(uint32_t v) {
  std::string ret;
  for (int i = 31; i >= 0; --i)
    ret += (v & (1 << i)) ? '1' : '0';
  return ret;
}

int main() {
  Image<uint32_t> input(256);
  for (int i = 0; i < 256; i++) {
    if (i < 16)
      input(i) = i;
    else if (i < 32)
      input(i) = 0xfffffffful - i;
    else
      input(i) = rand();
  }

  Var x;

  Func popcount_test("popcount_test");
  popcount_test(x) = popcount(input(x));

  Image<uint32_t> popcount_result = popcount_test.realize(256);
  for (int i = 0; i < 256; ++i) {
    if (popcount_result(i) != local_popcount(input(i))) {
      std::string bits_string = as_bits(input(i));
      printf("Popcount of %u [0b%s] returned %d (should be %d)\n",
             input(i), bits_string.c_str(), popcount_result(i),
             local_popcount(input(i)));
      return -1;
    }
  }

  Func ctlz_test("ctlz_test");
  ctlz_test(x) = count_leading_zeros(input(x));

  Image<uint32_t> ctlz_result = ctlz_test.realize(256);
  for (int i = 0; i < 256; ++i) {
    if (input(i) == 0)
      // results are undefined for zero input
      continue;

    if (ctlz_result(i) != local_count_leading_zeros(input(i))) {
      std::string bits_string = as_bits(input(i));
      printf("Ctlz of %u [0b%s] returned %d (should be %d)\n",
             input(i), bits_string.c_str(), ctlz_result(i),
             local_count_leading_zeros(input(i)));
      return -1;
    }
  }

  Func cttz_test("cttz_test");
  cttz_test(x) = count_trailing_zeros(input(x));

  Image<uint32_t> cttz_result = cttz_test.realize(256);
  for (int i = 0; i < 256; ++i) {
    if (input(i) == 0)
      // results are undefined for zero input
      continue;

    if (cttz_result(i) != local_count_trailing_zeros(input(i))) {
      std::string bits_string = as_bits(input(i));
      printf("Cttz of %u [0b%s] returned %d (should be %d)\n",
             input(i), bits_string.c_str(), cttz_result(i),
             local_count_trailing_zeros(input(i)));
      return -1;
    }
  }

  printf("Success!\n");
  return 0;
}
