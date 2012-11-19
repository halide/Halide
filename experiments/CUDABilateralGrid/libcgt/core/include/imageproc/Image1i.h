#ifndef IMAGE_1I_H
#define IMAGE_1I_H

#include <QtGlobal>
#include <QImage>
#include <QVector>

#include "common/Reference.h"
#include "vecmath/Vector2i.h"
#include "vecmath/Vector3i.h"

class Image1i
{
public:

	Image1i(); // default constructor creates the null image

	Image1i( int width, int height, qint32 fill = 0 );
	Image1i( const Vector2i& size, qint32 fill = 0 );
	Image1i( const Image1i& copy );
	Image1i( Reference< Image1i > copy );

	bool isNull() const;

	int width() const;
	int height() const;
	Vector2i size() const;

	qint32* pixels();

	qint32 pixel( int x, int y ) const;
	qint32 pixel( const Vector2i& xy ) const;

	void setPixel( int x, int y, qint32 pixel );
	void setPixel( const Vector2i& xy, qint32 pixel );

	Reference< Image1i > flipUD();

	qint32 bilinearSample( float x, float y ) const;

	// Returns a 4-component QImage
	// with RGB clamped to [0,255] and alpha = 255
	QImage toQImage();

	void savePNG( QString filename );
	bool saveTXT( QString filename );

private:

	int m_width;
	int m_height;
	QVector< qint32 > m_data;

};

#endif // IMAGE_1UB_H
