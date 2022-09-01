declare ptr @llvm.threadlocal.address.p0(ptr) mustprogress nocallback nofree nosync nounwind readnone speculatable willreturn

@current_user_context = weak thread_local global ptr null, align 8

define weak_odr ptr @halide_get_thread_local_user_context() nounwind alwaysinline {
entry:
  %0 = tail call ptr @llvm.threadlocal.address.p0(ptr nonnull @current_user_context)
  %1 = load ptr, ptr %0, align 8
  ret ptr %1
}

define weak_odr void @halide_set_thread_local_user_context(ptr noundef %user_context) nounwind alwaysinline {
entry:
  %0 = tail call ptr @llvm.threadlocal.address.p0(ptr nonnull @current_user_context)
  store ptr %user_context, ptr %0, align 8
  ret void
}

