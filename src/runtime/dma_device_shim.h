#ifndef _DMA_DEVICE_SHIM_H_
#define _DMA_DEVICE_SHIM_H_

using namespace Halide::Runtime::Internal::Qurt;

#ifdef __cplusplus
extern "C" {
#endif

#define CEILING(num, div) ((num + div -1)/(div))
#define ALIGN(x, a) CEILING(x, a) * a

typedef struct
{
	void* handle;
	addr_t hostAddress;
	int frameWidth;
	int frameHeight;
	int frameStride;
	int roiWidth;
	int roiHeight;
	int lumaStride;
	int chromaStride;
	bool read;
	t_eDmaFmt chromaType;
	t_eDmaFmt lumaType;
	int nComponents;
	bool padding;
	bool isUBWC;
	qurt_addr_t desc_address;
	int desc_size;

}dma_tPrepareParams;

typedef struct{
	void* handle;
	int xoffset;
	int yoffset;
	int roiWidth;
	int roiHeight;
	int offset;
	int l2ChromaOffset;
	int nComponents;
	addr_t ping_buffer;
	int  Offset;
}dma_tMoveParams;

typedef struct{
	int u16W;
	int u16H;
}dma_tPixAlignInfo;

/*Check for DMA Driver Availability
 * out: ERR if not available*/
int dma_isDMADriverReady();

/* Get Format Alignment
 * in:t_eDmaFmt NV12/P010
 * in:bool UBWC Type
 * out:dma_tPixAlignInfo
 * out: ERR if not aligned
 */
int dma_getFormatAlignment(t_eDmaFmt fmt, bool isUBWC, dma_tPixAlignInfo &pixAlign);

addr_t dma_lookUpPhysicalAddress(addr_t);

/* Get Minimum ROI Size
 * in:t_eDmaFmt
 * in:bool UBWC Type
 * out:dma_tPixAlignInfo
 * out:int ERR if not aligned
 */
int dma_getMinRoiSize(t_eDmaFmt fmt, bool isUBWC, dma_tPixAlignInfo &pixAlign);

/* Allocate DMA Engine
 * in: t_EDma_WaitType waitType
 * out:void* dmaHandle;
 */
void* dma_allocateDMAEngine();

/* Get Descriptor Size
 * in:t_eDmaFmt* fmtType
 * out:qurt_size_t
 */
qurt_size_t dma_getDescriptorSize(t_eDmaFmt* fmtType, int nComponents,int nFolds);

/* Get Stride
 * in:t_eDmaFmt fmtType
 * in:bool isUBWC
 * in: dma_tPixAlignInfo roiDims
 * out: lumaStride
 */
int dma_getStride(t_eDmaFmt, bool isUBWC, dma_tPixAlignInfo roiDims);

/* Get Memory Pool ID
 * out:qurt_mem_pool_t*
 * out:int ERR if not available
 */
int dma_getMemPoolID(qurt_mem_pool_t* pool_tcm);

/*Allocate Cache for DMA
 *in:qurt_mem_pool_t pool_tcm
 *in:qurt_size_t cache_size
 *out:qurt_addr_t
 */
qurt_addr_t dma_allocateCache(qurt_mem_pool_t pool_tcm, qurt_size_t cache_size, qurt_mem_region_t* region_tcm);

/*Lock Cache for DMA
 * in:qurt_addr_t
 * in:qurt_size_t
 * out: ERR if not available
 */
int dma_lockCache(qurt_addr_t cache_addr, qurt_size_t cache_size);

/* dma Prepare for Transfer
 * in:dma_tPrepareParams
 * out: Err if error occurs
 */
int dma_prepareForTransfer(dma_tPrepareParams params);

/* Blocks new ops till other DMA operations are finished
 * in:void*
 */
int dma_wait(void* handle);

/* DMA Move Data
 * in:dma_tMoveParams moveParams
 * out:return ERR/OK
 */
int dma_moveData(dma_tMoveParams params);

int dma_freeDMA(void* handle);

/*Unlock Cache for DMA
 * in:qurt_addr_t
 * in:qurt_size_t
 * out: ERR if not available
 */
int dma_unlockCache(qurt_addr_t cache_addr, qurt_size_t cache_size);

/* Free DMA
 * out: ERR
 */
int dma_freeDMA(void* handle);

/* Finish Frame
 * out: ERR
 */
int dma_finishFrame(void* handle);

/* MEM Region Delete
 *
 */
void dma_deleteMemRegion(qurt_mem_region_t);

unsigned int dma_getThreadID();

#ifdef __cplusplus
}
#endif

#endif
