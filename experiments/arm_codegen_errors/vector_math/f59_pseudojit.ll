; ModuleID = 'f59_pseudojit.bc'
target triple = "arm-linux-eabi"

%struct._IO_FILE = type { i32, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, i8*, %struct._IO_marker*, %struct._IO_FILE*, i32, i32, i32, i16, i8, [1 x i8], i8*, i64, i8*, i8*, i8*, i8*, i32, i32, [40 x i8] }
%struct._IO_marker = type { %struct._IO_marker*, %struct._IO_FILE*, i32 }
%struct.anon = type { [65536 x %struct.work], i32, i32, %union.pthread_mutex_t, %union.pthread_cond_t, [64 x i32], i32 }
%struct.work = type { void (i32, i8*)*, i32, i32, i8*, i32, i32 }
%union.pthread_mutex_t = type { %"struct.<anonymous union>::__pthread_mutex_s" }
%"struct.<anonymous union>::__pthread_mutex_s" = type { i32, i32, i32, i32, i32, %union.anon }
%union.anon = type { i32 }
%union.pthread_cond_t = type { %struct.anon.0 }
%struct.anon.0 = type { i32, i32, i64, i64, i64, i8*, i32, i32 }
%struct.timeval = type { i32, i32 }
%struct.buffer_t = type { i8*, i64, i8, i8, [4 x i32], [4 x i32], [4 x i32], i32 }
%struct.worker_arg = type { i32, %struct.work* }
%union.pthread_mutexattr_t = type { i32 }
%union.pthread_condattr_t = type { i32 }
%union.pthread_attr_t = type { i32, [32 x i8] }
%struct.timezone = type { i32, i32 }

@.str = private unnamed_addr constant [49 x i8] c"Allocated %u bytes at %p with an electric fence\0A\00", align 1
@stderr = external global %struct._IO_FILE*
@halide_error_handler = internal unnamed_addr global void (i8*)* null, align 4
@.str1 = private unnamed_addr constant [11 x i8] c"Error: %s\0A\00", align 1
@work_queue = internal global %struct.anon zeroinitializer, align 8
@.str2 = private unnamed_addr constant [5 x i8] c"!arg\00", align 1
@.str3 = private unnamed_addr constant [32 x i8] c"./architecture.posix.stdlib.cpp\00", align 1
@__PRETTY_FUNCTION__.worker = private unnamed_addr constant [21 x i8] c"void *worker(void *)\00", align 1
@thread_pool_initialized = weak global i8 0, align 1
@.str4 = private unnamed_addr constant [14 x i8] c"HL_NUMTHREADS\00", align 1
@threads = internal unnamed_addr global i32 0, align 4
@.str6 = private unnamed_addr constant [28 x i8] c"new_tail != work_queue.head\00", align 1
@__PRETTY_FUNCTION__.do_par_for = private unnamed_addr constant [63 x i8] c"void do_par_for(void (*)(int, uint8_t *), int, int, uint8_t *)\00", align 1
@initialized = weak global i8 0, align 1
@start = weak global %struct.timeval zeroinitializer, align 4
@llvm.used = appending global [44 x i8*] [i8* bitcast (float (float)* @sqrt_f32 to i8*), i8* bitcast (float (float)* @sin_f32 to i8*), i8* bitcast (float (float)* @cos_f32 to i8*), i8* bitcast (float (float)* @exp_f32 to i8*), i8* bitcast (float (float)* @log_f32 to i8*), i8* bitcast (float (float, float)* @pow_f32 to i8*), i8* bitcast (float (float)* @floor_f32 to i8*), i8* bitcast (float (float)* @ceil_f32 to i8*), i8* bitcast (float (float)* @round_f32 to i8*), i8* bitcast (double (double)* @sqrt_f64 to i8*), i8* bitcast (double (double)* @sin_f64 to i8*), i8* bitcast (double (double)* @cos_f64 to i8*), i8* bitcast (double (double)* @exp_f64 to i8*), i8* bitcast (double (double)* @log_f64 to i8*), i8* bitcast (double (double, double)* @pow_f64 to i8*), i8* bitcast (double (double)* @floor_f64 to i8*), i8* bitcast (double (double)* @ceil_f64 to i8*), i8* bitcast (double (double)* @round_f64 to i8*), i8* bitcast (float ()* @maxval_f32 to i8*), i8* bitcast (float ()* @minval_f32 to i8*), i8* bitcast (double ()* @maxval_f64 to i8*), i8* bitcast (double ()* @minval_f64 to i8*), i8* bitcast (i8 ()* @maxval_u8 to i8*), i8* bitcast (i8 ()* @minval_u8 to i8*), i8* bitcast (i16 ()* @maxval_u16 to i8*), i8* bitcast (i16 ()* @minval_u16 to i8*), i8* bitcast (i32 ()* @maxval_u32 to i8*), i8* bitcast (i32 ()* @minval_u32 to i8*), i8* bitcast (i64 ()* @maxval_u64 to i8*), i8* bitcast (i64 ()* @minval_u64 to i8*), i8* bitcast (i8 ()* @maxval_s8 to i8*), i8* bitcast (i8 ()* @minval_s8 to i8*), i8* bitcast (i16 ()* @maxval_s16 to i8*), i8* bitcast (i16 ()* @minval_s16 to i8*), i8* bitcast (i32 ()* @maxval_s32 to i8*), i8* bitcast (i32 ()* @minval_s32 to i8*), i8* bitcast (i64 ()* @maxval_s64 to i8*), i8* bitcast (i64 ()* @minval_s64 to i8*), i8* bitcast (i8 (i8)* @abs_i8 to i8*), i8* bitcast (i16 (i16)* @abs_i16 to i8*), i8* bitcast (i32 (i32)* @abs_i32 to i8*), i8* bitcast (i64 (i64)* @abs_i64 to i8*), i8* bitcast (float (float)* @abs_f32 to i8*), i8* bitcast (double (double)* @abs_f64 to i8*)], section "llvm.metadata"
@str = internal constant [52 x i8] c"HL_NUMTHREADS not defined. Defaulting to 8 threads.\00"
@assert_message = internal global [51 x i8] c"Stride on innermost dimension of .result must be 1\00"
@assert_message1 = internal global [48 x i8] c"Stride on innermost dimension of .i54 must be 1\00"
@assert_message2 = internal global [40 x i8] c"Min on dimension 0 of .result must be 0\00"
@assert_message3 = internal global [40 x i8] c"Min on dimension 1 of .result must be 0\00"
@assert_message4 = internal global [40 x i8] c"Min on dimension 2 of .result must be 0\00"
@assert_message5 = internal global [40 x i8] c"Min on dimension 3 of .result must be 0\00"
@assert_message6 = internal global [37 x i8] c"Min on dimension 0 of .i54 must be 0\00"
@assert_message7 = internal global [37 x i8] c"Min on dimension 1 of .i54 must be 0\00"
@assert_message8 = internal global [37 x i8] c"Min on dimension 2 of .i54 must be 0\00"
@assert_message9 = internal global [37 x i8] c"Min on dimension 3 of .i54 must be 0\00"
@assert_message10 = internal global [43 x i8] c"Function may load image .i54 out of bounds\00"
@assert_message11 = internal global [43 x i8] c"Function may load image .i54 out of bounds\00"
@assert_message12 = internal global [47 x i8] c"Function may access output image out of bounds\00"

