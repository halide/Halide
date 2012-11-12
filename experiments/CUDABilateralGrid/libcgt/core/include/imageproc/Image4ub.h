#ifndef IMAGE_4UB_H
#define IMAGE_4UB_H

#include <QImage>
#include <QString>
#include <QtGlobal>
#include <QVector>

#include "common/Reference.h"
#include "vecmath/Vector2i.h"
#include "vecmath/Vector4i.h"

class Image4f;

// TODO: resize()
class Image4ub
{
public:

	// default constructor creates the null image
	Image4ub();

	// Constructs an Image4ub from any format readable by QImage
	// creates the null image if it's unreadable
	Image4ub( QString filename );

	Image4ub( int width, int height, const Vector4i& fill = Vector4i( 0, 0, 0, 0 ) );
	Image4ub( const Vector2i& size, const Vector4i& fill = Vector4i( 0, 0, 0, 0 ) );

	Image4ub( const Image4ub& copy );
	Image4ub( Reference< Image4ub > copy );

	bool isNull() const;

	int width() const;
	int height() const;
	Vector2i size() const;

	const quint8* pixels() const;
	quint8* pixels();
	quint8* rowPointer( int y );	

	// fill the channel-th channel with value
	// channel is 0, 1, 2 or 3
	void fillChannel( int channel, quint8 value );

	Vector4i pixel( int x, int y ) const;
	Vector4i pixel( const Vector2i& xy ) const;

	void setPixel( int x, int y, const Vector4i& pixel ); // values > 255 are saturated
	void setPixel( const Vector2i& xy, const Vector4i& pixel ); // values > 255 are saturated

	Vector4i bilinearSample( float x, float y ) const;

	Image4ub flipUD() const;

	QImage toQImage();

	// ----- I/O -----
	bool load( QString filename );

	// Saves this image to depending on filename extension:
	//   ".png": portable network graphics (PNG) (4-component, 8 bits per channel)
	//   ".txt": human-readable TXT
	bool save( QString filename );

private:

	bool saveTXT( QString filename );

	int m_width;
	int m_height;
	QVector< quint8 > m_data;

};

#endif // IMAGE_4UB_H
