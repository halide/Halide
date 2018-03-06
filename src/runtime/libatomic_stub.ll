; This code is derived by compiling the following C++ with this command line:
; 
;   clang -emit-llvm -target mips-unknown-nacl-unknown -S <the.cpp> -o <the.ll>
; 
; The main change is changing the function attributes by adding 'alwaysinline'.
; It is possible this could be generated from C++ directly in the Halide build
; but I chose to do it this way as __atomic_ identifiers are a bit magical.
; (And I'm hoping the entire mechanism can be eliminated by manipulating
; the runtime compilation setup for the runtime.)
;
; extern "C" int __atomic_load_4(int *addr, int ordering) {
;   int result;
;   if (ordering == __ATOMIC_RELAXED) {
;     __atomic_load(addr, &result, __ATOMIC_RELAXED);
;   } else if (ordering == __ATOMIC_CONSUME) {
;     __atomic_load(addr, &result, __ATOMIC_CONSUME);
;   } else  if (ordering == __ATOMIC_ACQUIRE) {
;     __atomic_load(addr, &result, __ATOMIC_ACQUIRE);
;   } else if (ordering == __ATOMIC_SEQ_CST) {
;     __atomic_load(addr, &result, __ATOMIC_SEQ_CST);
;   }
; 
;   return result;
; }
; 
; extern "C" void __atomic_store_4(int *addr, int val, int ordering) {
;   int result;
;   if (ordering == __ATOMIC_RELAXED) {
;     __atomic_store(addr, &val, __ATOMIC_RELAXED);
;   } else if (ordering == __ATOMIC_RELEASE) {
;     __atomic_store(addr, &val, __ATOMIC_RELEASE);
;   } else if (ordering == __ATOMIC_SEQ_CST) {
;     __atomic_store(addr, &val, __ATOMIC_SEQ_CST);
;   }
; }
; 
; extern "C" bool __atomic_compare_exchange_4(int *addr, int *expected, int desired, int success_ordering, int fail_ordering) {
;   bool result;
; 
; // This has to use the strong version of CAS, which is unforunate but
; // likely not very costly. (__atomic_compare_exchange_4 is specified
; // as always being strong, thus there is no way to map this call
; // to the weak llvm instruction.)
; #define one_case(first_ordering) \
;   if (fail_ordering == __ATOMIC_RELAXED) { \
;     result = __atomic_compare_exchange(addr, expected, &desired, false, first_ordering, __ATOMIC_RELAXED); \
;   } else if (fail_ordering == __ATOMIC_CONSUME) { \
;     result = __atomic_compare_exchange(addr, expected, &desired, false, first_ordering, __ATOMIC_CONSUME); \
;   } else if (fail_ordering == __ATOMIC_ACQUIRE) { \
;     result = __atomic_compare_exchange(addr, expected, &desired, false, first_ordering, __ATOMIC_ACQUIRE); \
;   } else if (fail_ordering == __ATOMIC_RELEASE) { \
;     result = __atomic_compare_exchange(addr, expected, &desired, false, first_ordering, __ATOMIC_RELEASE); \
;   } else if (fail_ordering == __ATOMIC_ACQ_REL) { \
;     result = __atomic_compare_exchange(addr, expected, &desired, false, first_ordering, __ATOMIC_ACQ_REL); \
;   } else if (fail_ordering == __ATOMIC_SEQ_CST) { \
;     result = __atomic_compare_exchange(addr, expected, &desired, false, first_ordering, __ATOMIC_SEQ_CST); \
;   }
; 
;   if (success_ordering == __ATOMIC_RELAXED) {
;     one_case(__ATOMIC_RELAXED);
;   } else if (success_ordering == __ATOMIC_CONSUME) {
;     one_case(__ATOMIC_CONSUME);
;   } else if (success_ordering == __ATOMIC_ACQUIRE) {
;     one_case(__ATOMIC_ACQUIRE);
;   } else if (success_ordering == __ATOMIC_RELEASE) {
;     one_case(__ATOMIC_RELEASE);
;   } else if (success_ordering == __ATOMIC_ACQ_REL) {
;     one_case(__ATOMIC_ACQ_REL);
;   } else if (success_ordering == __ATOMIC_SEQ_CST) {
;     one_case(__ATOMIC_SEQ_CST);
;   }
; 
;   return result;
; }
; 
; #define op_case(op, addr, val, ordering) \
;   if (ordering == __ATOMIC_RELAXED) { \
;     result = op(addr, val, __ATOMIC_RELAXED); \
;   } else if (ordering == __ATOMIC_CONSUME) { \
;     result = op(addr, val, __ATOMIC_CONSUME); \
;   } else if (ordering == __ATOMIC_ACQUIRE) { \
;     result = op(addr, val, __ATOMIC_ACQUIRE); \
;   } else if (ordering == __ATOMIC_RELEASE) { \
;     result = op(addr, val, __ATOMIC_RELEASE); \
;   } else if (ordering == __ATOMIC_ACQ_REL) { \
;     result = op(addr, val, __ATOMIC_ACQ_REL); \
;   } else if (ordering == __ATOMIC_SEQ_CST) { \
;     result = op(addr, val, __ATOMIC_SEQ_CST); \
;   }
; 
; extern"C" int __atomic_fetch_or_4(int *addr, int val, int ordering) {
;   int result;
;   op_case(__atomic_fetch_or, addr, val, ordering);
;   return result;
; }
; 
; extern "C" int __atomic_fetch_and_4(int *addr, int val, int ordering) {
;   int result;
;   op_case(__atomic_fetch_and, addr, val, ordering);
;   return result;
; }
; 
; extern "C" int __atomic_and_fetch_4(int *addr, int val, int ordering) {
;   int result;
;   op_case(__atomic_and_fetch, addr, val, ordering);
;   return result;
; }

define weak_odr i32 @__atomic_load_4(i32* %addr, i32 %ordering) nounwind alwaysinline {
entry:
  %addr.addr = alloca i32*, align 4
  %ordering.addr = alloca i32, align 4
  %result = alloca i32, align 4
  store i32* %addr, i32** %addr.addr, align 4
  store i32 %ordering, i32* %ordering.addr, align 4
  %0 = load i32, i32* %ordering.addr, align 4
  %cmp = icmp eq i32 %0, 0
  br i1 %cmp, label %if.then, label %if.else

if.then:                                          ; preds = %entry
  %1 = load i32*, i32** %addr.addr, align 4
  %2 = load atomic i32, i32* %1 monotonic, align 4
  store i32 %2, i32* %result, align 4
  br label %if.end11

if.else:                                          ; preds = %entry
  %3 = load i32, i32* %ordering.addr, align 4
  %cmp1 = icmp eq i32 %3, 1
  br i1 %cmp1, label %if.then2, label %if.else3

if.then2:                                         ; preds = %if.else
  %4 = load i32*, i32** %addr.addr, align 4
  %5 = load atomic i32, i32* %4 acquire, align 4
  store i32 %5, i32* %result, align 4
  br label %if.end10

if.else3:                                         ; preds = %if.else
  %6 = load i32, i32* %ordering.addr, align 4
  %cmp4 = icmp eq i32 %6, 2
  br i1 %cmp4, label %if.then5, label %if.else6

if.then5:                                         ; preds = %if.else3
  %7 = load i32*, i32** %addr.addr, align 4
  %8 = load atomic i32, i32* %7 acquire, align 4
  store i32 %8, i32* %result, align 4
  br label %if.end9

if.else6:                                         ; preds = %if.else3
  %9 = load i32, i32* %ordering.addr, align 4
  %cmp7 = icmp eq i32 %9, 5
  br i1 %cmp7, label %if.then8, label %if.end

if.then8:                                         ; preds = %if.else6
  %10 = load i32*, i32** %addr.addr, align 4
  %11 = load atomic i32, i32* %10 seq_cst, align 4
  store i32 %11, i32* %result, align 4
  br label %if.end

if.end:                                           ; preds = %if.then8, %if.else6
  br label %if.end9

if.end9:                                          ; preds = %if.end, %if.then5
  br label %if.end10

if.end10:                                         ; preds = %if.end9, %if.then2
  br label %if.end11

if.end11:                                         ; preds = %if.end10, %if.then
  %12 = load i32, i32* %result, align 4
  ret i32 %12
}

define weak_odr void @__atomic_store_4(i32* %addr, i32 %val, i32 %ordering) nounwind alwaysinline {
entry:
  %addr.addr = alloca i32*, align 4
  %val.addr = alloca i32, align 4
  %ordering.addr = alloca i32, align 4
  %result = alloca i32, align 4
  store i32* %addr, i32** %addr.addr, align 4
  store i32 %val, i32* %val.addr, align 4
  store i32 %ordering, i32* %ordering.addr, align 4
  %0 = load i32, i32* %ordering.addr, align 4
  %cmp = icmp eq i32 %0, 0
  br i1 %cmp, label %if.then, label %if.else

if.then:                                          ; preds = %entry
  %1 = load i32*, i32** %addr.addr, align 4
  %2 = load i32, i32* %val.addr, align 4
  store atomic i32 %2, i32* %1 monotonic, align 4
  br label %if.end7

if.else:                                          ; preds = %entry
  %3 = load i32, i32* %ordering.addr, align 4
  %cmp1 = icmp eq i32 %3, 3
  br i1 %cmp1, label %if.then2, label %if.else3

if.then2:                                         ; preds = %if.else
  %4 = load i32*, i32** %addr.addr, align 4
  %5 = load i32, i32* %val.addr, align 4
  store atomic i32 %5, i32* %4 release, align 4
  br label %if.end6

if.else3:                                         ; preds = %if.else
  %6 = load i32, i32* %ordering.addr, align 4
  %cmp4 = icmp eq i32 %6, 5
  br i1 %cmp4, label %if.then5, label %if.end

if.then5:                                         ; preds = %if.else3
  %7 = load i32*, i32** %addr.addr, align 4
  %8 = load i32, i32* %val.addr, align 4
  store atomic i32 %8, i32* %7 seq_cst, align 4
  br label %if.end

if.end:                                           ; preds = %if.then5, %if.else3
  br label %if.end6

if.end6:                                          ; preds = %if.end, %if.then2
  br label %if.end7

if.end7:                                          ; preds = %if.end6, %if.then
  ret void
}

