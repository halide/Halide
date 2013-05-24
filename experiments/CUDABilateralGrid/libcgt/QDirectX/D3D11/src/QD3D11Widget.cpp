#include "QD3D11Widget.h"
#include "D3D11Utils.h"

#include <QResizeEvent>

QD3D11Widget::QD3D11Widget( QWidget* parent ) :
	QWidget( parent ),
	m_bD3DInitialized( false )
{
	setAttribute( Qt::WA_PaintOnScreen, true );
}

// virtual
QD3D11Widget::~QD3D11Widget()
{
	if( m_bD3DInitialized )
	{
		m_pImmediateContext->OMSetRenderTargets( 0, NULL, NULL );

		m_pDepthStencilView->Release();
		m_pDepthStencilBuffer->Release();

		m_pBackBufferRenderTargetView->Release();
		m_pBackBuffer->Release();

		m_pSwapChain->Release();
		m_pDevice->Release();
	}
}

bool QD3D11Widget::isValid() const
{
	return m_bD3DInitialized;
}

ID3D11Device* QD3D11Widget::device() const
{
	return m_pDevice;
}

ID3D11DeviceContext* QD3D11Widget::immediateContext() const
{
	return m_pImmediateContext;
}

void QD3D11Widget::clearBackBuffer( float* rgba, float depth )
{
	clearBackBufferColor( rgba );
	clearBackBufferDepth( depth );
}

void QD3D11Widget::clearBackBufferColor( float* rgba )
{
	m_pImmediateContext->ClearRenderTargetView( m_pBackBufferRenderTargetView, rgba );
}

void QD3D11Widget::clearBackBufferDepth( float depth )
{
	m_pImmediateContext->ClearDepthStencilView( m_pDepthStencilView, D3D11_CLEAR_DEPTH, depth, 0 );
}

void QD3D11Widget::clearBackBufferDepthStencil( float depth, UINT8 stencil )
{
	m_pImmediateContext->ClearDepthStencilView( m_pDepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, depth, stencil );
}

void QD3D11Widget::restoreBackBuffer()
{
	m_pImmediateContext->OMSetRenderTargets( 1, &m_pBackBufferRenderTargetView, m_pDepthStencilView );
}

ID3D11Texture2D* QD3D11Widget::backBufferColor()
{
	return m_pBackBuffer;
}

ID3D11RenderTargetView* QD3D11Widget::backBufferRenderTargetView()
{
	return m_pBackBufferRenderTargetView;
}

ID3D11Texture2D* QD3D11Widget::backBufferDepthStencil()
{
	return m_pDepthStencilBuffer;
}

ID3D11DepthStencilView* QD3D11Widget::backBufferDepthStencilView()
{
	return m_pDepthStencilView;
}

D3D11_VIEWPORT QD3D11Widget::fullWindowViewport()
{
	return m_fullWindowViewport;
}

// virtual
void QD3D11Widget::initializeD3D()
{

}

// virtual
void QD3D11Widget::paintD3D()
{

}

// virtual
void QD3D11Widget::resizeD3D( int width, int height )
{

}

// virtual
QPaintEngine* QD3D11Widget::paintEngine() const
{
	return NULL;
}

// virtual
void QD3D11Widget::paintEvent( QPaintEvent* e )
{
	if( m_bD3DInitialized )
	{
		paintD3D();
		m_pSwapChain->Present( 0, 0 );
	}
}

// virtual
void QD3D11Widget::resizeEvent( QResizeEvent* e )
{
	// resize swap chain and back buffer
	QSize size = e->size();
	int width = size.width();
	int height = size.height();

	if( !m_bD3DInitialized )
	{
		initialize( width, height );
	}

	// initialize might fail
	if( m_bD3DInitialized )
	{
		// set render targets to null
		m_pImmediateContext->OMSetRenderTargets( 0, NULL, NULL );

		// release the old render target view and make a new one
		m_pBackBufferRenderTargetView->Release();
		m_pBackBuffer->Release();

		// resize the swap chain
		resizeSwapChain( width, height );

		// recreate the render targets
		createBackBufferRenderTargetView();

		// resize depth stencil buffer
		resizeDepthStencilBuffer( width, height );

		// point device at new back buffers
		restoreBackBuffer();

		// resize viewport
		m_fullWindowViewport = D3D11Utils::createViewport( width, height );
		m_pImmediateContext->RSSetViewports( 1, &m_fullWindowViewport );

		resizeD3D( width, height );
	}
}

