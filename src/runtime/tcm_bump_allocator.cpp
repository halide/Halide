#include "tcm_bump_allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __XTENSA__
#include <xtensa/tie/xt_core.h>

extern void* TcmAllocMaxOnBank(uint8_t bank, size_t* size);
extern void tcm_free(void *ptr);
#else
// Xtensa cstub doesn't have wrappers for accessing threadptr. We do this by
// ourselves.
uintptr_t g_threadptr = 0;
uintptr_t XT_RUR_THREADPTR() { return g_threadptr; }
void XT_WUR_THREADPTR(uintptr_t v) { g_threadptr = v; }
#endif

TcmBumpAllocator::TcmBumpAllocator() {
#ifdef __XTENSA__
  if (XT_RUR_THREADPTR() == 0) {
    XT_WUR_THREADPTR(reinterpret_cast<uintptr_t>(this));

    for (int i = 0; i < kNumBanks; ++i) {
      size_t size = 0;
      void* ptr = nullptr;
      ptr = TcmAllocMaxOnBank(i, &size);
      start_[i] = reinterpret_cast<uintptr_t>(ptr);
      end_[i] = start_[i] + size;
    }
    Reset();
  }
#endif
}

TcmBumpAllocator::~TcmBumpAllocator() {
#ifdef __XTENSA__
  if (XT_RUR_THREADPTR() == reinterpret_cast<uintptr_t>(this)) {
    for (int i = 0; i < kNumBanks; ++i) {
      tcm_free(reinterpret_cast<void*>(start_[i]));
    }

    XT_WUR_THREADPTR(0);
  }
#endif
}

void TcmBumpAllocator::Reset() {
  for (int i = 0; i < kNumBanks; ++i) {
    ptr_[i] = start_[i];
  }
}

void TcmBumpAllocator::FreeInternal(void* ptr) {
  int bank = 0;
  for (int i = 0; i < kNumBanks; ++i) {
    if (reinterpret_cast<uintptr_t>(ptr) >= end_[i]) {
      bank += 1;
    }
  }

  ptr_[bank] = reinterpret_cast<uintptr_t>(ptr);
}

void* TcmBumpAllocator::Allocate(int size, int bank, int alignment) {
  return reinterpret_cast<TcmBumpAllocator*>(XT_RUR_THREADPTR())
      ->AllocInternal(size, bank, alignment);
}

void TcmBumpAllocator::Deallocate(void* ptr) {
  return reinterpret_cast<TcmBumpAllocator*>(XT_RUR_THREADPTR())
      ->FreeInternal(ptr);
}

void* TcmBumpAllocator::AllocInternal(int size, int bank, int alignment) {
  int mask = (alignment - 1);
  uintptr_t p = ptr_[bank];
  p = (p + mask) & ~mask;  // round up to the next multiple of alignment
  if (p + size + alignment > end_[bank]) {
    // Not enough memory.
    return nullptr;
  }
  ptr_[bank] = p + size;
  return reinterpret_cast<void*>(p);
}

#ifdef __cplusplus
}  // extern "C"
#endif