define weak_odr zeroext i1 @__atomic_compare_exchange_4(i32* %addr, i32* %expected, i32 %desired, i32 %success_ordering, i32 %fail_ordering) nounwind alwaysinline {
entry:
  %addr.addr = alloca i32*, align 4
  %expected.addr = alloca i32*, align 4
  %desired.addr = alloca i32, align 4
  %success_ordering.addr = alloca i32, align 4
  %fail_ordering.addr = alloca i32, align 4
  %result = alloca i8, align 1
  %cmpxchg.bool = alloca i8, align 1
    %cmpxchg.bool6 = alloca i8, align 1
  %cmpxchg.bool15 = alloca i8, align 1
  %cmpxchg.bool24 = alloca i8, align 1
  %cmpxchg.bool33 = alloca i8, align 1
  %cmpxchg.bool42 = alloca i8, align 1
  %cmpxchg.bool58 = alloca i8, align 1
  %cmpxchg.bool67 = alloca i8, align 1
  %cmpxchg.bool76 = alloca i8, align 1
  %cmpxchg.bool85 = alloca i8, align 1
  %cmpxchg.bool94 = alloca i8, align 1
  %cmpxchg.bool103 = alloca i8, align 1
  %cmpxchg.bool120 = alloca i8, align 1
  %cmpxchg.bool129 = alloca i8, align 1
  %cmpxchg.bool138 = alloca i8, align 1
  %cmpxchg.bool147 = alloca i8, align 1
  %cmpxchg.bool156 = alloca i8, align 1
  %cmpxchg.bool165 = alloca i8, align 1
  %cmpxchg.bool182 = alloca i8, align 1
  %cmpxchg.bool191 = alloca i8, align 1
  %cmpxchg.bool200 = alloca i8, align 1
  %cmpxchg.bool209 = alloca i8, align 1
  %cmpxchg.bool218 = alloca i8, align 1
  %cmpxchg.bool227 = alloca i8, align 1
  %cmpxchg.bool244 = alloca i8, align 1
  %cmpxchg.bool253 = alloca i8, align 1
  %cmpxchg.bool262 = alloca i8, align 1
  %cmpxchg.bool271 = alloca i8, align 1
  %cmpxchg.bool280 = alloca i8, align 1
  %cmpxchg.bool289 = alloca i8, align 1
  %cmpxchg.bool306 = alloca i8, align 1
  %cmpxchg.bool315 = alloca i8, align 1
  %cmpxchg.bool324 = alloca i8, align 1
  %cmpxchg.bool333 = alloca i8, align 1
  %cmpxchg.bool342 = alloca i8, align 1
  %cmpxchg.bool351 = alloca i8, align 1
  store i32* %addr, i32** %addr.addr, align 4
  store i32* %expected, i32** %expected.addr, align 4
  store i32 %desired, i32* %desired.addr, align 4
  store i32 %success_ordering, i32* %success_ordering.addr, align 4
  store i32 %fail_ordering, i32* %fail_ordering.addr, align 4
  %0 = load i32, i32* %success_ordering.addr, align 4
  %cmp = icmp eq i32 %0, 0
  br i1 %cmp, label %if.then, label %if.else53

if.then:                                          ; preds = %entry
  %1 = load i32, i32* %fail_ordering.addr, align 4
  %cmp1 = icmp eq i32 %1, 0
  br i1 %cmp1, label %if.then2, label %if.else

if.then2:                                         ; preds = %if.then
  %2 = load i32*, i32** %addr.addr, align 4
  %3 = load i32*, i32** %expected.addr, align 4
  %4 = load i32, i32* %3, align 4
  %5 = load i32, i32* %desired.addr, align 4
  %6 = cmpxchg i32* %2, i32 %4, i32 %5 monotonic monotonic
  %7 = extractvalue { i32, i1 } %6, 0
  %8 = extractvalue { i32, i1 } %6, 1
  br i1 %8, label %cmpxchg.continue, label %cmpxchg.store_expected

cmpxchg.store_expected:                           ; preds = %if.then2
  store i32 %7, i32* %3, align 4
  br label %cmpxchg.continue

cmpxchg.continue:                                 ; preds = %cmpxchg.store_expected, %if.then2
  %frombool = zext i1 %8 to i8
  store i8 %frombool, i8* %cmpxchg.bool, align 1
  %9 = load i8, i8* %cmpxchg.bool, align 1
  %tobool = trunc i8 %9 to i1
  %frombool3 = zext i1 %tobool to i8
  store i8 %frombool3, i8* %result, align 1
  br label %if.end52

if.else:                                          ; preds = %if.then
  %10 = load i32, i32* %fail_ordering.addr, align 4
  %cmp4 = icmp eq i32 %10, 1
  br i1 %cmp4, label %if.then5, label %if.else12

if.then5:                                         ; preds = %if.else
  %11 = load i32*, i32** %addr.addr, align 4
  %12 = load i32*, i32** %expected.addr, align 4
  %13 = load i32, i32* %12, align 4
  %14 = load i32, i32* %desired.addr, align 4
  %15 = cmpxchg i32* %11, i32 %13, i32 %14 monotonic monotonic
  %16 = extractvalue { i32, i1 } %15, 0
  %17 = extractvalue { i32, i1 } %15, 1
  br i1 %17, label %cmpxchg.continue8, label %cmpxchg.store_expected7

cmpxchg.store_expected7:                          ; preds = %if.then5
  store i32 %16, i32* %12, align 4
  br label %cmpxchg.continue8

cmpxchg.continue8:                                ; preds = %cmpxchg.store_expected7, %if.then5
  %frombool9 = zext i1 %17 to i8
  store i8 %frombool9, i8* %cmpxchg.bool6, align 1
  %18 = load i8, i8* %cmpxchg.bool6, align 1
  %tobool10 = trunc i8 %18 to i1
  %frombool11 = zext i1 %tobool10 to i8
  store i8 %frombool11, i8* %result, align 1
  br label %if.end51

if.else12:                                        ; preds = %if.else
  %19 = load i32, i32* %fail_ordering.addr, align 4
  %cmp13 = icmp eq i32 %19, 2
  br i1 %cmp13, label %if.then14, label %if.else21

if.then14:                                        ; preds = %if.else12
  %20 = load i32*, i32** %addr.addr, align 4
  %21 = load i32*, i32** %expected.addr, align 4
  %22 = load i32, i32* %21, align 4
  %23 = load i32, i32* %desired.addr, align 4
  %24 = cmpxchg i32* %20, i32 %22, i32 %23 monotonic monotonic
  %25 = extractvalue { i32, i1 } %24, 0
  %26 = extractvalue { i32, i1 } %24, 1
  br i1 %26, label %cmpxchg.continue17, label %cmpxchg.store_expected16

cmpxchg.store_expected16:                         ; preds = %if.then14
  store i32 %25, i32* %21, align 4
  br label %cmpxchg.continue17

cmpxchg.continue17:                               ; preds = %cmpxchg.store_expected16, %if.then14
  %frombool18 = zext i1 %26 to i8
  store i8 %frombool18, i8* %cmpxchg.bool15, align 1
  %27 = load i8, i8* %cmpxchg.bool15, align 1
  %tobool19 = trunc i8 %27 to i1
  %frombool20 = zext i1 %tobool19 to i8
  store i8 %frombool20, i8* %result, align 1
  br label %if.end50

if.else21:                                        ; preds = %if.else12
  %28 = load i32, i32* %fail_ordering.addr, align 4
  %cmp22 = icmp eq i32 %28, 3
  br i1 %cmp22, label %if.then23, label %if.else30

if.then23:                                        ; preds = %if.else21
  %29 = load i32*, i32** %addr.addr, align 4
  %30 = load i32*, i32** %expected.addr, align 4
  %31 = load i32, i32* %30, align 4
  %32 = load i32, i32* %desired.addr, align 4
  %33 = cmpxchg i32* %29, i32 %31, i32 %32 monotonic monotonic
  %34 = extractvalue { i32, i1 } %33, 0
  %35 = extractvalue { i32, i1 } %33, 1
  br i1 %35, label %cmpxchg.continue26, label %cmpxchg.store_expected25

cmpxchg.store_expected25:                         ; preds = %if.then23
  store i32 %34, i32* %30, align 4
  br label %cmpxchg.continue26

cmpxchg.continue26:                               ; preds = %cmpxchg.store_expected25, %if.then23
  %frombool27 = zext i1 %35 to i8
  store i8 %frombool27, i8* %cmpxchg.bool24, align 1
  %36 = load i8, i8* %cmpxchg.bool24, align 1
  %tobool28 = trunc i8 %36 to i1
  %frombool29 = zext i1 %tobool28 to i8
  store i8 %frombool29, i8* %result, align 1
  br label %if.end49

if.else30:                                        ; preds = %if.else21
  %37 = load i32, i32* %fail_ordering.addr, align 4
  %cmp31 = icmp eq i32 %37, 4
  br i1 %cmp31, label %if.then32, label %if.else39

if.then32:                                        ; preds = %if.else30
  %38 = load i32*, i32** %addr.addr, align 4
  %39 = load i32*, i32** %expected.addr, align 4
  %40 = load i32, i32* %39, align 4
  %41 = load i32, i32* %desired.addr, align 4
  %42 = cmpxchg i32* %38, i32 %40, i32 %41 monotonic monotonic
  %43 = extractvalue { i32, i1 } %42, 0
  %44 = extractvalue { i32, i1 } %42, 1
  br i1 %44, label %cmpxchg.continue35, label %cmpxchg.store_expected34

cmpxchg.store_expected34:                         ; preds = %if.then32
  store i32 %43, i32* %39, align 4
  br label %cmpxchg.continue35

cmpxchg.continue35:                               ; preds = %cmpxchg.store_expected34, %if.then32
  %frombool36 = zext i1 %44 to i8
  store i8 %frombool36, i8* %cmpxchg.bool33, align 1
  %45 = load i8, i8* %cmpxchg.bool33, align 1
  %tobool37 = trunc i8 %45 to i1
  %frombool38 = zext i1 %tobool37 to i8
  store i8 %frombool38, i8* %result, align 1
  br label %if.end48

if.else39:                                        ; preds = %if.else30
  %46 = load i32, i32* %fail_ordering.addr, align 4
  %cmp40 = icmp eq i32 %46, 5
  br i1 %cmp40, label %if.then41, label %if.end

if.then41:                                        ; preds = %if.else39
  %47 = load i32*, i32** %addr.addr, align 4
  %48 = load i32*, i32** %expected.addr, align 4
  %49 = load i32, i32* %48, align 4
  %50 = load i32, i32* %desired.addr, align 4
  %51 = cmpxchg i32* %47, i32 %49, i32 %50 monotonic monotonic
  %52 = extractvalue { i32, i1 } %51, 0
  %53 = extractvalue { i32, i1 } %51, 1
  br i1 %53, label %cmpxchg.continue44, label %cmpxchg.store_expected43

cmpxchg.store_expected43:                         ; preds = %if.then41
  store i32 %52, i32* %48, align 4
  br label %cmpxchg.continue44

cmpxchg.continue44:                               ; preds = %cmpxchg.store_expected43, %if.then41
  %frombool45 = zext i1 %53 to i8
  store i8 %frombool45, i8* %cmpxchg.bool42, align 1
  %54 = load i8, i8* %cmpxchg.bool42, align 1
  %tobool46 = trunc i8 %54 to i1
  %frombool47 = zext i1 %tobool46 to i8
  store i8 %frombool47, i8* %result, align 1
  br label %if.end

if.end:                                           ; preds = %cmpxchg.continue44, %if.else39
  br label %if.end48

if.end48:                                         ; preds = %if.end, %cmpxchg.continue35
  br label %if.end49

if.end49:                                         ; preds = %if.end48, %cmpxchg.continue26
  br label %if.end50

if.end50:                                         ; preds = %if.end49, %cmpxchg.continue17
  br label %if.end51

if.end51:                                         ; preds = %if.end50, %cmpxchg.continue8
  br label %if.end52

if.end52:                                         ; preds = %if.end51, %cmpxchg.continue
  br label %if.end368

if.else53:                                        ; preds = %entry
  %55 = load i32, i32* %success_ordering.addr, align 4
  %cmp54 = icmp eq i32 %55, 1
  br i1 %cmp54, label %if.then55, label %if.else115

if.then55:                                        ; preds = %if.else53
  %56 = load i32, i32* %fail_ordering.addr, align 4
  %cmp56 = icmp eq i32 %56, 0
  br i1 %cmp56, label %if.then57, label %if.else64

if.then57:                                        ; preds = %if.then55
  %57 = load i32*, i32** %addr.addr, align 4
  %58 = load i32*, i32** %expected.addr, align 4
  %59 = load i32, i32* %58, align 4
  %60 = load i32, i32* %desired.addr, align 4
  %61 = cmpxchg i32* %57, i32 %59, i32 %60 acquire monotonic
  %62 = extractvalue { i32, i1 } %61, 0
  %63 = extractvalue { i32, i1 } %61, 1
  br i1 %63, label %cmpxchg.continue60, label %cmpxchg.store_expected59

cmpxchg.store_expected59:                         ; preds = %if.then57
  store i32 %62, i32* %58, align 4
  br label %cmpxchg.continue60

cmpxchg.continue60:                               ; preds = %cmpxchg.store_expected59, %if.then57
  %frombool61 = zext i1 %63 to i8
  store i8 %frombool61, i8* %cmpxchg.bool58, align 1
  %64 = load i8, i8* %cmpxchg.bool58, align 1
  %tobool62 = trunc i8 %64 to i1
  %frombool63 = zext i1 %tobool62 to i8
  store i8 %frombool63, i8* %result, align 1
  br label %if.end114

if.else64:                                        ; preds = %if.then55
  %65 = load i32, i32* %fail_ordering.addr, align 4
  %cmp65 = icmp eq i32 %65, 1
  br i1 %cmp65, label %if.then66, label %if.else73

if.then66:                                        ; preds = %if.else64
  %66 = load i32*, i32** %addr.addr, align 4
  %67 = load i32*, i32** %expected.addr, align 4
  %68 = load i32, i32* %67, align 4
  %69 = load i32, i32* %desired.addr, align 4
  %70 = cmpxchg i32* %66, i32 %68, i32 %69 acquire acquire
  %71 = extractvalue { i32, i1 } %70, 0
  %72 = extractvalue { i32, i1 } %70, 1
  br i1 %72, label %cmpxchg.continue69, label %cmpxchg.store_expected68

cmpxchg.store_expected68:                         ; preds = %if.then66
  store i32 %71, i32* %67, align 4
  br label %cmpxchg.continue69

cmpxchg.continue69:                               ; preds = %cmpxchg.store_expected68, %if.then66
  %frombool70 = zext i1 %72 to i8
  store i8 %frombool70, i8* %cmpxchg.bool67, align 1
  %73 = load i8, i8* %cmpxchg.bool67, align 1
  %tobool71 = trunc i8 %73 to i1
  %frombool72 = zext i1 %tobool71 to i8
  store i8 %frombool72, i8* %result, align 1
  br label %if.end113

if.else73:                                        ; preds = %if.else64
  %74 = load i32, i32* %fail_ordering.addr, align 4
  %cmp74 = icmp eq i32 %74, 2
  br i1 %cmp74, label %if.then75, label %if.else82

if.then75:                                        ; preds = %if.else73
  %75 = load i32*, i32** %addr.addr, align 4
  %76 = load i32*, i32** %expected.addr, align 4
  %77 = load i32, i32* %76, align 4
  %78 = load i32, i32* %desired.addr, align 4
  %79 = cmpxchg i32* %75, i32 %77, i32 %78 acquire acquire
  %80 = extractvalue { i32, i1 } %79, 0
  %81 = extractvalue { i32, i1 } %79, 1
  br i1 %81, label %cmpxchg.continue78, label %cmpxchg.store_expected77

cmpxchg.store_expected77:                         ; preds = %if.then75
  store i32 %80, i32* %76, align 4
  br label %cmpxchg.continue78

cmpxchg.continue78:                               ; preds = %cmpxchg.store_expected77, %if.then75
  %frombool79 = zext i1 %81 to i8
  store i8 %frombool79, i8* %cmpxchg.bool76, align 1
  %82 = load i8, i8* %cmpxchg.bool76, align 1
  %tobool80 = trunc i8 %82 to i1
  %frombool81 = zext i1 %tobool80 to i8
  store i8 %frombool81, i8* %result, align 1
  br label %if.end112

if.else82:                                        ; preds = %if.else73
  %83 = load i32, i32* %fail_ordering.addr, align 4
  %cmp83 = icmp eq i32 %83, 3
  br i1 %cmp83, label %if.then84, label %if.else91

if.then84:                                        ; preds = %if.else82
  %84 = load i32*, i32** %addr.addr, align 4
  %85 = load i32*, i32** %expected.addr, align 4
  %86 = load i32, i32* %85, align 4
  %87 = load i32, i32* %desired.addr, align 4
  %88 = cmpxchg i32* %84, i32 %86, i32 %87 acquire monotonic
  %89 = extractvalue { i32, i1 } %88, 0
  %90 = extractvalue { i32, i1 } %88, 1
  br i1 %90, label %cmpxchg.continue87, label %cmpxchg.store_expected86

cmpxchg.store_expected86:                         ; preds = %if.then84
  store i32 %89, i32* %85, align 4
  br label %cmpxchg.continue87

cmpxchg.continue87:                               ; preds = %cmpxchg.store_expected86, %if.then84
  %frombool88 = zext i1 %90 to i8
  store i8 %frombool88, i8* %cmpxchg.bool85, align 1
  %91 = load i8, i8* %cmpxchg.bool85, align 1
  %tobool89 = trunc i8 %91 to i1
  %frombool90 = zext i1 %tobool89 to i8
  store i8 %frombool90, i8* %result, align 1
  br label %if.end111

if.else91:                                        ; preds = %if.else82
  %92 = load i32, i32* %fail_ordering.addr, align 4
  %cmp92 = icmp eq i32 %92, 4
  br i1 %cmp92, label %if.then93, label %if.else100

if.then93:                                        ; preds = %if.else91
  %93 = load i32*, i32** %addr.addr, align 4
  %94 = load i32*, i32** %expected.addr, align 4
  %95 = load i32, i32* %94, align 4
  %96 = load i32, i32* %desired.addr, align 4
  %97 = cmpxchg i32* %93, i32 %95, i32 %96 acquire monotonic
  %98 = extractvalue { i32, i1 } %97, 0
  %99 = extractvalue { i32, i1 } %97, 1
  br i1 %99, label %cmpxchg.continue96, label %cmpxchg.store_expected95

cmpxchg.store_expected95:                         ; preds = %if.then93
  store i32 %98, i32* %94, align 4
  br label %cmpxchg.continue96

cmpxchg.continue96:                               ; preds = %cmpxchg.store_expected95, %if.then93
  %frombool97 = zext i1 %99 to i8
  store i8 %frombool97, i8* %cmpxchg.bool94, align 1
  %100 = load i8, i8* %cmpxchg.bool94, align 1
  %tobool98 = trunc i8 %100 to i1
  %frombool99 = zext i1 %tobool98 to i8
  store i8 %frombool99, i8* %result, align 1
  br label %if.end110

if.else100:                                       ; preds = %if.else91
  %101 = load i32, i32* %fail_ordering.addr, align 4
  %cmp101 = icmp eq i32 %101, 5
  br i1 %cmp101, label %if.then102, label %if.end109

if.then102:                                       ; preds = %if.else100
  %102 = load i32*, i32** %addr.addr, align 4
  %103 = load i32*, i32** %expected.addr, align 4
  %104 = load i32, i32* %103, align 4
  %105 = load i32, i32* %desired.addr, align 4
  %106 = cmpxchg i32* %102, i32 %104, i32 %105 acquire acquire
  %107 = extractvalue { i32, i1 } %106, 0
  %108 = extractvalue { i32, i1 } %106, 1
  br i1 %108, label %cmpxchg.continue105, label %cmpxchg.store_expected104

cmpxchg.store_expected104:                        ; preds = %if.then102
  store i32 %107, i32* %103, align 4
  br label %cmpxchg.continue105

cmpxchg.continue105:                              ; preds = %cmpxchg.store_expected104, %if.then102
  %frombool106 = zext i1 %108 to i8
  store i8 %frombool106, i8* %cmpxchg.bool103, align 1
  %109 = load i8, i8* %cmpxchg.bool103, align 1
  %tobool107 = trunc i8 %109 to i1
  %frombool108 = zext i1 %tobool107 to i8
  store i8 %frombool108, i8* %result, align 1
  br label %if.end109

if.end109:                                        ; preds = %cmpxchg.continue105, %if.else100
  br label %if.end110

if.end110:                                        ; preds = %if.end109, %cmpxchg.continue96
  br label %if.end111

if.end111:                                        ; preds = %if.end110, %cmpxchg.continue87
  br label %if.end112

if.end112:                                        ; preds = %if.end111, %cmpxchg.continue78
  br label %if.end113

if.end113:                                        ; preds = %if.end112, %cmpxchg.continue69
  br label %if.end114

if.end114:                                        ; preds = %if.end113, %cmpxchg.continue60
  br label %if.end367

if.else115:                                       ; preds = %if.else53
  %110 = load i32, i32* %success_ordering.addr, align 4
  %cmp116 = icmp eq i32 %110, 2
  br i1 %cmp116, label %if.then117, label %if.else177

if.then117:                                       ; preds = %if.else115
  %111 = load i32, i32* %fail_ordering.addr, align 4
  %cmp118 = icmp eq i32 %111, 0
  br i1 %cmp118, label %if.then119, label %if.else126

if.then119:                                       ; preds = %if.then117
  %112 = load i32*, i32** %addr.addr, align 4
  %113 = load i32*, i32** %expected.addr, align 4
  %114 = load i32, i32* %113, align 4
  %115 = load i32, i32* %desired.addr, align 4
  %116 = cmpxchg i32* %112, i32 %114, i32 %115 acquire monotonic
  %117 = extractvalue { i32, i1 } %116, 0
  %118 = extractvalue { i32, i1 } %116, 1
  br i1 %118, label %cmpxchg.continue122, label %cmpxchg.store_expected121

cmpxchg.store_expected121:                        ; preds = %if.then119
  store i32 %117, i32* %113, align 4
  br label %cmpxchg.continue122

cmpxchg.continue122:                              ; preds = %cmpxchg.store_expected121, %if.then119
  %frombool123 = zext i1 %118 to i8
  store i8 %frombool123, i8* %cmpxchg.bool120, align 1
  %119 = load i8, i8* %cmpxchg.bool120, align 1
  %tobool124 = trunc i8 %119 to i1
  %frombool125 = zext i1 %tobool124 to i8
  store i8 %frombool125, i8* %result, align 1
  br label %if.end176

if.else126:                                       ; preds = %if.then117
  %120 = load i32, i32* %fail_ordering.addr, align 4
  %cmp127 = icmp eq i32 %120, 1
  br i1 %cmp127, label %if.then128, label %if.else135

if.then128:                                       ; preds = %if.else126
  %121 = load i32*, i32** %addr.addr, align 4
  %122 = load i32*, i32** %expected.addr, align 4
  %123 = load i32, i32* %122, align 4
  %124 = load i32, i32* %desired.addr, align 4
  %125 = cmpxchg i32* %121, i32 %123, i32 %124 acquire acquire
  %126 = extractvalue { i32, i1 } %125, 0
  %127 = extractvalue { i32, i1 } %125, 1
  br i1 %127, label %cmpxchg.continue131, label %cmpxchg.store_expected130

cmpxchg.store_expected130:                        ; preds = %if.then128
  store i32 %126, i32* %122, align 4
  br label %cmpxchg.continue131

cmpxchg.continue131:                              ; preds = %cmpxchg.store_expected130, %if.then128
  %frombool132 = zext i1 %127 to i8
  store i8 %frombool132, i8* %cmpxchg.bool129, align 1
  %128 = load i8, i8* %cmpxchg.bool129, align 1
  %tobool133 = trunc i8 %128 to i1
  %frombool134 = zext i1 %tobool133 to i8
  store i8 %frombool134, i8* %result, align 1
  br label %if.end175

if.else135:                                       ; preds = %if.else126
  %129 = load i32, i32* %fail_ordering.addr, align 4
  %cmp136 = icmp eq i32 %129, 2
  br i1 %cmp136, label %if.then137, label %if.else144

if.then137:                                       ; preds = %if.else135
  %130 = load i32*, i32** %addr.addr, align 4
  %131 = load i32*, i32** %expected.addr, align 4
  %132 = load i32, i32* %131, align 4
  %133 = load i32, i32* %desired.addr, align 4
  %134 = cmpxchg i32* %130, i32 %132, i32 %133 acquire acquire
  %135 = extractvalue { i32, i1 } %134, 0
  %136 = extractvalue { i32, i1 } %134, 1
  br i1 %136, label %cmpxchg.continue140, label %cmpxchg.store_expected139

cmpxchg.store_expected139:                        ; preds = %if.then137
  store i32 %135, i32* %131, align 4
  br label %cmpxchg.continue140

cmpxchg.continue140:                              ; preds = %cmpxchg.store_expected139, %if.then137
  %frombool141 = zext i1 %136 to i8
  store i8 %frombool141, i8* %cmpxchg.bool138, align 1
  %137 = load i8, i8* %cmpxchg.bool138, align 1
  %tobool142 = trunc i8 %137 to i1
  %frombool143 = zext i1 %tobool142 to i8
  store i8 %frombool143, i8* %result, align 1
  br label %if.end174

if.else144:                                       ; preds = %if.else135
  %138 = load i32, i32* %fail_ordering.addr, align 4
  %cmp145 = icmp eq i32 %138, 3
  br i1 %cmp145, label %if.then146, label %if.else153

if.then146:                                       ; preds = %if.else144
  %139 = load i32*, i32** %addr.addr, align 4
  %140 = load i32*, i32** %expected.addr, align 4
  %141 = load i32, i32* %140, align 4
  %142 = load i32, i32* %desired.addr, align 4
  %143 = cmpxchg i32* %139, i32 %141, i32 %142 acquire monotonic
  %144 = extractvalue { i32, i1 } %143, 0
  %145 = extractvalue { i32, i1 } %143, 1
  br i1 %145, label %cmpxchg.continue149, label %cmpxchg.store_expected148

cmpxchg.store_expected148:                        ; preds = %if.then146
  store i32 %144, i32* %140, align 4
  br label %cmpxchg.continue149

cmpxchg.continue149:                              ; preds = %cmpxchg.store_expected148, %if.then146
  %frombool150 = zext i1 %145 to i8
  store i8 %frombool150, i8* %cmpxchg.bool147, align 1
  %146 = load i8, i8* %cmpxchg.bool147, align 1
  %tobool151 = trunc i8 %146 to i1
  %frombool152 = zext i1 %tobool151 to i8
  store i8 %frombool152, i8* %result, align 1
  br label %if.end173

if.else153:                                       ; preds = %if.else144
  %147 = load i32, i32* %fail_ordering.addr, align 4
  %cmp154 = icmp eq i32 %147, 4
  br i1 %cmp154, label %if.then155, label %if.else162

if.then155:                                       ; preds = %if.else153
  %148 = load i32*, i32** %addr.addr, align 4
  %149 = load i32*, i32** %expected.addr, align 4
  %150 = load i32, i32* %149, align 4
  %151 = load i32, i32* %desired.addr, align 4
  %152 = cmpxchg i32* %148, i32 %150, i32 %151 acquire monotonic
  %153 = extractvalue { i32, i1 } %152, 0
  %154 = extractvalue { i32, i1 } %152, 1
  br i1 %154, label %cmpxchg.continue158, label %cmpxchg.store_expected157

cmpxchg.store_expected157:                        ; preds = %if.then155
  store i32 %153, i32* %149, align 4
  br label %cmpxchg.continue158

cmpxchg.continue158:                              ; preds = %cmpxchg.store_expected157, %if.then155
  %frombool159 = zext i1 %154 to i8
  store i8 %frombool159, i8* %cmpxchg.bool156, align 1
  %155 = load i8, i8* %cmpxchg.bool156, align 1
  %tobool160 = trunc i8 %155 to i1
  %frombool161 = zext i1 %tobool160 to i8
  store i8 %frombool161, i8* %result, align 1
  br label %if.end172

if.else162:                                       ; preds = %if.else153
  %156 = load i32, i32* %fail_ordering.addr, align 4
  %cmp163 = icmp eq i32 %156, 5
  br i1 %cmp163, label %if.then164, label %if.end171

if.then164:                                       ; preds = %if.else162
  %157 = load i32*, i32** %addr.addr, align 4
  %158 = load i32*, i32** %expected.addr, align 4
  %159 = load i32, i32* %158, align 4
  %160 = load i32, i32* %desired.addr, align 4
  %161 = cmpxchg i32* %157, i32 %159, i32 %160 acquire acquire
  %162 = extractvalue { i32, i1 } %161, 0
  %163 = extractvalue { i32, i1 } %161, 1
  br i1 %163, label %cmpxchg.continue167, label %cmpxchg.store_expected166

cmpxchg.store_expected166:                        ; preds = %if.then164
  store i32 %162, i32* %158, align 4
  br label %cmpxchg.continue167

cmpxchg.continue167:                              ; preds = %cmpxchg.store_expected166, %if.then164
  %frombool168 = zext i1 %163 to i8
  store i8 %frombool168, i8* %cmpxchg.bool165, align 1
  %164 = load i8, i8* %cmpxchg.bool165, align 1
  %tobool169 = trunc i8 %164 to i1
  %frombool170 = zext i1 %tobool169 to i8
  store i8 %frombool170, i8* %result, align 1
  br label %if.end171

if.end171:                                        ; preds = %cmpxchg.continue167, %if.else162
  br label %if.end172

if.end172:                                        ; preds = %if.end171, %cmpxchg.continue158
  br label %if.end173

if.end173:                                        ; preds = %if.end172, %cmpxchg.continue149
  br label %if.end174

if.end174:                                        ; preds = %if.end173, %cmpxchg.continue140
  br label %if.end175

if.end175:                                        ; preds = %if.end174, %cmpxchg.continue131
  br label %if.end176

if.end176:                                        ; preds = %if.end175, %cmpxchg.continue122
  br label %if.end366

if.else177:                                       ; preds = %if.else115
  %165 = load i32, i32* %success_ordering.addr, align 4
  %cmp178 = icmp eq i32 %165, 3
  br i1 %cmp178, label %if.then179, label %if.else239

if.then179:                                       ; preds = %if.else177
  %166 = load i32, i32* %fail_ordering.addr, align 4
  %cmp180 = icmp eq i32 %166, 0
  br i1 %cmp180, label %if.then181, label %if.else188

if.then181:                                       ; preds = %if.then179
  %167 = load i32*, i32** %addr.addr, align 4
  %168 = load i32*, i32** %expected.addr, align 4
  %169 = load i32, i32* %168, align 4
  %170 = load i32, i32* %desired.addr, align 4
  %171 = cmpxchg i32* %167, i32 %169, i32 %170 release monotonic
  %172 = extractvalue { i32, i1 } %171, 0
  %173 = extractvalue { i32, i1 } %171, 1
  br i1 %173, label %cmpxchg.continue184, label %cmpxchg.store_expected183

cmpxchg.store_expected183:                        ; preds = %if.then181
  store i32 %172, i32* %168, align 4
  br label %cmpxchg.continue184

cmpxchg.continue184:                              ; preds = %cmpxchg.store_expected183, %if.then181
  %frombool185 = zext i1 %173 to i8
  store i8 %frombool185, i8* %cmpxchg.bool182, align 1
  %174 = load i8, i8* %cmpxchg.bool182, align 1
  %tobool186 = trunc i8 %174 to i1
  %frombool187 = zext i1 %tobool186 to i8
  store i8 %frombool187, i8* %result, align 1
  br label %if.end238

if.else188:                                       ; preds = %if.then179
  %175 = load i32, i32* %fail_ordering.addr, align 4
  %cmp189 = icmp eq i32 %175, 1
  br i1 %cmp189, label %if.then190, label %if.else197

if.then190:                                       ; preds = %if.else188
  %176 = load i32*, i32** %addr.addr, align 4
  %177 = load i32*, i32** %expected.addr, align 4
  %178 = load i32, i32* %177, align 4
  %179 = load i32, i32* %desired.addr, align 4
  %180 = cmpxchg i32* %176, i32 %178, i32 %179 release acquire
  %181 = extractvalue { i32, i1 } %180, 0
  %182 = extractvalue { i32, i1 } %180, 1
  br i1 %182, label %cmpxchg.continue193, label %cmpxchg.store_expected192

cmpxchg.store_expected192:                        ; preds = %if.then190
  store i32 %181, i32* %177, align 4
  br label %cmpxchg.continue193

cmpxchg.continue193:                              ; preds = %cmpxchg.store_expected192, %if.then190
  %frombool194 = zext i1 %182 to i8
  store i8 %frombool194, i8* %cmpxchg.bool191, align 1
  %183 = load i8, i8* %cmpxchg.bool191, align 1
  %tobool195 = trunc i8 %183 to i1
  %frombool196 = zext i1 %tobool195 to i8
  store i8 %frombool196, i8* %result, align 1
  br label %if.end237

if.else197:                                       ; preds = %if.else188
  %184 = load i32, i32* %fail_ordering.addr, align 4
  %cmp198 = icmp eq i32 %184, 2
  br i1 %cmp198, label %if.then199, label %if.else206

if.then199:                                       ; preds = %if.else197
  %185 = load i32*, i32** %addr.addr, align 4
  %186 = load i32*, i32** %expected.addr, align 4
  %187 = load i32, i32* %186, align 4
  %188 = load i32, i32* %desired.addr, align 4
  %189 = cmpxchg i32* %185, i32 %187, i32 %188 release acquire
  %190 = extractvalue { i32, i1 } %189, 0
  %191 = extractvalue { i32, i1 } %189, 1
  br i1 %191, label %cmpxchg.continue202, label %cmpxchg.store_expected201

cmpxchg.store_expected201:                        ; preds = %if.then199
  store i32 %190, i32* %186, align 4
  br label %cmpxchg.continue202

cmpxchg.continue202:                              ; preds = %cmpxchg.store_expected201, %if.then199
  %frombool203 = zext i1 %191 to i8
  store i8 %frombool203, i8* %cmpxchg.bool200, align 1
  %192 = load i8, i8* %cmpxchg.bool200, align 1
  %tobool204 = trunc i8 %192 to i1
  %frombool205 = zext i1 %tobool204 to i8
  store i8 %frombool205, i8* %result, align 1
  br label %if.end236

if.else206:                                       ; preds = %if.else197
  %193 = load i32, i32* %fail_ordering.addr, align 4
  %cmp207 = icmp eq i32 %193, 3
  br i1 %cmp207, label %if.then208, label %if.else215

if.then208:                                       ; preds = %if.else206
  %194 = load i32*, i32** %addr.addr, align 4
  %195 = load i32*, i32** %expected.addr, align 4
  %196 = load i32, i32* %195, align 4
  %197 = load i32, i32* %desired.addr, align 4
  %198 = cmpxchg i32* %194, i32 %196, i32 %197 release monotonic
  %199 = extractvalue { i32, i1 } %198, 0
  %200 = extractvalue { i32, i1 } %198, 1
  br i1 %200, label %cmpxchg.continue211, label %cmpxchg.store_expected210

cmpxchg.store_expected210:                        ; preds = %if.then208
  store i32 %199, i32* %195, align 4
  br label %cmpxchg.continue211

cmpxchg.continue211:                              ; preds = %cmpxchg.store_expected210, %if.then208
  %frombool212 = zext i1 %200 to i8
  store i8 %frombool212, i8* %cmpxchg.bool209, align 1
  %201 = load i8, i8* %cmpxchg.bool209, align 1
  %tobool213 = trunc i8 %201 to i1
  %frombool214 = zext i1 %tobool213 to i8
  store i8 %frombool214, i8* %result, align 1
  br label %if.end235

if.else215:                                       ; preds = %if.else206
  %202 = load i32, i32* %fail_ordering.addr, align 4
  %cmp216 = icmp eq i32 %202, 4
  br i1 %cmp216, label %if.then217, label %if.else224

if.then217:                                       ; preds = %if.else215
  %203 = load i32*, i32** %addr.addr, align 4
  %204 = load i32*, i32** %expected.addr, align 4
  %205 = load i32, i32* %204, align 4
  %206 = load i32, i32* %desired.addr, align 4
  %207 = cmpxchg i32* %203, i32 %205, i32 %206 release monotonic
  %208 = extractvalue { i32, i1 } %207, 0
  %209 = extractvalue { i32, i1 } %207, 1
  br i1 %209, label %cmpxchg.continue220, label %cmpxchg.store_expected219

cmpxchg.store_expected219:                        ; preds = %if.then217
  store i32 %208, i32* %204, align 4
  br label %cmpxchg.continue220

cmpxchg.continue220:                              ; preds = %cmpxchg.store_expected219, %if.then217
  %frombool221 = zext i1 %209 to i8
  store i8 %frombool221, i8* %cmpxchg.bool218, align 1
  %210 = load i8, i8* %cmpxchg.bool218, align 1
  %tobool222 = trunc i8 %210 to i1
  %frombool223 = zext i1 %tobool222 to i8
  store i8 %frombool223, i8* %result, align 1
  br label %if.end234

if.else224:                                       ; preds = %if.else215
  %211 = load i32, i32* %fail_ordering.addr, align 4
  %cmp225 = icmp eq i32 %211, 5
  br i1 %cmp225, label %if.then226, label %if.end233

if.then226:                                       ; preds = %if.else224
  %212 = load i32*, i32** %addr.addr, align 4
  %213 = load i32*, i32** %expected.addr, align 4
  %214 = load i32, i32* %213, align 4
  %215 = load i32, i32* %desired.addr, align 4
  %216 = cmpxchg i32* %212, i32 %214, i32 %215 release monotonic
  %217 = extractvalue { i32, i1 } %216, 0
  %218 = extractvalue { i32, i1 } %216, 1
  br i1 %218, label %cmpxchg.continue229, label %cmpxchg.store_expected228

cmpxchg.store_expected228:                        ; preds = %if.then226
  store i32 %217, i32* %213, align 4
  br label %cmpxchg.continue229

cmpxchg.continue229:                              ; preds = %cmpxchg.store_expected228, %if.then226
  %frombool230 = zext i1 %218 to i8
  store i8 %frombool230, i8* %cmpxchg.bool227, align 1
  %219 = load i8, i8* %cmpxchg.bool227, align 1
  %tobool231 = trunc i8 %219 to i1
  %frombool232 = zext i1 %tobool231 to i8
  store i8 %frombool232, i8* %result, align 1
  br label %if.end233

if.end233:                                        ; preds = %cmpxchg.continue229, %if.else224
  br label %if.end234

if.end234:                                        ; preds = %if.end233, %cmpxchg.continue220
  br label %if.end235

if.end235:                                        ; preds = %if.end234, %cmpxchg.continue211
  br label %if.end236

if.end236:                                        ; preds = %if.end235, %cmpxchg.continue202
  br label %if.end237

if.end237:                                        ; preds = %if.end236, %cmpxchg.continue193
  br label %if.end238

if.end238:                                        ; preds = %if.end237, %cmpxchg.continue184
  br label %if.end365

if.else239:                                       ; preds = %if.else177
  %220 = load i32, i32* %success_ordering.addr, align 4
  %cmp240 = icmp eq i32 %220, 4
  br i1 %cmp240, label %if.then241, label %if.else301

if.then241:                                       ; preds = %if.else239
  %221 = load i32, i32* %fail_ordering.addr, align 4
  %cmp242 = icmp eq i32 %221, 0
  br i1 %cmp242, label %if.then243, label %if.else250

if.then243:                                       ; preds = %if.then241
  %222 = load i32*, i32** %addr.addr, align 4
  %223 = load i32*, i32** %expected.addr, align 4
  %224 = load i32, i32* %223, align 4
  %225 = load i32, i32* %desired.addr, align 4
  %226 = cmpxchg i32* %222, i32 %224, i32 %225 acq_rel monotonic
  %227 = extractvalue { i32, i1 } %226, 0
  %228 = extractvalue { i32, i1 } %226, 1
  br i1 %228, label %cmpxchg.continue246, label %cmpxchg.store_expected245

cmpxchg.store_expected245:                        ; preds = %if.then243
  store i32 %227, i32* %223, align 4
  br label %cmpxchg.continue246

cmpxchg.continue246:                              ; preds = %cmpxchg.store_expected245, %if.then243
  %frombool247 = zext i1 %228 to i8
  store i8 %frombool247, i8* %cmpxchg.bool244, align 1
  %229 = load i8, i8* %cmpxchg.bool244, align 1
  %tobool248 = trunc i8 %229 to i1
  %frombool249 = zext i1 %tobool248 to i8
  store i8 %frombool249, i8* %result, align 1
  br label %if.end300

if.else250:                                       ; preds = %if.then241
  %230 = load i32, i32* %fail_ordering.addr, align 4
  %cmp251 = icmp eq i32 %230, 1
  br i1 %cmp251, label %if.then252, label %if.else259

if.then252:                                       ; preds = %if.else250
  %231 = load i32*, i32** %addr.addr, align 4
  %232 = load i32*, i32** %expected.addr, align 4
  %233 = load i32, i32* %232, align 4
  %234 = load i32, i32* %desired.addr, align 4
  %235 = cmpxchg i32* %231, i32 %233, i32 %234 acq_rel acquire
  %236 = extractvalue { i32, i1 } %235, 0
  %237 = extractvalue { i32, i1 } %235, 1
  br i1 %237, label %cmpxchg.continue255, label %cmpxchg.store_expected254

cmpxchg.store_expected254:                        ; preds = %if.then252
  store i32 %236, i32* %232, align 4
  br label %cmpxchg.continue255

cmpxchg.continue255:                              ; preds = %cmpxchg.store_expected254, %if.then252
  %frombool256 = zext i1 %237 to i8
  store i8 %frombool256, i8* %cmpxchg.bool253, align 1
  %238 = load i8, i8* %cmpxchg.bool253, align 1
  %tobool257 = trunc i8 %238 to i1
  %frombool258 = zext i1 %tobool257 to i8
  store i8 %frombool258, i8* %result, align 1
  br label %if.end299

if.else259:                                       ; preds = %if.else250
  %239 = load i32, i32* %fail_ordering.addr, align 4
  %cmp260 = icmp eq i32 %239, 2
  br i1 %cmp260, label %if.then261, label %if.else268

if.then261:                                       ; preds = %if.else259
  %240 = load i32*, i32** %addr.addr, align 4
  %241 = load i32*, i32** %expected.addr, align 4
  %242 = load i32, i32* %241, align 4
  %243 = load i32, i32* %desired.addr, align 4
  %244 = cmpxchg i32* %240, i32 %242, i32 %243 acq_rel acquire
  %245 = extractvalue { i32, i1 } %244, 0
  %246 = extractvalue { i32, i1 } %244, 1
  br i1 %246, label %cmpxchg.continue264, label %cmpxchg.store_expected263

cmpxchg.store_expected263:                        ; preds = %if.then261
  store i32 %245, i32* %241, align 4
  br label %cmpxchg.continue264

cmpxchg.continue264:                              ; preds = %cmpxchg.store_expected263, %if.then261
  %frombool265 = zext i1 %246 to i8
  store i8 %frombool265, i8* %cmpxchg.bool262, align 1
  %247 = load i8, i8* %cmpxchg.bool262, align 1
  %tobool266 = trunc i8 %247 to i1
  %frombool267 = zext i1 %tobool266 to i8
  store i8 %frombool267, i8* %result, align 1
  br label %if.end298

if.else268:                                       ; preds = %if.else259
  %248 = load i32, i32* %fail_ordering.addr, align 4
  %cmp269 = icmp eq i32 %248, 3
  br i1 %cmp269, label %if.then270, label %if.else277

if.then270:                                       ; preds = %if.else268
  %249 = load i32*, i32** %addr.addr, align 4
  %250 = load i32*, i32** %expected.addr, align 4
  %251 = load i32, i32* %250, align 4
  %252 = load i32, i32* %desired.addr, align 4
  %253 = cmpxchg i32* %249, i32 %251, i32 %252 acq_rel monotonic
  %254 = extractvalue { i32, i1 } %253, 0
  %255 = extractvalue { i32, i1 } %253, 1
  br i1 %255, label %cmpxchg.continue273, label %cmpxchg.store_expected272

cmpxchg.store_expected272:                        ; preds = %if.then270
  store i32 %254, i32* %250, align 4
  br label %cmpxchg.continue273

cmpxchg.continue273:                              ; preds = %cmpxchg.store_expected272, %if.then270
  %frombool274 = zext i1 %255 to i8
  store i8 %frombool274, i8* %cmpxchg.bool271, align 1
  %256 = load i8, i8* %cmpxchg.bool271, align 1
  %tobool275 = trunc i8 %256 to i1
  %frombool276 = zext i1 %tobool275 to i8
  store i8 %frombool276, i8* %result, align 1
  br label %if.end297

if.else277:                                       ; preds = %if.else268
  %257 = load i32, i32* %fail_ordering.addr, align 4
  %cmp278 = icmp eq i32 %257, 4
  br i1 %cmp278, label %if.then279, label %if.else286

if.then279:                                       ; preds = %if.else277
  %258 = load i32*, i32** %addr.addr, align 4
  %259 = load i32*, i32** %expected.addr, align 4
  %260 = load i32, i32* %259, align 4
  %261 = load i32, i32* %desired.addr, align 4
  %262 = cmpxchg i32* %258, i32 %260, i32 %261 acq_rel monotonic
  %263 = extractvalue { i32, i1 } %262, 0
  %264 = extractvalue { i32, i1 } %262, 1
  br i1 %264, label %cmpxchg.continue282, label %cmpxchg.store_expected281

cmpxchg.store_expected281:                        ; preds = %if.then279
  store i32 %263, i32* %259, align 4
  br label %cmpxchg.continue282

cmpxchg.continue282:                              ; preds = %cmpxchg.store_expected281, %if.then279
  %frombool283 = zext i1 %264 to i8
  store i8 %frombool283, i8* %cmpxchg.bool280, align 1
  %265 = load i8, i8* %cmpxchg.bool280, align 1
  %tobool284 = trunc i8 %265 to i1
  %frombool285 = zext i1 %tobool284 to i8
  store i8 %frombool285, i8* %result, align 1
  br label %if.end296

if.else286:                                       ; preds = %if.else277
  %266 = load i32, i32* %fail_ordering.addr, align 4
  %cmp287 = icmp eq i32 %266, 5
  br i1 %cmp287, label %if.then288, label %if.end295

if.then288:                                       ; preds = %if.else286
  %267 = load i32*, i32** %addr.addr, align 4
  %268 = load i32*, i32** %expected.addr, align 4
  %269 = load i32, i32* %268, align 4
  %270 = load i32, i32* %desired.addr, align 4
  %271 = cmpxchg i32* %267, i32 %269, i32 %270 acq_rel acquire
  %272 = extractvalue { i32, i1 } %271, 0
  %273 = extractvalue { i32, i1 } %271, 1
  br i1 %273, label %cmpxchg.continue291, label %cmpxchg.store_expected290

cmpxchg.store_expected290:                        ; preds = %if.then288
  store i32 %272, i32* %268, align 4
  br label %cmpxchg.continue291

cmpxchg.continue291:                              ; preds = %cmpxchg.store_expected290, %if.then288
  %frombool292 = zext i1 %273 to i8
  store i8 %frombool292, i8* %cmpxchg.bool289, align 1
  %274 = load i8, i8* %cmpxchg.bool289, align 1
  %tobool293 = trunc i8 %274 to i1
  %frombool294 = zext i1 %tobool293 to i8
  store i8 %frombool294, i8* %result, align 1
  br label %if.end295

if.end295:                                        ; preds = %cmpxchg.continue291, %if.else286
  br label %if.end296

if.end296:                                        ; preds = %if.end295, %cmpxchg.continue282
  br label %if.end297

if.end297:                                        ; preds = %if.end296, %cmpxchg.continue273
  br label %if.end298

if.end298:                                        ; preds = %if.end297, %cmpxchg.continue264
  br label %if.end299

if.end299:                                        ; preds = %if.end298, %cmpxchg.continue255
  br label %if.end300

if.end300:                                        ; preds = %if.end299, %cmpxchg.continue246
  br label %if.end364

if.else301:                                       ; preds = %if.else239
  %275 = load i32, i32* %success_ordering.addr, align 4
  %cmp302 = icmp eq i32 %275, 5
  br i1 %cmp302, label %if.then303, label %if.end363

if.then303:                                       ; preds = %if.else301
  %276 = load i32, i32* %fail_ordering.addr, align 4
  %cmp304 = icmp eq i32 %276, 0
  br i1 %cmp304, label %if.then305, label %if.else312

if.then305:                                       ; preds = %if.then303
  %277 = load i32*, i32** %addr.addr, align 4
  %278 = load i32*, i32** %expected.addr, align 4
  %279 = load i32, i32* %278, align 4
  %280 = load i32, i32* %desired.addr, align 4
  %281 = cmpxchg i32* %277, i32 %279, i32 %280 seq_cst monotonic
  %282 = extractvalue { i32, i1 } %281, 0
  %283 = extractvalue { i32, i1 } %281, 1
  br i1 %283, label %cmpxchg.continue308, label %cmpxchg.store_expected307

cmpxchg.store_expected307:                        ; preds = %if.then305
  store i32 %282, i32* %278, align 4
  br label %cmpxchg.continue308

cmpxchg.continue308:                              ; preds = %cmpxchg.store_expected307, %if.then305
  %frombool309 = zext i1 %283 to i8
  store i8 %frombool309, i8* %cmpxchg.bool306, align 1
  %284 = load i8, i8* %cmpxchg.bool306, align 1
  %tobool310 = trunc i8 %284 to i1
  %frombool311 = zext i1 %tobool310 to i8
  store i8 %frombool311, i8* %result, align 1
  br label %if.end362

if.else312:                                       ; preds = %if.then303
  %285 = load i32, i32* %fail_ordering.addr, align 4
  %cmp313 = icmp eq i32 %285, 1
  br i1 %cmp313, label %if.then314, label %if.else321

if.then314:                                       ; preds = %if.else312
  %286 = load i32*, i32** %addr.addr, align 4
  %287 = load i32*, i32** %expected.addr, align 4
  %288 = load i32, i32* %287, align 4
  %289 = load i32, i32* %desired.addr, align 4
  %290 = cmpxchg i32* %286, i32 %288, i32 %289 seq_cst acquire
  %291 = extractvalue { i32, i1 } %290, 0
  %292 = extractvalue { i32, i1 } %290, 1
  br i1 %292, label %cmpxchg.continue317, label %cmpxchg.store_expected316

cmpxchg.store_expected316:                        ; preds = %if.then314
  store i32 %291, i32* %287, align 4
  br label %cmpxchg.continue317

cmpxchg.continue317:                              ; preds = %cmpxchg.store_expected316, %if.then314
  %frombool318 = zext i1 %292 to i8
  store i8 %frombool318, i8* %cmpxchg.bool315, align 1
  %293 = load i8, i8* %cmpxchg.bool315, align 1
  %tobool319 = trunc i8 %293 to i1
  %frombool320 = zext i1 %tobool319 to i8
  store i8 %frombool320, i8* %result, align 1
  br label %if.end361

if.else321:                                       ; preds = %if.else312
  %294 = load i32, i32* %fail_ordering.addr, align 4
  %cmp322 = icmp eq i32 %294, 2
  br i1 %cmp322, label %if.then323, label %if.else330

if.then323:                                       ; preds = %if.else321
  %295 = load i32*, i32** %addr.addr, align 4
  %296 = load i32*, i32** %expected.addr, align 4
  %297 = load i32, i32* %296, align 4
  %298 = load i32, i32* %desired.addr, align 4
  %299 = cmpxchg i32* %295, i32 %297, i32 %298 seq_cst acquire
  %300 = extractvalue { i32, i1 } %299, 0
  %301 = extractvalue { i32, i1 } %299, 1
  br i1 %301, label %cmpxchg.continue326, label %cmpxchg.store_expected325

cmpxchg.store_expected325:                        ; preds = %if.then323
  store i32 %300, i32* %296, align 4
  br label %cmpxchg.continue326

cmpxchg.continue326:                              ; preds = %cmpxchg.store_expected325, %if.then323
  %frombool327 = zext i1 %301 to i8
  store i8 %frombool327, i8* %cmpxchg.bool324, align 1
  %302 = load i8, i8* %cmpxchg.bool324, align 1
  %tobool328 = trunc i8 %302 to i1
  %frombool329 = zext i1 %tobool328 to i8
  store i8 %frombool329, i8* %result, align 1
  br label %if.end360

if.else330:                                       ; preds = %if.else321
  %303 = load i32, i32* %fail_ordering.addr, align 4
  %cmp331 = icmp eq i32 %303, 3
  br i1 %cmp331, label %if.then332, label %if.else339

if.then332:                                       ; preds = %if.else330
  %304 = load i32*, i32** %addr.addr, align 4
  %305 = load i32*, i32** %expected.addr, align 4
  %306 = load i32, i32* %305, align 4
  %307 = load i32, i32* %desired.addr, align 4
  %308 = cmpxchg i32* %304, i32 %306, i32 %307 seq_cst monotonic
  %309 = extractvalue { i32, i1 } %308, 0
  %310 = extractvalue { i32, i1 } %308, 1
  br i1 %310, label %cmpxchg.continue335, label %cmpxchg.store_expected334

cmpxchg.store_expected334:                        ; preds = %if.then332
  store i32 %309, i32* %305, align 4
  br label %cmpxchg.continue335

cmpxchg.continue335:                              ; preds = %cmpxchg.store_expected334, %if.then332
  %frombool336 = zext i1 %310 to i8
  store i8 %frombool336, i8* %cmpxchg.bool333, align 1
  %311 = load i8, i8* %cmpxchg.bool333, align 1
  %tobool337 = trunc i8 %311 to i1
  %frombool338 = zext i1 %tobool337 to i8
  store i8 %frombool338, i8* %result, align 1
  br label %if.end359

if.else339:                                       ; preds = %if.else330
  %312 = load i32, i32* %fail_ordering.addr, align 4
  %cmp340 = icmp eq i32 %312, 4
  br i1 %cmp340, label %if.then341, label %if.else348

if.then341:                                       ; preds = %if.else339
  %313 = load i32*, i32** %addr.addr, align 4
  %314 = load i32*, i32** %expected.addr, align 4
  %315 = load i32, i32* %314, align 4
  %316 = load i32, i32* %desired.addr, align 4
  %317 = cmpxchg i32* %313, i32 %315, i32 %316 seq_cst monotonic
  %318 = extractvalue { i32, i1 } %317, 0
  %319 = extractvalue { i32, i1 } %317, 1
  br i1 %319, label %cmpxchg.continue344, label %cmpxchg.store_expected343

cmpxchg.store_expected343:                        ; preds = %if.then341
  store i32 %318, i32* %314, align 4
  br label %cmpxchg.continue344

cmpxchg.continue344:                              ; preds = %cmpxchg.store_expected343, %if.then341
  %frombool345 = zext i1 %319 to i8
  store i8 %frombool345, i8* %cmpxchg.bool342, align 1
  %320 = load i8, i8* %cmpxchg.bool342, align 1
  %tobool346 = trunc i8 %320 to i1
  %frombool347 = zext i1 %tobool346 to i8
  store i8 %frombool347, i8* %result, align 1
  br label %if.end358

if.else348:                                       ; preds = %if.else339
  %321 = load i32, i32* %fail_ordering.addr, align 4
  %cmp349 = icmp eq i32 %321, 5
  br i1 %cmp349, label %if.then350, label %if.end357

if.then350:                                       ; preds = %if.else348
  %322 = load i32*, i32** %addr.addr, align 4
  %323 = load i32*, i32** %expected.addr, align 4
  %324 = load i32, i32* %323, align 4
  %325 = load i32, i32* %desired.addr, align 4
  %326 = cmpxchg i32* %322, i32 %324, i32 %325 seq_cst seq_cst
  %327 = extractvalue { i32, i1 } %326, 0
  %328 = extractvalue { i32, i1 } %326, 1
  br i1 %328, label %cmpxchg.continue353, label %cmpxchg.store_expected352

cmpxchg.store_expected352:                        ; preds = %if.then350
  store i32 %327, i32* %323, align 4
  br label %cmpxchg.continue353

cmpxchg.continue353:                              ; preds = %cmpxchg.store_expected352, %if.then350
  %frombool354 = zext i1 %328 to i8
  store i8 %frombool354, i8* %cmpxchg.bool351, align 1
  %329 = load i8, i8* %cmpxchg.bool351, align 1
  %tobool355 = trunc i8 %329 to i1
  %frombool356 = zext i1 %tobool355 to i8
  store i8 %frombool356, i8* %result, align 1
  br label %if.end357

if.end357:                                        ; preds = %cmpxchg.continue353, %if.else348
  br label %if.end358

if.end358:                                        ; preds = %if.end357, %cmpxchg.continue344
  br label %if.end359

if.end359:                                        ; preds = %if.end358, %cmpxchg.continue335
  br label %if.end360

if.end360:                                        ; preds = %if.end359, %cmpxchg.continue326
  br label %if.end361

if.end361:                                        ; preds = %if.end360, %cmpxchg.continue317
  br label %if.end362

if.end362:                                        ; preds = %if.end361, %cmpxchg.continue308
  br label %if.end363

if.end363:                                        ; preds = %if.end362, %if.else301
  br label %if.end364

if.end364:                                        ; preds = %if.end363, %if.end300
  br label %if.end365

if.end365:                                        ; preds = %if.end364, %if.end238
  br label %if.end366

if.end366:                                        ; preds = %if.end365, %if.end176
  br label %if.end367

if.end367:                                        ; preds = %if.end366, %if.end114
  br label %if.end368

if.end368:                                        ; preds = %if.end367, %if.end52
  %330 = load i8, i8* %result, align 1
  %tobool369 = trunc i8 %330 to i1
  ret i1 %tobool369
}