// TODO: optional: refresh rate, multisampling, driver type (hardware vs ref vs WARP)
HRESULT QD3D11Widget::initialize( int width, int height )
{
	QSize size( width, height );
	resize( size );

	HRESULT hr = createSwapChainAndDevice( width, height );
	if( SUCCEEDED( hr ) )
	{
		hr = createBackBufferRenderTargetView();
		if( SUCCEEDED( hr ) )
		{
			hr = createDepthStencilBuffers( width, height );
			if( SUCCEEDED( hr ) )
			{
				// set render targets
				m_pImmediateContext->OMSetRenderTargets( 1, &m_pBackBufferRenderTargetView, m_pDepthStencilView );

				// setup viewport
				m_fullWindowViewport = D3D11Utils::createViewport( width, height );
				m_pImmediateContext->RSSetViewports( 1, &m_fullWindowViewport );

				initializeD3D();
				m_bD3DInitialized = true;
			}
		}
	}	
	return hr;
}

HRESULT QD3D11Widget::createSwapChainAndDevice( int width, int height )
{
	UINT createDeviceFlags = 0;

#ifdef _DEBUG
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	DXGI_SWAP_CHAIN_DESC sd;
	ZeroMemory( &sd, sizeof( sd ) );
	sd.BufferCount = 1;
	sd.BufferDesc.Width = width;
	sd.BufferDesc.Height = height;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = 0;
	sd.BufferDesc.RefreshRate.Denominator = 0;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = winId();
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	sd.Windowed = TRUE;
	
	D3D_DRIVER_TYPE driverType = D3D_DRIVER_TYPE_HARDWARE;

	D3D_FEATURE_LEVEL requestedFeatureLevel = D3D_FEATURE_LEVEL_11_0;
	D3D_FEATURE_LEVEL actualFeatureLevel;

	// create the device and the swap chain
	HRESULT hr = D3D11CreateDeviceAndSwapChain
	(
		NULL, // existing DXGI adapter
		driverType, // driver type		
		NULL, // HMODULE pointing to loaded software rasterizer dll
		createDeviceFlags, // device creation flags
		&requestedFeatureLevel, // DX feature level
		1, // number of feature levels: 1
		D3D11_SDK_VERSION, // SDK version, must be D3D11_SDK_VERSION
		&sd, // swap chain description
		&m_pSwapChain, // output swap chain
		&m_pDevice, // output device
		&actualFeatureLevel, // output feature level achieved
		&m_pImmediateContext // output immediate context
	);

	return hr;
}

HRESULT QD3D11Widget::createBackBufferRenderTargetView()
{
	// grab the back buffer from the swap chain
	// and create a render target view out of it
	HRESULT hr = m_pSwapChain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), reinterpret_cast< void** >( &m_pBackBuffer ) );
	if( SUCCEEDED( hr ) )
	{
		hr = m_pDevice->CreateRenderTargetView( m_pBackBuffer, NULL, &m_pBackBufferRenderTargetView );
	}

	return hr;
}

HRESULT QD3D11Widget::createDepthStencilBuffers( int width, int height )
{
	// create depth stencil texture
	D3D11_TEXTURE2D_DESC dsd;
	dsd.Width = width;
	dsd.Height = height;
	dsd.MipLevels = 1;
	dsd.ArraySize = 1;
	dsd.Format = DXGI_FORMAT_D32_FLOAT;
	dsd.SampleDesc.Count = 1;
	dsd.SampleDesc.Quality = 0;
	dsd.Usage = D3D11_USAGE_DEFAULT;
	dsd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	dsd.CPUAccessFlags = 0;
	dsd.MiscFlags = 0;

	HRESULT hr = m_pDevice->CreateTexture2D( &dsd, NULL, &m_pDepthStencilBuffer );
	if( SUCCEEDED( hr ) )
	{
		// create the depth stencil view
		D3D11_DEPTH_STENCIL_VIEW_DESC dsv;
		ZeroMemory( &dsv, sizeof( dsv ) );
		dsv.Format = dsd.Format;
		dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		dsv.Texture2D.MipSlice = 0;

		hr = m_pDevice->CreateDepthStencilView( m_pDepthStencilBuffer, &dsv, &m_pDepthStencilView );
	}

	return hr;
}

HRESULT QD3D11Widget::resizeSwapChain( int width, int height )
{
	// 2 buffers: front and back
	// 0 at the end: no flags
	HRESULT hr = m_pSwapChain->ResizeBuffers( 2, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0 );
	return hr;
}

HRESULT QD3D11Widget::resizeDepthStencilBuffer( int width, int height )
{
	m_pDepthStencilView->Release();
	m_pDepthStencilBuffer->Release();

	HRESULT hr = createDepthStencilBuffers( width, height );
	return hr;
}
