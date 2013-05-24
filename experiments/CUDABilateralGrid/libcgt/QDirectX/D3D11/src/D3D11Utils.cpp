#include "D3D11Utils.h"

#include <QFile>
#include <QTextStream>

#include <common/BasicTypes.h>
#include <color/ColorUtils.h>
#include "DynamicTexture2D.h"
#include "StagingTexture2D.h"
#include "StagingStructuredBuffer.h"

// static
QVector< IDXGIAdapter* > D3D11Utils::getDXGIAdapters()
{
	QVector< IDXGIAdapter* > adapters;

	IDXGIFactory1* pFactory;
	HRESULT hr = CreateDXGIFactory1( __uuidof( IDXGIFactory1 ), ( void** )( &pFactory ) );
	if( SUCCEEDED( hr ) )
	{
		uint i = 0;
		IDXGIAdapter* pAdapter;
		while( pFactory->EnumAdapters( i, &pAdapter ) != DXGI_ERROR_NOT_FOUND )
		{ 
			adapters.append( pAdapter );
			++i; 
		}

		pFactory->Release();
	}

	return adapters;
}

// static
D3D11_VIEWPORT D3D11Utils::createViewport( int width, int height )
{
	return createViewport( 0, 0, width, height, 0.0f, 1.0f );
}

// static
D3D11_VIEWPORT D3D11Utils::createViewport( const Vector2i& wh )
{
	return createViewport( wh[ 0 ], wh[ 1 ] );
}

// static
D3D11_VIEWPORT D3D11Utils::createViewport( int topLeftX, int topLeftY, int width, int height, float zMin, float zMax )
{
	D3D11_VIEWPORT vp;
	vp.TopLeftX = topLeftX;
	vp.TopLeftY = topLeftY;
	vp.Width = width;
	vp.Height = height;
	vp.MinDepth = zMin;
	vp.MaxDepth = zMax;

	return vp;
}

// static
QVector< VertexPosition4fNormal3fTexture2f > D3D11Utils::createBox( bool normalsPointOutward )
{
	Vector4f positions[ 8 ];
	for( int i = 0; i < 8; ++i )
	{
		positions[ i ][ 0 ] = ( i & 0x1 )? 1 : 0;
		positions[ i ][ 1 ] = ( i & 0x2 )? 1 : 0;
		positions[ i ][ 2 ] = ( i & 0x4 )? 1 : 0;
		positions[ i ][ 3 ] = 1;
	}

	Vector3f normals[ 6 ]; // pointing outward
	normals[ 0 ] = Vector3f( 1, 0, 0 ); // right
	normals[ 1 ] = Vector3f( -1, 0, 0 ); // left
	normals[ 2 ] = Vector3f( 0, 1, 0 ); // top
	normals[ 3 ] = Vector3f( 0, -1, 0 ); // bottom
	normals[ 4 ] = Vector3f( 0, 0, 1 ); // front
	normals[ 5 ] = Vector3f( 0, 0, -1 ); // back

	Vector2f tc( 0, 0 );

	QVector< VertexPosition4fNormal3fTexture2f > vertexArray;
	vertexArray.reserve( 36 );	

	if( normalsPointOutward )
	{
		// bottom
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 4 ], normals[ 3 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 5 ], normals[ 3 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 0 ], normals[ 3 ], tc ) );

		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 0 ], normals[ 3 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 5 ], normals[ 3 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 1 ], normals[ 3 ], tc ) );

		// left
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 4 ], normals[ 1 ], tc ) ); 
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 0 ], normals[ 1 ], tc ) ); 
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 6 ], normals[ 1 ], tc ) ); 

		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 6 ], normals[ 1 ], tc ) ); 
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 0 ], normals[ 1 ], tc ) ); 
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 2 ], normals[ 1 ], tc ) ); 

		// back
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 0 ], normals[ 5 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 1 ], normals[ 5 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 2 ], normals[ 5 ], tc ) );

		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 2 ], normals[ 5 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 1 ], normals[ 5 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 3 ], normals[ 5 ], tc ) );

		// front
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 6 ], normals[ 4 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 7 ], normals[ 4 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 4 ], normals[ 4 ], tc ) );

		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 4 ], normals[ 4 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 7 ], normals[ 4 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 5 ], normals[ 4 ], tc ) );		

		// top
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 2 ], normals[ 2 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 3 ], normals[ 2 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 6 ], normals[ 2 ], tc ) );

		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 6 ], normals[ 2 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 3 ], normals[ 2 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 7 ], normals[ 2 ], tc ) );		

		// right
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 1 ], normals[ 0 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 5 ], normals[ 0 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 3 ], normals[ 0 ], tc ) );
																				
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 3 ], normals[ 0 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 5 ], normals[ 0 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 7 ], normals[ 0 ], tc ) );
	}
	else
	{
		// bottom
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 4 ], normals[ 2 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 5 ], normals[ 2 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 0 ], normals[ 2 ], tc ) );

		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 0 ], normals[ 2 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 5 ], normals[ 2 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 1 ], normals[ 2 ], tc ) );

		// left
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 4 ], normals[ 0 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 0 ], normals[ 0 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 6 ], normals[ 0 ], tc ) );

		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 6 ], normals[ 0 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 0 ], normals[ 0 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 2 ], normals[ 0 ], tc ) );

		// back
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 0 ], normals[ 4 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 1 ], normals[ 4 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 2 ], normals[ 4 ], tc ) );

		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 2 ], normals[ 4 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 1 ], normals[ 4 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 3 ], normals[ 4 ], tc ) );

		// front
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 6 ], normals[ 5 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 7 ], normals[ 5 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 4 ], normals[ 5 ], tc ) );

		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 4 ], normals[ 5 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 7 ], normals[ 5 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 5 ], normals[ 5 ], tc ) );		

		// top
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 2 ], normals[ 3 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 3 ], normals[ 3 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 6 ], normals[ 3 ], tc ) );

		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 6 ], normals[ 3 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 3 ], normals[ 3 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 7 ], normals[ 3 ], tc ) );		

		// right
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 1 ], normals[ 1 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 5 ], normals[ 1 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 3 ], normals[ 1 ], tc ) );

		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 3 ], normals[ 1 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 5 ], normals[ 1 ], tc ) );
		vertexArray.append( VertexPosition4fNormal3fTexture2f( positions[ 7 ], normals[ 1 ], tc ) );
	}

	return vertexArray;
}