; Function Attrs: noinline nounwind optnone
define weak_odr i32 @__atomic_fetch_or_4(i32* %addr, i32 %val, i32 %ordering) nounwind alwaysinline {
entry:
  %addr.addr = alloca i32*, align 4
  %val.addr = alloca i32, align 4
  %ordering.addr = alloca i32, align 4
  %result = alloca i32, align 4
  %.atomictmp = alloca i32, align 4
  %atomic-temp = alloca i32, align 4
  %.atomictmp3 = alloca i32, align 4
  %atomic-temp4 = alloca i32, align 4
  %.atomictmp8 = alloca i32, align 4
  %atomic-temp9 = alloca i32, align 4
  %.atomictmp13 = alloca i32, align 4
  %atomic-temp14 = alloca i32, align 4
  %.atomictmp18 = alloca i32, align 4
  %atomic-temp19 = alloca i32, align 4
  %.atomictmp23 = alloca i32, align 4
  %atomic-temp24 = alloca i32, align 4
  store i32* %addr, i32** %addr.addr, align 4
  store i32 %val, i32* %val.addr, align 4
  store i32 %ordering, i32* %ordering.addr, align 4
  %0 = load i32, i32* %ordering.addr, align 4
  %cmp = icmp eq i32 %0, 0
  br i1 %cmp, label %if.then, label %if.else

if.then:                                          ; preds = %entry
  %1 = load i32*, i32** %addr.addr, align 4
  %2 = load i32, i32* %val.addr, align 4
  store i32 %2, i32* %.atomictmp, align 4
  %3 = load i32, i32* %.atomictmp, align 4
  %4 = atomicrmw or i32* %1, i32 %3 monotonic
  store i32 %4, i32* %atomic-temp, align 4
  %5 = load i32, i32* %atomic-temp, align 4
  store i32 %5, i32* %result, align 4
  br label %if.end29

if.else:                                          ; preds = %entry
  %6 = load i32, i32* %ordering.addr, align 4
  %cmp1 = icmp eq i32 %6, 1
  br i1 %cmp1, label %if.then2, label %if.else5

if.then2:                                         ; preds = %if.else
  %7 = load i32*, i32** %addr.addr, align 4
  %8 = load i32, i32* %val.addr, align 4
  store i32 %8, i32* %.atomictmp3, align 4
  %9 = load i32, i32* %.atomictmp3, align 4
  %10 = atomicrmw or i32* %7, i32 %9 acquire
  store i32 %10, i32* %atomic-temp4, align 4
  %11 = load i32, i32* %atomic-temp4, align 4
  store i32 %11, i32* %result, align 4
  br label %if.end28

if.else5:                                         ; preds = %if.else
  %12 = load i32, i32* %ordering.addr, align 4
  %cmp6 = icmp eq i32 %12, 2
  br i1 %cmp6, label %if.then7, label %if.else10

if.then7:                                         ; preds = %if.else5
  %13 = load i32*, i32** %addr.addr, align 4
  %14 = load i32, i32* %val.addr, align 4
  store i32 %14, i32* %.atomictmp8, align 4
  %15 = load i32, i32* %.atomictmp8, align 4
  %16 = atomicrmw or i32* %13, i32 %15 acquire
  store i32 %16, i32* %atomic-temp9, align 4
  %17 = load i32, i32* %atomic-temp9, align 4
  store i32 %17, i32* %result, align 4
  br label %if.end27

if.else10:                                        ; preds = %if.else5
  %18 = load i32, i32* %ordering.addr, align 4
  %cmp11 = icmp eq i32 %18, 3
  br i1 %cmp11, label %if.then12, label %if.else15

if.then12:                                        ; preds = %if.else10
  %19 = load i32*, i32** %addr.addr, align 4
  %20 = load i32, i32* %val.addr, align 4
  store i32 %20, i32* %.atomictmp13, align 4
  %21 = load i32, i32* %.atomictmp13, align 4
  %22 = atomicrmw or i32* %19, i32 %21 release
  store i32 %22, i32* %atomic-temp14, align 4
  %23 = load i32, i32* %atomic-temp14, align 4
  store i32 %23, i32* %result, align 4
  br label %if.end26

if.else15:                                        ; preds = %if.else10
  %24 = load i32, i32* %ordering.addr, align 4
  %cmp16 = icmp eq i32 %24, 4
  br i1 %cmp16, label %if.then17, label %if.else20

if.then17:                                        ; preds = %if.else15
  %25 = load i32*, i32** %addr.addr, align 4
  %26 = load i32, i32* %val.addr, align 4
  store i32 %26, i32* %.atomictmp18, align 4
  %27 = load i32, i32* %.atomictmp18, align 4
  %28 = atomicrmw or i32* %25, i32 %27 acq_rel
  store i32 %28, i32* %atomic-temp19, align 4
  %29 = load i32, i32* %atomic-temp19, align 4
  store i32 %29, i32* %result, align 4
  br label %if.end25

if.else20:                                        ; preds = %if.else15
  %30 = load i32, i32* %ordering.addr, align 4
  %cmp21 = icmp eq i32 %30, 5
  br i1 %cmp21, label %if.then22, label %if.end

if.then22:                                        ; preds = %if.else20
  %31 = load i32*, i32** %addr.addr, align 4
  %32 = load i32, i32* %val.addr, align 4
  store i32 %32, i32* %.atomictmp23, align 4
  %33 = load i32, i32* %.atomictmp23, align 4
  %34 = atomicrmw or i32* %31, i32 %33 seq_cst
  store i32 %34, i32* %atomic-temp24, align 4
  %35 = load i32, i32* %atomic-temp24, align 4
  store i32 %35, i32* %result, align 4
  br label %if.end

if.end:                                           ; preds = %if.then22, %if.else20
  br label %if.end25

if.end25:                                         ; preds = %if.end, %if.then17
  br label %if.end26

if.end26:                                         ; preds = %if.end25, %if.then12
  br label %if.end27

if.end27:                                         ; preds = %if.end26, %if.then7
  br label %if.end28

if.end28:                                         ; preds = %if.end27, %if.then2
  br label %if.end29

if.end29:                                         ; preds = %if.end28, %if.then
  %36 = load i32, i32* %result, align 4
  ret i32 %36
}

