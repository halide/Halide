#ifndef STATIC_STRUCTURED_BUFFER_H
#define STATIC_STRUCTURED_BUFFER_H

#include <D3D11.h>

class StaticStructuredBuffer
{
public:

	static StaticStructuredBuffer* create( ID3D11Device* pDevice,
		int nElements, int elementSizeBytes );
	
	virtual ~StaticStructuredBuffer();

	int numElements() const;
	int elementSizeBytes() const;
	int sizeInBytes() const;

	ID3D11Buffer* buffer() const;
	ID3D11ShaderResourceView* shaderResourceView() const;
	ID3D11UnorderedAccessView* unorderedAccessView() const;

private:

	StaticStructuredBuffer( ID3D11Device* pDevice,
		int nElements, int elementSizeBytes,
		ID3D11Buffer* pBuffer );

	int m_nElements;
	int m_elementSizeBytes;

	ID3D11Buffer* m_pBuffer;
	ID3D11ShaderResourceView* m_pSRV;
	ID3D11UnorderedAccessView* m_pUAV;
};

#endif // STATIC_STRUCTURED_BUFFER_H
