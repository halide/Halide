#include "SimpleTriangleWidget.h"

#include <QTimer>
#include <cstdio>
#include <D3DX11Async.h>
#include <D3DX10Math.h>
#include <D3Dcompiler.h>

#include "DynamicVertexBuffer.h"
#include "VertexPosition4fColor4f.h"

SimpleTriangleWidget::SimpleTriangleWidget() :
	QD3D11Widget()
{
	m_bRotating = true;
	m_theta = 0;
}

#include "DynamicTexture2D.h"
#include "RenderTarget.h"
#include "D3D11Utils.h"
#include "EffectManager.h"
#include <common/Reference.h>
#include <imageproc/Image4f.h>

// virtual
void SimpleTriangleWidget::initializeD3D()
{
	printf( "initializing\n" );

	loadShaders();

	int nVertices = 3;
	m_pVertexBuffer = new DynamicVertexBuffer( device(), nVertices, VertexPosition4fColor4f::sizeInBytes() );

	// populate vertex buffer
	float* pStream = reinterpret_cast< float* >( m_pVertexBuffer->mapForWriteDiscard().pData );

	// TODO: Ilya, see if you can put:
	// Vector4fs into VertexPosition4fColor4f, check that they're the right size
	// and make a std::vector< VertexPosition4fColor4f > of them
	// and then you can do a memcpy

	// v0
	pStream[ 0 ] = 0.0f;
	pStream[ 1 ] = 0.0f;
	pStream[ 2 ] = 0.0f;
	pStream[ 3 ] = 1.0f;
	pStream[ 4 ] = 1.0f;
	pStream[ 5 ] = 0.0f;
	pStream[ 6 ] = 0.0f;
	pStream[ 7 ] = 1.0f;

	pStream[ 8 ] = 5.0f;
	pStream[ 9 ] = 0.0f;
	pStream[ 10 ] = 0.0f;
	pStream[ 11 ] = 1.0f;
	pStream[ 12 ] = 0.0f;
	pStream[ 13 ] = 1.0f;
	pStream[ 14 ] = 0.0f;
	pStream[ 15 ] = 1.0f;

	pStream[ 16 ] = 0.0f;
	pStream[ 17 ] = 5.0f;
	pStream[ 18 ] = 0.0f;
	pStream[ 19 ] = 1.0f;
	pStream[ 20 ] = 0.0f;
	pStream[ 21 ] = 0.0f;
	pStream[ 22 ] = 1.0f;
	pStream[ 23 ] = 1.0f;

	m_pVertexBuffer->unmap();

	m_pPass = m_pEffect->GetTechniqueByIndex( 0 )->GetPassByIndex( 0 );
	D3DX11_PASS_DESC passDesc;
	m_pPass->GetDesc( &passDesc );
	device()->CreateInputLayout( VertexPosition4fColor4f::s_layout, 2, passDesc.pIAInputSignature, passDesc.IAInputSignatureSize, &m_pInputLayout );

	m_pAnimationTimer = new QTimer;
	m_pAnimationTimer->setInterval( 33 );
	QObject::connect( m_pAnimationTimer, SIGNAL( timeout() ), this, SLOT( handleTimeout() ) );
	m_pAnimationTimer->start();
}

void SimpleTriangleWidget::loadShaders()
{
	ID3DBlob* pCompiledEffect;
	ID3DBlob* pErrorMessages;

	UINT shadeFlags = 0;

#if _DEBUG
	shadeFlags |= D3DCOMPILE_DEBUG;
	shadeFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	// compile effect	
	HRESULT hr = D3DX11CompileFromFile
	(
		L"simple.fx", // filename
		NULL, // #defines
		NULL, // includes
		NULL, // function name,
		"fx_5_0", // profile
		shadeFlags, // shade flags
		0, // effect flags
		NULL, // thread pump
		&pCompiledEffect,
		&pErrorMessages,
		NULL // return value
	);

	// actually construct the effect
	if( SUCCEEDED( hr ) )
	{
		hr = D3DX11CreateEffectFromMemory
		(
			pCompiledEffect->GetBufferPointer(),
			pCompiledEffect->GetBufferSize(),
			0, // flags
			device(),
			&m_pEffect
		);		
	}

	if( pErrorMessages != NULL )
	{
		pErrorMessages->Release();
	}
	if( pCompiledEffect != NULL )
	{
		pCompiledEffect->Release();
	}
}

// virtual
void SimpleTriangleWidget::resizeD3D( int width, int height )
{
	printf( "resizing\n" );
}

// virtual
void SimpleTriangleWidget::paintD3D()
{
	float black[ 4 ] = { 0, 0, 0, 0 };
	float red[ 4 ] = { 1, 0, 0, 0 };
	float blue[ 4 ] = { 0, 0, 1, 0 };

	clearBackBuffer( black );

	D3DXMATRIX rotZ;
	D3DXMatrixRotationZ( &rotZ, m_theta );

	D3DXVECTOR3 eye( 0, 0, 5 );
	D3DXVECTOR3 at( 0, 0, 0 );
	D3DXVECTOR3 up( 0, 1, 0 );

	D3DXMATRIX lookAt;
	D3DXMatrixLookAtRH( &lookAt, &eye, &at, &up );

	D3DXMATRIX proj;
	float aspect = static_cast< float >( width() ) / height();
	D3DXMatrixPerspectiveFovRH( &proj, 60.0f * 3.141592f / 180.0f, aspect, 0.1f, 1000.0f );

	D3DXMATRIX vw;
	D3DXMatrixMultiply( &vw, &rotZ, &lookAt );

	D3DXMATRIX pvw;
	D3DXMatrixMultiply( &pvw, &vw, &proj );

	m_pEffect->GetVariableByName( "pvw" )->AsMatrix()->SetMatrix( pvw );

	UINT stride = m_pVertexBuffer->defaultStride();
	UINT offset = m_pVertexBuffer->defaultOffset();
	ID3D11Buffer* pBuffer = m_pVertexBuffer->buffer();
	immediateContext()->IASetVertexBuffers( 0, 1, &pBuffer, &stride, &offset );
	immediateContext()->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	immediateContext()->IASetInputLayout( m_pInputLayout );

	m_pPass->Apply( 0, immediateContext() );

	immediateContext()->Draw( 3, 0 );
}


void SimpleTriangleWidget::handleTimeout()
{
	const float dTheta = 1.0f * 3.141592f / 180.f;

	if( m_bRotating )
	{
		m_theta += dTheta;
	}

	update();
}