define weak_odr i32 @__atomic_fetch_and_4(i32* %addr, i32 %val, i32 %ordering) nounwind alwaysinline {
entry:
  %addr.addr = alloca i32*, align 4
  %val.addr = alloca i32, align 4
  %ordering.addr = alloca i32, align 4
  %result = alloca i32, align 4
  %.atomictmp = alloca i32, align 4
  %atomic-temp = alloca i32, align 4
  %.atomictmp3 = alloca i32, align 4
  %atomic-temp4 = alloca i32, align 4
  %.atomictmp8 = alloca i32, align 4
  %atomic-temp9 = alloca i32, align 4
  %.atomictmp13 = alloca i32, align 4
  %atomic-temp14 = alloca i32, align 4
  %.atomictmp18 = alloca i32, align 4
  %atomic-temp19 = alloca i32, align 4
  %.atomictmp23 = alloca i32, align 4
  %atomic-temp24 = alloca i32, align 4
  store i32* %addr, i32** %addr.addr, align 4
  store i32 %val, i32* %val.addr, align 4
  store i32 %ordering, i32* %ordering.addr, align 4
  %0 = load i32, i32* %ordering.addr, align 4
  %cmp = icmp eq i32 %0, 0
  br i1 %cmp, label %if.then, label %if.else

if.then:                                          ; preds = %entry
  %1 = load i32*, i32** %addr.addr, align 4
  %2 = load i32, i32* %val.addr, align 4
  store i32 %2, i32* %.atomictmp, align 4
  %3 = load i32, i32* %.atomictmp, align 4
  %4 = atomicrmw and i32* %1, i32 %3 monotonic
  store i32 %4, i32* %atomic-temp, align 4
  %5 = load i32, i32* %atomic-temp, align 4
  store i32 %5, i32* %result, align 4
  br label %if.end29

if.else:                                          ; preds = %entry
  %6 = load i32, i32* %ordering.addr, align 4
  %cmp1 = icmp eq i32 %6, 1
  br i1 %cmp1, label %if.then2, label %if.else5

if.then2:                                         ; preds = %if.else
  %7 = load i32*, i32** %addr.addr, align 4
  %8 = load i32, i32* %val.addr, align 4
  store i32 %8, i32* %.atomictmp3, align 4
  %9 = load i32, i32* %.atomictmp3, align 4
  %10 = atomicrmw and i32* %7, i32 %9 acquire
  store i32 %10, i32* %atomic-temp4, align 4
  %11 = load i32, i32* %atomic-temp4, align 4
  store i32 %11, i32* %result, align 4
  br label %if.end28

if.else5:                                         ; preds = %if.else
  %12 = load i32, i32* %ordering.addr, align 4
  %cmp6 = icmp eq i32 %12, 2
  br i1 %cmp6, label %if.then7, label %if.else10

if.then7:                                         ; preds = %if.else5
  %13 = load i32*, i32** %addr.addr, align 4
  %14 = load i32, i32* %val.addr, align 4
  store i32 %14, i32* %.atomictmp8, align 4
  %15 = load i32, i32* %.atomictmp8, align 4
  %16 = atomicrmw and i32* %13, i32 %15 acquire
  store i32 %16, i32* %atomic-temp9, align 4
  %17 = load i32, i32* %atomic-temp9, align 4
  store i32 %17, i32* %result, align 4
  br label %if.end27

if.else10:                                        ; preds = %if.else5
  %18 = load i32, i32* %ordering.addr, align 4
  %cmp11 = icmp eq i32 %18, 3
  br i1 %cmp11, label %if.then12, label %if.else15

if.then12:                                        ; preds = %if.else10
  %19 = load i32*, i32** %addr.addr, align 4
  %20 = load i32, i32* %val.addr, align 4
  store i32 %20, i32* %.atomictmp13, align 4
  %21 = load i32, i32* %.atomictmp13, align 4
  %22 = atomicrmw and i32* %19, i32 %21 release
  store i32 %22, i32* %atomic-temp14, align 4
  %23 = load i32, i32* %atomic-temp14, align 4
  store i32 %23, i32* %result, align 4
  br label %if.end26

if.else15:                                        ; preds = %if.else10
  %24 = load i32, i32* %ordering.addr, align 4
  %cmp16 = icmp eq i32 %24, 4
  br i1 %cmp16, label %if.then17, label %if.else20

if.then17:                                        ; preds = %if.else15
  %25 = load i32*, i32** %addr.addr, align 4
  %26 = load i32, i32* %val.addr, align 4
  store i32 %26, i32* %.atomictmp18, align 4
  %27 = load i32, i32* %.atomictmp18, align 4
  %28 = atomicrmw and i32* %25, i32 %27 acq_rel
  store i32 %28, i32* %atomic-temp19, align 4
  %29 = load i32, i32* %atomic-temp19, align 4
  store i32 %29, i32* %result, align 4
  br label %if.end25

if.else20:                                        ; preds = %if.else15
  %30 = load i32, i32* %ordering.addr, align 4
  %cmp21 = icmp eq i32 %30, 5
  br i1 %cmp21, label %if.then22, label %if.end

if.then22:                                        ; preds = %if.else20
  %31 = load i32*, i32** %addr.addr, align 4
  %32 = load i32, i32* %val.addr, align 4
  store i32 %32, i32* %.atomictmp23, align 4
  %33 = load i32, i32* %.atomictmp23, align 4
  %34 = atomicrmw and i32* %31, i32 %33 seq_cst
  store i32 %34, i32* %atomic-temp24, align 4
  %35 = load i32, i32* %atomic-temp24, align 4
  store i32 %35, i32* %result, align 4
  br label %if.end

if.end:                                           ; preds = %if.then22, %if.else20
  br label %if.end25

if.end25:                                         ; preds = %if.end, %if.then17
  br label %if.end26

if.end26:                                         ; preds = %if.end25, %if.then12
  br label %if.end27

if.end27:                                         ; preds = %if.end26, %if.then7
  br label %if.end28

if.end28:                                         ; preds = %if.end27, %if.then2
  br label %if.end29

if.end29:                                         ; preds = %if.end28, %if.then
  %36 = load i32, i32* %result, align 4
  ret i32 %36
}

