#ifndef D3D11UTILS_TEXTURE_H
#define D3D11UTILS_TEXTURE_H

#include <memory>
#include <D3D11.h>
#include <D3DX11.h>

#include <imageproc/Image1f.h>
#include <imageproc/Image1i.h>
#include <imageproc/Image4f.h>
#include <imageproc/Image4ub.h>

#include "DynamicTexture2D.h"

// TODO: shared_ptr< Image > --> Image

class D3D11Utils_Texture
{
public:

	// Loads a texture from a standard image file, 8-bits per color channel
	// By default, Image4ub loads images such that the bottom left has coordinates (0,0) and is at location 0 memory
	// set flipUV to true to flip it up/down
	static std::shared_ptr< DynamicTexture2D > createTextureFromFile( ID3D11Device* pDevice, QString filename, bool flipUV = true );

	// TODO: const Image<T>&
	static std::shared_ptr< DynamicTexture2D > createTextureFromImage( ID3D11Device* pDevice, std::shared_ptr< Image1f > im, bool flipUV = true );
	static std::shared_ptr< DynamicTexture2D > createTextureFromImage( ID3D11Device* pDevice, std::shared_ptr< Image4f > im, bool flipUV = true );
	static std::shared_ptr< DynamicTexture2D > createTextureFromImage( ID3D11Device* pDevice, Image4ub& im, bool flipUV = true );

	// image (cpu) --> texture (gpu)
	static void copyImageToTexture( std::shared_ptr< Image1f > im, std::shared_ptr< DynamicTexture2D > tex, bool flipUV = true );
	static void copyImageToTexture( std::shared_ptr< Image4f > im, std::shared_ptr< DynamicTexture2D > tex, bool flipUV = true );
	static void copyImageToTexture( const Image4ub& im, std::shared_ptr< DynamicTexture2D > tex, bool flipUV = true );

	// texture (gpu) --> image (cpu)
	static void copyTextureToImage( ID3D11Device* pDevice, ID3D11Texture2D* pTexture, std::shared_ptr< Image1f > im );
	static void copyTextureToImage( ID3D11Device* pDevice, ID3D11Texture2D* pTexture, std::shared_ptr< Image1i > im );
	static void copyTextureToImage( ID3D11Device* pDevice, ID3D11Texture2D* pTexture, std::shared_ptr< Image4ub > im );
	static void copyTextureToImage( ID3D11Device* pDevice, ID3D11Texture2D* pTexture, std::shared_ptr< Image4f > im );

	// texture (gpu) --> file (disk)
	static void saveTextureToPFM( ID3D11Device* pDevice, ID3D11Texture2D* pTexture, QString filename );
	static void saveTextureToPFM4( ID3D11Device* pDevice, ID3D11Texture2D* pTexture, QString filename );
	static void saveTextureToPNG( ID3D11Device* pDevice, ID3D11Texture2D* pTexture, QString filename, bool scale = false, float factor = 1.f );
	static void saveTextureToTXT( ID3D11Device* pDevice, ID3D11Texture2D* pTexture, QString filename );
	
	// HACK
	static void saveTextureToBinary( ID3D11Device* pDevice, ID3D11Texture2D* pTexture, QString filename );
};

#endif // D3D11UTILS_TEXTURE_H
