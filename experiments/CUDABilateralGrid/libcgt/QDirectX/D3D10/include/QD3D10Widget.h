#ifndef QD3D10WIDGET_H
#define QD3D10WIDGET_H

#include <d3d10_1.h>
#include <d3d10.h>
#include <d3dx10.h>

#include <QWidget>
#include <QStack>

class QD3D10Widget : public QWidget
{
public:
	
	QD3D10Widget( QWidget* parent = NULL );
	virtual ~QD3D10Widget();

	HRESULT initialize( int width, int height );

	ID3D10Device* device() const;

	void clearBackBuffer( float* rgba, float depth = 1 );

	void clearBackBufferColor( float* rgba );
	void clearBackBufferDepth( float depth = 1 );
	void clearBackBufferDepthStencil( float depth, UINT8 stencil );

	// set output buffers to the internal back buffer (color and depth-stencil)
	void restoreBackBuffer();

	ID3D10Texture2D* backBufferColor();
	ID3D10RenderTargetView* backBufferRenderTargetView();

	ID3D10Texture2D* backBufferDepthStencil();
	ID3D10DepthStencilView* backBufferDepthStencilView();

protected:
	
	virtual void initializeD3D();
	virtual void resizeD3D( int width, int height );
	virtual void paintD3D();

	IDXGISwapChain* m_pSwapChain;
	ID3D10Device* m_pDevice;

	ID3D10Texture2D* m_pBackBuffer;
	ID3D10RenderTargetView* m_pBackBufferRenderTargetView;

	ID3D10Texture2D* m_pDepthStencilBuffer;
	ID3D10DepthStencilView* m_pDepthStencilView;	

private:

	virtual QPaintEngine* paintEngine() const;
	virtual void paintEvent( QPaintEvent* e );
	virtual void resizeEvent( QResizeEvent* e );

	bool m_bD3DInitialized;

	// initialization
	HRESULT createSwapChainAndDevice( int width, int height );
	HRESULT createBackBufferRenderTargetView();
	HRESULT createDepthStencilBuffers( int width, int height );

	// resizing
	HRESULT resizeSwapChain( int width, int height );
	HRESULT resizeDepthStencilBuffer( int width, int height );

};

#endif // QD3D10WIDGET_H
