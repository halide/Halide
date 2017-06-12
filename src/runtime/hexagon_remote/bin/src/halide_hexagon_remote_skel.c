#ifndef _HALIDE_HEXAGON_REMOTE_SKEL_H
#define _HALIDE_HEXAGON_REMOTE_SKEL_H
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

static const Type types[2];
static const SequenceType sequenceTypes[1] = {{&(types[1]),0x0,0x4,0x4,0x0}};
static const Type types[2] = {{0x1,{{(const uintptr_t)0,(const uintptr_t)0}}, 2,0x1},{SLIM_IFPTR32(0x8,0x10),{{(const uintptr_t)&(types[0]),(const uintptr_t)0x0}}, 9,SLIM_IFPTR32(0x4,0x8)}};
static const Parameter parameters[8] = {{SLIM_IFPTR32(0x8,0x10),{{(const uintptr_t)&(types[0]),(const uintptr_t)0x0}}, 9,SLIM_IFPTR32(0x4,0x8),0,0},{0x4,{{(const uintptr_t)0,(const uintptr_t)0}}, 2,0x4,3,0},{0x4,{{(const uintptr_t)0,(const uintptr_t)0}}, 2,0x4,0,0},{SLIM_IFPTR32(0x8,0x10),{{(const uintptr_t)&(sequenceTypes[0]),0}}, 25,SLIM_IFPTR32(0x4,0x8),0,0},{SLIM_IFPTR32(0x8,0x10),{{(const uintptr_t)&(sequenceTypes[0]),0}}, 25,SLIM_IFPTR32(0x4,0x8),3,0},{SLIM_IFPTR32(0x8,0x10),{{(const uintptr_t)&(types[0]),(const uintptr_t)0x0}}, 9,SLIM_IFPTR32(0x4,0x8),3,0},{0x4,{{(const uintptr_t)0,(const uintptr_t)1}}, 2,0x4,3,0},{0x4,{{(const uintptr_t)0,(const uintptr_t)1}}, 2,0x4,0,0}};
static const Parameter* const parameterArrays[20] = {(&(parameters[7])),(&(parameters[2])),(&(parameters[2])),(&(parameters[7])),(&(parameters[2])),(&(parameters[2])),(&(parameters[7])),(&(parameters[7])),(&(parameters[2])),(&(parameters[2])),(&(parameters[3])),(&(parameters[4])),(&(parameters[3])),(&(parameters[2])),(&(parameters[0])),(&(parameters[1])),(&(parameters[6])),(&(parameters[6])),(&(parameters[5])),(&(parameters[6]))};
static const Method methods[9] = {{REMOTE_SCALARS_MAKEX(0,0,0x2,0x1,0x0,0x0),0x4,0x4,3,2,(&(parameterArrays[14])),0x4,0x4},{REMOTE_SCALARS_MAKEX(0,0,0x2,0x1,0x0,0x0),0x8,0x4,4,3,(&(parameterArrays[13])),0x4,0x4},{REMOTE_SCALARS_MAKEX(0,0,0x0,0x0,0x0,0x0),0x0,0x0,0,0,0,0x0,0x0},{REMOTE_SCALARS_MAKEX(0,0,255,255,15,15),0x14,0x0,9,5,(&(parameterArrays[8])),0x4,0x1},{REMOTE_SCALARS_MAKEX(0,0,0x1,0x0,0x0,0x0),0x4,0x0,1,1,(&(parameterArrays[1])),0x4,0x0},{REMOTE_SCALARS_MAKEX(0,0,0x1,0x2,0x0,0x0),0x4,0x4,4,2,(&(parameterArrays[18])),0x4,0x4},{REMOTE_SCALARS_MAKEX(0,0,0x0,0x1,0x0,0x0),0x0,0x8,2,2,(&(parameterArrays[16])),0x1,0x4},{REMOTE_SCALARS_MAKEX(0,0,0x1,0x0,0x0,0x0),0x4,0x0,1,1,(&(parameterArrays[0])),0x4,0x0},{REMOTE_SCALARS_MAKEX(0,0,0x1,0x0,0x0,0x0),0x20,0x0,8,8,(&(parameterArrays[0])),0x4,0x0}};
static const Method* const methodArrays[10] = {&(methods[0]),&(methods[1]),&(methods[2]),&(methods[2]),&(methods[3]),&(methods[4]),&(methods[5]),&(methods[6]),&(methods[7]),&(methods[8])};
static const char strings[349] = "initialize_kernels_v3\0busbwUsagePercentage\0set_performance_mode\0poll_profiler_state\0release_kernels_v2\0bwMegabytesPerSec\0set_performance\0output_buffers\0mipsPerThread\0input_scalars\0input_buffers\0power_hvx_off\0get_symbol_v4\0power_hvx_on\0set_latency\0set_bus_bw\0module_ptr\0mipsTotal\0read_size\0set_mips\0poll_log\0threads\0sym_ptr\0symbol\0func\0name\0code\0run\0";
static const uint16_t methodStrings[34] = {121,289,152,269,247,103,22,235,239,345,258,323,180,137,166,208,258,335,315,64,330,307,298,303,279,0,340,258,43,59,84,258,194,222};
static const uint16_t methodStringsArrays[10] = {25,15,33,32,9,30,22,19,28,0};
__QAIC_SLIM_EXPORT const Interface __QAIC_SLIM(halide_hexagon_remote_slim) = {10,&(methodArrays[0]),0,0,&(methodStringsArrays [0]),methodStrings,strings};
#endif //_HALIDE_HEXAGON_REMOTE_SLIM_H
extern int adsp_mmap_fd_getinfo(int, uint32_t *);
#ifdef __cplusplus
extern "C" {
#endif
static __inline int _skel_method(int (*_pfn)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd;
   uint32_t _in0[1];
   uint32_t _in1[1];
   uint32_t _in2[1];
   uint32_t _in3[1];
   uint32_t _in4[1];
   uint32_t _in5[1];
   uint32_t _in6[1];
   uint32_t _in7[1];
   uint32_t* _primIn;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, (_pra + ((1 + 0) + (0 + 0))) <= _praEnd);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 32);
   _primIn = _pra[0].buf.pv;
   _COPY(_in0, 0, _primIn, 0, 4);
   _COPY(_in1, 0, _primIn, 4, 4);
   _COPY(_in2, 0, _primIn, 8, 4);
   _COPY(_in3, 0, _primIn, 12, 4);
   _COPY(_in4, 0, _primIn, 16, 4);
   _COPY(_in5, 0, _primIn, 20, 4);
   _COPY(_in6, 0, _primIn, 24, 4);
   _COPY(_in7, 0, _primIn, 28, 4);
   _TRY(_nErr, _pfn(*_in0, *_in1, *_in2, *_in3, *_in4, *_in5, *_in6, *_in7));
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_1(int (*_pfn)(uint32_t), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd;
   uint32_t _in0[1];
   uint32_t* _primIn;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, (_pra + ((1 + 0) + (0 + 0))) <= _praEnd);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 4);
   _primIn = _pra[0].buf.pv;
   _COPY(_in0, 0, _primIn, 0, 4);
   _TRY(_nErr, _pfn(*_in0));
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_2(int (*_pfn)(uint32_t*, uint32_t*), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd;
   uint32_t _rout0[1];
   uint32_t _rout1[1];
   uint32_t* _primROut;
   int _numIn[1];
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, (_pra + ((0 + 1) + (0 + 0))) <= _praEnd);
   _numIn[0] = (REMOTE_SCALARS_INBUFS(_sc) - 0);
   _ASSERT(_nErr, _pra[(_numIn[0] + 0)].buf.nLen >= 8);
   _primROut = _pra[(_numIn[0] + 0)].buf.pv;
   _TRY(_nErr, _pfn(_rout0, _rout1));
   _COPY(_primROut, 0, _rout0, 0, 4);
   _COPY(_primROut, 4, _rout1, 0, 4);
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_3(int (*_pfn)(char*, uint32_t, uint32_t*), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd;
   char* _rout0[1];
   uint32_t _rout0Len[1];
   uint32_t _rout1[1];
   uint32_t* _primIn;
   int _numIn[1];
   uint32_t* _primROut;
   remote_arg* _praIn;
   remote_arg* _praROut;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, (_pra + ((1 + 2) + (0 + 0))) <= _praEnd);
   _numIn[0] = (REMOTE_SCALARS_INBUFS(_sc) - 1);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 4);
   _primIn = _pra[0].buf.pv;
   _ASSERT(_nErr, _pra[(_numIn[0] + 1)].buf.nLen >= 4);
   _primROut = _pra[(_numIn[0] + 1)].buf.pv;
   _COPY(_rout0Len, 0, _primIn, 0, 4);
   _praIn = (_pra + 1);
   _praROut = (_praIn + _numIn[0] + 1);
   _ASSERT(_nErr, (int)((_praROut[0].buf.nLen / 1)) >= (int)(_rout0Len[0]));
   _rout0[0] = _praROut[0].buf.pv;
   _TRY(_nErr, _pfn(*_rout0, *_rout0Len, _rout1));
   _COPY(_primROut, 0, _rout1, 0, 4);
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_pack(remote_arg* _praROutPost, remote_arg* _ppraROutPost[1], void* _primROut, char* _in0[1], uint32_t _in0Len[1]) {
   int _nErr = 0;
   remote_arg* _praROutPostStart = _praROutPost;
   remote_arg** _ppraROutPostStart = _ppraROutPost;
   _ppraROutPost = &_praROutPost;
   _ppraROutPostStart[0] += (_praROutPost - _praROutPostStart) +0;
   return _nErr;
}
static __inline int _skel_pack_1(remote_arg* _praROutPost, remote_arg* _ppraROutPost[1], void* _primROut, char* _rout0[1], uint32_t _rout0Len[1]) {
   int _nErr = 0;
   remote_arg* _praROutPostStart = _praROutPost;
   remote_arg** _ppraROutPostStart = _ppraROutPost;
   _ppraROutPost = &_praROutPost;
   _ppraROutPostStart[0] += (_praROutPost - _praROutPostStart) +1;
   return _nErr;
}
static __inline int _skel_unpack(_allocator* _al, remote_arg* _praIn, remote_arg* _ppraIn[1], remote_arg* _praROut, remote_arg* _ppraROut[1], void* _primIn, void* _primROut, char* _in0[1], uint32_t _in0Len[1]) {
   int _nErr = 0;
   remote_arg* _praInStart = _praIn;
   remote_arg** _ppraInStart = _ppraIn;
   remote_arg* _praROutStart = _praROut;
   remote_arg** _ppraROutStart = _ppraROut;
   _ppraIn = &_praIn;
   _ppraROut = &_praROut;
   _COPY(_in0Len, 0, _primIn, 0, 4);
   _ASSERT(_nErr, (int)((_praIn[0].buf.nLen / 1)) >= (int)(_in0Len[0]));
   _in0[0] = _praIn[0].buf.pv;
   _ppraInStart[0] += (_praIn - _praInStart) + 1;
   _ppraROutStart[0] += (_praROut - _praROutStart) +0;
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_unpack_1(_allocator* _al, remote_arg* _praIn, remote_arg* _ppraIn[1], remote_arg* _praROut, remote_arg* _ppraROut[1], void* _primIn, void* _primROut, char* _rout0[1], uint32_t _rout0Len[1]) {
   int _nErr = 0;
   remote_arg* _praInStart = _praIn;
   remote_arg** _ppraInStart = _ppraIn;
   remote_arg* _praROutStart = _praROut;
   remote_arg** _ppraROutStart = _ppraROut;
   _ppraIn = &_praIn;
   _ppraROut = &_praROut;
   _COPY(_rout0Len, 0, _primIn, 0, 4);
   _ASSERT(_nErr, (int)((_praROut[0].buf.nLen / 1)) >= (int)(_rout0Len[0]));
   _rout0[0] = _praROut[0].buf.pv;
   _ppraInStart[0] += (_praIn - _praInStart) + 0;
   _ppraROutStart[0] += (_praROut - _praROutStart) +1;
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_4(int (*_pfn)(uint32_t, uint32_t, void*, uint32_t, void*, uint32_t, void*, uint32_t), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd;
   uint32_t _in0[1];
   uint32_t _in1[1];
   void* _in2[1];
   uint32_t _in2Len[1];
   void* _rout3[1];
   uint32_t _rout3Len[1];
   void* _in4[1];
   uint32_t _in4Len[1];
   uint32_t* _primIn;
   int _numIn[1];
   remote_arg* _praIn;
   remote_arg* _praROut;
   remote_arg* _praROutPost;
   remote_arg** _ppraROutPost = &_praROutPost;
   _allocator _al[1] = {{0}};
   remote_arg** _ppraIn = &_praIn;
   remote_arg** _ppraROut = &_praROut;
   char* _seq_primIn2;
   char* _seq_nat2;
   int _ii;
   int _nErr = 0;
   char* _seq_primIn3;
   char* _seq_nat3;
   char* _seq_primIn4;
   char* _seq_nat4;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, (_pra + ((4 + 0) + (0 + 0))) <= _praEnd);
   _numIn[0] = (REMOTE_SCALARS_INBUFS(_sc) - 1);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 20);
   _primIn = _pra[0].buf.pv;
   _praIn = (_pra + 1);
   _praROut = (_praIn + _numIn[0] + 0);
   _praROutPost = _praROut;
   _COPY(_in0, 0, _primIn, 0, 4);
   _COPY(_in1, 0, _primIn, 4, 4);
   _COPY(_in2Len, 0, _primIn, 8, 4);
   _allocator_init(_al, 0, 0);
   _ASSERT(_nErr, (int)((_praIn[0].buf.nLen / 4)) >= (int)(_in2Len[0]));
   _ALLOCATE(_nErr, _al, (_in2Len[0] * SLIM_IFPTR32(8, 16)), SLIM_IFPTR32(4, 8), _in2[0]);
   for(_ii = 0, _seq_primIn2 = (char*)_praIn[0].buf.pv, _seq_nat2 = (char*)_in2[0];_ii < (int)_in2Len[0];++_ii, _seq_primIn2 = (_seq_primIn2 + 4), _seq_nat2 = (_seq_nat2 + SLIM_IFPTR32(8, 16)))
   {
      _TRY(_nErr, _skel_unpack(_al, (_praIn + 1), _ppraIn, (_praROut + 0), _ppraROut, _seq_primIn2, 0, SLIM_IFPTR32((char**)&(((uint32_t*)_seq_nat2)[0]), (char**)&(((uint64_t*)_seq_nat2)[0])), SLIM_IFPTR32((uint32_t*)&(((uint32_t*)_seq_nat2)[1]), (uint32_t*)&(((uint32_t*)_seq_nat2)[2]))));
   }
   _COPY(_rout3Len, 0, _primIn, 12, 4);
   _ASSERT(_nErr, (int)((_praIn[1].buf.nLen / 4)) >= (int)(_rout3Len[0]));
   _ALLOCATE(_nErr, _al, (_rout3Len[0] * SLIM_IFPTR32(8, 16)), SLIM_IFPTR32(4, 8), _rout3[0]);
   for(_ii = 0, _seq_primIn3 = (char*)_praIn[1].buf.pv, _seq_nat3 = (char*)_rout3[0];_ii < (int)_rout3Len[0];++_ii, _seq_primIn3 = (_seq_primIn3 + 4), _seq_nat3 = (_seq_nat3 + SLIM_IFPTR32(8, 16)))
   {
      _TRY(_nErr, _skel_unpack_1(_al, (_praIn + 2), _ppraIn, (_praROut + 0), _ppraROut, _seq_primIn3, 0, SLIM_IFPTR32((char**)&(((uint32_t*)_seq_nat3)[0]), (char**)&(((uint64_t*)_seq_nat3)[0])), SLIM_IFPTR32((uint32_t*)&(((uint32_t*)_seq_nat3)[1]), (uint32_t*)&(((uint32_t*)_seq_nat3)[2]))));
   }
   _COPY(_in4Len, 0, _primIn, 16, 4);
   _ASSERT(_nErr, (int)((_praIn[2].buf.nLen / 4)) >= (int)(_in4Len[0]));
   _ALLOCATE(_nErr, _al, (_in4Len[0] * SLIM_IFPTR32(8, 16)), SLIM_IFPTR32(4, 8), _in4[0]);
   for(_ii = 0, _seq_primIn4 = (char*)_praIn[2].buf.pv, _seq_nat4 = (char*)_in4[0];_ii < (int)_in4Len[0];++_ii, _seq_primIn4 = (_seq_primIn4 + 4), _seq_nat4 = (_seq_nat4 + SLIM_IFPTR32(8, 16)))
   {
      _TRY(_nErr, _skel_unpack(_al, (_praIn + 3), _ppraIn, (_praROut + 0), _ppraROut, _seq_primIn4, 0, SLIM_IFPTR32((char**)&(((uint32_t*)_seq_nat4)[0]), (char**)&(((uint64_t*)_seq_nat4)[0])), SLIM_IFPTR32((uint32_t*)&(((uint32_t*)_seq_nat4)[1]), (uint32_t*)&(((uint32_t*)_seq_nat4)[2]))));
   }
   _TRY(_nErr, _pfn(*_in0, *_in1, *_in2, *_in2Len, *_rout3, *_rout3Len, *_in4, *_in4Len));
   for(_ii = 0, _seq_nat2 = (char*)_in2[0];_ii < (int)_in2Len[0];++_ii, _seq_nat2 = (_seq_nat2 + SLIM_IFPTR32(8, 16)))
   {
      _TRY(_nErr, _skel_pack((_praROutPost + 0), _ppraROutPost, 0, SLIM_IFPTR32((char**)&(((uint32_t*)_seq_nat2)[0]), (char**)&(((uint64_t*)_seq_nat2)[0])), SLIM_IFPTR32((uint32_t*)&(((uint32_t*)_seq_nat2)[1]), (uint32_t*)&(((uint32_t*)_seq_nat2)[2]))));
   }
   for(_ii = 0, _seq_nat3 = (char*)_rout3[0];_ii < (int)_rout3Len[0];++_ii, _seq_nat3 = (_seq_nat3 + SLIM_IFPTR32(8, 16)))
   {
      _TRY(_nErr, _skel_pack_1((_praROutPost + 0), _ppraROutPost, 0, SLIM_IFPTR32((char**)&(((uint32_t*)_seq_nat3)[0]), (char**)&(((uint64_t*)_seq_nat3)[0])), SLIM_IFPTR32((uint32_t*)&(((uint32_t*)_seq_nat3)[1]), (uint32_t*)&(((uint32_t*)_seq_nat3)[2]))));
   }
   for(_ii = 0, _seq_nat4 = (char*)_in4[0];_ii < (int)_in4Len[0];++_ii, _seq_nat4 = (_seq_nat4 + SLIM_IFPTR32(8, 16)))
   {
      _TRY(_nErr, _skel_pack((_praROutPost + 0), _ppraROutPost, 0, SLIM_IFPTR32((char**)&(((uint32_t*)_seq_nat4)[0]), (char**)&(((uint64_t*)_seq_nat4)[0])), SLIM_IFPTR32((uint32_t*)&(((uint32_t*)_seq_nat4)[1]), (uint32_t*)&(((uint32_t*)_seq_nat4)[2]))));
   }
   _CATCH(_nErr) {}
   _allocator_deinit(_al);
   return _nErr;
}
static __inline int _skel_method_5(int (*_pfn)(void), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, (_pra + ((0 + 0) + (0 + 0))) <= _praEnd);
   _TRY(_nErr, _pfn());
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_6(int (*_pfn)(uint32_t, char*, uint32_t, uint32_t*), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd;
   uint32_t _in0[1];
   char* _in1[1];
   uint32_t _in1Len[1];
   uint32_t _rout2[1];
   uint32_t* _primIn;
   int _numIn[1];
   uint32_t* _primROut;
   remote_arg* _praIn;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, (_pra + ((2 + 1) + (0 + 0))) <= _praEnd);
   _numIn[0] = (REMOTE_SCALARS_INBUFS(_sc) - 1);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 8);
   _primIn = _pra[0].buf.pv;
   _ASSERT(_nErr, _pra[(_numIn[0] + 1)].buf.nLen >= 4);
   _primROut = _pra[(_numIn[0] + 1)].buf.pv;
   _COPY(_in0, 0, _primIn, 0, 4);
   _COPY(_in1Len, 0, _primIn, 4, 4);
   _praIn = (_pra + 1);
   _ASSERT(_nErr, (int)((_praIn[0].buf.nLen / 1)) >= (int)(_in1Len[0]));
   _in1[0] = _praIn[0].buf.pv;
   _TRY(_nErr, _pfn(*_in0, *_in1, *_in1Len, _rout2));
   _COPY(_primROut, 0, _rout2, 0, 4);
   _CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_7(int (*_pfn)(char*, uint32_t, uint32_t*), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd;
   char* _in0[1];
   uint32_t _in0Len[1];
   uint32_t _rout1[1];
   uint32_t* _primIn;
   int _numIn[1];
   uint32_t* _primROut;
   remote_arg* _praIn;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _ASSERT(_nErr, (_pra + ((2 + 1) + (0 + 0))) <= _praEnd);
   _numIn[0] = (REMOTE_SCALARS_INBUFS(_sc) - 1);
   _ASSERT(_nErr, _pra[0].buf.nLen >= 4);
   _primIn = _pra[0].buf.pv;
   _ASSERT(_nErr, _pra[(_numIn[0] + 1)].buf.nLen >= 4);
   _primROut = _pra[(_numIn[0] + 1)].buf.pv;
   _COPY(_in0Len, 0, _primIn, 0, 4);
   _praIn = (_pra + 1);
   _ASSERT(_nErr, (int)((_praIn[0].buf.nLen / 1)) >= (int)(_in0Len[0]));
   _in0[0] = _praIn[0].buf.pv;
   _TRY(_nErr, _pfn(*_in0, *_in0Len, _rout1));
   _COPY(_primROut, 0, _rout1, 0, 4);
   _CATCH(_nErr) {}
   return _nErr;
}
__QAIC_SKEL_EXPORT int __QAIC_SKEL(halide_hexagon_remote_skel_invoke)(uint32_t _sc, remote_arg* _pra) __QAIC_SKEL_ATTRIBUTE {
   switch(REMOTE_SCALARS_METHOD(_sc))
   {
      case 0:
      return _skel_method_7((void*)__QAIC_IMPL(halide_hexagon_remote_initialize_kernels_v3), _sc, _pra);
      case 1:
      return _skel_method_6((void*)__QAIC_IMPL(halide_hexagon_remote_get_symbol_v4), _sc, _pra);
      case 2:
      return _skel_method_5((void*)__QAIC_IMPL(halide_hexagon_remote_power_hvx_on), _sc, _pra);
      case 3:
      return _skel_method_5((void*)__QAIC_IMPL(halide_hexagon_remote_power_hvx_off), _sc, _pra);
      case 4:
      return _skel_method_4((void*)__QAIC_IMPL(halide_hexagon_remote_run), _sc, _pra);
      case 5:
      return _skel_method_1((void*)__QAIC_IMPL(halide_hexagon_remote_release_kernels_v2), _sc, _pra);
      case 6:
      return _skel_method_3((void*)__QAIC_IMPL(halide_hexagon_remote_poll_log), _sc, _pra);
      case 7:
      return _skel_method_2((void*)__QAIC_IMPL(halide_hexagon_remote_poll_profiler_state), _sc, _pra);
      case 8:
      return _skel_method_1((void*)__QAIC_IMPL(halide_hexagon_remote_set_performance_mode), _sc, _pra);
      case 9:
      return _skel_method((void*)__QAIC_IMPL(halide_hexagon_remote_set_performance), _sc, _pra);
   }
   return AEE_EUNSUPPORTED;
}
#ifdef __cplusplus
}
#endif
#endif //_HALIDE_HEXAGON_REMOTE_SKEL_H
