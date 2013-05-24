#include "D3D10Utils.h"

#include <common/BasicTypes.h>
#include <color/ColorUtils.h>
#include "DynamicTexture2D.h"
#include "StagingTexture2D.h"

// static
D3D10_VIEWPORT D3D10Utils::createViewport( int width, int height )
{
	return createViewport( 0, 0, width, height, 0.0f, 1.0f );
}

// static
D3D10_VIEWPORT D3D10Utils::createViewport( int topLeftX, int topLeftY, int width, int height, float zMin, float zMax )
{
	D3D10_VIEWPORT vp;
	vp.TopLeftX = topLeftX;
	vp.TopLeftY = topLeftY;
	vp.Width = width;
	vp.Height = height;
	vp.MinDepth = zMin;
	vp.MaxDepth = zMax;

	return vp;
}

// static
QVector< VertexPosition4fNormal3fTexture2f > D3D10Utils::createBox( bool normalsPointOutward )
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
Reference< DynamicVertexBuffer > D3D10Utils::createAxes( ID3D10Device* pDevice )
{
	Reference< DynamicVertexBuffer > buffer = new DynamicVertexBuffer( pDevice, 6, VertexPosition4fColor4f::sizeInBytes() );

	VertexPosition4fColor4f* vertexArray = reinterpret_cast< VertexPosition4fColor4f* >( buffer->mapForWriteDiscard() );
	writeAxes( vertexArray );
	buffer->unmap();

	return buffer;
}

// static
void D3D10Utils::writeAxes( VertexPosition4fColor4f* vertexArray )
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
Reference< DynamicVertexBuffer > D3D10Utils::createFullScreenQuad( ID3D10Device* pDevice )
{
	Reference< DynamicVertexBuffer > buffer = new DynamicVertexBuffer( pDevice, 6, VertexPosition4f::sizeInBytes() );

	VertexPosition4f* vertexArray = reinterpret_cast< VertexPosition4f* >( buffer->mapForWriteDiscard() );
	writeFullScreenQuad( vertexArray );
	buffer->unmap();

	return buffer;
}

// static
Reference< DynamicVertexBuffer > D3D10Utils::createScreenAlignedQuad( float x, float y, float width, float height, ID3D10Device* pDevice )
{
	Reference< DynamicVertexBuffer > buffer = new DynamicVertexBuffer( pDevice, 6, VertexPosition4fTexture2f::sizeInBytes() );

	VertexPosition4fTexture2f* vertexArray = reinterpret_cast< VertexPosition4fTexture2f* >( buffer->mapForWriteDiscard() );
	writeScreenAlignedQuad( x, y, width, height, vertexArray );
	buffer->unmap();

	return buffer;
}

// static
void D3D10Utils::writeFullScreenQuad( VertexPosition4f* vertexArray )
{
	vertexArray[ 0 ] = VertexPosition4f( -1, -1, 0, 1 );
	vertexArray[ 1 ] = VertexPosition4f( 1, -1, 0, 1 );
	vertexArray[ 2 ] = VertexPosition4f( -1, 1, 0, 1 );

	vertexArray[ 3 ] = VertexPosition4f( -1, 1, 0, 1 );
	vertexArray[ 4 ] = VertexPosition4f( 1, -1, 0, 1 );
	vertexArray[ 5 ] = VertexPosition4f( 1, 1, 0, 1 );
}

// static
void D3D10Utils::writeScreenAlignedQuad( float x, float y, float width, float height, VertexPosition4fTexture2f* vertexArray, bool flipUV )
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
Reference< DynamicTexture2D > D3D10Utils::createTextureFromFile( ID3D10Device* pDevice, QString filename, bool flipUV )
{
	Reference< Image4ub > im = new Image4ub( filename );
	return createTextureFromImage( pDevice, im, flipUV );
}

// static
Reference< DynamicTexture2D > D3D10Utils::createTextureFromImage( ID3D10Device* pDevice, Reference< Image4ub > im, bool flipUV )
{
	Reference< DynamicTexture2D > pTexture = DynamicTexture2D::createUnsignedByte4( pDevice, im->width(), im->height() );
	copyImageToTexture( im, pTexture, flipUV );
	return pTexture;
}

// static
Reference< DynamicTexture2D > D3D10Utils::createTextureFromImage( ID3D10Device* pDevice, Reference< Image4f > im, bool flipUV )
{
	Reference< DynamicTexture2D > pTexture = DynamicTexture2D::createFloat4( pDevice, im->width(), im->height() );
	copyImageToTexture( im, pTexture, flipUV );
	return pTexture;
}

