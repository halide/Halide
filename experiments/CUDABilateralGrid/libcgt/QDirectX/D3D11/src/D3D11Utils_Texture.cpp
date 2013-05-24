#include "D3D11Utils_Texture.h"

#include <color/ColorUtils.h>

#include "StagingTexture2D.h"

// static
std::shared_ptr< DynamicTexture2D > D3D11Utils_Texture::createTextureFromFile( ID3D11Device* pDevice, QString filename, bool flipUV )
{
	Image4ub im( filename );
	return createTextureFromImage( pDevice, im, flipUV );
}

// static
std::shared_ptr< DynamicTexture2D > D3D11Utils_Texture::createTextureFromImage( ID3D11Device* pDevice, std::shared_ptr< Image1f > im, bool flipUV )
{
	std::shared_ptr< DynamicTexture2D > pTexture( DynamicTexture2D::createFloat1( pDevice, im->width(), im->height() ) );
	copyImageToTexture( im, pTexture, flipUV );
	return pTexture;
}

// static
std::shared_ptr< DynamicTexture2D > D3D11Utils_Texture::createTextureFromImage( ID3D11Device* pDevice, std::shared_ptr< Image4f > im, bool flipUV )
{
	std::shared_ptr< DynamicTexture2D > pTexture( DynamicTexture2D::createFloat4( pDevice, im->width(), im->height() ) );
	copyImageToTexture( im, pTexture, flipUV );
	return pTexture;
}

// static
std::shared_ptr< DynamicTexture2D > D3D11Utils_Texture::createTextureFromImage( ID3D11Device* pDevice, Image4ub& im, bool flipUV )
{
	std::shared_ptr< DynamicTexture2D > pTexture( DynamicTexture2D::createUnsignedByte4( pDevice, im.width(), im.height() ) );
	copyImageToTexture( im, pTexture, flipUV );
	return pTexture;
}


