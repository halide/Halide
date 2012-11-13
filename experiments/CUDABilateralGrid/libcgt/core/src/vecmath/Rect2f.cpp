#include "vecmath/Rect2f.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

Rect2f::Rect2f() :

m_origin( 0.f, 0.f ),
m_size( 0.f, 0.f )

{

}

Rect2f::Rect2f( float originX, float originY, float width, float height ) :

m_origin( originX, originY ),
m_size( width, height )

{

}

Rect2f::Rect2f( float width, float height ) :

m_origin( 0.f, 0.f ),
m_size( width, height )

{

}

Rect2f::Rect2f( const Vector2f& origin, const Vector2f& size ) :

m_origin( origin ),
m_size( size )

{

}

Rect2f::Rect2f( const Vector2f& size ) :

m_origin( 0.f, 0.f ),
m_size( size )

{

}

Rect2f::Rect2f( const Rect2f& copy ) :

m_origin( copy.m_origin ),
m_size( copy.m_size )

{

}

Rect2f& Rect2f::operator = ( const Rect2f& copy )
{
	if( this != &copy )
	{
		m_origin = copy.m_origin;
		m_size = copy.m_size;
	}
	return *this;
}

Vector2f Rect2f::origin() const
{
	return m_origin;
}

Vector2f& Rect2f::origin()
{
	return m_origin;
}

Vector2f Rect2f::size() const
{
	return m_size;
}

Vector2f& Rect2f::size()
{
	return m_size;
}

Vector2f Rect2f::bottomLeft() const
{
	return m_origin;
}

Vector2f Rect2f::bottomRight() const
{
	return m_origin + Vector2f( m_size.x, 0.f );
}

Vector2f Rect2f::topLeft() const
{
	return m_origin + Vector2f( 0.f, m_size.y );
}

Vector2f Rect2f::topRight() const
{
	return m_origin + m_size;
}

float Rect2f::width() const
{
	return m_size.x;
}

float Rect2f::height() const
{
	return m_size.y;
}

float Rect2f::area() const
{
	return( m_size.x * m_size.y );
}

bool Rect2f::contains( const Vector2f& point )
{
	return
	(
		( point.x > m_origin.x ) &&
		( point.x < ( m_origin.x + m_size.x ) ) &&
		( point.y > m_origin.y ) &&
		( point.y < ( m_origin.y + m_size.y ) )
	);
}