define weak void @__copy_to_host(%struct.buffer_t* %buf) {
  ret void
}

define weak i8* @fast_malloc(i32 %x) {
  %1 = add i32 %x, 32
  %2 = tail call noalias i8* @malloc(i32 %1) nounwind
  %3 = ptrtoint i8* %2 to i32
  %4 = add i32 %3, 32
  %5 = and i32 %4, -32
  %6 = inttoptr i32 %5 to i8*
  %7 = inttoptr i32 %5 to i8**
  %8 = getelementptr inbounds i8** %7, i32 -1
  store i8* %2, i8** %8, align 4, !tbaa !0
  ret i8* %6
}

declare noalias i8* @malloc(i32) nounwind

define weak void @fast_free(i8* %ptr) {
  %1 = getelementptr inbounds i8* %ptr, i32 -4
  %2 = bitcast i8* %1 to i8**
  %3 = load i8** %2, align 4, !tbaa !0
  tail call void @free(i8* %3) nounwind
  ret void
}

declare void @free(i8* nocapture) nounwind

define weak i8* @safe_malloc(i32 %x) {
  %mem = alloca i8*, align 4
  %1 = add i32 %x, 4095
  %2 = and i32 %1, -4096
  %3 = add i32 %2, 12288
  %4 = call i32 @posix_memalign(i8** %mem, i32 4096, i32 %3) nounwind
  %5 = load i8** %mem, align 4, !tbaa !0
  %6 = call i32 (i8*, ...)* @printf(i8* getelementptr inbounds ([49 x i8]* @.str, i32 0, i32 0), i32 %2, i8* %5)
  %7 = load i8** %mem, align 4, !tbaa !0
  %8 = add i32 %2, 4096
  %9 = getelementptr inbounds i8* %7, i32 %8
  %10 = bitcast i8* %7 to i8**
  store i8* %9, i8** %10, align 4, !tbaa !0
  %11 = load i8** %mem, align 4, !tbaa !0
  %12 = call i32 @mprotect(i8* %11, i32 4096, i32 0) nounwind
  %13 = load i8** %mem, align 4, !tbaa !0
  %14 = getelementptr inbounds i8* %13, i32 %8
  %15 = call i32 @mprotect(i8* %14, i32 4096, i32 0) nounwind
  %16 = load i8** %mem, align 4, !tbaa !0
  %17 = getelementptr inbounds i8* %16, i32 4096
  ret i8* %17
}

declare i32 @posix_memalign(i8**, i32, i32) nounwind

declare i32 @printf(i8* nocapture, ...) nounwind

declare i32 @mprotect(i8*, i32, i32) nounwind

define weak void @safe_free(i8* %ptr) {
  %1 = getelementptr inbounds i8* %ptr, i32 -4096
  %2 = tail call i32 @mprotect(i8* %1, i32 4096, i32 7) nounwind
  %3 = bitcast i8* %1 to i8**
  %4 = load i8** %3, align 4, !tbaa !0
  %5 = tail call i32 @mprotect(i8* %4, i32 4096, i32 7) nounwind
  tail call void @free(i8* %1) nounwind
  ret void
}

define weak i32 @hlprintf(i8* %fmt, ...) {
  %args = alloca i8*, align 4
  %1 = bitcast i8** %args to i8*
  call void @llvm.va_start(i8* %1)
  %2 = load %struct._IO_FILE** @stderr, align 4, !tbaa !0
  %3 = load i8** %args, align 4, !tbaa !0
  %4 = call i32 @vfprintf(%struct._IO_FILE* %2, i8* %fmt, i8* %3)
  call void @llvm.va_end(i8* %1)
  ret i32 %4
}

declare void @llvm.va_start(i8*) nounwind

declare i32 @vfprintf(%struct._IO_FILE* nocapture, i8* nocapture, i8*) nounwind

declare void @llvm.va_end(i8*) nounwind

define weak void @halide_error(i8* %msg) {
  %1 = load void (i8*)** @halide_error_handler, align 4, !tbaa !0
  %2 = icmp eq void (i8*)* %1, null
  br i1 %2, label %4, label %3

; <label>:3                                       ; preds = %0
  tail call void %1(i8* %msg)
  ret void

; <label>:4                                       ; preds = %0
  %5 = load %struct._IO_FILE** @stderr, align 4, !tbaa !0
  %6 = tail call i32 (%struct._IO_FILE*, i8*, ...)* @fprintf(%struct._IO_FILE* %5, i8* getelementptr inbounds ([11 x i8]* @.str1, i32 0, i32 0), i8* %msg)
  tail call void @exit(i32 1) noreturn nounwind
  unreachable
}

declare i32 @fprintf(%struct._IO_FILE* nocapture, i8* nocapture, ...) nounwind

declare void @exit(i32) noreturn nounwind

define weak void @set_error_handler(void (i8*)* %handler) {
  store void (i8*)* %handler, void (i8*)** @halide_error_handler, align 4, !tbaa !0
  ret void
}

