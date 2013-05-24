#include "ComputeDevice.h"

// static
ComputeDevice* ComputeDevice::create( IDXGIAdapter* pAdapter )
{
	UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

#ifdef _DEBUG
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
	
	// For some odd reason, D3D11 requires that:
	// if pAdapter != NULL (you used a known adapter)
	// then the driver type must be D3D_DRIVER_TYPE_UNKNOWN
	// http://msdn.microsoft.com/en-us/library/windows/desktop/ff476082(v=vs.85).aspx
	D3D_DRIVER_TYPE driverType =
		( pAdapter == NULL ) ?
		D3D_DRIVER_TYPE_HARDWARE :
		D3D_DRIVER_TYPE_UNKNOWN;

	D3D_FEATURE_LEVEL requestedFeatureLevel = D3D_FEATURE_LEVEL_11_0;
	D3D_FEATURE_LEVEL actualFeatureLevel;

	ID3D11Device* pDevice;
	ID3D11DeviceContext* pImmediateContext;

	HRESULT hr = D3D11CreateDevice
	(
		pAdapter, // existing DXGI adapter
		driverType, // driver type
		NULL, // HMODULE pointing to loaded software rasterizer dll
		createDeviceFlags, // device creation flags
		&requestedFeatureLevel, // DX feature level
		1, // number of feature levels: 1
		D3D11_SDK_VERSION, // SDK version, must be D3D11_SDK_VERSION
		&pDevice,
		&actualFeatureLevel,
		&pImmediateContext
	);

	ComputeDevice* pCD = NULL;
	if( SUCCEEDED( hr ) )
	{
		pCD = new ComputeDevice( pDevice, pImmediateContext );
	}
	return pCD;
}

// virtual
ComputeDevice::~ComputeDevice()
{
	m_pImmediateContext->Release();
	m_pDevice->Release();
}

ID3D11Device* ComputeDevice::device()
{
	return m_pDevice;
}

ID3D11DeviceContext* ComputeDevice::immediateContext()
{
	return m_pImmediateContext;
}

ComputeDevice::ComputeDevice( ID3D11Device* pDevice, ID3D11DeviceContext* pImmediateContext ) :

	m_pDevice( pDevice ),
	m_pImmediateContext( pImmediateContext )

{

}
