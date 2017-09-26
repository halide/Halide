#ifndef _HALIDE_HEXAGON_REMOTE_STUB_H
#define _HALIDE_HEXAGON_REMOTE_STUB_H
#include "halide_hexagon_remote.h"
#ifndef _QAIC_ENV_H
#define _QAIC_ENV_H

#ifdef __GNUC__
#ifdef __clang__
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#else
#pragma GCC diagnostic ignored "-Wpragmas"
#endif
#pragma GCC diagnostic ignored "-Wuninitialized"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#ifndef _ATTRIBUTE_UNUSED

#ifdef _WIN32
#define _ATTRIBUTE_UNUSED
#else
#define _ATTRIBUTE_UNUSED __attribute__ ((unused))
#endif

#endif // _ATTRIBUTE_UNUSED

#ifndef __QAIC_REMOTE
#define __QAIC_REMOTE(ff) ff
#endif //__QAIC_REMOTE

#ifndef __QAIC_HEADER
#define __QAIC_HEADER(ff) ff
#endif //__QAIC_HEADER

#ifndef __QAIC_HEADER_EXPORT
#define __QAIC_HEADER_EXPORT
#endif // __QAIC_HEADER_EXPORT

#ifndef __QAIC_HEADER_ATTRIBUTE
#define __QAIC_HEADER_ATTRIBUTE
#endif // __QAIC_HEADER_ATTRIBUTE

#ifndef __QAIC_IMPL
#define __QAIC_IMPL(ff) ff
#endif //__QAIC_IMPL

#ifndef __QAIC_IMPL_EXPORT
#define __QAIC_IMPL_EXPORT
#endif // __QAIC_IMPL_EXPORT

#ifndef __QAIC_IMPL_ATTRIBUTE
#define __QAIC_IMPL_ATTRIBUTE
#endif // __QAIC_IMPL_ATTRIBUTE

#ifndef __QAIC_STUB
#define __QAIC_STUB(ff) ff
#endif //__QAIC_STUB

#ifndef __QAIC_STUB_EXPORT
#define __QAIC_STUB_EXPORT
#endif // __QAIC_STUB_EXPORT

#ifndef __QAIC_STUB_ATTRIBUTE
#define __QAIC_STUB_ATTRIBUTE
#endif // __QAIC_STUB_ATTRIBUTE

#ifndef __QAIC_SKEL
#define __QAIC_SKEL(ff) ff
#endif //__QAIC_SKEL__

#ifndef __QAIC_SKEL_EXPORT
#define __QAIC_SKEL_EXPORT
#endif // __QAIC_SKEL_EXPORT

#ifndef __QAIC_SKEL_ATTRIBUTE
#define __QAIC_SKEL_ATTRIBUTE
#endif // __QAIC_SKEL_ATTRIBUTE

#ifdef __QAIC_DEBUG__
   #ifndef __QAIC_DBG_PRINTF__
   #include <stdio.h>
   #define __QAIC_DBG_PRINTF__( ee ) do { printf ee ; } while(0)
   #endif
#else
   #define __QAIC_DBG_PRINTF__( ee ) (void)0
#endif


#define _OFFSET(src, sof)  ((void*)(((char*)(src)) + (sof)))

#define _COPY(dst, dof, src, sof, sz)  \
   do {\
         struct __copy { \
            char ar[sz]; \
         };\
         *(struct __copy*)_OFFSET(dst, dof) = *(struct __copy*)_OFFSET(src, sof);\
   } while (0)

#define _COPYIF(dst, dof, src, sof, sz)  \
   do {\
      if(_OFFSET(dst, dof) != _OFFSET(src, sof)) {\
         _COPY(dst, dof, src, sof, sz); \
      } \
   } while (0)

_ATTRIBUTE_UNUSED
static __inline void _qaic_memmove(void* dst, void* src, int size) {
   int i;
   for(i = 0; i < size; ++i) {
      ((char*)dst)[i] = ((char*)src)[i];
   }
}

#define _MEMMOVEIF(dst, src, sz)  \
   do {\
      if(dst != src) {\
         _qaic_memmove(dst, src, sz);\
      } \
   } while (0)


#define _ASSIGN(dst, src, sof)  \
   do {\
      dst = OFFSET(src, sof); \
   } while (0)

#define _STD_STRLEN_IF(str) (str == 0 ? 0 : strlen(str))

#include "AEEStdErr.h"