define weak i8* @worker(i8* %void_arg) {
  %1 = icmp ne i8* %void_arg, null
  %2 = getelementptr inbounds i8* %void_arg, i32 4
  %3 = bitcast i8* %2 to %struct.work**
  %4 = bitcast i8* %void_arg to i32*
  br label %.backedge

.backedge:                                        ; preds = %50, %45, %36, %0
  %5 = tail call i32 @pthread_mutex_lock(%union.pthread_mutex_t* getelementptr inbounds (%struct.anon* @work_queue, i32 0, i32 3)) nounwind
  br i1 %1, label %6, label %30

; <label>:6                                       ; preds = %.backedge
  %7 = load %struct.work** %3, align 4, !tbaa !0
  %8 = getelementptr inbounds %struct.work* %7, i32 0, i32 4
  %9 = load i32* %8, align 4, !tbaa !3
  %10 = load i32* %4, align 4, !tbaa !3
  %11 = icmp eq i32 %9, %10
  br i1 %11, label %30, label %12

; <label>:12                                      ; preds = %6
  %13 = getelementptr inbounds %struct.work* %7, i32 0, i32 5
  %14 = load i32* %13, align 4, !tbaa !3
  %15 = icmp eq i32 %14, 0
  br i1 %15, label %.loopexit, label %16

; <label>:16                                      ; preds = %12
  %17 = tail call i32 @pthread_mutex_unlock(%union.pthread_mutex_t* getelementptr inbounds (%struct.anon* @work_queue, i32 0, i32 3)) nounwind
  %18 = tail call i32 @pthread_mutex_lock(%union.pthread_mutex_t* getelementptr inbounds (%struct.anon* @work_queue, i32 0, i32 3)) nounwind
  %19 = load %struct.work** %3, align 4, !tbaa !0
  %20 = getelementptr inbounds %struct.work* %19, i32 0, i32 5
  %21 = load i32* %20, align 4, !tbaa !3
  %22 = icmp eq i32 %21, 0
  br i1 %22, label %.loopexit, label %.lr.ph

.lr.ph:                                           ; preds = %.lr.ph, %16
  %23 = tail call i32 @pthread_mutex_unlock(%union.pthread_mutex_t* getelementptr inbounds (%struct.anon* @work_queue, i32 0, i32 3)) nounwind
  %24 = tail call i32 @pthread_mutex_lock(%union.pthread_mutex_t* getelementptr inbounds (%struct.anon* @work_queue, i32 0, i32 3)) nounwind
  %25 = load %struct.work** %3, align 4, !tbaa !0
  %26 = getelementptr inbounds %struct.work* %25, i32 0, i32 5
  %27 = load i32* %26, align 4, !tbaa !3
  %28 = icmp eq i32 %27, 0
  br i1 %28, label %.loopexit, label %.lr.ph

.loopexit:                                        ; preds = %.lr.ph, %16, %12
  %29 = tail call i32 @pthread_mutex_unlock(%union.pthread_mutex_t* getelementptr inbounds (%struct.anon* @work_queue, i32 0, i32 3)) nounwind
  ret i8* null

; <label>:30                                      ; preds = %6, %.backedge
  %31 = load i32* getelementptr inbounds (%struct.anon* @work_queue, i32 0, i32 1), align 8, !tbaa !3
  %32 = load i32* getelementptr inbounds (%struct.anon* @work_queue, i32 0, i32 2), align 4, !tbaa !3
  %33 = icmp eq i32 %31, %32
  br i1 %33, label %34, label %39

; <label>:34                                      ; preds = %30
  br i1 %1, label %35, label %36

; <label>:35                                      ; preds = %34
  tail call void @__assert_fail(i8* getelementptr inbounds ([5 x i8]* @.str2, i32 0, i32 0), i8* getelementptr inbounds ([32 x i8]* @.str3, i32 0, i32 0), i32 146, i8* getelementptr inbounds ([21 x i8]* @__PRETTY_FUNCTION__.worker, i32 0, i32 0)) noreturn nounwind
  unreachable

; <label>:36                                      ; preds = %34
  %37 = tail call i32 @pthread_cond_wait(%union.pthread_cond_t* getelementptr inbounds (%struct.anon* @work_queue, i32 0, i32 4), %union.pthread_mutex_t* getelementptr inbounds (%struct.anon* @work_queue, i32 0, i32 3))
  %38 = tail call i32 @pthread_mutex_unlock(%union.pthread_mutex_t* getelementptr inbounds (%struct.anon* @work_queue, i32 0, i32 3)) nounwind
  br label %.backedge

; <label>:39                                      ; preds = %30
  %40 = getelementptr inbounds %struct.anon* @work_queue, i32 0, i32 0, i32 %31, i32 1
  %41 = load i32* %40, align 4, !tbaa !3
  %42 = getelementptr inbounds %struct.anon* @work_queue, i32 0, i32 0, i32 %31, i32 2
  %43 = load i32* %42, align 8, !tbaa !3
  %44 = icmp eq i32 %41, %43
  br i1 %44, label %45, label %50

; <label>:45                                      ; preds = %39
  %46 = add nsw i32 %31, 1
  %47 = srem i32 %46, 65536
  store i32 %47, i32* getelementptr inbounds (%struct.anon* @work_queue, i32 0, i32 1), align 8, !tbaa !3
  %48 = getelementptr inbounds %struct.anon* @work_queue, i32 0, i32 0, i32 %31, i32 4
  store i32 0, i32* %48, align 8, !tbaa !3
  %49 = tail call i32 @pthread_mutex_unlock(%union.pthread_mutex_t* getelementptr inbounds (%struct.anon* @work_queue, i32 0, i32 3)) nounwind
  br label %.backedge

; <label>:50                                      ; preds = %39
  %.0 = getelementptr inbounds %struct.anon* @work_queue, i32 0, i32 0, i32 %31, i32 0
  %tmp = load void (i32, i8*)** %.0, align 8
  %.3 = getelementptr inbounds %struct.anon* @work_queue, i32 0, i32 0, i32 %31, i32 3
  %tmp3 = load i8** %.3, align 4
  %.5 = getelementptr inbounds %struct.anon* @work_queue, i32 0, i32 0, i32 %31, i32 5
  %51 = add nsw i32 %41, 1
  store i32 %51, i32* %40, align 4, !tbaa !3
  %52 = load i32* %.5, align 4, !tbaa !3
  %53 = add nsw i32 %52, 1
  store i32 %53, i32* %.5, align 4, !tbaa !3
  %54 = tail call i32 @pthread_mutex_unlock(%union.pthread_mutex_t* getelementptr inbounds (%struct.anon* @work_queue, i32 0, i32 3)) nounwind
  tail call void %tmp(i32 %41, i8* %tmp3)
  %55 = tail call i32 @pthread_mutex_lock(%union.pthread_mutex_t* getelementptr inbounds (%struct.anon* @work_queue, i32 0, i32 3)) nounwind
  %56 = load i32* %.5, align 4, !tbaa !3
  %57 = add nsw i32 %56, -1
  store i32 %57, i32* %.5, align 4, !tbaa !3
  %58 = tail call i32 @pthread_mutex_unlock(%union.pthread_mutex_t* getelementptr inbounds (%struct.anon* @work_queue, i32 0, i32 3)) nounwind
  br label %.backedge
}

declare i32 @pthread_mutex_lock(%union.pthread_mutex_t*) nounwind

declare i32 @pthread_mutex_unlock(%union.pthread_mutex_t*) nounwind

declare void @__assert_fail(i8*, i8*, i32, i8*) noreturn nounwind

declare i32 @pthread_cond_wait(%union.pthread_cond_t*, %union.pthread_mutex_t*)