// static
Reference< DynamicTexture2D > D3D10Utils::createTextureFromImage( ID3D10Device* pDevice, Reference< Image1f > im, bool flipUV )
{
	Reference< DynamicTexture2D > pTexture = DynamicTexture2D::createFloat1( pDevice, im->width(), im->height() );
	copyImageToTexture( im, pTexture, flipUV );
	return pTexture;
}

// static
void D3D10Utils::copyImageToTexture( Reference< Image1f > im, Reference< DynamicTexture2D > tex, bool flipUV )
{
	int width = im->width();
	int height = im->height();

	D3D10_MAPPED_TEXTURE2D mapping = tex->map();

	float* sourceData = im->pixels();
	quint8* destData = reinterpret_cast< quint8* >( mapping.pData );

	for( int y = 0; y < height; ++y )
	{
		int yy = flipUV ? height - y - 1 : y;

		float* sourceRow = &( sourceData[ yy * width ] );
		quint8* destRow = &( destData[ y * mapping.RowPitch ] );

		memcpy( destRow, sourceRow, width * sizeof( float ) );
	}	

	tex->unmap();
}

// static
void D3D10Utils::copyImageToTexture( Reference< Image4f > im, Reference< DynamicTexture2D > tex, bool flipUV )
{
	int width = im->width();
	int height = im->height();

	D3D10_MAPPED_TEXTURE2D mapping = tex->map();

	float* sourceData = im->pixels();
	ubyte* destDataBytes = reinterpret_cast< ubyte* >( mapping.pData );

	// if the pitch matches and no flip is requested
	// then just directly copy
	if( mapping.RowPitch == 4 * width * sizeof( float ) && !flipUV )
	{
		float* destDataFloat = reinterpret_cast< float* >( mapping.pData );
		memcpy( destDataFloat, sourceData, 4 * width * height * sizeof( float ) );
	}
	// otherwise, have to go row by row
	else
	{
		for( int y = 0; y < height; ++y )
		{
			int yy = flipUV ? height - y - 1 : y;

			float* sourceRow = &( sourceData[ 4 * yy * width ] );
			ubyte* destRow = &( destDataBytes[ y * mapping.RowPitch ] );

			memcpy( destRow, sourceRow, 4 * width * sizeof( float ) );
		}
	}

	tex->unmap();
}


// static
void D3D10Utils::copyImageToTexture( Reference< Image4ub > im, Reference< DynamicTexture2D > tex, bool flipUV )
{
	int width = im->width();
	int height = im->height();

	D3D10_MAPPED_TEXTURE2D mapping = tex->map();

	quint8* sourceData = im->pixels();
	quint8* destData = reinterpret_cast< quint8* >( mapping.pData );

	for( int y = 0; y < height; ++y )
	{
		int yy = flipUV ? height - y - 1 : y;

		quint8* sourceRow = &( sourceData[ 4 * yy * width ] );
		quint8* destRow = &( destData[ y * mapping.RowPitch ] );

		memcpy( destRow, sourceRow, 4 * width * sizeof( quint8 ) );
	}	

	tex->unmap();
}

// static
void D3D10Utils::copyTextureToImage( ID3D10Device* pDevice, ID3D10Texture2D* pTexture, Reference< Image1f > im )
{
	D3D10_TEXTURE2D_DESC desc;
	pTexture->GetDesc( &desc );

	int width = desc.Width;
	int height = desc.Height;

	Reference< StagingTexture2D > pST;

	if( desc.Format == DXGI_FORMAT_R32_FLOAT )
	{
		pST = StagingTexture2D::createFloat1( pDevice, width, height );
	}

	if( desc.Format == DXGI_FORMAT_R32_FLOAT )
	{
		pST->copyFrom( pTexture );
		D3D10_MAPPED_TEXTURE2D mt = pST->map();
		float* sourceData = reinterpret_cast< float* >( mt.pData );

		for( int y = 0; y < height; ++y )
		{
			int yy = height - y - 1;

			float* sourceRow = &( sourceData[ y * mt.RowPitch / sizeof( float ) ] );
			for( int x = 0; x < width; ++x )
			{
				float g = sourceRow[ x ];
				im->setPixel( x, yy, g );
			}
		}

		pST->unmap();
	}
	else
	{
		printf( "Warning: unable to copy texture to image, format is unsupported\n" );
	}
}

