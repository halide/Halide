#ifndef COMPUTE_DEVICE_H
#define COMPUTE_DEVICE_H

#include <D3D11.h>

class ComputeDevice
{
public:

	// Creates a compute device using the specified adapter.
	// A NULL adapter means the "default" adapter.
	static ComputeDevice* create( IDXGIAdapter* pAdapter = NULL );
	virtual ~ComputeDevice();

	ID3D11Device* device();
	ID3D11DeviceContext* immediateContext();

private:

	ComputeDevice( ID3D11Device* pDevice, ID3D11DeviceContext* pImmediateContext );

	ID3D11Device* m_pDevice;
	ID3D11DeviceContext* m_pImmediateContext;
};

#endif // COMPUTE_DEVICE_H
