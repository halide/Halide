; ModuleID = '<stdin>'

%struct.buffer_t = type { i8*, i64, i32, i32, [4 x i64], i64 }

@ptx_src_ptr = common global i8* null, align 8

declare void @init()

declare void @dev_malloc_if_missing(%struct.buffer_t*)

declare void @copy_to_dev(%struct.buffer_t*)

declare void @copy_to_host(%struct.buffer_t*)

declare void @dev_run(i32, i32, i32, i32, i32, i32, i32, i8**)

define i32 @kernel_wrapper_tmpl(%struct.buffer_t* %input, %struct.buffer_t* %result, i32 %N) nounwind uwtable ssp {
entry:
  %N.addr = alloca i32, align 4
  %cuArgs = alloca [3 x i8*], align 16
  store i32 %N, i32* %N.addr, align 4
  call void @init() nounwind
  %0 = load i32* %N.addr, align 4
  %sub = add nsw i32 %0, 15
  %div = sdiv i32 %sub, 16
  call void @dev_malloc_if_missing(%struct.buffer_t* %input) nounwind
  call void @dev_malloc_if_missing(%struct.buffer_t* %result) nounwind
  call void @copy_to_dev(%struct.buffer_t* %input) nounwind
  %arrayinit.begin = getelementptr inbounds [3 x i8*]* %cuArgs, i64 0, i64 0
  %dev = getelementptr inbounds %struct.buffer_t* %input, i32 0, i32 1
  %1 = bitcast i64* %dev to i8*
  store i8* %1, i8** %arrayinit.begin
  %arrayinit.element = getelementptr inbounds [3 x i8*]* %cuArgs, i64 0, i64 1
  %dev1 = getelementptr inbounds %struct.buffer_t* %result, i32 0, i32 1
  %2 = bitcast i64* %dev1 to i8*
  store i8* %2, i8** %arrayinit.element
  %arrayinit.element2 = getelementptr inbounds [3 x i8*]* %cuArgs, i64 0, i64 2
  %3 = bitcast i32* %N.addr to i8*
  store i8* %3, i8** %arrayinit.element2
  %arraydecay = getelementptr inbounds [3 x i8*]* %cuArgs, i32 0, i32 0
  call void @dev_run(i32 %div, i32 1, i32 1, i32 16, i32 1, i32 1, i32 0, i8** %arraydecay) nounwind
  call void @copy_to_host(%struct.buffer_t* %result) nounwind
  ret i32 0
}