define weak void @do_par_for(void (i32, i8*)* %f, i32 %min, i32 %size, i8* %closure) {
  %arg = alloca %struct.worker_arg, align 4
  %1 = load i8* @thread_pool_initialized, align 1, !tbaa !4
  %2 = and i8 %1, 1
  %3 = icmp eq i8 %2, 0
  br i1 %3, label %4, label %23

; <label>:4                                       ; preds = %0
  %5 = call i32 @pthread_mutex_init(%union.pthread_mutex_t* getelementptr inbounds (%struct.anon* @work_queue, i32 0, i32 3), %union.pthread_mutexattr_t* null) nounwind
  %6 = call i32 @pthread_cond_init(%union.pthread_cond_t* getelementptr inbounds (%struct.anon* @work_queue, i32 0, i32 4), %union.pthread_condattr_t* null) nounwind
  store i32 0, i32* getelementptr inbounds (%struct.anon* @work_queue, i32 0, i32 2), align 4, !tbaa !3
  store i32 0, i32* getelementptr inbounds (%struct.anon* @work_queue, i32 0, i32 1), align 8, !tbaa !3
  store i32 1, i32* getelementptr inbounds (%struct.anon* @work_queue, i32 0, i32 6), align 8, !tbaa !3
  %7 = call i8* @getenv(i8* getelementptr inbounds ([14 x i8]* @.str4, i32 0, i32 0)) nounwind
  store i32 8, i32* @threads, align 4, !tbaa !3
  %8 = icmp eq i8* %7, null
  br i1 %8, label %11, label %9

; <label>:9                                       ; preds = %4
  %10 = call i32 @atoi(i8* %7) nounwind readonly
  store i32 %10, i32* @threads, align 4, !tbaa !3
  br label %12

; <label>:11                                      ; preds = %4
  %puts = call i32 @puts(i8* getelementptr inbounds ([52 x i8]* @str, i32 0, i32 0))
  %.pr = load i32* @threads, align 4
  br label %12

; <label>:12                                      ; preds = %11, %9
  %13 = phi i32 [ %.pr, %11 ], [ %10, %9 ]
  %14 = icmp sgt i32 %13, 64
  br i1 %14, label %.preheader.thread, label %.preheader

.preheader.thread:                                ; preds = %12
  store i32 64, i32* @threads, align 4, !tbaa !3
  br label %.lr.ph

.preheader:                                       ; preds = %12
  %15 = add nsw i32 %13, -1
  %16 = icmp sgt i32 %15, 0
  br i1 %16, label %.lr.ph, label %._crit_edge

.lr.ph:                                           ; preds = %.lr.ph, %.preheader, %.preheader.thread
  %i.06 = phi i32 [ %19, %.lr.ph ], [ 0, %.preheader.thread ], [ 0, %.preheader ]
  %17 = getelementptr inbounds %struct.anon* @work_queue, i32 0, i32 5, i32 %i.06
  %18 = call i32 @pthread_create(i32* %17, %union.pthread_attr_t* null, i8* (i8*)* @worker, i8* null) nounwind
  %19 = add nsw i32 %i.06, 1
  %20 = load i32* @threads, align 4, !tbaa !3
  %21 = add nsw i32 %20, -1
  %22 = icmp slt i32 %19, %21
  br i1 %22, label %.lr.ph, label %._crit_edge

._crit_edge:                                      ; preds = %.lr.ph, %.preheader
  store i8 1, i8* @thread_pool_initialized, align 1, !tbaa !4
  br label %23

; <label>:23                                      ; preds = %._crit_edge, %0
  %24 = call i32 @pthread_mutex_lock(%union.pthread_mutex_t* getelementptr inbounds (%struct.anon* @work_queue, i32 0, i32 3)) nounwind
  %25 = add nsw i32 %size, %min
  %26 = load i32* getelementptr inbounds (%struct.anon* @work_queue, i32 0, i32 6), align 8, !tbaa !3
  %27 = add nsw i32 %26, 1
  %28 = add nsw i32 %26, 2
  %29 = icmp eq i32 %26, 0
  %. = select i1 %29, i32 %28, i32 %27
  %job.4.0 = select i1 %29, i32 %27, i32 %26
  store i32 %., i32* getelementptr inbounds (%struct.anon* @work_queue, i32 0, i32 6), align 8
  %30 = load i32* getelementptr inbounds (%struct.anon* @work_queue, i32 0, i32 2), align 4, !tbaa !3
  %.0 = getelementptr inbounds %struct.anon* @work_queue, i32 0, i32 0, i32 %30, i32 0
  store void (i32, i8*)* %f, void (i32, i8*)** %.0, align 8
  %.1 = getelementptr inbounds %struct.anon* @work_queue, i32 0, i32 0, i32 %30, i32 1
  store i32 %min, i32* %.1, align 4
  %.2 = getelementptr inbounds %struct.anon* @work_queue, i32 0, i32 0, i32 %30, i32 2
  store i32 %25, i32* %.2, align 8
  %.3 = getelementptr inbounds %struct.anon* @work_queue, i32 0, i32 0, i32 %30, i32 3
  store i8* %closure, i8** %.3, align 4
  %.4 = getelementptr inbounds %struct.anon* @work_queue, i32 0, i32 0, i32 %30, i32 4
  store i32 %job.4.0, i32* %.4, align 8
  %.5 = getelementptr inbounds %struct.anon* @work_queue, i32 0, i32 0, i32 %30, i32 5
  store i32 0, i32* %.5, align 4
  %31 = load i32* getelementptr inbounds (%struct.anon* @work_queue, i32 0, i32 2), align 4, !tbaa !3
  %32 = getelementptr inbounds %struct.anon* @work_queue, i32 0, i32 0, i32 %31
  %33 = getelementptr inbounds %struct.worker_arg* %arg, i32 0, i32 0
  store i32 %job.4.0, i32* %33, align 4, !tbaa !3
  %34 = getelementptr inbounds %struct.worker_arg* %arg, i32 0, i32 1
  store %struct.work* %32, %struct.work** %34, align 4, !tbaa !0
  %35 = add nsw i32 %31, 1
  %36 = srem i32 %35, 65536
  %37 = load i32* getelementptr inbounds (%struct.anon* @work_queue, i32 0, i32 1), align 8, !tbaa !3
  %38 = icmp eq i32 %36, %37
  br i1 %38, label %39, label %40

; <label>:39                                      ; preds = %23
  call void @__assert_fail(i8* getelementptr inbounds ([28 x i8]* @.str6, i32 0, i32 0), i8* getelementptr inbounds ([32 x i8]* @.str3, i32 0, i32 0), i32 214, i8* getelementptr inbounds ([63 x i8]* @__PRETTY_FUNCTION__.do_par_for, i32 0, i32 0)) noreturn nounwind
  unreachable

; <label>:40                                      ; preds = %23
  store i32 %36, i32* getelementptr inbounds (%struct.anon* @work_queue, i32 0, i32 2), align 4, !tbaa !3
  %41 = call i32 @pthread_mutex_unlock(%union.pthread_mutex_t* getelementptr inbounds (%struct.anon* @work_queue, i32 0, i32 3)) nounwind
  %42 = call i32 @pthread_cond_broadcast(%union.pthread_cond_t* getelementptr inbounds (%struct.anon* @work_queue, i32 0, i32 4)) nounwind
  %43 = bitcast %struct.worker_arg* %arg to i8*
  %44 = call i8* @worker(i8* %43)
  ret void
}

