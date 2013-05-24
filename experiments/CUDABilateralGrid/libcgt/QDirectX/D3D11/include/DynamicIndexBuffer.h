#ifndef DYNAMIC_INDEX_BUFFER_H
#define DYNAMIC_INDEX_BUFFER_H

#include <D3D11.h>
#include <common/BasicTypes.h>

class DynamicIndexBuffer
{
public:

	// Indices are always unsigned 32-bit integers
	// (D3D also allows 16-bit ones)
	static DXGI_FORMAT s_format;
	
	DynamicIndexBuffer( ID3D11Device* pDevice, int capacity );
	virtual ~DynamicIndexBuffer();

	int capacity() const;

	ID3D11Buffer* buffer();

	uint* mapForWriteDiscard();
	void unmap();

private:

	int m_capacity;

	ID3D11Buffer* m_pBuffer;
	ID3D11DeviceContext* m_pContext;

};

#endif // DYNAMIC_INDEX_BUFFER_H