// static
void D3D11Utils_Texture::copyImageToTexture( std::shared_ptr< Image1f > im, std::shared_ptr< DynamicTexture2D > tex, bool flipUV )
{
	int width = im->width();
	int height = im->height();

	D3D11_MAPPED_SUBRESOURCE mapping = tex->mapForWriteDiscard();

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
void D3D11Utils_Texture::copyImageToTexture( std::shared_ptr< Image4f > im, std::shared_ptr< DynamicTexture2D > tex, bool flipUV )
{
	int width = im->width();
	int height = im->height();

	D3D11_MAPPED_SUBRESOURCE mapping = tex->mapForWriteDiscard();

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
void D3D11Utils_Texture::copyImageToTexture( const Image4ub& im, std::shared_ptr< DynamicTexture2D > tex, bool flipUV )
{
	int width = im.width();
	int height = im.height();

	D3D11_MAPPED_SUBRESOURCE mapping = tex->mapForWriteDiscard();

	const quint8* sourceData = im.pixels();
	quint8* destData = reinterpret_cast< quint8* >( mapping.pData );

	for( int y = 0; y < height; ++y )
	{
		int yy = flipUV ? height - y - 1 : y;

		const quint8* sourceRow = &( sourceData[ 4 * yy * width ] );
		quint8* destRow = &( destData[ y * mapping.RowPitch ] );

		memcpy( destRow, sourceRow, 4 * width * sizeof( quint8 ) );
	}	

	tex->unmap();
}

// static
void D3D11Utils_Texture::copyTextureToImage( ID3D11Device* pDevice, ID3D11Texture2D* pTexture, std::shared_ptr< Image1f > im )
{
	D3D11_TEXTURE2D_DESC desc;
	pTexture->GetDesc( &desc );

	int width = desc.Width;
	int height = desc.Height;

	std::shared_ptr< StagingTexture2D > pST;

	if( desc.Format == DXGI_FORMAT_R32_FLOAT )
	{
		pST.reset( StagingTexture2D::createFloat1( pDevice, width, height ) );
	}

	if( desc.Format == DXGI_FORMAT_R32_FLOAT )
	{
		pST->copyFrom( pTexture );
		D3D11_MAPPED_SUBRESOURCE mt = pST->mapForReadWrite();
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
void D3D11Utils_Texture::copyTextureToImage( ID3D11Device* pDevice, ID3D11Texture2D* pTexture, std::shared_ptr< Image1i > im )
{
	D3D11_TEXTURE2D_DESC desc;
	pTexture->GetDesc( &desc );

	int width = desc.Width;
	int height = desc.Height;
	
	std::shared_ptr< StagingTexture2D > pST;

	if( desc.Format == DXGI_FORMAT_R16_UNORM )
	{
		pST.reset( StagingTexture2D::createUnsignedShort1UNorm( pDevice, width, height ) );
	}
	else if( desc.Format == DXGI_FORMAT_R16_UINT )
	{
		pST.reset( StagingTexture2D::createUnsignedShort1( pDevice, width, height ) );
	}

	if( desc.Format == DXGI_FORMAT_R16_UNORM ||
		desc.Format == DXGI_FORMAT_R16_UINT )
	{
		pST->copyFrom( pTexture );
		D3D11_MAPPED_SUBRESOURCE mt = pST->mapForReadWrite();
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
void D3D11Utils_Texture::copyTextureToImage( ID3D11Device* pDevice, ID3D11Texture2D* pTexture, std::shared_ptr< Image4ub > im )
{
	D3D11_TEXTURE2D_DESC desc;
	pTexture->GetDesc( &desc );

	int width = desc.Width;
	int height = desc.Height;

	std::shared_ptr< StagingTexture2D > pST;

	if( desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM )
	{
		pST.reset( StagingTexture2D::createUnsignedByte4( pDevice, width, height ) );
	}

	if( desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM )
	{
		pST->copyFrom( pTexture );
		D3D11_MAPPED_SUBRESOURCE mt = pST->mapForReadWrite();
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
void D3D11Utils_Texture::copyTextureToImage( ID3D11Device* pDevice, ID3D11Texture2D* pTexture, std::shared_ptr< Image4f > im )
{
	D3D11_TEXTURE2D_DESC desc;
	pTexture->GetDesc( &desc );

	int width = desc.Width;
	int height = desc.Height;

	std::shared_ptr< StagingTexture2D > pST;

	if( desc.Format == DXGI_FORMAT_R32G32_FLOAT )
	{
		pST.reset( StagingTexture2D::createFloat2( pDevice, width, height ) );
	}
	else if( desc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT )
	{
		pST.reset( StagingTexture2D::createFloat4( pDevice, width, height ) );
	}

	if( desc.Format == DXGI_FORMAT_R32G32_FLOAT )
	{
		pST->copyFrom( pTexture );
		D3D11_MAPPED_SUBRESOURCE mt = pST->mapForReadWrite();
		ubyte* sourceData = reinterpret_cast< ubyte* >( mt.pData );

		for( int y = 0; y < height; ++y )
		{
			int yy = height - y - 1;

			float* sourceRow = reinterpret_cast< float* >( &( sourceData[ y * mt.RowPitch ] ) );
			for( int x = 0; x < width; ++x )
			{
				float r = sourceRow[ 2 * x ];
				float g = sourceRow[ 2 * x + 1 ];
				im->setPixel( x, yy, Vector4f( r, g, 0, 1 ) );
			}
		}
		pST->unmap();
	}
	else if( desc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT )
	{
		pST->copyFrom( pTexture );
		D3D11_MAPPED_SUBRESOURCE mt = pST->mapForReadWrite();
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
void D3D11Utils_Texture::saveTextureToPFM( ID3D11Device* pDevice, ID3D11Texture2D* pTexture, QString filename )
{
	D3D11_TEXTURE2D_DESC desc;
	pTexture->GetDesc( &desc );

	int width = desc.Width;
	int height = desc.Height;

	switch( desc.Format )
	{
		case DXGI_FORMAT_R32_FLOAT:
		{
			std::shared_ptr< Image1f > im( new Image1f( width, height ) );
			copyTextureToImage( pDevice, pTexture, im );
			im->save( filename );
			break;
		}
		case DXGI_FORMAT_R32G32_FLOAT:
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
		{
			std::shared_ptr< Image4f > im( new Image4f( width, height ) );
			copyTextureToImage( pDevice, pTexture, im );
			im->save( filename );
			break;		
		}
		default:
		{			
			printf( "Unsupported format, TODO: USHORT_16 for depths\n" );
			break;
		}
	}
}

// static
void D3D11Utils_Texture::saveTextureToPFM4( ID3D11Device* pDevice, ID3D11Texture2D* pTexture, QString filename )
{
	D3D11_TEXTURE2D_DESC desc;
	pTexture->GetDesc( &desc );

	int width = desc.Width;
	int height = desc.Height;

	switch( desc.Format )
	{
	case DXGI_FORMAT_R32G32B32A32_FLOAT:
		{
			std::shared_ptr< Image4f > im( new Image4f( width, height ) );
			copyTextureToImage( pDevice, pTexture, im );
			im->save( filename );
			break;		
		}
	default:
		{			
			printf( "Unsupported format, TODO: USHORT_16 for depths\n" );
			break;
		}
	}
}

// static
void D3D11Utils_Texture::saveTextureToPNG( ID3D11Device* pDevice, ID3D11Texture2D* pTexture, QString filename, bool scale, float factor )
{
	D3D11_TEXTURE2D_DESC desc;
	pTexture->GetDesc( &desc );

	int width = desc.Width;
	int height = desc.Height;

	switch( desc.Format )
	{
	case DXGI_FORMAT_R16_UNORM:
	case DXGI_FORMAT_R16_UINT:
		{
			std::shared_ptr< Image1i > im( new Image1i( width, height ) );
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
			std::shared_ptr< Image4ub > im( new Image4ub( width, height ) );
			copyTextureToImage( pDevice, pTexture, im );
			im->save( filename );
			break;
		}
	case DXGI_FORMAT_R32_FLOAT:
		{
			std::shared_ptr< Image1f > im( new Image1f( width, height ) );
			copyTextureToImage( pDevice, pTexture, im );
			im->save( filename );
			break;
		}
	case DXGI_FORMAT_R32G32B32A32_FLOAT:
		{
			std::shared_ptr< Image4f > im( new Image4f( width, height ) );
			copyTextureToImage( pDevice, pTexture, im );
			im->save( filename );
			break;
		}
	}
}

// static
void D3D11Utils_Texture::saveTextureToTXT( ID3D11Device* pDevice, ID3D11Texture2D* pTexture, QString filename )
{
	D3D11_TEXTURE2D_DESC desc;
	pTexture->GetDesc( &desc );

	int width = desc.Width;
	int height = desc.Height;

	switch( desc.Format )
	{
	case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16_UINT:
		{
			std::shared_ptr< Image1i > im( new Image1i( width, height ) );
			copyTextureToImage( pDevice, pTexture, im );
			im->saveTXT( filename );
			break;
		}
	case DXGI_FORMAT_R32_FLOAT:
		{
			std::shared_ptr< Image1f > im( new Image1f( width, height ) );
			copyTextureToImage( pDevice, pTexture, im );
			im->save( filename );
			break;
		}
	case DXGI_FORMAT_R32G32_FLOAT:
	case DXGI_FORMAT_R32G32B32A32_FLOAT:
		{
			std::shared_ptr< Image4f > im( new Image4f( width, height ) );
			copyTextureToImage( pDevice, pTexture, im );
			im->save( filename );
			break;
		}
	default:
		{
			printf( "saveTextureToTXT: texture format unsupported\n" );
		}
	}
}


// static
void D3D11Utils_Texture::saveTextureToBinary( ID3D11Device* pDevice, ID3D11Texture2D* pTexture, QString filename )
{
	// TODO: port Array2D to libcgt
	D3D11_TEXTURE2D_DESC desc;
	pTexture->GetDesc( &desc );

	int width = desc.Width;
	int height = desc.Height;

	if( desc.Format == DXGI_FORMAT_R16_UINT )
	{
		std::shared_ptr< Image1i > im0( new Image1i( width, height ) );
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
		std::shared_ptr< Image1f > im0( new Image1f( width, height ) );
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