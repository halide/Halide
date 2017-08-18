#ifndef _DMA_DEVICE_SHIM_C_
#define _DMA_DEVICE_SHIM_C_

#include "hexagon_mini_dma.h"
#include "mini_qurt.h"
#include "hexagon_dma_device_shim.h"

using namespace Halide::Runtime::Internal::Qurt;

typedef struct dmaDummyLib
{
	int width;
	void* host_address;
}t_dmaDummyLib;

/*DUMMY DRIVER IMPLEMENTATION */

int dma_isDMADriverReady()
{
	return QURT_EOK;
}

int dma_getFormatAlignment(t_eDmaFmt eFmt, bool isUbwc, dma_tPixAlignInfo& pixAlignInfo)
{
	int nRet = 0;
	
	pixAlignInfo.u16H = 16;
	pixAlignInfo.u16W = 128;

	return nRet;
}

addr_t dma_lookUpPhysicalAddress(addr_t addr)
{
	return addr;
	
}

int dma_getMinRoiSize(t_eDmaFmt eFmt, bool isUbwc, dma_tPixAlignInfo& pixAlignInfo)
{
	int nRet = 0;

	pixAlignInfo.u16H = 16;
	pixAlignInfo.u16W = 128;

	return nRet;
}

void* dma_allocateDMAEngine()
{
	t_dmaDummyLib* dmaHandle = NULL;
  
  dmaHandle = (t_dmaDummyLib *)malloc(sizeof(t_dmaDummyLib));
  
  return (void*)dmaHandle;
}  

qurt_size_t dma_getDescriptorSize(t_eDmaFmt* fmtType, int nComponents,int nFolds)
{
	qurt_size_t region_tcm_desc_size = 0;
	if(fmtType != NULL)
	region_tcm_desc_size = ALIGN(64, 0x1000);
	return region_tcm_desc_size;
}

int dma_getStride(t_eDmaFmt fmtType, bool isUBWC, dma_tPixAlignInfo roiDims)
{
	
	int stride = roiDims.u16W; 
	
	return stride;
}

int dma_getMemPoolID(qurt_mem_pool_t *mem_pool)
{
	int nRet = 0;
	*mem_pool = 1;
	
	return nRet;
}

qurt_addr_t dma_allocateCache(qurt_mem_pool_t pool_tcm, qurt_size_t region_tcm_size, qurt_mem_region_t* region_tcm)
{
	unsigned char* buf_vaddr;
	
	buf_vaddr = (unsigned char*) malloc(region_tcm_size*sizeof(unsigned char*));
	
	if(region_tcm != 0)
	 *region_tcm = *reinterpret_cast<unsigned int*>(&buf_vaddr);
	
	memset(buf_vaddr, 0, region_tcm_size*sizeof(unsigned char*));
	qurt_addr_t buf_addr = *reinterpret_cast<unsigned int *>(&buf_vaddr);
	return buf_addr;
}

int dma_lockCache(qurt_addr_t tcm_buf_vaddr, qurt_size_t region_tcm_size)
{
	int nRet  = QURT_EOK;
	//do nothing
	return nRet;
}

int dma_unlockCache(qurt_addr_t tcm_buf_vaddr, qurt_size_t region_tcm_size)
{
	int nRet  = QURT_EOK;
	//do nothing
	return nRet;
}

int dma_prepareForTransfer(dma_tPrepareParams param)
{
	int nRet  = QURT_EOK;
	t_dmaDummyLib* dmaHandle = (t_dmaDummyLib*)param.handle;
	if(dmaHandle != 0)
	{
		dmaHandle->host_address = (void*)param.hostAddress;
		dmaHandle->width = param.frameWidth;
	}

//do Nothing
return nRet;
}

int dma_wait(void* handle)
{
	int nRet = QURT_EOK;
//do nothing
	return nRet;
	
}	

int dma_moveData(dma_tMoveParams param)
{
	
	int nRet  = QURT_EOK;
	t_dmaDummyLib* dmaHandle = (t_dmaDummyLib*)param.handle;
	if(dmaHandle != 0)
    {
	   unsigned char* host_addr = (unsigned char*)dmaHandle->host_address;
	   unsigned char* dest_addr = (unsigned char*)param.ping_buffer;
	   int x = param.xoffset;
	   int y = param.yoffset;
	   int w = param.roiWidth;
	   int h = param.roiHeight;
	   
	   unsigned int offset_buf = param.Offset;
	   for(int xii=0;xii<h;xii++){
			for(int yii=0;yii<w;yii++){
				int xin = xii*w;  /* w should be Fold Offset */ 
	            int yin = yii;
							
				int RoiOffset = x+y*dmaHandle->width;
				int xout = xii*dmaHandle->width;
				int yout = yii;
							
				dest_addr[offset_buf+yin+xin] =  host_addr[RoiOffset + yout + xout ] ; /* Thid offset for Pinh Pong*/
			//	printf("dest_addr[%d]=%d;", offset+yin+xin, dest_addr[offset+yin+xin]);
	   		}
		}    
    }		
	return nRet;

}
/* Free DMA
 * out: ERR
 */
int dma_freeDMA(void* handle)
{
	int nRet  = QURT_EOK;
	t_dmaDummyLib* dmaHandle = (t_dmaDummyLib*)handle;
	
	if(dmaHandle != 0)
	free(dmaHandle);

	return nRet;
}

/* Finish Frame
 * out: ERR
 */
int dma_finishFrame(void* handle)
{
	int nRet  = QURT_EOK;
	//do nothing
	return nRet;
}

unsigned int dma_getThreadID()
{
	static int i=0;
	i++;
	return i;
}


void dma_deleteMemRegion(qurt_mem_region_t cache_mem)
{
    unsigned char* temp = reinterpret_cast<unsigned char*>(cache_mem);
	free(temp);
}
#endif
