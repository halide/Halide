#ifndef IMAGE_4F_H
#define IMAGE_4F_H

#include <QImage>
#include <QString>
#include <QVector>

#include <common/Reference.h>
#include <common/Array2D.h>
#include <vecmath/Vector2i.h>
#include <vecmath/Vector2f.h>
#include <vecmath/Vector4i.h>
#include <vecmath/Vector4f.h>

class Image4f
{
public:

	Image4f(); // default constructor creates the null image

	// Creates an Image4f from any format readable by QImage
	// as well as little-endian PFM and "PFM4" (4-component PFM with header "PF4")
	Image4f( QString filename );
	
	Image4f( int width, int height, const Vector4f& fill = Vector4f( 0, 0, 0, 0 ) );
	Image4f( const Vector2i& size, const Vector4f& fill = Vector4f( 0, 0, 0, 0 ) );
	
	Image4f( const Image4f& copy );
	Image4f( Reference< Image4f > copy );

	bool isNull() const;

	int width() const;
	int height() const;
	Vector2i size() const;

	float* pixels();
	Vector4f* pixelsVector4f();

	Vector4f pixel( int x, int y ) const;
	Vector4f pixel( const Vector2i& xy ) const;
	void setPixel( int x, int y, const Vector4f& pixel );
	void setPixel( const Vector2i& xy, const Vector4f& pixel );

	// pixel in [0,255]
	void setPixel( int x, int y, const Vector4i& pixel );
	void setPixel( const Vector2i& xy, const Vector4i& pixel );

	Vector4f operator () ( int x, int y ) const;
	Vector4f operator () ( const Vector2i& xy ) const;

	Vector4f bilinearSample( float x, float y ) const;
	Vector4f bilinearSample( const Vector2f& xy ) const;

	Image4f flipUD() const;

	// Clamps this Image4f to [0,1]
	// and returns a QImage in [0,255]
	QImage toQImage();

	// ---- I/O ----
	bool load( QString filename );
	
	// Saves this image to depending on filename extension:
	//   portable network graphics (PNG) (4-component, 8 bits per channel)
	//   little-endian PFM (3-component PFM with header "PF", 32 bits per channel alpha is dropped)
	//   *non-standard* little-endian PFM4 (4-component PFM with header "PF4", 32 bits per channel)
	//   human-readable TXT
	bool save( QString filename );	

private:

	bool loadQImage( QString filename );
	bool loadPFM( QString filename );
	bool loadPFM4( QString filename );

	bool savePNG( QString filename );
	bool savePFM( QString filename );
	bool savePFM4( QString filename );
	bool saveTXT( QString filename );

	int m_width;
	int m_height;
	Array2D< float > m_data;

};

#endif // IMAGE_4F_H
