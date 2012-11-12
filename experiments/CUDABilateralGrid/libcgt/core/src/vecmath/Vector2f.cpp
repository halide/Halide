#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "math/Arithmetic.h"
#include "vecmath/Vector2f.h"
#include "vecmath/Vector2d.h"
#include "vecmath/Vector2i.h"
#include "vecmath/Vector3f.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////
Vector2f::Vector2f( const Vector2d& rv )
{
    m_elements[0] = static_cast< float >( rv.x );
    m_elements[1] = static_cast< float >( rv.y );
}

Vector2f::Vector2f( const Vector2i& rv )
{
    m_elements[ 0 ] = static_cast< float >( rv.x );
    m_elements[ 1 ] = static_cast< float >( rv.y );
}

Vector2f& Vector2f::operator = ( const Vector2d& rv )
{
    m_elements[ 0 ] = static_cast< float >( rv.x );
    m_elements[ 1 ] = static_cast< float >( rv.y );

    return *this;
}

Vector2f& Vector2f::operator = ( const Vector2i& rv )
{
    m_elements[ 0 ] = static_cast< float >( rv.x );
    m_elements[ 1 ] = static_cast< float >( rv.y );

    return *this;
}

Vector2i Vector2f::floored() const
{
	return Vector2i( Arithmetic::floorToInt( m_elements[ 0 ] ), Arithmetic::floorToInt( m_elements[ 1 ] ) );
}

// ---- Utility ----
void Vector2f::print() const
{
	printf( "< %1.2f, %1.2f >\n",
		m_elements[0], m_elements[1] );
}

QString Vector2f::toString() const
{
	return QString( "%1%2" ).arg( x, 10, 'g', 4 ).arg( y, 10, 'g', 4 );
}

//static
Vector3f Vector2f::cross( const Vector2f& v0, const Vector2f& v1 )
{
	return Vector3f
		(
			0,
			0,
			v0.x * v1.y - v0.y * v1.x
		);
}

// static
Vector2f Vector2f::lerp( const Vector2f& v0, const Vector2f& v1, float alpha )
{
	return alpha * ( v1 - v0 ) + v0;
}
