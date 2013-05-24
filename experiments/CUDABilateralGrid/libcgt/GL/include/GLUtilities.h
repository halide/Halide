#ifndef GL_UTILITIES_H
#define GL_UTILITIES_H

#include <GL/glew.h>
#include "vecmath/Matrix4f.h"
#include "vecmath/Vector2i.h"
#include "vecmath/Rect2f.h"

class GLUtilities
{
public:

	static Matrix4f orthoCamera( int viewportWidth, int viewportHeight );

	static Matrix4f getProjectionMatrix();
	static Matrix4f getModelviewMatrix();

	// same thing as getProjectionMatrix() * getModelviewMatrix()
	static Matrix4f getModelviewProjectionMatrix();

	static void setupOrthoCamera( int viewportWidth, int viewportHeight );

	static void setupOrthoCamera( const Vector2i& viewportSize );

	// draw a screen aligned quad.  if zeroOneTextureCoordinates are true,
	// then the tex coordinates range from 0 to 1.  Otherwise, they will range
	// from 0 to width and 0 to height
	static void drawQuad( int width, int height, bool zeroOneTextureCoordinates = false, bool tTextureCoordinateZeroOnBottom = true );
	static void drawQuad( const Vector2i& size, bool zeroOneTextureCoordinates = false, bool tTextureCoordinateZeroOnBottom = true );
	static void drawQuad( int x, int y, int width, int height, bool zeroOneTextureCoordinates = false, bool tTextureCoordinateZeroOnBottom = true );

	static void drawQuad( const Rect2f& position, const Rect2f& textureCoordinates, bool flipTextureCoordinatesUpDown = false );

	static void drawCross( float width, float height );

	static float* readDepthBuffer( int x, int y, int width, int height );
	static void dumpDepthBufferText( int x, int y, int width, int height, const char* szFilename );
	static void dumpFrameBufferLuminanceText( int x, int y, int width, int height, const char* szFilename );
	static void dumpFrameBufferLuminanceBinary( int x, int y, int width, int height, const char* szFilename );
	static void dumpFrameBufferRGBABinary( int x, int y, int width, int height, const char* szFilename );	
	static void dumpFrameBufferRGBAText( int x, int y, int width, int height, const char* szFilename );	

	static void printGLRenderer();
	static void printGLVendor();
	static void printGLVersion();	
	
	// prints the last GL error if there was one
	// returns true if there was NO error
	static bool printGLLastError();

private:

};

#endif
