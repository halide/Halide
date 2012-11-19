#include "color/ColorUtils.h"

#include <cassert>
#include <cmath>
#include <math/Arithmetic.h>

// static
const float ColorUtils::LOG_LUMINANCE_EPSILON = 0.001f;

// static
const float ColorUtils::LOG_LAB_EPSILON = 10.f;

// static
int ColorUtils::floatToInt( float f )
{
	f = ColorUtils::saturate( f );
	return ColorUtils::saturate( Arithmetic::roundToInt( f * 255 ) );
}

// static
float ColorUtils::intToFloat( int i )
{
	return ColorUtils::saturate( i / 255.f );
}

// static
Vector3i ColorUtils::floatToInt( const Vector3f& f )
{
	return Vector3i
	(
		ColorUtils::floatToInt( f[ 0 ] ),
		ColorUtils::floatToInt( f[ 1 ] ),
		ColorUtils::floatToInt( f[ 2 ] )
	);
}

// static
Vector3f ColorUtils::intToFloat( const Vector3i& i )
{
	return Vector3f
	(
		ColorUtils::intToFloat( i[ 0 ] ),
		ColorUtils::intToFloat( i[ 1 ] ),
		ColorUtils::intToFloat( i[ 2 ] )
	);
}

// static
Vector4i ColorUtils::floatToInt( const Vector4f& f )
{
	return Vector4i
	(
		ColorUtils::floatToInt( f[ 0 ] ),
		ColorUtils::floatToInt( f[ 1 ] ),
		ColorUtils::floatToInt( f[ 2 ] ),
		ColorUtils::floatToInt( f[ 3 ] )
	);
}

// static
Vector4f ColorUtils::intToFloat( const Vector4i& i )
{
	return Vector4f
	(
		ColorUtils::intToFloat( i[ 0 ] ),
		ColorUtils::intToFloat( i[ 1 ] ),
		ColorUtils::intToFloat( i[ 2 ] ),
		ColorUtils::intToFloat( i[ 3 ] )
	);
}

// static
ubyte ColorUtils::floatToUnsignedByte( float f )
{
	f = ColorUtils::saturate( f );
	return ColorUtils::saturate( Arithmetic::roundToInt( f * 255 ) );
}

// static
float ColorUtils::unsignedByteToFloat( ubyte ub )
{
	return ColorUtils::saturate( ub / 255.f );
}

// static
float ColorUtils::rgb2luminance( float rgb[3] )
{
	return( 0.3279f * rgb[0] + 0.6557f * rgb[1] + 0.0164f * rgb[2] );
}

// static
float ColorUtils::rgb2luminance( ubyte rgb[3] )
{
	return
	(
		0.3279f * unsignedByteToFloat( rgb[0] ) +
		0.6557f * unsignedByteToFloat( rgb[1] ) +
		0.0164f * unsignedByteToFloat( rgb[2] )
	);
}

// static
void ColorUtils::rgbArray2LuminanceArray( UnsignedByteArray rgb, UnsignedByteArray luminance )
{
	assert( rgb.length() % 3 == 0 );

	int nPixels = rgb.length() / 3;
	assert( nPixels == luminance.length() );

	for( int i = 0; i < nPixels; ++i )
	{
		luminance[ i ] = floatToUnsignedByte( rgb2luminance( &( rgb[ 3 * i ] ) ) );
	}
}

// static
float ColorUtils::rgba2luminance( float rgba[4] )
{
	return( 0.3279f * rgba[0] + 0.6557f * rgba[1] + 0.0164f * rgba[2] );
}

// static
float ColorUtils::rgba2luminance( ubyte rgba[4] )
{
	return
	(
		0.3279f * unsignedByteToFloat( rgba[0] ) +
		0.6557f * unsignedByteToFloat( rgba[1] ) +
		0.0164f * unsignedByteToFloat( rgba[2] )
	);
}

// static
Vector3f ColorUtils::rgb2xyz( const Vector3f& rgb )
{
	float rOut = ( rgb.x > 0.04045f ) ?
		pow( ( rgb.x + 0.055f ) / 1.055f, 2.4f ) :
		rgb.x / 12.92f;
	float gOut = ( rgb.y > 0.04045 ) ?
		pow( ( rgb.y + 0.055f ) / 1.055f, 2.4f ) :
		rgb.y / 12.92f;
	float bOut = ( rgb.z > 0.04045f ) ?
		pow( ( rgb.z + 0.055f ) / 1.055f, 2.4f ) :
		rgb.z / 12.92f;
	
	Vector3f rgbOut = 100 * Vector3f( rOut, gOut, bOut );
	
	return Vector3f
	(
		Vector3f::dot( rgbOut, Vector3f( 0.4124f, 0.3576f, 0.1805f ) ),
		Vector3f::dot( rgbOut, Vector3f( 0.2126f, 0.7152f, 0.0722f ) ),
		Vector3f::dot( rgbOut, Vector3f( 0.0193f, 0.1192f, 0.9505f ) )
	);	
}