declare i32 @pthread_mutex_init(%union.pthread_mutex_t*, %union.pthread_mutexattr_t*) nounwind

declare i32 @pthread_cond_init(%union.pthread_cond_t*, %union.pthread_condattr_t*) nounwind

declare i8* @getenv(i8* nocapture) nounwind readonly

declare i32 @atoi(i8* nocapture) nounwind readonly

declare i32 @pthread_create(i32*, %union.pthread_attr_t*, i8* (i8*)*, i8*) nounwind

declare i32 @pthread_cond_broadcast(%union.pthread_cond_t*) nounwind

define linkonce_odr float @sqrt_f32(float %x) nounwind readnone inlinehint alwaysinline {
  %1 = tail call float @sqrtf(float %x) nounwind readnone
  ret float %1
}

declare float @sqrtf(float) nounwind readnone

define linkonce_odr float @sin_f32(float %x) nounwind readnone inlinehint alwaysinline {
  %1 = tail call float @sinf(float %x) nounwind readnone
  ret float %1
}

declare float @sinf(float) nounwind readnone

define linkonce_odr float @cos_f32(float %x) nounwind readnone inlinehint alwaysinline {
  %1 = tail call float @cosf(float %x) nounwind readnone
  ret float %1
}

declare float @cosf(float) nounwind readnone

define linkonce_odr float @exp_f32(float %x) nounwind inlinehint alwaysinline {
  %1 = tail call float @expf(float %x) nounwind
  ret float %1
}

declare float @expf(float) nounwind

define linkonce_odr float @log_f32(float %x) nounwind inlinehint alwaysinline {
  %1 = tail call float @logf(float %x) nounwind
  ret float %1
}

declare float @logf(float) nounwind

define linkonce_odr float @pow_f32(float %x, float %y) nounwind readonly inlinehint alwaysinline {
  %1 = tail call float @llvm.pow.f32(float %x, float %y)
  ret float %1
}

declare float @llvm.pow.f32(float, float) nounwind readonly

define linkonce_odr float @floor_f32(float %x) nounwind readnone inlinehint alwaysinline {
  %1 = tail call float @floorf(float %x) nounwind readnone
  ret float %1
}

declare float @floorf(float) nounwind readnone

define linkonce_odr float @ceil_f32(float %x) nounwind readnone inlinehint alwaysinline {
  %1 = tail call float @ceilf(float %x) nounwind readnone
  ret float %1
}

declare float @ceilf(float) nounwind readnone

define linkonce_odr float @round_f32(float %x) nounwind readnone inlinehint alwaysinline {
  %1 = tail call float @roundf(float %x) nounwind readnone
  ret float %1
}

declare float @roundf(float) nounwind readnone

define linkonce_odr double @sqrt_f64(double %x) nounwind readnone inlinehint alwaysinline {
  %1 = tail call double @sqrt(double %x) nounwind readnone
  ret double %1
}

declare double @sqrt(double) nounwind readnone

define linkonce_odr double @sin_f64(double %x) nounwind readnone inlinehint alwaysinline {
  %1 = tail call double @sin(double %x) nounwind readnone
  ret double %1
}

declare double @sin(double) nounwind readnone

define linkonce_odr double @cos_f64(double %x) nounwind readnone inlinehint alwaysinline {
  %1 = tail call double @cos(double %x) nounwind readnone
  ret double %1
}

declare double @cos(double) nounwind readnone

define linkonce_odr double @exp_f64(double %x) nounwind inlinehint alwaysinline {
  %1 = tail call double @exp(double %x) nounwind
  ret double %1
}

declare double @exp(double) nounwind

define linkonce_odr double @log_f64(double %x) nounwind inlinehint alwaysinline {
  %1 = tail call double @log(double %x) nounwind
  ret double %1
}

declare double @log(double) nounwind

define linkonce_odr double @pow_f64(double %x, double %y) nounwind readonly inlinehint alwaysinline {
  %1 = tail call double @llvm.pow.f64(double %x, double %y)
  ret double %1
}

declare double @llvm.pow.f64(double, double) nounwind readonly

define linkonce_odr double @floor_f64(double %x) nounwind readnone inlinehint alwaysinline {
  %1 = tail call double @floor(double %x) nounwind readnone
  ret double %1
}

declare double @floor(double) nounwind readnone

define linkonce_odr double @ceil_f64(double %x) nounwind readnone inlinehint alwaysinline {
  %1 = tail call double @ceil(double %x) nounwind readnone
  ret double %1
}

declare double @ceil(double) nounwind readnone

define linkonce_odr double @round_f64(double %x) nounwind readnone inlinehint alwaysinline {
  %1 = tail call double @round(double %x) nounwind readnone
  ret double %1
}

declare double @round(double) nounwind readnone

define linkonce_odr float @maxval_f32() nounwind readnone inlinehint alwaysinline {
  ret float 0x47EFFFFFE0000000
}

define linkonce_odr float @minval_f32() nounwind readnone inlinehint alwaysinline {
  ret float 0xC7EFFFFFE0000000
}

define linkonce_odr double @maxval_f64() nounwind readnone inlinehint alwaysinline {
  ret double 0x7FEFFFFFFFFFFFFF
}

define linkonce_odr double @minval_f64() nounwind readnone inlinehint alwaysinline {
  ret double 0xFFEFFFFFFFFFFFFF
}

define linkonce_odr zeroext i8 @maxval_u8() nounwind readnone inlinehint alwaysinline {
  ret i8 -1
}

define linkonce_odr zeroext i8 @minval_u8() nounwind readnone inlinehint alwaysinline {
  ret i8 0
}

define linkonce_odr zeroext i16 @maxval_u16() nounwind readnone inlinehint alwaysinline {
  ret i16 -1
}

define linkonce_odr zeroext i16 @minval_u16() nounwind readnone inlinehint alwaysinline {
  ret i16 0
}

define linkonce_odr i32 @maxval_u32() nounwind readnone inlinehint alwaysinline {
  ret i32 -1
}

define linkonce_odr i32 @minval_u32() nounwind readnone inlinehint alwaysinline {
  ret i32 0
}

define linkonce_odr i64 @maxval_u64() nounwind readnone inlinehint alwaysinline {
  ret i64 -1
}

define linkonce_odr i64 @minval_u64() nounwind readnone inlinehint alwaysinline {
  ret i64 0
}

define linkonce_odr signext i8 @maxval_s8() nounwind readnone inlinehint alwaysinline {
  ret i8 127
}

