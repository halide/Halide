#pragma once

#include <D3D11.h>

class StaticDataBuffer
{
public:

	static StaticDataBuffer* createFloat( ID3D11Device* pDevice, int nElements );
	static StaticDataBuffer* createFloat2( ID3D11Device* pDevice, int nElements );
	static StaticDataBuffer* createFloat4( ID3D11Device* pDevice, int nElements );
	static StaticDataBuffer* createUInt2( ID3D11Device* pDevice, int nElements );

	virtual ~StaticDataBuffer();

	int numElements() const;
	int elementSizeBytes() const;
	int sizeInBytes() const;
	DXGI_FORMAT format() const;

	// Update this <-- srcData
	void update( ID3D11DeviceContext* pContext, const void* srcData );

	ID3D11Buffer* buffer() const;
	ID3D11ShaderResourceView* shaderResourceView() const;

private:

	StaticDataBuffer( ID3D11Device* pDevice,
		int nElements, int elementSizeBytes,
		DXGI_FORMAT format,
		ID3D11Buffer* pBuffer );

	static D3D11_BUFFER_DESC createStaticBufferDescription( int nElements, int elementSizeBytes );

	int m_nElements;
	int m_elementSizeBytes;
	DXGI_FORMAT m_format;

	ID3D11Buffer* m_pBuffer;
	ID3D11ShaderResourceView* m_pSRV;
};