// static
DynamicVertexBuffer* D3D11Utils::createFrustum( ID3D11Device* pDevice,
	const Vector3f& eye, QVector< Vector3f > frustumCorners,
	const Vector4f& color )
{
	DynamicVertexBuffer* buffer = new DynamicVertexBuffer( pDevice, 24, VertexPosition4fColor4f::sizeInBytes() );
	
	VertexPosition4fColor4f* vertexArray = buffer->mapForWriteDiscardAs< VertexPosition4fColor4f >();
	writeFrustum( eye, frustumCorners, color, vertexArray );
	buffer->unmap();

	return buffer;
}

// static
void D3D11Utils::writeFrustum( const Vector3f& eye, QVector< Vector3f > frustumCorners, const Vector4f& color,
	VertexPosition4fColor4f* vertexArray )
{
	// 4 lines from eye to each far corner
	vertexArray[0].position = Vector4f( eye, 1 );
	vertexArray[1].position = Vector4f( frustumCorners[4], 1 );

	vertexArray[2].position = Vector4f( eye, 1 );
	vertexArray[3].position = Vector4f( frustumCorners[5], 1 );

	vertexArray[4].position = Vector4f( eye, 1 );
	vertexArray[5].position = Vector4f( frustumCorners[6], 1 );

	vertexArray[6].position = Vector4f( eye, 1 );
	vertexArray[7].position = Vector4f( frustumCorners[7], 1 );

	// 4 lines between near corners
	vertexArray[8].position = Vector4f( frustumCorners[0], 1 );
	vertexArray[9].position = Vector4f( frustumCorners[1], 1 );

	vertexArray[10].position = Vector4f( frustumCorners[1], 1 );
	vertexArray[11].position = Vector4f( frustumCorners[2], 1 );

	vertexArray[12].position = Vector4f( frustumCorners[2], 1 );
	vertexArray[13].position = Vector4f( frustumCorners[3], 1 );

	vertexArray[14].position = Vector4f( frustumCorners[3], 1 );
	vertexArray[15].position = Vector4f( frustumCorners[0], 1 );

	// 4 lines between far corners
	vertexArray[16].position = Vector4f( frustumCorners[4], 1 );
	vertexArray[17].position = Vector4f( frustumCorners[5], 1 );

	vertexArray[18].position = Vector4f( frustumCorners[5], 1 );
	vertexArray[19].position = Vector4f( frustumCorners[6], 1 );

	vertexArray[20].position = Vector4f( frustumCorners[6], 1 );
	vertexArray[21].position = Vector4f( frustumCorners[7], 1 );

	vertexArray[22].position = Vector4f( frustumCorners[7], 1 );
	vertexArray[23].position = Vector4f( frustumCorners[4], 1 );

	for( int i = 0; i < 24; ++i )
	{
		vertexArray[i].color = color;
	}
}