// static
void D3D10Utils::copyTextureToImage( ID3D10Device* pDevice, ID3D10Texture2D* pTexture, Reference< Image1i > im )
{
	D3D10_TEXTURE2D_DESC desc;
	pTexture->GetDesc( &desc );

	int width = desc.Width;
	int height = desc.Height;
	
	Reference< StagingTexture2D > pST;

	if( desc.Format == DXGI_FORMAT_R16_UNORM )
	{
		pST = StagingTexture2D::createUnsignedShort1UNorm( pDevice, width, height );
	}
	else if( desc.Format == DXGI_FORMAT_R16_UINT )
	{
		pST = StagingTexture2D::createUnsignedShort1( pDevice, width, height );
	}

	if( desc.Format == DXGI_FORMAT_R16_UNORM ||
		desc.Format == DXGI_FORMAT_R16_UINT )
	{
		pST->copyFrom( pTexture );
		D3D10_MAPPED_TEXTURE2D mt = pST->map();
		ubyte* sourceData = reinterpret_cast< ubyte* >( mt.pData );

		for( int y = 0; y < height; ++y )
		{
			int yy = height - y - 1;

			ushort* sourceRow = reinterpret_cast< ushort* >( &( sourceData[ y * mt.RowPitch ] ) );
			for( int x = 0; x < width; ++x )
			{
				ushort r = sourceRow[ x ];
				im->setPixel( x, yy, r );
			}
		}
		pST->unmap();
	}
	else
	{
		printf( "Warning: unable to copy texture to image, format is unsupported\n" );
	}
}

// static
void D3D10Utils::copyTextureToImage( ID3D10Device* pDevice, ID3D10Texture2D* pTexture, Reference< Image4ub > im )
{
	D3D10_TEXTURE2D_DESC desc;
	pTexture->GetDesc( &desc );

	int width = desc.Width;
	int height = desc.Height;

	Reference< StagingTexture2D > pST;

	if( desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM )
	{
		pST = StagingTexture2D::createUnsignedByte4( pDevice, width, height );
	}

	if( desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM )
	{
		pST->copyFrom( pTexture );
		D3D10_MAPPED_TEXTURE2D mt = pST->map();
		ubyte* sourceData = reinterpret_cast< ubyte* >( mt.pData );

		for( int y = 0; y < height; ++y )
		{
			int yy = height - y - 1;

			ubyte* sourceRow = &( sourceData[ y * mt.RowPitch ] );
			for( int x = 0; x < width; ++x )
			{
				ubyte r = sourceRow[ 4 * x ];
				ubyte g = sourceRow[ 4 * x + 1 ];
				ubyte b = sourceRow[ 4 * x + 2 ];
				ubyte a = sourceRow[ 4 * x + 3 ];
				im->setPixel( x, yy, Vector4i( r, g, b, a ) );
			}
		}
		pST->unmap();
	}
	else
	{
		printf( "Warning: unable to copy texture to image, format is unsupported\n" );
	}
}

// static
void D3D10Utils::copyTextureToImage( ID3D10Device* pDevice, ID3D10Texture2D* pTexture, Reference< Image4f > im )
{
	D3D10_TEXTURE2D_DESC desc;
	pTexture->GetDesc( &desc );

	int width = desc.Width;
	int height = desc.Height;

	Reference< StagingTexture2D > pST;

	if( desc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT )
	{
		pST = StagingTexture2D::createFloat4( pDevice, width, height );
	}

	if( desc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT )
	{
		pST->copyFrom( pTexture );
		D3D10_MAPPED_TEXTURE2D mt = pST->map();
		ubyte* sourceData = reinterpret_cast< ubyte* >( mt.pData );

		for( int y = 0; y < height; ++y )
		{
			int yy = height - y - 1;

			float* sourceRow = reinterpret_cast< float* >( &( sourceData[ y * mt.RowPitch ] ) );
			for( int x = 0; x < width; ++x )
			{
				float r = sourceRow[ 4 * x ];
				float g = sourceRow[ 4 * x + 1 ];
				float b = sourceRow[ 4 * x + 2 ];
				float a = sourceRow[ 4 * x + 3 ];
				im->setPixel( x, yy, Vector4f( r, g, b, a ) );
			}
		}
		pST->unmap();
	}
	else
	{
		printf( "Warning: unable to copy texture to image, format is unsupported\n" );
	}
}

