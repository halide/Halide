#ifndef HALIDE_RUNTIME_TCM_BUMP_ALLOCATOR_H
#define HALIDE_RUNTIME_TCM_BUMP_ALLOCATOR_H

#include "HalideRuntime.h"

// A TCM allocator that uses bump-pointer allocation. This should be used as a
// singleton class.
//
// To use this allocator, create an instance before any TCM allocation. When an
// instance is created, it will acquire from GXP ALL available TCM on bank 1 and
// bank 0, which will be freed when the last instance is destroyed. Once the
// instance is created, it will register its own address in register THREADPTR,
// so you don't need to pass the object around. Allocation and deallocation can
// be done through the static functions.
//
// Example usage:
//
//   int KernelMain() {
//     TcmBumpAllocator tcm;
//     ...
//   }
class TcmBumpAllocator {
 public:
  TcmBumpAllocator();
  ~TcmBumpAllocator();

  // Allocates TCM of the specified size, bank, and alignment.
  static void* Allocate(int size, int bank, int alignment);

  // Frees the previously allocated TCM.
  static void Deallocate(void* ptr);

  // Frees all previously allocated TCM.
  void Reset();

 private:
  static constexpr int kNumBanks = 2;

  void* AllocInternal(int size, int bank, int alignment);
  void FreeInternal(void* ptr);
  void ResetInternal();

  uintptr_t start_[kNumBanks];  // start of the bank
  uintptr_t end_[kNumBanks];    // end of the bank
  uintptr_t ptr_[kNumBanks];    // next available memory
};

#endif  // HALIDE_RUNTIME_TCM_BUMP_ALLOCATOR_H