// static
DynamicVertexBuffer* D3D11Utils::createAxes( ID3D11Device* pDevice )
{
	DynamicVertexBuffer* buffer = new DynamicVertexBuffer( pDevice, 6, VertexPosition4fColor4f::sizeInBytes() );

	VertexPosition4fColor4f* vertexArray = reinterpret_cast< VertexPosition4fColor4f* >( buffer->mapForWriteDiscard().pData );
	writeAxes( vertexArray );
	buffer->unmap();

	return buffer;
}

// static
void D3D11Utils::writeAxes( VertexPosition4fColor4f* vertexArray )
{
	// x
	vertexArray[ 0 ] = VertexPosition4fColor4f( 0, 0, 0, 1, 0, 0 );
	vertexArray[ 1 ] = VertexPosition4fColor4f( 1, 0, 0, 1, 0, 0 );

	// y
	vertexArray[ 2 ] = VertexPosition4fColor4f( 0, 0, 0, 0, 1, 0 );
	vertexArray[ 3 ] = VertexPosition4fColor4f( 0, 1, 0, 0, 1, 0 );

	// z
	vertexArray[ 4 ] = VertexPosition4fColor4f( 0, 0, 0, 0, 0, 1 );
	vertexArray[ 5 ] = VertexPosition4fColor4f( 0, 0, 1, 0, 0, 1 );
}

// static
std::shared_ptr< DynamicVertexBuffer > D3D11Utils::createFullScreenQuad( ID3D11Device* pDevice )
{
	std::shared_ptr< DynamicVertexBuffer > buffer( new DynamicVertexBuffer( pDevice, 6, VertexPosition4f::sizeInBytes() ) );

	VertexPosition4f* vertexArray = reinterpret_cast< VertexPosition4f* >( buffer->mapForWriteDiscard().pData );
	writeFullScreenQuad( vertexArray );
	buffer->unmap();

	return buffer;
}

// static
std::shared_ptr< DynamicVertexBuffer > D3D11Utils::createScreenAlignedQuad( float x, float y, float width, float height, ID3D11Device* pDevice )
{
	std::shared_ptr< DynamicVertexBuffer > buffer( new DynamicVertexBuffer( pDevice, 6, VertexPosition4fTexture2f::sizeInBytes() ) );

	VertexPosition4fTexture2f* vertexArray = reinterpret_cast< VertexPosition4fTexture2f* >( buffer->mapForWriteDiscard().pData );
	writeScreenAlignedQuad( x, y, width, height, vertexArray );
	buffer->unmap();

	return buffer;
}

// static
void D3D11Utils::writeFullScreenQuad( VertexPosition4f* vertexArray )
{
	vertexArray[ 0 ] = VertexPosition4f( -1, -1, 0, 1 );
	vertexArray[ 1 ] = VertexPosition4f( 1, -1, 0, 1 );
	vertexArray[ 2 ] = VertexPosition4f( -1, 1, 0, 1 );

	vertexArray[ 3 ] = VertexPosition4f( -1, 1, 0, 1 );
	vertexArray[ 4 ] = VertexPosition4f( 1, -1, 0, 1 );
	vertexArray[ 5 ] = VertexPosition4f( 1, 1, 0, 1 );
}

// static
void D3D11Utils::writeScreenAlignedQuad( float x, float y, float width, float height, VertexPosition4f* vertexArray )
{
	vertexArray[ 0 ] = VertexPosition4f( x, y, 0, 1 );
	vertexArray[ 1 ] = VertexPosition4f( x + width, y, 0, 1 );
	vertexArray[ 2 ] = VertexPosition4f( x, y + height, 0, 1 );

	vertexArray[ 3 ] = VertexPosition4f( x, y + height, 0, 1 );
	vertexArray[ 4 ] = VertexPosition4f( x + width, y, 0, 1 );
	vertexArray[ 5 ] = VertexPosition4f( x + width, y + height, 0, 1 );
}

