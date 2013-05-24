#ifndef GL_TEXTURE_RECTANGLE_H
#define GL_TEXTURE_RECTANGLE_H

#include <GL/glew.h>

#include "common/BasicTypes.h"
#include "common/Reference.h"
#include "imageproc/Image1f.h"
#include "imageproc/Image3ub.h"
#include "imageproc/Image4f.h"
#include "imageproc/Image4ub.h"
#include "vecmath/Vector2i.h"

#include "GLTexture.h"

class GLTextureRectangle : public GLTexture
{
public:

	// TODO: getFloatData() --> pass in a buffer and return a buffer.  If one passed in is NULL, creates a new one
	// TODO: make nBitsPercomponent an enum
	// TODO: integer texture: nBits = 8 (ubyte), 16, 32, signed/unsigned
	// TODO: getIntegerFormat( int nBits, bool signed )

	static void setEnabled( bool bEnabled );	

	static Reference< GLTextureRectangle > create( Reference< Image4ub > image );
	static Reference< GLTextureRectangle > create( Reference< Image1f > image, int nBitsPerComponent = 32 );
	static Reference< GLTextureRectangle > create( Reference< Image4f > image, int nBitsPerComponent = 32 );

	// creates a depth texture	
	static Reference< GLTextureRectangle > createDepthTexture( int width, int height, int nBits = 32, const float* data = 0 );

	// creates a ubyte1 (8 bits luminance for each pixel) texture
	static GLTextureRectangle* createUnsignedByte1Texture( int width, int height, const ubyte* aubData = 0);

	// creates a ubyte3 (8 bits for each component) texture
	static Reference< GLTextureRectangle > createUnsignedByte3Texture( int width, int height, const ubyte* data = 0 );
	static Reference< GLTextureRectangle > createUnsignedByte3Texture( const Vector2i& size, const ubyte* data = 0 );

	// creates a ubyte4 (8 bits for each component) texture
	static Reference< GLTextureRectangle > createUnsignedByte4Texture( int width, int height, const ubyte* data = 0 );
	static Reference< GLTextureRectangle > createUnsignedByte4Texture( const Vector2i& size, const ubyte* data = 0 );

	// creates a float1 texture
	static Reference< GLTextureRectangle > createFloat1Texture( int width, int height, int nBitsPerComponent = 32, const float* afData = 0 );
	static Reference< GLTextureRectangle > createFloat1Texture( const Vector2i& size, int nBitsPerComponent = 32, const float* afData = 0 );

	// creates a float2 texture
	static GLTextureRectangle* createFloat2Texture( int width, int height, int nBitsPerComponent = 32, const float* afData = 0 );

	// creates a float3 texture
	static GLTextureRectangle* createFloat3Texture( int width, int height, int nBitsPerComponent = 32, const float* afData = 0 );

	// creates a float4 texture	
	static Reference< GLTextureRectangle > createFloat4Texture( int width, int height, int nBitsPerComponent = 32, const float* afData = 0 );
	static Reference< GLTextureRectangle > createFloat4Texture( const Vector2i& size, int nBitsPerComponent = 32, const float* data = 0 );	

	// uploads data to hardware
	void setData( Reference< Image1f > image );
	void setData( Reference< Image3ub > image );
	void setData( Reference< Image4ub > image );
	void setData( Reference< Image4f > image );
	
	Reference< Image3ub > getImage3ub( Reference< Image3ub > output = NULL );

	Reference< Image1f > getImage1f( Reference< Image1f > output = NULL );
	Reference< Image4f > getImage4f( Reference< Image4f > output = NULL );

	void setFloat1Data( const float* data );
	void setFloat3Data( const float* data );
	void setFloat4Data( const float* data );		

	void setUnsignedByte1Data( const GLubyte* data );
	void setUnsignedByte3Data( const GLubyte* data );
	void setUnsignedByte4Data( const GLubyte* data );

	// numElements = width * height
	int numElements();
	int getWidth();
	int getHeight();
	Vector2i size();

	// sets the wrap mode of the currently bound texture
	virtual void setAllWrapModes( GLint iMode );	

	// always dumps as RGBA and float mode
	virtual void dumpToCSV( QString filename );
	virtual void dumpToTXT( QString filename, GLint level = 0, GLenum format = GL_RGBA, GLenum type = GL_FLOAT );

	void savePPM( QString filename );
	void savePNG( QString filename );
	void savePFM( QString filename );

private:

	// wrapper around constructor
	// creates the texture, sets wrap mode to clamp, and filter mode to nearest
	static GLTextureRectangle* createTextureRectangle( int width, int height,
		GLTexture::GLTextureInternalFormat internalFormat );

	GLTextureRectangle( int width, int height,
		GLTexture::GLTextureInternalFormat internalFormat );

	int m_width;
	int m_height;
};

#endif
