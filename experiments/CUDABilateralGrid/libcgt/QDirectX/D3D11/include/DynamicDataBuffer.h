#pragma once

#include <D3D11.h>

class DynamicDataBuffer
{
public:

	static DynamicDataBuffer* createFloat( ID3D11Device* pDevice, int nElements );
	static DynamicDataBuffer* createFloat2( ID3D11Device* pDevice, int nElements );
	static DynamicDataBuffer* createFloat3( ID3D11Device* pDevice, int nElements );
	static DynamicDataBuffer* createFloat4( ID3D11Device* pDevice, int nElements );
	static DynamicDataBuffer* createUInt2( ID3D11Device* pDevice, int nElements );
	static DynamicDataBuffer* createUInt4( ID3D11Device* pDevice, int nElements );

	virtual ~DynamicDataBuffer();

	int numElements() const;
	int elementSizeBytes() const;
	int sizeInBytes() const;
	DXGI_FORMAT format() const;

	// Update this <-- srcData
	void update( ID3D11DeviceContext* pContext, const void* srcData );

	ID3D11Buffer* buffer() const;
	ID3D11ShaderResourceView* shaderResourceView() const;

	D3D11_MAPPED_SUBRESOURCE mapForWriteDiscard();

	// same as reinterpret_cast< T* >( mapForWriteDiscard().pData )
	template< typename T >
	T* mapForWriteDiscardAs()
	{
		return reinterpret_cast< T* >( mapForWriteDiscard().pData );
	}

	void unmap();

private:

	DynamicDataBuffer( ID3D11Device* pDevice,
		int nElements, int elementSizeBytes,
		DXGI_FORMAT format,
		ID3D11Buffer* pBuffer );

	static D3D11_BUFFER_DESC createDynamicBufferDescription( int nElements, int elementSizeBytes );

	int m_nElements;
	int m_elementSizeBytes;
	DXGI_FORMAT m_format;

	ID3D11Buffer* m_pBuffer;
	ID3D11DeviceContext* m_pContext;

	ID3D11ShaderResourceView* m_pSRV;
};