// static
Vector3f ColorUtils::xyz2lab( const Vector3f& xyz,
							 const Vector3f& xyzRef,
							 float epsilon,
							 float kappa )
{
	Vector3f xyzNormalized = xyz / xyzRef;
	
	float fx = ( xyzNormalized.x > epsilon ) ?
		pow( xyzNormalized.x, 1.f / 3.f ) :
		( ( kappa * xyzNormalized.x + 16.f ) / 116.f );
	float fy = ( xyzNormalized.y > epsilon ) ?
		pow( xyzNormalized.y, 1.f / 3.f ) :
		( ( kappa * xyzNormalized.y + 16.f ) / 116.f );
	float fz = ( xyzNormalized.z > epsilon ) ?
		pow( xyzNormalized.z, 1.f / 3.f ) :
		( ( kappa * xyzNormalized.z + 16.f ) / 116.f );
		
	return Vector3f
	(
		( 116.f * fy ) - 16.f,
		500.f * ( fx - fy ),
		200.f * ( fy - fz )
	);
}

// static
Vector3f ColorUtils::rgb2lab( const Vector3f& rgb )
{
	return ColorUtils::xyz2lab( ColorUtils::rgb2xyz( rgb ) );
}

// static
Vector3f ColorUtils::hsv2rgb( const Vector3f& hsv )
{
	float h = hsv.x;
	float s = hsv.y;
	float v = hsv.z;

	float r;
	float g;
	float b;

	h *= 360.f;
	int i;
	float f, p, q, t;
	
	if( s == 0 )
	{
		// achromatic (grey)
		return Vector3f( v, v, v );
	}
	else
	{
		h /= 60.f; // sector 0 to 5
		i = Arithmetic::floorToInt( h );
		f = h - i; // factorial part of h
		p = v * ( 1.f - s );
		q = v * ( 1.f - s * f );
		t = v * ( 1.f - s * ( 1.f - f ) );
		
		switch( i )
		{
			case 0: r = v; g = t; b = p; break;
			case 1: r = q; g = v; b = p; break;
			case 2: r = p; g = v; b = t; break;
			case 3: r = p; g = q; b = v; break;
			case 4: r = t; g = p; b = v; break;
			default: r = v; g = p; b = q; break;
		}

		return Vector3f( r, g, b );
	}
} 


// static
float ColorUtils::logL( float l )
{
	const float logMin = log( LOG_LAB_EPSILON );
	const float logRange = log( 100 + LOG_LAB_EPSILON ) - logMin;

	float logL = log( l + LOG_LAB_EPSILON );

	// scale between 0 and 1
	float logL_ZO = ( logL - logMin ) / logRange;

	// scale between 0 and 100
	return 100.f * logL_ZO;
}

// static
float ColorUtils::expL( float ll )
{
	const float logMin = log( LOG_LAB_EPSILON );
	const float logRange = log( 100 + LOG_LAB_EPSILON ) - logMin;

	// scale between 0 and 1
	float logL_ZO = ll / 100.f;
	// bring back to log scale
	float logL = logL_ZO * logRange + logMin;

	// exponentiate
	return exp( logL ) - LOG_LAB_EPSILON;
}

// static
float ColorUtils::saturate( float f )
{
	if( f < 0 )
	{
		f = 0;
	}
	if( f > 1 )
	{
		f = 1;
	}
	return f;
}

// static
Vector4f ColorUtils::saturate( const Vector4f& v )
{
	return Vector4f
	(
		ColorUtils::saturate( v[ 0 ] ),	
		ColorUtils::saturate( v[ 1 ] ),	
		ColorUtils::saturate( v[ 2 ] ),	
		ColorUtils::saturate( v[ 3 ] )
	);
}

// static
ubyte ColorUtils::saturate( int i )
{
	if( i < 0 )
	{
		i = 0;
	}
	if( i > 255 )
	{
		i = 255;
	}
	return static_cast< ubyte >( i );
}

// static
Vector4i ColorUtils::saturate( const Vector4i& v )
{
	return Vector4i
	(
		ColorUtils::saturate( v[ 0 ] ),	
		ColorUtils::saturate( v[ 1 ] ),	
		ColorUtils::saturate( v[ 2 ] ),	
		ColorUtils::saturate( v[ 3 ] )
	);
}
