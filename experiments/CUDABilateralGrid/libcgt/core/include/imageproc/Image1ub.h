#ifndef IMAGE_1UB_H
#define IMAGE_1UB_H

#include <QtGlobal>
#include <QImage>
#include <QVector>

#include "common/Reference.h"
#include "vecmath/Vector2i.h"
#include "vecmath/Vector3i.h"

class Image1ub
{
public:

	Image1ub(); // default constructor creates the null image

	Image1ub( int width, int height, quint8 fill = 0 );
	Image1ub( const Vector2i& size, quint8 fill = 0 );
	Image1ub( const Image1ub& copy );
	Image1ub( Reference< Image1ub > copy );

	bool isNull() const;

	int width() const;
	int height() const;
	Vector2i size() const;

	quint8* pixels();

	quint8 pixel( int x, int y ) const;
	quint8 pixel( const Vector2i& xy ) const;

	void setPixel( int x, int y, quint8 pixel );
	void setPixel( const Vector2i& xy, quint8 pixel );

	quint8 bilinearSample( float x, float y ) const;

	// returns a 4-channel QImage
	// with rgb = value and alpha = 255
	QImage toQImage(); 
	void savePNG( QString filename );

private:

	int m_width;
	int m_height;
	QVector< quint8 > m_data;

};

#endif // IMAGE_1UB_H