define weak_odr i32 @__atomic_and_fetch_4(i32* %addr, i32 %val, i32 %ordering) nounwind alwaysinline {
entry:
  %addr.addr = alloca i32*, align 4
  %val.addr = alloca i32, align 4
  %ordering.addr = alloca i32, align 4
  %result = alloca i32, align 4
  %.atomictmp = alloca i32, align 4
  %atomic-temp = alloca i32, align 4
  %.atomictmp3 = alloca i32, align 4
  %atomic-temp4 = alloca i32, align 4
  %.atomictmp8 = alloca i32, align 4
  %atomic-temp9 = alloca i32, align 4
  %.atomictmp13 = alloca i32, align 4
  %atomic-temp14 = alloca i32, align 4
  %.atomictmp18 = alloca i32, align 4
  %atomic-temp19 = alloca i32, align 4
  %.atomictmp23 = alloca i32, align 4
  %atomic-temp24 = alloca i32, align 4
  store i32* %addr, i32** %addr.addr, align 4
  store i32 %val, i32* %val.addr, align 4
  store i32 %ordering, i32* %ordering.addr, align 4
  %0 = load i32, i32* %ordering.addr, align 4
  %cmp = icmp eq i32 %0, 0
  br i1 %cmp, label %if.then, label %if.else

if.then:                                          ; preds = %entry
  %1 = load i32*, i32** %addr.addr, align 4
  %2 = load i32, i32* %val.addr, align 4
  store i32 %2, i32* %.atomictmp, align 4
  %3 = load i32, i32* %.atomictmp, align 4
  %4 = atomicrmw and i32* %1, i32 %3 monotonic
  %5 = and i32 %4, %3
  store i32 %5, i32* %atomic-temp, align 4
  %6 = load i32, i32* %atomic-temp, align 4
  store i32 %6, i32* %result, align 4
  br label %if.end29

if.else:                                          ; preds = %entry
  %7 = load i32, i32* %ordering.addr, align 4
  %cmp1 = icmp eq i32 %7, 1
  br i1 %cmp1, label %if.then2, label %if.else5

if.then2:                                         ; preds = %if.else
  %8 = load i32*, i32** %addr.addr, align 4
  %9 = load i32, i32* %val.addr, align 4
  store i32 %9, i32* %.atomictmp3, align 4
  %10 = load i32, i32* %.atomictmp3, align 4
  %11 = atomicrmw and i32* %8, i32 %10 acquire
  %12 = and i32 %11, %10
  store i32 %12, i32* %atomic-temp4, align 4
  %13 = load i32, i32* %atomic-temp4, align 4
  store i32 %13, i32* %result, align 4
  br label %if.end28

if.else5:                                         ; preds = %if.else
  %14 = load i32, i32* %ordering.addr, align 4
  %cmp6 = icmp eq i32 %14, 2
  br i1 %cmp6, label %if.then7, label %if.else10

if.then7:                                         ; preds = %if.else5
  %15 = load i32*, i32** %addr.addr, align 4
  %16 = load i32, i32* %val.addr, align 4
  store i32 %16, i32* %.atomictmp8, align 4
  %17 = load i32, i32* %.atomictmp8, align 4
  %18 = atomicrmw and i32* %15, i32 %17 acquire
  %19 = and i32 %18, %17
  store i32 %19, i32* %atomic-temp9, align 4
  %20 = load i32, i32* %atomic-temp9, align 4
  store i32 %20, i32* %result, align 4
  br label %if.end27

if.else10:                                        ; preds = %if.else5
  %21 = load i32, i32* %ordering.addr, align 4
  %cmp11 = icmp eq i32 %21, 3
  br i1 %cmp11, label %if.then12, label %if.else15

if.then12:                                        ; preds = %if.else10
  %22 = load i32*, i32** %addr.addr, align 4
  %23 = load i32, i32* %val.addr, align 4
  store i32 %23, i32* %.atomictmp13, align 4
  %24 = load i32, i32* %.atomictmp13, align 4
  %25 = atomicrmw and i32* %22, i32 %24 release
  %26 = and i32 %25, %24
  store i32 %26, i32* %atomic-temp14, align 4
  %27 = load i32, i32* %atomic-temp14, align 4
  store i32 %27, i32* %result, align 4
  br label %if.end26

if.else15:                                        ; preds = %if.else10
  %28 = load i32, i32* %ordering.addr, align 4
  %cmp16 = icmp eq i32 %28, 4
  br i1 %cmp16, label %if.then17, label %if.else20

if.then17:                                        ; preds = %if.else15
  %29 = load i32*, i32** %addr.addr, align 4
  %30 = load i32, i32* %val.addr, align 4
  store i32 %30, i32* %.atomictmp18, align 4
  %31 = load i32, i32* %.atomictmp18, align 4
  %32 = atomicrmw and i32* %29, i32 %31 acq_rel
  %33 = and i32 %32, %31
  store i32 %33, i32* %atomic-temp19, align 4
  %34 = load i32, i32* %atomic-temp19, align 4
  store i32 %34, i32* %result, align 4
  br label %if.end25

if.else20:                                        ; preds = %if.else15
  %35 = load i32, i32* %ordering.addr, align 4
  %cmp21 = icmp eq i32 %35, 5
  br i1 %cmp21, label %if.then22, label %if.end

if.then22:                                        ; preds = %if.else20
  %36 = load i32*, i32** %addr.addr, align 4
  %37 = load i32, i32* %val.addr, align 4
  store i32 %37, i32* %.atomictmp23, align 4
  %38 = load i32, i32* %.atomictmp23, align 4
  %39 = atomicrmw and i32* %36, i32 %38 seq_cst
  %40 = and i32 %39, %38
  store i32 %40, i32* %atomic-temp24, align 4
  %41 = load i32, i32* %atomic-temp24, align 4
  store i32 %41, i32* %result, align 4
  br label %if.end

if.end:                                           ; preds = %if.then22, %if.else20
  br label %if.end25

if.end25:                                         ; preds = %if.end, %if.then17
  br label %if.end26

if.end26:                                         ; preds = %if.end25, %if.then12
  br label %if.end27

if.end27:                                         ; preds = %if.end26, %if.then7
  br label %if.end28

if.end28:                                         ; preds = %if.end27, %if.then2
  br label %if.end29

if.end29:                                         ; preds = %if.end28, %if.then
  %42 = load i32, i32* %result, align 4
  ret i32 %42
}