// static
void D3D10Utils::saveTextureToBinary( ID3D10Device* pDevice, ID3D10Texture2D* pTexture, QString filename )
{
	// TODO: port Array2D to libcgt
	D3D10_TEXTURE2D_DESC desc;
	pTexture->GetDesc( &desc );

	int width = desc.Width;
	int height = desc.Height;

	if( desc.Format == DXGI_FORMAT_R16_UINT )
	{
		Reference< Image1i > im0 = new Image1i( width, height );
		copyTextureToImage( pDevice, pTexture, im0 );
		Reference< Image1i > im = im0->flipUD();

		qint32* pixels = im->pixels();

		FILE* fp = fopen( filename.toAscii().constData(), "wb" );
		fwrite( &width, sizeof( int ), 1, fp );
		fwrite( &height, sizeof( int ), 1, fp );
		fwrite( pixels, sizeof( int ), width * height, fp );
		fflush( fp );
		fclose( fp );
	}
	else if( desc.Format == DXGI_FORMAT_R32_FLOAT )
	{
		Reference< Image1f > im0 = new Image1f( width, height );
		copyTextureToImage( pDevice, pTexture, im0 );		
		Image1f im = im0->flipUD();

		float* pixels = im.pixels();

		FILE* fp = fopen( filename.toAscii().constData(), "wb" );
		fwrite( &width, sizeof( int ), 1, fp );
		fwrite( &height, sizeof( int ), 1, fp );
		fwrite( pixels, sizeof( float ), width * height, fp );
		fflush( fp );
		fclose( fp );
	}
}

// static
void D3D10Utils::saveTextureToPFM( ID3D10Device* pDevice, ID3D10Texture2D* pTexture, QString filename )
{
	D3D10_TEXTURE2D_DESC desc;
	pTexture->GetDesc( &desc );

	int width = desc.Width;
	int height = desc.Height;

	switch( desc.Format )
	{
		case DXGI_FORMAT_R32_FLOAT:
		{
			Reference< Image1f > im = new Image1f( width, height );
			copyTextureToImage( pDevice, pTexture, im );
			im->save( filename );
			break;
		}
	}
}

// static
void D3D10Utils::saveTextureToPNG( ID3D10Device* pDevice, ID3D10Texture2D* pTexture, QString filename, bool scale, float factor )
{
	D3D10_TEXTURE2D_DESC desc;
	pTexture->GetDesc( &desc );

	int width = desc.Width;
	int height = desc.Height;

	switch( desc.Format )
	{
	case DXGI_FORMAT_R16_UNORM:
	case DXGI_FORMAT_R16_UINT:
		{
			Reference< Image1i > im = new Image1i( width, height );
			copyTextureToImage( pDevice, pTexture, im );

			if( scale )
			{
				int n = im->width() * im->height();
				qint32* pixels = im->pixels();				
				for( int i = 0; i < n; ++i )
				{
					int p = pixels[ i ];
					float f = factor * p + 0.5f;
					p = static_cast< qint32 >( f );
					pixels[ i ] = ColorUtils::saturate( p );
				}
			}

			im->savePNG( filename );
			break;
		}
	case DXGI_FORMAT_R8G8B8A8_UNORM:
		{
			Reference< Image4ub > im = new Image4ub( width, height );
			copyTextureToImage( pDevice, pTexture, im );
			im->save( filename );
			break;
		}
	case DXGI_FORMAT_R32_FLOAT:
		{
			Reference< Image1f > im = new Image1f( width, height );
			copyTextureToImage( pDevice, pTexture, im );
			im->save( filename );
			break;
		}
	case DXGI_FORMAT_R32G32B32A32_FLOAT:
		{
			Reference< Image4f > im = new Image4f( width, height );
			copyTextureToImage( pDevice, pTexture, im );
			im->save( filename );
			break;
		}
	}
}

// static
void D3D10Utils::saveTextureToTXT( ID3D10Device* pDevice, ID3D10Texture2D* pTexture, QString filename )
{
	D3D10_TEXTURE2D_DESC desc;
	pTexture->GetDesc( &desc );

	int width = desc.Width;
	int height = desc.Height;

	switch( desc.Format )
	{
	case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16_UINT:
		{
			Reference< Image1i > im = new Image1i( width, height );
			copyTextureToImage( pDevice, pTexture, im );
			im->saveTXT( filename );
			break;
		}
	default:
		{
			printf( "saveTextureToTXT: texture format unsupported\n" );
		}
	}
}