define linkonce_odr signext i8 @minval_s8() nounwind readnone inlinehint alwaysinline {
  ret i8 -128
}

define linkonce_odr signext i16 @maxval_s16() nounwind readnone inlinehint alwaysinline {
  ret i16 32767
}

define linkonce_odr signext i16 @minval_s16() nounwind readnone inlinehint alwaysinline {
  ret i16 -32768
}

define linkonce_odr i32 @maxval_s32() nounwind readnone inlinehint alwaysinline {
  ret i32 2147483647
}

define linkonce_odr i32 @minval_s32() nounwind readnone inlinehint alwaysinline {
  ret i32 -2147483648
}

define linkonce_odr i64 @maxval_s64() nounwind readnone inlinehint alwaysinline {
  ret i64 9223372036854775807
}

define linkonce_odr i64 @minval_s64() nounwind readnone inlinehint alwaysinline {
  ret i64 -9223372036854775808
}

define linkonce_odr signext i8 @abs_i8(i8 signext %a) nounwind readnone inlinehint alwaysinline {
  %1 = sext i8 %a to i32
  %2 = icmp sgt i8 %a, -1
  %3 = sub nsw i32 0, %1
  %4 = select i1 %2, i32 %1, i32 %3
  %5 = trunc i32 %4 to i8
  ret i8 %5
}

define linkonce_odr signext i16 @abs_i16(i16 signext %a) nounwind readnone inlinehint alwaysinline {
  %1 = sext i16 %a to i32
  %2 = icmp sgt i16 %a, -1
  %3 = sub nsw i32 0, %1
  %4 = select i1 %2, i32 %1, i32 %3
  %5 = trunc i32 %4 to i16
  ret i16 %5
}

define linkonce_odr i32 @abs_i32(i32 %a) nounwind readnone inlinehint alwaysinline {
  %1 = icmp sgt i32 %a, -1
  %2 = sub nsw i32 0, %a
  %3 = select i1 %1, i32 %a, i32 %2
  ret i32 %3
}

define linkonce_odr i64 @abs_i64(i64 %a) nounwind readnone inlinehint alwaysinline {
  %1 = icmp sgt i64 %a, -1
  %2 = sub nsw i64 0, %a
  %3 = select i1 %1, i64 %a, i64 %2
  ret i64 %3
}

define linkonce_odr float @abs_f32(float %a) nounwind readnone inlinehint alwaysinline {
  %1 = fcmp ult float %a, 0.000000e+00
  br i1 %1, label %2, label %4

; <label>:2                                       ; preds = %0
  %3 = fsub float -0.000000e+00, %a
  br label %4

; <label>:4                                       ; preds = %2, %0
  %5 = phi float [ %3, %2 ], [ %a, %0 ]
  ret float %5
}

define linkonce_odr double @abs_f64(double %a) nounwind readnone inlinehint alwaysinline {
  %1 = fcmp ult double %a, 0.000000e+00
  br i1 %1, label %2, label %4

; <label>:2                                       ; preds = %0
  %3 = fsub double -0.000000e+00, %a
  br label %4

; <label>:4                                       ; preds = %2, %0
  %5 = phi double [ %3, %2 ], [ %a, %0 ]
  ret double %5
}

define weak i32 @currentTime() {
  %now = alloca %struct.timeval, align 4
  %1 = load i8* @initialized, align 1, !tbaa !4
  %2 = and i8 %1, 1
  %3 = icmp eq i8 %2, 0
  br i1 %3, label %4, label %6

; <label>:4                                       ; preds = %0
  %5 = call i32 @gettimeofday(%struct.timeval* @start, %struct.timezone* null) nounwind
  store i8 1, i8* @initialized, align 1, !tbaa !4
  br label %19

; <label>:6                                       ; preds = %0
  %7 = call i32 @gettimeofday(%struct.timeval* %now, %struct.timezone* null) nounwind
  %8 = getelementptr inbounds %struct.timeval* %now, i32 0, i32 0
  %9 = load i32* %8, align 4, !tbaa !5
  %10 = load i32* getelementptr inbounds (%struct.timeval* @start, i32 0, i32 0), align 4, !tbaa !5
  %11 = sub nsw i32 %9, %10
  %12 = mul nsw i32 %11, 1000
  %13 = getelementptr inbounds %struct.timeval* %now, i32 0, i32 1
  %14 = load i32* %13, align 4, !tbaa !5
  %15 = load i32* getelementptr inbounds (%struct.timeval* @start, i32 0, i32 1), align 4, !tbaa !5
  %16 = sub nsw i32 %14, %15
  %17 = sdiv i32 %16, 1000
  %18 = add nsw i32 %17, %12
  br label %19

; <label>:19                                      ; preds = %6, %4
  %.0 = phi i32 [ %18, %6 ], [ 0, %4 ]
  ret i32 %.0
}

declare i32 @gettimeofday(%struct.timeval*, %struct.timezone*) nounwind

declare i32 @puts(i8* nocapture) nounwind

