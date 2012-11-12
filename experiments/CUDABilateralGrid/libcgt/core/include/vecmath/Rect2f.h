#ifndef RECT_2F_H
#define RECT_2F_H

#include "Vector2f.h"

// TODO: unify with BoundingBox2f, they're the same thing

class Rect2f
{
public:
	
	Rect2f(); // (0,0,0,0)
	Rect2f( float originX, float originY, float width, float height );
	Rect2f( float width, float height ); // (0, 0, width, height)
	Rect2f( const Vector2f& origin, const Vector2f& size );	
	Rect2f( const Vector2f& size ); // (0, 0, width, height)

	Rect2f( const Rect2f& copy ); // copy constructor
	Rect2f& operator = ( const Rect2f& copy ); // assignment operator

	// TODO: Vector2f& origin() non-const
	// rename origin --> bottomLeft()?

	Vector2f origin() const;
	Vector2f& origin();

	Vector2f size() const;
	Vector2f& size();

	Vector2f bottomLeft() const; // for convenience, same as origin
	Vector2f bottomRight() const;
	Vector2f topLeft() const;
	Vector2f topRight() const;

	float width() const;
	float height() const;
	float area() const;

	bool contains( const Vector2f& point );

private:

	Vector2f m_origin;
	Vector2f m_size;

};

#endif // RECT_2F_H
