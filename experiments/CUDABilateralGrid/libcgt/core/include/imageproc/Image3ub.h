#ifndef IMAGE_3UB_H
#define IMAGE_3UB_H

#include <QImage>
#include <QString>
#include <QtGlobal>
#include <QVector>

#include "common/Reference.h"
#include "vecmath/Vector2i.h"
#include "vecmath/Vector3i.h"

class Image4f;

class Image3ub
{
public:

	Image3ub(); // default constructor creates the null image
	Image3ub( QString filename );

	Image3ub( int width, int height, const Vector3i& fillValue = Vector3i( 0, 0, 0 ) );
	Image3ub( const Vector2i& size, const Vector3i& fillValue = Vector3i( 0, 0, 0 ) );
	Image3ub( const Image3ub& copy );
	Image3ub( Reference< Image3ub > copy );

	bool isNull() const;

	int width() const;
	int height() const;
	Vector2i size() const;

	quint8* pixels();

	// TODO: do clamp_to_edge for the others too
	Vector3i pixel( int x, int y ) const;
	Vector3i pixel( const Vector2i& xy ) const;
	
	void setPixel( int x, int y, const Vector3i& pixel ); // values > 255 are saturated
	void setPixel( const Vector2i& xy, const Vector3i& pixel ); // values > 255 are saturated

	Vector3i bilinearSample( int x, int y ) const;

	void fill( const Vector3i& fill );

	// returns a 4-channel QImage with alpha = 255
	QImage toQImage();
	void savePNG( QString filename );

private:

	int m_width;
	int m_height;
	QVector< quint8 > m_data;

};

#endif // IMAGE_3UB_H
