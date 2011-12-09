%struct.buffer_t = type { i8*, i64, i32, i32, [4 x i64], i64 }

@ptx_src_ptr = common global i8* null, align 8
@.str = private unnamed_addr constant [9 x i8] c"_im_main\00", align 1

declare void @init()
declare i64 @dev_malloc(i64 %bytes)
declare void @dev_malloc_if_missing(%struct.buffer_t* %buf)
declare void @copy_to_dev(%struct.buffer_t* %buf)
declare void @copy_to_host(%struct.buffer_t* %buf)
declare void @dev_run(i32 %blocksX, i32 %blocksY, i32 %blocksZ, i32 %threadsX, i32 %threadsY, i32 %threadsZ, i32 %shared_mem_bytes, i8** %args)

define i32 @kernel_wrapper_tmpl(%struct.buffer_t* %input, %struct.buffer_t* %result, i32 %N) nounwind uwtable ssp {
entry:
  %input.addr = alloca %struct.buffer_t*, align 8
  %result.addr = alloca %struct.buffer_t*, align 8
  %N.addr = alloca i32, align 4
  %threadsX = alloca i32, align 4
  %threadsY = alloca i32, align 4
  %threadsZ = alloca i32, align 4
  %blocksX = alloca i32, align 4
  %blocksY = alloca i32, align 4
  %blocksZ = alloca i32, align 4
  %cuArgs = alloca [3 x i8*], align 16
  store %struct.buffer_t* %input, %struct.buffer_t** %input.addr, align 8
  store %struct.buffer_t* %result, %struct.buffer_t** %result.addr, align 8
  store i32 %N, i32* %N.addr, align 4
  call void @init()
  store i32 16, i32* %threadsX, align 4
  store i32 1, i32* %threadsY, align 4
  store i32 1, i32* %threadsZ, align 4
  %0 = load i32* %N.addr, align 4
  %1 = load i32* %threadsX, align 4
  %add = add nsw i32 %0, %1
  %sub = sub nsw i32 %add, 1
  %2 = load i32* %threadsX, align 4
  %div = sdiv i32 %sub, %2
  store i32 %div, i32* %blocksX, align 4
  store i32 1, i32* %blocksY, align 4
  store i32 1, i32* %blocksZ, align 4
  %3 = load %struct.buffer_t** %input.addr, align 8
  call void @dev_malloc_if_missing(%struct.buffer_t* %3)
  %4 = load %struct.buffer_t** %result.addr, align 8
  call void @dev_malloc_if_missing(%struct.buffer_t* %4)
  %5 = load %struct.buffer_t** %input.addr, align 8
  call void @copy_to_dev(%struct.buffer_t* %5)
  %arrayinit.begin = getelementptr inbounds [3 x i8*]* %cuArgs, i64 0, i64 0
  %6 = load %struct.buffer_t** %input.addr, align 8
  %dev = getelementptr inbounds %struct.buffer_t* %6, i32 0, i32 1
  %7 = bitcast i64* %dev to i8*
  store i8* %7, i8** %arrayinit.begin
  %arrayinit.element = getelementptr inbounds i8** %arrayinit.begin, i64 1
  %8 = load %struct.buffer_t** %result.addr, align 8
  %dev1 = getelementptr inbounds %struct.buffer_t* %8, i32 0, i32 1
  %9 = bitcast i64* %dev1 to i8*
  store i8* %9, i8** %arrayinit.element
  %arrayinit.element2 = getelementptr inbounds i8** %arrayinit.element, i64 1
  %10 = bitcast i32* %N.addr to i8*
  store i8* %10, i8** %arrayinit.element2
  %11 = load i32* %blocksX, align 4
  %12 = load i32* %blocksY, align 4
  %13 = load i32* %blocksZ, align 4
  %14 = load i32* %threadsX, align 4
  %15 = load i32* %threadsY, align 4
  %16 = load i32* %threadsZ, align 4
  %arraydecay = getelementptr inbounds [3 x i8*]* %cuArgs, i32 0, i32 0
  call void @dev_run(i32 %11, i32 %12, i32 %13, i32 %14, i32 %15, i32 %16, i32 0, i8** %arraydecay)
  %17 = load %struct.buffer_t** %result.addr, align 8
  call void @copy_to_host(%struct.buffer_t* %17)
  ret i32 0
}
