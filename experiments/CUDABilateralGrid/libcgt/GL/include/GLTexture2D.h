#ifndef GL_TEXTURE_2D_H
#define GL_TEXTURE_2D_H

#include <GL/glew.h>

#include <common/BasicTypes.h>
#include "GLTexture.h"

class GLTexture2D : public GLTexture
{
public:

	static void setEnabled( bool bEnabled );

	// creates a depth texture	
	static GLTexture2D* createDepthTexture( int width, int height, int nBits = 32, const uint* auiData = 0 );

	// creates a ubyte1 (8 bits luminance for each pixel) texture
	static GLTexture2D* createUnsignedByte1Texture( int width, int height, const ubyte* aubData = 0 );

	// creates a ubyte3 (8 bits for each component) texture
	static GLTexture2D* createUnsignedByte3Texture( int width, int height, const ubyte* aubData = 0 );

	// creates a ubyte4 (8 bits for each component) texture
	static GLTexture2D* createUnsignedByte4Texture( int width, int height, const ubyte* aubData = 0 );

	// creates a float1 texture
	static GLTexture2D* createFloat1Texture( int width, int height, int nBitsPerComponent = 32, const float* afData = 0 );

	// creates a float3 texture
	static GLTexture2D* createFloat3Texture( int width, int height, int nBitsPerComponent = 32, const float* afData = 0 );

	// creates a float4 texture	
	static GLTexture2D* createFloat4Texture( int width, int height, int nBitsPerComponent = 32, const float* afData = 0 );

	// uploads float array to hardware
	// by default, assumes that the entire texture is being updated
	// (pass in width and height = 0)
	// otherwise, specify the rectangle
	void setFloat1Data( const float* afData, int xOffset = 0, int yOffset = 0, int width = 0, int height = 0 );
	void setFloat3Data( const float* afData, int xOffset = 0, int yOffset = 0, int width = 0, int height = 0 );
	void setFloat4Data( const float* afData, int xOffset = 0, int yOffset = 0, int width = 0, int height = 0 );

	// uploads unsigned byte array to hardware
	// by default, assumes that the entire texture is being updated
	// (pass in width and height = 0)
	// otherwise, specify the rectangle
	void setUnsignedByte1Data( const GLubyte* aubData,
		int xOffset = 0, int yOffset = 0,
		int width = 0, int height = 0 );
	void setUnsignedByte3Data( const GLubyte* aubData,
		int xOffset = 0, int yOffset = 0,
		int width = 0, int height = 0 );	
	void setUnsignedByte4Data( const GLubyte* aubData,
		int xOffset = 0, int yOffset = 0,
		int width = 0, int height = 0 );

	int getWidth();
	int getHeight();	

	// sets the wrap mode of the currently bound texture
	virtual void setAllWrapModes( GLint iMode );

	// always dumps as RGBA and float mode
	virtual void dumpToTXT( QString filename, GLint level = 0, GLenum format = GL_RGBA, GLenum type = GL_FLOAT );
	virtual void dumpToCSV( QString filename );

	// always dumps as RGBA and unsigned byte mode
	void dumpToPPM( QString filename );	
	void dumpToPNG( QString filename );

	// always dumps as RGB and float mode
	void dumpToPFM( QString filename );

private:

	// wrapper around constructor
	// creates the texture, sets wrap mode to clamp, and filter mode to nearest
	static GLTexture2D* createTexture2D( int width, int height,
		GLTexture::GLTextureInternalFormat internalFormat );

	GLTexture2D( int width, int height,
		GLTexture::GLTextureInternalFormat internalFormat );

	int m_iWidth;
	int m_iHeight;
};

#endif
