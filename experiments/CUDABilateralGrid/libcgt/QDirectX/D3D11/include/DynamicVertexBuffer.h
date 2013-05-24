#pragma once

#include <D3D11.h>

class DynamicVertexBuffer
{
public:

	// TODO: resize
	// TODO: isEmpty() (support size 0 by storing null and resizing correctly)

	// Create a new DynamicVertexBuffer
	// capacity is specified in number of vertices
	// vertexSizeBytes is the number of bytes per vertex
	// total capacity in bytes is capacity * vertexSizeBytes
	DynamicVertexBuffer( ID3D11Device* pDevice, int capacity, int vertexSizeBytes );
	virtual ~DynamicVertexBuffer();

	int capacity();

	ID3D11Buffer* buffer();
	UINT defaultStride();
	UINT defaultOffset();

	D3D11_MAPPED_SUBRESOURCE mapForWriteDiscard();

	// same as reinterpret_cast< T* >( mapForWriteDiscard().pData )
	template< typename T >
	T* mapForWriteDiscardAs()
	{
		return reinterpret_cast< T* >( mapForWriteDiscard().pData );
	}

	void unmap();	

private:

	int m_capacity;
	int m_vertexSizeBytes;
	
	ID3D11Buffer* m_pBuffer;
	ID3D11DeviceContext* m_pContext;
	
};
