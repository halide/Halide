#ifndef STAGING_STRUCTURED_BUFFER_H
#define STAGING_STRUCTURED_BUFFER_H

#include <D3D11.h>

class StagingStructuredBuffer
{
public:

	static StagingStructuredBuffer* create( ID3D11Device* pDevice,
		int nElements, int elementSizeBytes );

	virtual ~StagingStructuredBuffer();

	int numElements() const;
	int elementSizeBytes() const;

	ID3D11Buffer* buffer() const;
	
	// read/write mapping
	D3D11_MAPPED_SUBRESOURCE mapForReadWrite();
	
	// same as reinterpret_cast< T* >( mapForReadWrite().pData )
	template< typename T >
	T* mapForReadWriteAs()
	{
		return reinterpret_cast< T* >( mapForReadWrite().pData );
	}

	void unmap();

	// Copy from pSource to this
	void copyFrom( ID3D11Buffer* pSource );	

	// Copy from this to pTarget
	void copyTo( ID3D11Buffer* pTarget );

	// Copy count items, starting with srcIndex, from pSource to this
	// indices and count are in units of elementSizeBytes() bytes
	// (i.e., if each element is 16 bytes, then index 2 would be at
	// byte offset 32)
	void copyRangeFrom( ID3D11Buffer* pSource, int srcIndex, int count,
		int dstIndex = 0 );

	void copyRangeTo( int srcIndex, int count,
		ID3D11Buffer* pTarget, int dstIndex = 0 );

	// TODO: D3D11Resource superclass?
	// that has copy from / to?

private:

	StagingStructuredBuffer( ID3D11Device* pDevice,
		int nElements, int elementSizeBytes,
		ID3D11Buffer* pBuffer );

	int m_nElements;
	int m_elementSizeBytes;

	ID3D11Device* m_pDevice;
	ID3D11Buffer* m_pBuffer;
	ID3D11DeviceContext* m_pContext;
};

#endif // STAGING_STRUCTURED_BUFFER_H