define void @f59_pseudojit_inner(i8* %.i54, i32 %.i54.extent.0, i32 %.i54.extent.1, i32 %.i54.extent.2, i32 %.i54.extent.3, i32 %.i54.stride.0, i32 %.i54.stride.1, i32 %.i54.stride.2, i32 %.i54.stride.3, i32 %.i54.min.0, i32 %.i54.min.1, i32 %.i54.min.2, i32 %.i54.min.3, i8* %.result, i32 %.result.extent.0, i32 %.result.extent.1, i32 %.result.extent.2, i32 %.result.extent.3, i32 %.result.stride.0, i32 %.result.stride.1, i32 %.result.stride.2, i32 %.result.stride.3, i32 %.result.min.0, i32 %.result.min.1, i32 %.result.min.2, i32 %.result.min.3) {
entry:
  %0 = icmp eq i32 %.result.stride.0, 1
  br i1 %0, label %after_assert, label %assert

assert:                                           ; preds = %entry
  call void @halide_error(i8* getelementptr inbounds ([51 x i8]* @assert_message, i32 0, i32 0))
  ret void

after_assert:                                     ; preds = %entry
  %1 = icmp eq i32 %.i54.stride.0, 1
  br i1 %1, label %after_assert2, label %assert1

assert1:                                          ; preds = %after_assert
  call void @halide_error(i8* getelementptr inbounds ([48 x i8]* @assert_message1, i32 0, i32 0))
  ret void

after_assert2:                                    ; preds = %after_assert
  %2 = icmp eq i32 %.result.min.0, 0
  br i1 %2, label %after_assert4, label %assert3

assert3:                                          ; preds = %after_assert2
  call void @halide_error(i8* getelementptr inbounds ([40 x i8]* @assert_message2, i32 0, i32 0))
  ret void

after_assert4:                                    ; preds = %after_assert2
  %3 = icmp eq i32 %.result.min.1, 0
  br i1 %3, label %after_assert6, label %assert5

assert5:                                          ; preds = %after_assert4
  call void @halide_error(i8* getelementptr inbounds ([40 x i8]* @assert_message3, i32 0, i32 0))
  ret void

after_assert6:                                    ; preds = %after_assert4
  %4 = icmp eq i32 %.result.min.2, 0
  br i1 %4, label %after_assert8, label %assert7

assert7:                                          ; preds = %after_assert6
  call void @halide_error(i8* getelementptr inbounds ([40 x i8]* @assert_message4, i32 0, i32 0))
  ret void

after_assert8:                                    ; preds = %after_assert6
  %5 = icmp eq i32 %.result.min.3, 0
  br i1 %5, label %after_assert10, label %assert9

assert9:                                          ; preds = %after_assert8
  call void @halide_error(i8* getelementptr inbounds ([40 x i8]* @assert_message5, i32 0, i32 0))
  ret void

after_assert10:                                   ; preds = %after_assert8
  %6 = icmp eq i32 %.i54.min.0, 0
  br i1 %6, label %after_assert12, label %assert11

assert11:                                         ; preds = %after_assert10
  call void @halide_error(i8* getelementptr inbounds ([37 x i8]* @assert_message6, i32 0, i32 0))
  ret void

after_assert12:                                   ; preds = %after_assert10
  %7 = icmp eq i32 %.i54.min.1, 0
  br i1 %7, label %after_assert14, label %assert13

assert13:                                         ; preds = %after_assert12
  call void @halide_error(i8* getelementptr inbounds ([37 x i8]* @assert_message7, i32 0, i32 0))
  ret void

after_assert14:                                   ; preds = %after_assert12
  %8 = icmp eq i32 %.i54.min.2, 0
  br i1 %8, label %after_assert16, label %assert15

assert15:                                         ; preds = %after_assert14
  call void @halide_error(i8* getelementptr inbounds ([37 x i8]* @assert_message8, i32 0, i32 0))
  ret void

after_assert16:                                   ; preds = %after_assert14
  %9 = icmp eq i32 %.i54.min.3, 0
  br i1 %9, label %after_assert18, label %assert17

assert17:                                         ; preds = %after_assert16
  call void @halide_error(i8* getelementptr inbounds ([37 x i8]* @assert_message9, i32 0, i32 0))
  ret void

after_assert18:                                   ; preds = %after_assert16
  %10 = add i32 %.result.extent.0, 1
  %11 = sdiv i32 %10, 2
  %12 = mul i32 %11, 2
  %13 = add i32 %12, -1
  %14 = icmp slt i32 %13, %.i54.extent.0
  br i1 %14, label %after_assert20, label %assert19

assert19:                                         ; preds = %after_assert18
  call void @halide_error(i8* getelementptr inbounds ([43 x i8]* @assert_message10, i32 0, i32 0))
  ret void

after_assert20:                                   ; preds = %after_assert18
  %15 = add i32 %.result.extent.1, -1
  %16 = icmp slt i32 %15, %.i54.extent.1
  br i1 %16, label %after_assert22, label %assert21

assert21:                                         ; preds = %after_assert20
  call void @halide_error(i8* getelementptr inbounds ([43 x i8]* @assert_message11, i32 0, i32 0))
  ret void

after_assert22:                                   ; preds = %after_assert20
  %17 = add i32 %.result.extent.0, 1
  %18 = sdiv i32 %17, 2
  %19 = mul i32 %18, 2
  %20 = add i32 %19, -1
  %21 = icmp sle i32 %20, %.result.extent.0
  br i1 %21, label %after_assert24, label %assert23

assert23:                                         ; preds = %after_assert22
  call void @halide_error(i8* getelementptr inbounds ([47 x i8]* @assert_message12, i32 0, i32 0))
  ret void

after_assert24:                                   ; preds = %after_assert22
  %22 = add i32 0, %.result.extent.1
  br label %f59.v57_loop

f59.v57_loop:                                     ; preds = %f59.v56_afterloop, %after_assert24
  %f59.v57 = phi i32 [ 0, %after_assert24 ], [ %f59.v57_nextvar, %f59.v56_afterloop ]
  %23 = add i32 %.result.extent.0, 1
  %24 = sdiv i32 %23, 2
  %25 = add i32 0, %24
  br label %f59.v56_loop

f59.v56_loop:                                     ; preds = %f59.v56_loop, %f59.v57_loop
  %f59.v56 = phi i32 [ 0, %f59.v57_loop ], [ %f59.v56_nextvar, %f59.v56_loop ]
  %26 = bitcast i8* %.result to float*
  %27 = mul i32 %.result.stride.1, %f59.v57
  %28 = mul i32 %f59.v56, 2
  %29 = add i32 %28, %27
  %30 = add i32 %29, 1
  %31 = insertelement <2 x i32> undef, i32 %29, i32 0
  %32 = add i32 %30, 1
  %33 = insertelement <2 x i32> %31, i32 %30, i32 1
  %34 = bitcast i8* %.i54 to double*
  %35 = mul i32 %f59.v57, %.i54.stride.1
  %36 = mul i32 %f59.v56, 2
  %37 = add i32 %36, %35
  %38 = add i32 %37, 1
  %39 = insertelement <2 x i32> undef, i32 %37, i32 0
  %40 = add i32 %38, 1
  %41 = insertelement <2 x i32> %39, i32 %38, i32 1
  %42 = extractelement <2 x i32> %41, i32 1
  %43 = getelementptr double* %34, i32 %42
  %44 = load double* %43
  %45 = extractelement <2 x i32> %41, i32 0
  %46 = getelementptr double* %34, i32 %45
  %47 = load double* %46
  %48 = insertelement <2 x double> undef, double %47, i32 0
  %49 = insertelement <2 x double> %48, double %44, i32 1
  %50 = fptrunc <2 x double> %49 to <2 x float>
  %51 = extractelement <2 x float> %50, i32 0
  %extern_pow_f32 = call float @pow_f32(float 0x3FF19999A0000000, float %51)
  %52 = extractelement <2 x float> %50, i32 1
  %extern_pow_f3225 = call float @pow_f32(float 0x3FF19999A0000000, float %52)
  %53 = insertelement <2 x float> undef, float %extern_pow_f3225, i32 1
  %54 = insertelement <2 x float> %53, float %extern_pow_f32, i32 0
  %55 = extractelement <2 x i32> %33, i32 0
  %56 = getelementptr float* %26, i32 %55
  %57 = extractelement <2 x float> %54, i32 0
  store float %57, float* %56
  %58 = extractelement <2 x i32> %33, i32 1
  %59 = getelementptr float* %26, i32 %58
  %60 = extractelement <2 x float> %54, i32 1
  store float %60, float* %59
  %f59.v56_nextvar = add i32 %f59.v56, 1
  %61 = icmp ne i32 %f59.v56_nextvar, %25
  br i1 %61, label %f59.v56_loop, label %f59.v56_afterloop

f59.v56_afterloop:                                ; preds = %f59.v56_loop
  %f59.v57_nextvar = add i32 %f59.v57, 1
  %62 = icmp ne i32 %f59.v57_nextvar, %22
  br i1 %62, label %f59.v57_loop, label %f59.v57_afterloop

f59.v57_afterloop:                                ; preds = %f59.v56_afterloop
  ret void
}

