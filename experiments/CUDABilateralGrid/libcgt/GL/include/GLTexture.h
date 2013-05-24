#ifndef GL_TEXTURE_H
#define GL_TEXTURE_H

#include <GL/glew.h>
#include <common/BasicTypes.h>
#include <QString>

class GLTexture
{
public:

	// TODO: make getNumComponents() work always (depending on setData())
	// getData() and dump() should automatically know	

	// TODO: mipmaps
	enum GLTextureFilterMode
	{
		GLTextureFilterMode_NEAREST = GL_NEAREST,
		GLTextureFilterMode_LINEAR = GL_LINEAR
	};

	// TODO: add prefixes
	enum GLTextureInternalFormat
	{		
		DEPTH_COMPONENT_FLOAT_16 = GL_DEPTH_COMPONENT16,
		DEPTH_COMPONENT_FLOAT_24 = GL_DEPTH_COMPONENT24,
		DEPTH_COMPONENT_FLOAT_32 = GL_DEPTH_COMPONENT32,

		LUMINANCE_UNSIGNED_BYTE_8 = GL_LUMINANCE8,
		LUMINANCE_FLOAT_16 = GL_LUMINANCE16F_ARB,
		LUMINANCE_FLOAT_32 = GL_LUMINANCE32F_ARB,

		// for older hardware
		RED_FLOAT_16 = GL_FLOAT_R16_NV,
		RED_FLOAT_32 = GL_FLOAT_R32_NV,

		LUMINANCE_ALPHA_FLOAT_16 = GL_LUMINANCE_ALPHA16F_ARB,
		LUMINANCE_ALPHA_FLOAT_32 = GL_LUMINANCE_ALPHA32F_ARB,

		RGB_UNSIGNED_BYTE_8 = GL_RGB8,
		RGB_FLOAT_16 = GL_RGB16F_ARB,
		RGB_FLOAT_32 = GL_RGB32F_ARB,

		RGBA_UNSIGNED_BYTE_8 = GL_RGBA8,
		RGBA_FLOAT_16 = GL_RGBA16F_ARB,
		RGBA_FLOAT_32 = GL_RGBA32F_ARB
	};

	static int getMaxTextureSize();
	static GLfloat getLargestSupportedAnisotropy();

	virtual ~GLTexture();

	// binds this texture object to the currently active texture unit	
	void bind();

	// unbinds a texture from the currently active texture unit
	void unbind();

	GLuint getTextureId();
	GLenum getTarget();
	GLTextureInternalFormat getInternalFormat();
	int getNumComponents();
	int getNumBitsPerComponent();

	GLTextureFilterMode getMinFilterMode();
	GLTextureFilterMode getMagFilterMode();

	void setFilterModeNearest();
	void setFilterModeLinear();

	void setFilterMode( GLTextureFilterMode minAndMagMode );
	void setFilterMode( GLTextureFilterMode minFilterMode, GLTextureFilterMode magFilterMode );
	
	void setAnisotropicFiltering( GLfloat anisotropy );

	// eParam: GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R
	// TODO: use an enum
	void setWrapMode( GLenum eParam, GLint iMode );

	// iMode = GL_CLAMP_TO_EDGE, etc
	// TODO: use an enum
	virtual void setAllWrapModes( GLint iMode ) = 0;

	virtual void getFloat1Data( float* afOutput, int level = 0 );
	virtual void getFloat3Data( float* afOutput, int level = 0 );
	virtual void getFloat4Data( float* afOutput, int level = 0 );

	virtual void getUnsignedByte1Data( ubyte* aubOutput, int level = 0 );
	virtual void getUnsignedByte3Data( ubyte* aubOutput, int level = 0 );
	virtual void getUnsignedByte4Data( ubyte* aubOutput, int level = 0 );

	virtual void dumpToCSV( QString filename ) = 0;
	virtual void dumpToTXT( QString filename, GLint level = 0, GLenum format = GL_RGBA, GLenum type = GL_FLOAT ) = 0;

protected:
	
	GLTexture( GLenum eTarget, GLTextureInternalFormat internalFormat );

private:

	void getTexImage( GLint level, GLenum format, GLenum type, void* avOutput );

	GLenum m_eTarget;
	GLuint m_iTextureId;	
	GLTextureInternalFormat m_eInternalFormat;

	int m_nComponents;
	int m_nBitsPerComponent;
	
};

#endif