// static
void D3D11Utils::writeScreenAlignedQuad( float x, float y, float width, float height, VertexPosition4fTexture2f* vertexArray, bool flipUV )
{
	if( flipUV )
	{
		vertexArray[ 0 ] = VertexPosition4fTexture2f( x, y, 0, 1, 0, 1 );
		vertexArray[ 1 ] = VertexPosition4fTexture2f( x + width, y, 0, 1, 1, 1 );
		vertexArray[ 2 ] = VertexPosition4fTexture2f( x, y + height, 0, 1, 0, 0 );

		vertexArray[ 3 ] = VertexPosition4fTexture2f( x, y + height, 0, 1, 0, 0 );
		vertexArray[ 4 ] = VertexPosition4fTexture2f( x + width, y, 0, 1, 1, 1 );
		vertexArray[ 5 ] = VertexPosition4fTexture2f( x + width, y + height, 0, 1, 1, 0 );
	}
	else
	{
		vertexArray[ 0 ] = VertexPosition4fTexture2f( x, y, 0, 1, 0, 0 );
		vertexArray[ 1 ] = VertexPosition4fTexture2f( x + width, y, 0, 1, 1, 0 );
		vertexArray[ 2 ] = VertexPosition4fTexture2f( x, y + height, 0, 1, 0, 1 );

		vertexArray[ 3 ] = VertexPosition4fTexture2f( x, y + height, 0, 1, 0, 1 );
		vertexArray[ 4 ] = VertexPosition4fTexture2f( x + width, y, 0, 1, 1, 0 );
		vertexArray[ 5 ] = VertexPosition4fTexture2f( x + width, y + height, 0, 1, 1, 1 );
	}
}

// static
bool D3D11Utils::saveFloat2BufferToTXT( ID3D11Device* pDevice, Reference< StaticDataBuffer > pBuffer, QString filename )
{
	int ne = pBuffer->numElements();
	int esb = pBuffer->elementSizeBytes();
	Reference< StagingStructuredBuffer > sb = StagingStructuredBuffer::create
	(
		pDevice, ne, esb
	);

	sb->copyFrom( pBuffer->buffer() );

	QFile outputFile( filename );

	// try to open the file in write only mode
	if( !( outputFile.open( QIODevice::WriteOnly ) ) )
	{
		return false;
	}

	QTextStream outputTextStream( &outputFile );
	outputTextStream.setCodec( "UTF-8" );

	outputTextStream << "float2 buffer: nElements = " << ne << "\n";
	outputTextStream << "[index]: x y\n";

	float* pData = reinterpret_cast< float* >( sb->mapForReadWrite().pData );
	for( int i = 0; i < ne; ++i )
	{
		float x = pData[ 2 * i ];
		float y = pData[ 2 * i + 1 ];

		outputTextStream << "[" << i << "]: " << x << " " << y << "\n";
	}

	outputFile.close();
	return true;
}

// static
bool D3D11Utils::saveFloat2BufferToTXT( ID3D11Device* pDevice, Reference< StaticStructuredBuffer > pBuffer, QString filename )
{
	int ne = pBuffer->numElements();
	int esb = pBuffer->elementSizeBytes();
	Reference< StagingStructuredBuffer > sb = StagingStructuredBuffer::create
		(
			pDevice, ne, esb
		);

	sb->copyFrom( pBuffer->buffer() );

	QFile outputFile( filename );

	// try to open the file in write only mode
	if( !( outputFile.open( QIODevice::WriteOnly ) ) )
	{
		return false;
	}

	QTextStream outputTextStream( &outputFile );
	outputTextStream.setCodec( "UTF-8" );

	outputTextStream << "float2 buffer: nElements = " << ne << "\n";
	outputTextStream << "[index]: x y\n";

	float* pData = reinterpret_cast< float* >( sb->mapForReadWrite().pData );
	for( int i = 0; i < ne; ++i )
	{
		float x = pData[ 2 * i ];
		float y = pData[ 2 * i + 1 ];

		outputTextStream << "[" << i << "]: " << x << " " << y << "\n";
	}

	outputFile.close();
	return true;
}