define void @f59_pseudojit(%struct.buffer_t*, %struct.buffer_t*) {
entry:
  %.min.3_ref = getelementptr %struct.buffer_t* %0, i32 0, i32 6, i32 3
  %.i54.min.3 = load i32* %.min.3_ref
  %.min.2_ref = getelementptr %struct.buffer_t* %0, i32 0, i32 6, i32 2
  %.i54.min.2 = load i32* %.min.2_ref
  %.min.1_ref = getelementptr %struct.buffer_t* %0, i32 0, i32 6, i32 1
  %.i54.min.1 = load i32* %.min.1_ref
  %.min.0_ref = getelementptr %struct.buffer_t* %0, i32 0, i32 6, i32 0
  %.i54.min.0 = load i32* %.min.0_ref
  %.stride.3_ref = getelementptr %struct.buffer_t* %0, i32 0, i32 5, i32 3
  %.i54.stride.3 = load i32* %.stride.3_ref
  %.stride.2_ref = getelementptr %struct.buffer_t* %0, i32 0, i32 5, i32 2
  %.i54.stride.2 = load i32* %.stride.2_ref
  %.stride.1_ref = getelementptr %struct.buffer_t* %0, i32 0, i32 5, i32 1
  %.i54.stride.1 = load i32* %.stride.1_ref
  %.stride.0_ref = getelementptr %struct.buffer_t* %0, i32 0, i32 5, i32 0
  %.i54.stride.0 = load i32* %.stride.0_ref
  %.extent.3_ref = getelementptr %struct.buffer_t* %0, i32 0, i32 4, i32 3
  %.i54.extent.3 = load i32* %.extent.3_ref
  %.extent.2_ref = getelementptr %struct.buffer_t* %0, i32 0, i32 4, i32 2
  %.i54.extent.2 = load i32* %.extent.2_ref
  %.extent.1_ref = getelementptr %struct.buffer_t* %0, i32 0, i32 4, i32 1
  %.i54.extent.1 = load i32* %.extent.1_ref
  %.extent.0_ref = getelementptr %struct.buffer_t* %0, i32 0, i32 4, i32 0
  %.i54.extent.0 = load i32* %.extent.0_ref
  %.host_ref = getelementptr %struct.buffer_t* %0, i32 0, i32 0
  %.i54 = load i8** %.host_ref
  %.min.3_ref1 = getelementptr %struct.buffer_t* %1, i32 0, i32 6, i32 3
  %.result.min.3 = load i32* %.min.3_ref1
  %.min.2_ref3 = getelementptr %struct.buffer_t* %1, i32 0, i32 6, i32 2
  %.result.min.2 = load i32* %.min.2_ref3
  %.min.1_ref5 = getelementptr %struct.buffer_t* %1, i32 0, i32 6, i32 1
  %.result.min.1 = load i32* %.min.1_ref5
  %.min.0_ref7 = getelementptr %struct.buffer_t* %1, i32 0, i32 6, i32 0
  %.result.min.0 = load i32* %.min.0_ref7
  %.stride.3_ref9 = getelementptr %struct.buffer_t* %1, i32 0, i32 5, i32 3
  %.result.stride.3 = load i32* %.stride.3_ref9
  %.stride.2_ref11 = getelementptr %struct.buffer_t* %1, i32 0, i32 5, i32 2
  %.result.stride.2 = load i32* %.stride.2_ref11
  %.stride.1_ref13 = getelementptr %struct.buffer_t* %1, i32 0, i32 5, i32 1
  %.result.stride.1 = load i32* %.stride.1_ref13
  %.stride.0_ref15 = getelementptr %struct.buffer_t* %1, i32 0, i32 5, i32 0
  %.result.stride.0 = load i32* %.stride.0_ref15
  %.extent.3_ref17 = getelementptr %struct.buffer_t* %1, i32 0, i32 4, i32 3
  %.result.extent.3 = load i32* %.extent.3_ref17
  %.extent.2_ref19 = getelementptr %struct.buffer_t* %1, i32 0, i32 4, i32 2
  %.result.extent.2 = load i32* %.extent.2_ref19
  %.extent.1_ref21 = getelementptr %struct.buffer_t* %1, i32 0, i32 4, i32 1
  %.result.extent.1 = load i32* %.extent.1_ref21
  %.extent.0_ref23 = getelementptr %struct.buffer_t* %1, i32 0, i32 4, i32 0
  %.result.extent.0 = load i32* %.extent.0_ref23
  %.host_ref25 = getelementptr %struct.buffer_t* %1, i32 0, i32 0
  %.result = load i8** %.host_ref25
  call void @f59_pseudojit_inner(i8* %.i54, i32 %.i54.extent.0, i32 %.i54.extent.1, i32 %.i54.extent.2, i32 %.i54.extent.3, i32 %.i54.stride.0, i32 %.i54.stride.1, i32 %.i54.stride.2, i32 %.i54.stride.3, i32 %.i54.min.0, i32 %.i54.min.1, i32 %.i54.min.2, i32 %.i54.min.3, i8* %.result, i32 %.result.extent.0, i32 %.result.extent.1, i32 %.result.extent.2, i32 %.result.extent.3, i32 %.result.stride.0, i32 %.result.stride.1, i32 %.result.stride.2, i32 %.result.stride.3, i32 %.result.min.0, i32 %.result.min.1, i32 %.result.min.2, i32 %.result.min.3)
  ret void
}

define void @f59_pseudojit_c_wrapper(i8**) {
entry:
  %1 = getelementptr i8** %0, i32 0
  %2 = load i8** %1
  %3 = bitcast i8* %2 to %struct.buffer_t*
  %4 = getelementptr i8** %0, i32 1
  %5 = load i8** %4
  %6 = bitcast i8* %5 to %struct.buffer_t*
  call void @f59_pseudojit(%struct.buffer_t* %3, %struct.buffer_t* %6)
  ret void
}

!0 = metadata !{metadata !"any pointer", metadata !1}
!1 = metadata !{metadata !"omnipotent char", metadata !2}
!2 = metadata !{metadata !"Simple C/C++ TBAA", null}
!3 = metadata !{metadata !"int", metadata !1}
!4 = metadata !{metadata !"bool", metadata !1}
!5 = metadata !{metadata !"long", metadata !1}