#define _TRY(ee, func) \
   do { \
      if (AEE_SUCCESS != ((ee) = func)) {\
         __QAIC_DBG_PRINTF__((__FILE__ ":%d:error:%d:%s\n", __LINE__, (int)(ee),#func));\
         goto ee##bail;\
      } \
   } while (0)

#define _CATCH(exception) exception##bail: if (exception != AEE_SUCCESS)

#define _ASSERT(nErr, ff) _TRY(nErr, 0 == (ff) ? AEE_EBADPARM : AEE_SUCCESS)

#ifdef __QAIC_DEBUG__
#define _ALLOCATE(nErr, pal, size, alignment, pv) _TRY(nErr, _allocator_alloc(pal, __FILE_LINE__, size, alignment, (void**)&pv))
#else
#define _ALLOCATE(nErr, pal, size, alignment, pv) _TRY(nErr, _allocator_alloc(pal, 0, size, alignment, (void**)&pv))
#endif


#endif // _QAIC_ENV_H

#include "remote.h"
#ifndef _ALLOCATOR_H
#define _ALLOCATOR_H

#include <stdlib.h>
#include <stdint.h>

typedef struct _heap _heap;
struct _heap {
   _heap* pPrev;
   const char* loc;
   uint64_t buf;
};

typedef struct _allocator {
   _heap* pheap;
   uint8_t* stack;
   uint8_t* stackEnd;
   int nSize;
} _allocator;

_ATTRIBUTE_UNUSED
static __inline int _heap_alloc(_heap** ppa, const char* loc, int size, void** ppbuf) {
   _heap* pn = 0;
   pn = malloc(size + sizeof(_heap) - sizeof(uint64_t));
   if(pn != 0) {
      pn->pPrev = *ppa;
      pn->loc = loc;
      *ppa = pn;
      *ppbuf = (void*)&(pn->buf);
      return 0;
   } else {
      return -1;
   }
}
#define _ALIGN_SIZE(x, y) (((x) + (y-1)) & ~(y-1))

_ATTRIBUTE_UNUSED
static __inline int _allocator_alloc(_allocator* me,
                                    const char* loc,
                                    int size,
                                    unsigned int al,
                                    void** ppbuf) {
   if(size < 0) {
      return -1;
   } else if (size == 0) {
      *ppbuf = 0;
      return 0;
   }
   if((_ALIGN_SIZE((uintptr_t)me->stackEnd, al) + size) < (uintptr_t)me->stack + me->nSize) {
      *ppbuf = (uint8_t*)_ALIGN_SIZE((uintptr_t)me->stackEnd, al);
      me->stackEnd = (uint8_t*)_ALIGN_SIZE((uintptr_t)me->stackEnd, al) + size;
      return 0;
   } else {
      return _heap_alloc(&me->pheap, loc, size, ppbuf);
   }
}

_ATTRIBUTE_UNUSED
static __inline void _allocator_deinit(_allocator* me) {
   _heap* pa = me->pheap;
   while(pa != 0) {
      _heap* pn = pa;
      const char* loc = pn->loc;
      (void)loc;
      pa = pn->pPrev;
      free(pn);
   }
}

_ATTRIBUTE_UNUSED
static __inline void _allocator_init(_allocator* me, uint8_t* stack, int stackSize) {
   me->stack =  stack;
   me->stackEnd =  stack + stackSize;
   me->nSize = stackSize;
   me->pheap = 0;
}


#endif // _ALLOCATOR_H

#ifndef SLIM_H
#define SLIM_H

#include <stdint.h>

//a C data structure for the idl types that can be used to implement
//static and dynamic language bindings fairly efficiently.
//
//the goal is to have a minimal ROM and RAM footprint and without
//doing too many allocations.  A good way to package these things seemed
//like the module boundary, so all the idls within  one module can share
//all the type references.


#define PARAMETER_IN       0x0
#define PARAMETER_OUT      0x1
#define PARAMETER_INOUT    0x2
#define PARAMETER_ROUT     0x3
#define PARAMETER_INROUT   0x4

//the types that we get from idl
#define TYPE_OBJECT             0x0
#define TYPE_INTERFACE          0x1
#define TYPE_PRIMITIVE          0x2
#define TYPE_ENUM               0x3
#define TYPE_STRING             0x4
#define TYPE_WSTRING            0x5
#define TYPE_STRUCTURE          0x6
#define TYPE_UNION              0x7
#define TYPE_ARRAY              0x8
#define TYPE_SEQUENCE           0x9

//these require the pack/unpack to recurse
//so it's a hint to those languages that can optimize in cases where
//recursion isn't necessary.
#define TYPE_COMPLEX_STRUCTURE  (0x10 | TYPE_STRUCTURE)
#define TYPE_COMPLEX_UNION      (0x10 | TYPE_UNION)
#define TYPE_COMPLEX_ARRAY      (0x10 | TYPE_ARRAY)
#define TYPE_COMPLEX_SEQUENCE   (0x10 | TYPE_SEQUENCE)


typedef struct Type Type;

#define INHERIT_TYPE\
   int32_t nativeSize;                /*in the simple case its the same as wire size and alignment*/\
   union {\
      struct {\
         const uintptr_t         p1;\
         const uintptr_t         p2;\
      } _cast;\
      struct {\
         uint32_t  iid;\
         uint32_t  bNotNil;\
      } object;\
      struct {\
         const Type  *arrayType;\
         int32_t      nItems;\
      } array;\
      struct {\
         const Type *seqType;\
         int32_t      nMaxLen;\
      } seqSimple; \
      struct {\
         uint32_t bFloating;\
         uint32_t bSigned;\
      } prim; \
      const SequenceType* seqComplex;\
      const UnionType  *unionType;\
      const StructType *structType;\
      int32_t         stringMaxLen;\
      uint8_t        bInterfaceNotNil;\
   } param;\
   uint8_t    type;\
   uint8_t    nativeAlignment\

typedef struct UnionType UnionType;
typedef struct StructType StructType;
typedef struct SequenceType SequenceType;
struct Type {
   INHERIT_TYPE;
};

struct SequenceType {
   const Type *         seqType;
   uint32_t               nMaxLen;
   uint32_t               inSize;
   uint32_t               routSizePrimIn;
   uint32_t               routSizePrimROut;
};

//byte offset from the start of the case values for
//this unions case value array.  it MUST be aligned
//at the alignment requrements for the descriptor
//
//if negative it means that the unions cases are
//simple enumerators, so the value read from the descriptor
//can be used directly to find the correct case
typedef union CaseValuePtr CaseValuePtr;
union CaseValuePtr {
   const uint8_t*   value8s;
   const uint16_t*  value16s;
   const uint32_t*  value32s;
   const uint64_t*  value64s;
};

//these are only used in complex cases
//so I pulled them out of the type definition as references to make
//the type smaller
struct UnionType {
   const Type           *descriptor;
   uint32_t               nCases;
   const CaseValuePtr   caseValues;
   const Type * const   *cases;
   int32_t               inSize;
   int32_t               routSizePrimIn;
   int32_t               routSizePrimROut;
   uint8_t                inAlignment;
   uint8_t                routAlignmentPrimIn;
   uint8_t                routAlignmentPrimROut;
   uint8_t                inCaseAlignment;
   uint8_t                routCaseAlignmentPrimIn;
   uint8_t                routCaseAlignmentPrimROut;
   uint8_t                nativeCaseAlignment;
   uint8_t              bDefaultCase;
};

struct StructType {
   uint32_t               nMembers;
   const Type * const   *members;
   int32_t               inSize;
   int32_t               routSizePrimIn;
   int32_t               routSizePrimROut;
   uint8_t                inAlignment;
   uint8_t                routAlignmentPrimIn;
   uint8_t                routAlignmentPrimROut;
};

typedef struct Parameter Parameter;
struct Parameter {
   INHERIT_TYPE;
   uint8_t    mode;
   uint8_t  bNotNil;
};

#define SLIM_IFPTR32(is32,is64) (sizeof(uintptr_t) == 4 ? (is32) : (is64))
#define SLIM_SCALARS_IS_DYNAMIC(u) (((u) & 0x00ffffff) == 0x00ffffff)

typedef struct Method Method;
struct Method {
   uint32_t                    uScalars;            //no method index
   int32_t                     primInSize;
   int32_t                     primROutSize;
   int                         maxArgs;
   int                         numParams;
   const Parameter * const     *params;
   uint8_t                       primInAlignment;
   uint8_t                       primROutAlignment;
};

typedef struct Interface Interface;

struct Interface {
   int                            nMethods;
   const Method  * const          *methodArray;
   int                            nIIds;
   const uint32_t                   *iids;
   const uint16_t*                  methodStringArray;
   const uint16_t*                  methodStrings;
   const char*                    strings;
};


#endif //SLIM_H


#ifndef _HALIDE_HEXAGON_REMOTE_SLIM_H
#define _HALIDE_HEXAGON_REMOTE_SLIM_H
#include "remote.h"
#include <stdint.h>

#ifndef __QAIC_SLIM
#define __QAIC_SLIM(ff) ff
#endif
#ifndef __QAIC_SLIM_EXPORT
#define __QAIC_SLIM_EXPORT
#endif

static const Type types[3];
static const SequenceType sequenceTypes[1] = {{&(types[1]),0x0,0x4,0x4,0x0}};
static const Type types[3] = {{0x1,{{(const uintptr_t)0,(const uintptr_t)0}}, 2,0x1},{SLIM_IFPTR32(0x8,0x10),{{(const uintptr_t)&(types[0]),(const uintptr_t)0x0}}, 9,SLIM_IFPTR32(0x4,0x8)},{0x8,{{(const uintptr_t)0,(const uintptr_t)0}}, 2,0x8}};
static const Parameter parameters[9] = {{SLIM_IFPTR32(0x8,0x10),{{(const uintptr_t)&(types[0]),(const uintptr_t)0x0}}, 9,SLIM_IFPTR32(0x4,0x8),0,0},{0x4,{{(const uintptr_t)0,(const uintptr_t)0}}, 2,0x4,3,0},{0x4,{{(const uintptr_t)0,(const uintptr_t)0}}, 2,0x4,0,0},{SLIM_IFPTR32(0x8,0x10),{{(const uintptr_t)&(sequenceTypes[0]),0}}, 25,SLIM_IFPTR32(0x4,0x8),0,0},{SLIM_IFPTR32(0x8,0x10),{{(const uintptr_t)&(sequenceTypes[0]),0}}, 25,SLIM_IFPTR32(0x4,0x8),3,0},{SLIM_IFPTR32(0x8,0x10),{{(const uintptr_t)&(types[2]),(const uintptr_t)0x0}}, 9,SLIM_IFPTR32(0x4,0x8),0,0},{SLIM_IFPTR32(0x8,0x10),{{(const uintptr_t)&(types[0]),(const uintptr_t)0x0}}, 9,SLIM_IFPTR32(0x4,0x8),3,0},{0x4,{{(const uintptr_t)0,(const uintptr_t)1}}, 2,0x4,3,0},{0x4,{{(const uintptr_t)0,(const uintptr_t)1}}, 2,0x4,0,0}};
static const Parameter* const parameterArrays[23] = {(&(parameters[8])),(&(parameters[2])),(&(parameters[2])),(&(parameters[8])),(&(parameters[2])),(&(parameters[2])),(&(parameters[8])),(&(parameters[8])),(&(parameters[2])),(&(parameters[2])),(&(parameters[3])),(&(parameters[4])),(&(parameters[5])),(&(parameters[2])),(&(parameters[0])),(&(parameters[1])),(&(parameters[0])),(&(parameters[0])),(&(parameters[1])),(&(parameters[7])),(&(parameters[7])),(&(parameters[6])),(&(parameters[7]))};
static const Method methods[9] = {{REMOTE_SCALARS_MAKEX(0,0,0x3,0x1,0x0,0x0),0x8,0x4,5,3,(&(parameterArrays[16])),0x4,0x4},{REMOTE_SCALARS_MAKEX(0,0,0x2,0x1,0x0,0x0),0x8,0x4,4,3,(&(parameterArrays[13])),0x4,0x4},{REMOTE_SCALARS_MAKEX(0,0,0x0,0x0,0x0,0x0),0x0,0x0,0,0,0,0x0,0x0},{REMOTE_SCALARS_MAKEX(0,0,255,255,15,15),0x14,0x0,9,5,(&(parameterArrays[8])),0x4,0x1},{REMOTE_SCALARS_MAKEX(0,0,0x1,0x0,0x0,0x0),0x4,0x0,1,1,(&(parameterArrays[1])),0x4,0x0},{REMOTE_SCALARS_MAKEX(0,0,0x1,0x2,0x0,0x0),0x4,0x4,4,2,(&(parameterArrays[21])),0x4,0x4},{REMOTE_SCALARS_MAKEX(0,0,0x0,0x1,0x0,0x0),0x0,0x8,2,2,(&(parameterArrays[19])),0x1,0x4},{REMOTE_SCALARS_MAKEX(0,0,0x1,0x0,0x0,0x0),0x4,0x0,1,1,(&(parameterArrays[0])),0x4,0x0},{REMOTE_SCALARS_MAKEX(0,0,0x1,0x0,0x0,0x0),0x20,0x0,8,8,(&(parameterArrays[0])),0x4,0x0}};
static const Method* const methodArrays[10] = {&(methods[0]),&(methods[1]),&(methods[2]),&(methods[2]),&(methods[3]),&(methods[4]),&(methods[5]),&(methods[6]),&(methods[7]),&(methods[8])};
static const char strings[336] = "busbwUsagePercentage\0set_performance_mode\0poll_profiler_state\0bwMegabytesPerSec\0set_performance\0release_library\0output_buffers\0mipsPerThread\0input_buffers\0power_hvx_off\0get_symbol_v4\0power_hvx_on\0load_library\0set_latency\0set_bus_bw\0module_ptr\0mipsTotal\0read_size\0set_mips\0poll_log\0threads\0scalars\0sym_ptr\0symbol\0run_v2\0soname\0func\0code\0";
static const uint16_t methodStrings[35] = {80,263,127,243,221,62,0,209,213,312,232,305,141,112,289,169,232,321,297,196,319,331,232,42,326,281,272,277,253,21,37,96,232,155,183};
static const uint16_t methodStringsArrays[10] = {19,15,34,33,9,31,26,23,29,0};
__QAIC_SLIM_EXPORT const Interface __QAIC_SLIM(halide_hexagon_remote_slim) = {10,&(methodArrays[0]),0,0,&(methodStringsArrays [0]),methodStrings,strings};
#endif //_HALIDE_HEXAGON_REMOTE_SLIM_H
#ifdef __cplusplus
extern "C" {
#endif

#ifndef _const_halide_hexagon_remote_handle
#define _const_halide_hexagon_remote_handle ((remote_handle)-1)
#endif //_const_halide_hexagon_remote_handle

static void _halide_hexagon_remote_pls_dtor(void* data) {
   remote_handle* ph = (remote_handle*)data;
   if(_const_halide_hexagon_remote_handle != *ph) {
      (void)__QAIC_REMOTE(remote_handle_close)(*ph);
      *ph = _const_halide_hexagon_remote_handle;
   }
}

static int _halide_hexagon_remote_pls_ctor(void* ctx, void* data) {
   remote_handle* ph = (remote_handle*)data;
   *ph = _const_halide_hexagon_remote_handle;
   if(*ph == (remote_handle)-1) {
      return __QAIC_REMOTE(remote_handle_open)((const char*)ctx, ph);
   }
   return 0;
}

#if (defined __qdsp6__) || (defined __hexagon__)
#pragma weak  adsp_pls_add_lookup
extern int adsp_pls_add_lookup(uint32_t type, uint32_t key, int size, int (*ctor)(void* ctx, void* data), void* ctx, void (*dtor)(void* ctx), void** ppo);
#pragma weak  HAP_pls_add_lookup
extern int HAP_pls_add_lookup(uint32_t type, uint32_t key, int size, int (*ctor)(void* ctx, void* data), void* ctx, void (*dtor)(void* ctx), void** ppo);

__QAIC_STUB_EXPORT remote_handle _halide_hexagon_remote_handle(void) {
   remote_handle* ph;
   if(adsp_pls_add_lookup) {
      if(0 == adsp_pls_add_lookup((uint32_t)_halide_hexagon_remote_handle, 0, sizeof(*ph),  _halide_hexagon_remote_pls_ctor, "halide_hexagon_remote",  _halide_hexagon_remote_pls_dtor, (void**)&ph))  {
         return *ph;
      }
      return (remote_handle)-1;
   } else if(HAP_pls_add_lookup) {
      if(0 == HAP_pls_add_lookup((uint32_t)_halide_hexagon_remote_handle, 0, sizeof(*ph),  _halide_hexagon_remote_pls_ctor, "halide_hexagon_remote",  _halide_hexagon_remote_pls_dtor, (void**)&ph))  {
         return *ph;
      }
      return (remote_handle)-1;
   }
   return(remote_handle)-1;
}

#else //__qdsp6__ || __hexagon__

uint32_t _halide_hexagon_remote_atomic_CompareAndExchange(uint32_t * volatile puDest, uint32_t uExchange, uint32_t uCompare);

#ifdef _WIN32
#include "Windows.h"
uint32_t _halide_hexagon_remote_atomic_CompareAndExchange(uint32_t * volatile puDest, uint32_t uExchange, uint32_t uCompare) {
   return (uint32_t)InterlockedCompareExchange((volatile LONG*)puDest, (LONG)uExchange, (LONG)uCompare);
}
#elif __GNUC__
uint32_t _halide_hexagon_remote_atomic_CompareAndExchange(uint32_t * volatile puDest, uint32_t uExchange, uint32_t uCompare) {
   return __sync_val_compare_and_swap(puDest, uCompare, uExchange);
}
#endif //_WIN32


__QAIC_STUB_EXPORT remote_handle _halide_hexagon_remote_handle(void) {
   static remote_handle handle = _const_halide_hexagon_remote_handle;
   if((remote_handle)-1 != handle) {
      return handle;
   } else {
      remote_handle tmp;
      int nErr = _halide_hexagon_remote_pls_ctor("halide_hexagon_remote", (void*)&tmp);
      if(nErr) {
         return (remote_handle)-1;
      }
      if(((remote_handle)-1 != handle) || ((remote_handle)-1 != (remote_handle)_halide_hexagon_remote_atomic_CompareAndExchange((uint32_t*)&handle, (uint32_t)tmp, (uint32_t)-1))) {
         _halide_hexagon_remote_pls_dtor(&tmp);
      }
      return handle;
   }
}

#endif //__qdsp6__

__QAIC_STUB_EXPORT int __QAIC_STUB(halide_hexagon_remote_skel_invoke)(uint32_t _sc, remote_arg* _pra) __QAIC_STUB_ATTRIBUTE {
   return __QAIC_REMOTE(remote_handle_invoke)(_halide_hexagon_remote_handle(), _sc, _pra);
}

#ifdef __cplusplus
}
#endif


#ifdef __cplusplus
extern "C" {
#endif
extern int remote_register_dma_handle(int, uint32_t);
static __inline int _stub_method(remote_handle _handle, uint32_t _mid, char* _in0[1], uint32_t _in0Len[1], char* _in1[1], uint32_t _in1Len[1], uint32_t _rout2[1]) {
   int _numIn[1];
   remote_arg _pra[4];
   uint32_t _primIn[2];
   uint32_t _primROut[1];
   remote_arg* _praIn;
   int _nErr = 0;
   _numIn[0] = 2;
   _pra[0].buf.pv = (void*)_primIn;
   _pra[0].buf.nLen = sizeof(_primIn);
   _pra[(_numIn[0] + 1)].buf.pv = (void*)_primROut;
   _pra[(_numIn[0] + 1)].buf.nLen = sizeof(_primROut);
   _COPY(_primIn, 0, _in0Len, 0, 4);
   _praIn = (_pra + 1);
   _praIn[0].buf.pv = _in0[0];
   _praIn[0].buf.nLen = (1 * _in0Len[0]);
   _COPY(_primIn, 4, _in1Len, 0, 4);
   _praIn[1].buf.pv = _in1[0];
   _praIn[1].buf.nLen = (1 * _in1Len[0]);
   _TRY(_nErr, __QAIC_REMOTE(remote_handle_invoke)(_handle, REMOTE_SCALARS_MAKEX(0, _mid, 3, 1, 0, 0), _pra));
   _COPY(_rout2, 0, _primROut, 0, 4);
   _CATCH(_nErr) {}
   return _nErr;
}
__QAIC_STUB_EXPORT int __QAIC_STUB(halide_hexagon_remote_load_library)(const char* soname, int sonameLen, const unsigned char* code, int codeLen, halide_hexagon_remote_handle_t* module_ptr) __QAIC_STUB_ATTRIBUTE {
   uint32_t _mid = 0;
   return _stub_method(_halide_hexagon_remote_handle(), _mid, (char**)&soname, (uint32_t*)&sonameLen, (char**)&code, (uint32_t*)&codeLen, (uint32_t*)module_ptr);
}
static __inline int _stub_method_1(remote_handle _handle, uint32_t _mid, uint32_t _in0[1], char* _in1[1], uint32_t _in1Len[1], uint32_t _rout2[1]) {
   int _numIn[1];
   remote_arg _pra[3];
   uint32_t _primIn[2];
   uint32_t _primROut[1];
   remote_arg* _praIn;
   int _nErr = 0;
   _numIn[0] = 1;
   _pra[0].buf.pv = (void*)_primIn;
   _pra[0].buf.nLen = sizeof(_primIn);
   _pra[(_numIn[0] + 1)].buf.pv = (void*)_primROut;
   _pra[(_numIn[0] + 1)].buf.nLen = sizeof(_primROut);
   _COPY(_primIn, 0, _in0, 0, 4);
   _COPY(_primIn, 4, _in1Len, 0, 4);
   _praIn = (_pra + 1);
   _praIn[0].buf.pv = _in1[0];
   _praIn[0].buf.nLen = (1 * _in1Len[0]);
   _TRY(_nErr, __QAIC_REMOTE(remote_handle_invoke)(_handle, REMOTE_SCALARS_MAKEX(0, _mid, 2, 1, 0, 0), _pra));
   _COPY(_rout2, 0, _primROut, 0, 4);
   _CATCH(_nErr) {}
   return _nErr;
}
__QAIC_STUB_EXPORT int __QAIC_STUB(halide_hexagon_remote_get_symbol_v4)(halide_hexagon_remote_handle_t module_ptr, const char* name, int nameLen, halide_hexagon_remote_handle_t* sym_ptr) __QAIC_STUB_ATTRIBUTE {
   uint32_t _mid = 1;
   return _stub_method_1(_halide_hexagon_remote_handle(), _mid, (uint32_t*)&module_ptr, (char**)&name, (uint32_t*)&nameLen, (uint32_t*)sym_ptr);
}
static __inline int _stub_method_2(remote_handle _handle, uint32_t _mid) {
   remote_arg* _pra = 0;
   int _nErr = 0;
   _TRY(_nErr, __QAIC_REMOTE(remote_handle_invoke)(_handle, REMOTE_SCALARS_MAKEX(0, _mid, 0, 0, 0, 0), _pra));
   _CATCH(_nErr) {}
   return _nErr;
}
__QAIC_STUB_EXPORT int __QAIC_STUB(halide_hexagon_remote_power_hvx_on)(void) __QAIC_STUB_ATTRIBUTE {
   uint32_t _mid = 2;
   return _stub_method_2(_halide_hexagon_remote_handle(), _mid);
}
__QAIC_STUB_EXPORT int __QAIC_STUB(halide_hexagon_remote_power_hvx_off)(void) __QAIC_STUB_ATTRIBUTE {
   uint32_t _mid = 3;
   return _stub_method_2(_halide_hexagon_remote_handle(), _mid);
}
static __inline int _stub_unpack(remote_arg* _praROutPost, remote_arg* _ppraROutPost[1], void* _primROut, char* _rout0[1], uint32_t _rout0Len[1]) {
   int _nErr = 0;
   remote_arg* _praROutPostStart = _praROutPost;
   remote_arg** _ppraROutPostStart = _ppraROutPost;
   _ppraROutPost = &_praROutPost;
   _ppraROutPostStart[0] += (_praROutPost - _praROutPostStart) +1;
   return _nErr;
}
static __inline int _stub_unpack_1(remote_arg* _praROutPost, remote_arg* _ppraROutPost[1], void* _primROut, char* _in0[1], uint32_t _in0Len[1]) {
   int _nErr = 0;
   remote_arg* _praROutPostStart = _praROutPost;
   remote_arg** _ppraROutPostStart = _ppraROutPost;
   _ppraROutPost = &_praROutPost;
   _ppraROutPostStart[0] += (_praROutPost - _praROutPostStart) +0;
   return _nErr;
}
static __inline int _stub_pack(_allocator* _al, remote_arg* _praIn, remote_arg* _ppraIn[1], remote_arg* _praROut, remote_arg* _ppraROut[1], remote_arg* _praHIn, remote_arg* _ppraHIn[1], remote_arg* _praHROut, remote_arg* _ppraHROut[1], void* _primIn, void* _primROut, char* _rout0[1], uint32_t _rout0Len[1]) {
   int _nErr = 0;
   remote_arg* _praInStart = _praIn;
   remote_arg** _ppraInStart = _ppraIn;
   remote_arg* _praROutStart = _praROut;
   remote_arg** _ppraROutStart = _ppraROut;
   _ppraIn = &_praIn;
   _ppraROut = &_praROut;
   _COPY(_primIn, 0, _rout0Len, 0, 4);
   _praROut[0].buf.pv = _rout0[0];
   _praROut[0].buf.nLen = (1 * _rout0Len[0]);
   _ppraInStart[0] += (_praIn - _praInStart) + 0;
   _ppraROutStart[0] += (_praROut - _praROutStart) +1;
   return _nErr;
}
static __inline int _stub_pack_1(_allocator* _al, remote_arg* _praIn, remote_arg* _ppraIn[1], remote_arg* _praROut, remote_arg* _ppraROut[1], remote_arg* _praHIn, remote_arg* _ppraHIn[1], remote_arg* _praHROut, remote_arg* _ppraHROut[1], void* _primIn, void* _primROut, char* _in0[1], uint32_t _in0Len[1]) {
   int _nErr = 0;
   remote_arg* _praInStart = _praIn;
   remote_arg** _ppraInStart = _ppraIn;
   remote_arg* _praROutStart = _praROut;
   remote_arg** _ppraROutStart = _ppraROut;
   _ppraIn = &_praIn;
   _ppraROut = &_praROut;
   _COPY(_primIn, 0, _in0Len, 0, 4);
   _praIn[0].buf.pv = _in0[0];
   _praIn[0].buf.nLen = (1 * _in0Len[0]);
   _ppraInStart[0] += (_praIn - _praInStart) + 1;
   _ppraROutStart[0] += (_praROut - _praROutStart) +0;
   return _nErr;
}
static __inline void _count(int _numIn[1], int _numROut[1], int _numInH[1], int _numROutH[1], char* _rout0[1], uint32_t _rout0Len[1]) {
   _numIn[0] += 0;
   _numROut[0] += 1;
   _numInH[0] += 0;
   _numROutH[0] += 0;
}
static __inline void _count_1(int _numIn[1], int _numROut[1], int _numInH[1], int _numROutH[1], char* _in0[1], uint32_t _in0Len[1]) {
   _numIn[0] += 1;
   _numROut[0] += 0;
   _numInH[0] += 0;
   _numROutH[0] += 0;
}
static __inline int _stub_method_3(remote_handle _handle, uint32_t _mid, uint32_t _in0[1], uint32_t _in1[1], void* _in2[1], uint32_t _in2Len[1], void* _rout3[1], uint32_t _rout3Len[1], char* _in4[1], uint32_t _in4Len[1]) {
   remote_arg* _pra;
   int _numIn[1];
   int _numROut[1];
   int _numInH[1];
   int _numROutH[1];
   char* _seq_nat2;
   int _ii;
   char* _seq_nat3;
   _allocator _al[1] = {{0}};
   uint32_t _primIn[5];
   remote_arg* _praIn;
   remote_arg* _praROut;
   remote_arg* _praROutPost;
   remote_arg** _ppraROutPost = &_praROutPost;
   remote_arg** _ppraIn = &_praIn;
   remote_arg** _ppraROut = &_praROut;
   remote_arg* _praHIn = 0;
   remote_arg** _ppraHIn = &_praHIn;
   remote_arg* _praHROut = 0;
   remote_arg** _ppraHROut = &_praHROut;
   char* _seq_primIn2;
   int _nErr = 0;
   char* _seq_primIn3;
   _numIn[0] = 3;
   _numROut[0] = 0;
   _numInH[0] = 0;
   _numROutH[0] = 0;
   for(_ii = 0, _seq_nat2 = (char*)_in2[0];_ii < (int)_in2Len[0];++_ii, _seq_nat2 = (_seq_nat2 + SLIM_IFPTR32(8, 16)))
   {
      _count_1(_numIn, _numROut, _numInH, _numROutH, SLIM_IFPTR32((char**)&(((uint32_t*)_seq_nat2)[0]), (char**)&(((uint64_t*)_seq_nat2)[0])), SLIM_IFPTR32((uint32_t*)&(((uint32_t*)_seq_nat2)[1]), (uint32_t*)&(((uint32_t*)_seq_nat2)[2])));
   }
   for(_ii = 0, _seq_nat3 = (char*)_rout3[0];_ii < (int)_rout3Len[0];++_ii, _seq_nat3 = (_seq_nat3 + SLIM_IFPTR32(8, 16)))
   {
      _count(_numIn, _numROut, _numInH, _numROutH, SLIM_IFPTR32((char**)&(((uint32_t*)_seq_nat3)[0]), (char**)&(((uint64_t*)_seq_nat3)[0])), SLIM_IFPTR32((uint32_t*)&(((uint32_t*)_seq_nat3)[1]), (uint32_t*)&(((uint32_t*)_seq_nat3)[2])));
   }
   _allocator_init(_al, 0, 0);
   _ALLOCATE(_nErr, _al, ((((((((_numIn[0] + _numROut[0]) + _numInH[0]) + _numROutH[0]) + 1) + 0) + 0) + 0) * sizeof(_pra[0])), 4, _pra);
   _pra[0].buf.pv = (void*)_primIn;
   _pra[0].buf.nLen = sizeof(_primIn);
   _praIn = (_pra + 1);
   _praROut = (_praIn + _numIn[0] + 0);
   _praROutPost = _praROut;
   _COPY(_primIn, 0, _in0, 0, 4);
   _COPY(_primIn, 4, _in1, 0, 4);
   _COPY(_primIn, 8, _in2Len, 0, 4);
   if(_praHIn == 0)
   {
      _praHIn = ((_praROut + _numROut[0]) + 0);
   }
   if(_praHROut == 0)
      (_praHROut = _praHIn + _numInH[0] + 0);
   _ALLOCATE(_nErr, _al, (_in2Len[0] * 4), 4, _praIn[0].buf.pv);
   _praIn[0].buf.nLen = (4 * _in2Len[0]);
   for(_ii = 0, _seq_primIn2 = (char*)_praIn[0].buf.pv, _seq_nat2 = (char*)_in2[0];_ii < (int)_in2Len[0];++_ii, _seq_primIn2 = (_seq_primIn2 + 4), _seq_nat2 = (_seq_nat2 + SLIM_IFPTR32(8, 16)))
   {
      _TRY(_nErr, _stub_pack_1(_al, (_praIn + 1), _ppraIn, (_praROut + 0), _ppraROut, _praHIn, _ppraHIn, _praHROut, _ppraHROut, _seq_primIn2, 0, SLIM_IFPTR32((char**)&(((uint32_t*)_seq_nat2)[0]), (char**)&(((uint64_t*)_seq_nat2)[0])), SLIM_IFPTR32((uint32_t*)&(((uint32_t*)_seq_nat2)[1]), (uint32_t*)&(((uint32_t*)_seq_nat2)[2]))));
   }
   _COPY(_primIn, 12, _rout3Len, 0, 4);
   _ALLOCATE(_nErr, _al, (_rout3Len[0] * 4), 4, _praIn[1].buf.pv);
   _praIn[1].buf.nLen = (4 * _rout3Len[0]);
   for(_ii = 0, _seq_primIn3 = (char*)_praIn[1].buf.pv, _seq_nat3 = (char*)_rout3[0];_ii < (int)_rout3Len[0];++_ii, _seq_primIn3 = (_seq_primIn3 + 4), _seq_nat3 = (_seq_nat3 + SLIM_IFPTR32(8, 16)))
   {
      _TRY(_nErr, _stub_pack(_al, (_praIn + 2), _ppraIn, (_praROut + 0), _ppraROut, _praHIn, _ppraHIn, _praHROut, _ppraHROut, _seq_primIn3, 0, SLIM_IFPTR32((char**)&(((uint32_t*)_seq_nat3)[0]), (char**)&(((uint64_t*)_seq_nat3)[0])), SLIM_IFPTR32((uint32_t*)&(((uint32_t*)_seq_nat3)[1]), (uint32_t*)&(((uint32_t*)_seq_nat3)[2]))));
   }
   _COPY(_primIn, 16, _in4Len, 0, 4);
   _praIn[2].buf.pv = _in4[0];
   _praIn[2].buf.nLen = (8 * _in4Len[0]);
   _ASSERT(_nErr, (_numInH[0] + 0) <= 15);
   _ASSERT(_nErr, (_numROutH[0] + 0) <= 15);
   _TRY(_nErr, __QAIC_REMOTE(remote_handle_invoke)(_handle, REMOTE_SCALARS_MAKEX(0, _mid, (_numIn[0] + 1), (_numROut[0] + 0), (_numInH[0] + 0), (_numROutH[0] + 0)), _pra));
   for(_ii = 0, _seq_nat2 = (char*)_in2[0];_ii < (int)_in2Len[0];++_ii, _seq_nat2 = (_seq_nat2 + SLIM_IFPTR32(8, 16)))
   {
      _TRY(_nErr, _stub_unpack_1((_praROutPost + 0), _ppraROutPost, 0, SLIM_IFPTR32((char**)&(((uint32_t*)_seq_nat2)[0]), (char**)&(((uint64_t*)_seq_nat2)[0])), SLIM_IFPTR32((uint32_t*)&(((uint32_t*)_seq_nat2)[1]), (uint32_t*)&(((uint32_t*)_seq_nat2)[2]))));
   }
   for(_ii = 0, _seq_nat3 = (char*)_rout3[0];_ii < (int)_rout3Len[0];++_ii, _seq_nat3 = (_seq_nat3 + SLIM_IFPTR32(8, 16)))
   {
      _TRY(_nErr, _stub_unpack((_praROutPost + 0), _ppraROutPost, 0, SLIM_IFPTR32((char**)&(((uint32_t*)_seq_nat3)[0]), (char**)&(((uint64_t*)_seq_nat3)[0])), SLIM_IFPTR32((uint32_t*)&(((uint32_t*)_seq_nat3)[1]), (uint32_t*)&(((uint32_t*)_seq_nat3)[2]))));
   }
   _CATCH(_nErr) {}
   _allocator_deinit(_al);
   return _nErr;
}
__QAIC_STUB_EXPORT int __QAIC_STUB(halide_hexagon_remote_run_v2)(halide_hexagon_remote_handle_t module_ptr, halide_hexagon_remote_handle_t symbol, const halide_hexagon_remote_buffer* input_buffers, int input_buffersLen, halide_hexagon_remote_buffer* output_buffers, int output_buffersLen, const halide_hexagon_remote_scalar_t* scalars, int scalarsLen) __QAIC_STUB_ATTRIBUTE {
   uint32_t _mid = 4;
   return _stub_method_3(_halide_hexagon_remote_handle(), _mid, (uint32_t*)&module_ptr, (uint32_t*)&symbol, (void**)&input_buffers, (uint32_t*)&input_buffersLen, (void**)&output_buffers, (uint32_t*)&output_buffersLen, (char**)&scalars, (uint32_t*)&scalarsLen);
}
static __inline int _stub_method_4(remote_handle _handle, uint32_t _mid, uint32_t _in0[1]) {
   remote_arg _pra[1];
   uint32_t _primIn[1];
   int _nErr = 0;
   _pra[0].buf.pv = (void*)_primIn;
   _pra[0].buf.nLen = sizeof(_primIn);
   _COPY(_primIn, 0, _in0, 0, 4);
   _TRY(_nErr, __QAIC_REMOTE(remote_handle_invoke)(_handle, REMOTE_SCALARS_MAKEX(0, _mid, 1, 0, 0, 0), _pra));
   _CATCH(_nErr) {}
   return _nErr;
}
__QAIC_STUB_EXPORT int __QAIC_STUB(halide_hexagon_remote_release_library)(halide_hexagon_remote_handle_t module_ptr) __QAIC_STUB_ATTRIBUTE {
   uint32_t _mid = 5;
   return _stub_method_4(_halide_hexagon_remote_handle(), _mid, (uint32_t*)&module_ptr);
}
static __inline int _stub_method_5(remote_handle _handle, uint32_t _mid, char* _rout0[1], uint32_t _rout0Len[1], uint32_t _rout1[1]) {
   int _numIn[1];
   remote_arg _pra[3];
   uint32_t _primIn[1];
   uint32_t _primROut[1];
   remote_arg* _praIn;
   remote_arg* _praROut;
   int _nErr = 0;
   _numIn[0] = 0;
   _pra[0].buf.pv = (void*)_primIn;
   _pra[0].buf.nLen = sizeof(_primIn);
   _pra[(_numIn[0] + 1)].buf.pv = (void*)_primROut;
   _pra[(_numIn[0] + 1)].buf.nLen = sizeof(_primROut);
   _COPY(_primIn, 0, _rout0Len, 0, 4);
   _praIn = (_pra + 1);
   _praROut = (_praIn + _numIn[0] + 1);
   _praROut[0].buf.pv = _rout0[0];
   _praROut[0].buf.nLen = (1 * _rout0Len[0]);
   _TRY(_nErr, __QAIC_REMOTE(remote_handle_invoke)(_handle, REMOTE_SCALARS_MAKEX(0, _mid, 1, 2, 0, 0), _pra));
   _COPY(_rout1, 0, _primROut, 0, 4);
   _CATCH(_nErr) {}
   return _nErr;
}
__QAIC_STUB_EXPORT int __QAIC_STUB(halide_hexagon_remote_poll_log)(char* log, int logLen, int* read_size) __QAIC_STUB_ATTRIBUTE {
   uint32_t _mid = 6;
   return _stub_method_5(_halide_hexagon_remote_handle(), _mid, (char**)&log, (uint32_t*)&logLen, (uint32_t*)read_size);
}
static __inline int _stub_method_6(remote_handle _handle, uint32_t _mid, uint32_t _rout0[1], uint32_t _rout1[1]) {
   int _numIn[1];
   remote_arg _pra[1];
   uint32_t _primROut[2];
   int _nErr = 0;
   _numIn[0] = 0;
   _pra[(_numIn[0] + 0)].buf.pv = (void*)_primROut;
   _pra[(_numIn[0] + 0)].buf.nLen = sizeof(_primROut);
   _TRY(_nErr, __QAIC_REMOTE(remote_handle_invoke)(_handle, REMOTE_SCALARS_MAKEX(0, _mid, 0, 1, 0, 0), _pra));
   _COPY(_rout0, 0, _primROut, 0, 4);
   _COPY(_rout1, 0, _primROut, 4, 4);
   _CATCH(_nErr) {}
   return _nErr;
}
__QAIC_STUB_EXPORT int __QAIC_STUB(halide_hexagon_remote_poll_profiler_state)(int* func, int* threads) __QAIC_STUB_ATTRIBUTE {
   uint32_t _mid = 7;
   return _stub_method_6(_halide_hexagon_remote_handle(), _mid, (uint32_t*)func, (uint32_t*)threads);
}
__QAIC_STUB_EXPORT int __QAIC_STUB(halide_hexagon_remote_set_performance_mode)(int mode) __QAIC_STUB_ATTRIBUTE {
   uint32_t _mid = 8;
   return _stub_method_4(_halide_hexagon_remote_handle(), _mid, (uint32_t*)&mode);
}
static __inline int _stub_method_7(remote_handle _handle, uint32_t _mid, uint32_t _in0[1], uint32_t _in1[1], uint32_t _in2[1], uint32_t _in3[1], uint32_t _in4[1], uint32_t _in5[1], uint32_t _in6[1], uint32_t _in7[1]) {
   remote_arg _pra[1];
   uint32_t _primIn[8];
   int _nErr = 0;
   _pra[0].buf.pv = (void*)_primIn;
   _pra[0].buf.nLen = sizeof(_primIn);
   _COPY(_primIn, 0, _in0, 0, 4);
   _COPY(_primIn, 4, _in1, 0, 4);
   _COPY(_primIn, 8, _in2, 0, 4);
   _COPY(_primIn, 12, _in3, 0, 4);
   _COPY(_primIn, 16, _in4, 0, 4);
   _COPY(_primIn, 20, _in5, 0, 4);
   _COPY(_primIn, 24, _in6, 0, 4);
   _COPY(_primIn, 28, _in7, 0, 4);
   _TRY(_nErr, __QAIC_REMOTE(remote_handle_invoke)(_handle, REMOTE_SCALARS_MAKEX(0, _mid, 1, 0, 0, 0), _pra));
   _CATCH(_nErr) {}
   return _nErr;
}
__QAIC_STUB_EXPORT int __QAIC_STUB(halide_hexagon_remote_set_performance)(int set_mips, unsigned int mipsPerThread, unsigned int mipsTotal, int set_bus_bw, unsigned int bwMegabytesPerSec, unsigned int busbwUsagePercentage, int set_latency, int latency) __QAIC_STUB_ATTRIBUTE {
   uint32_t _mid = 9;
   return _stub_method_7(_halide_hexagon_remote_handle(), _mid, (uint32_t*)&set_mips, (uint32_t*)&mipsPerThread, (uint32_t*)&mipsTotal, (uint32_t*)&set_bus_bw, (uint32_t*)&bwMegabytesPerSec, (uint32_t*)&busbwUsagePercentage, (uint32_t*)&set_latency, (uint32_t*)&latency);
}
#ifdef __cplusplus
}
#endif
#endif //_HALIDE_HEXAGON_REMOTE_STUB_H
