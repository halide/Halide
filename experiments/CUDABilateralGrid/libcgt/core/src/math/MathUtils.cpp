#include <cmath>
#include <cstdlib>

#include "math/MathUtils.h"

#ifndef M_E
const float MathUtils::E = 2.71828182845904523536f;
#else
const float MathUtils::E = M_E;
#endif
#ifndef M_PI
const float MathUtils::PI = 3.14159265358979323846f;
const float MathUtils::HALF_PI = 1.57079632679489661923f;
const float MathUtils::QUARTER_PI = 0.78539816339744830962f;
#else
const float MathUtils::PI = M_PI;
const float MathUtils::HALF_PI = M_PI_2;
const float MathUtils::QUARTER_PI = M_PI_4;
#endif

const float MathUtils::TWO_PI = 2.0f * MathUtils::PI;

// static
float MathUtils::cot( float x )
{
	return 1.f / tanf( x );
}

// static
float MathUtils::asinh( float x )
{
    return log(x + sqrt(x * x + 1.f));
}

// static
int MathUtils::sign( float f )
{
	if( f < 0 )
	{
		return -1;
	}
	else if( f > 0 )
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

// static
bool MathUtils::sameSign( float x, float y )
{
	return sign( x ) == sign( y );
}

// static
float MathUtils::degreesToRadians( float degrees )
{
	return static_cast< float >( degrees * MathUtils::PI / 180.0f );
}

// static
double MathUtils::degreesToRadians( double degrees )
{
	return( degrees * MathUtils::PI / 180.0 );
}

// static
float MathUtils::radiansToDegrees( float radians )
{
	return static_cast< float >( radians * 180.0f / MathUtils::PI );
}

// static
double MathUtils::radiansToDegrees( double radians )
{
	return( radians * 180.0 / MathUtils::PI );
}

// static
int MathUtils::clampToRangeInt( int x, int min, int max )
{
	if( x >= max )
	{
		x = max - 1;
	}
	if( x < min )
	{
		x = min;
	}

	return x;
}

// static
float MathUtils::clampToRangeFloat( float x, float min, float max )
{
	if( x > max )
	{
		x = max;
	}
	if( x < min )
	{
		x = min;
	}
	
	return x;
}

// static
double MathUtils::clampToRangeDouble( double x, double min, double max )
{
	if( x > max )
	{
		x = max;
	}
	if( x < min )
	{
		x = min;
	}

	return x;
}

// static
sbyte MathUtils::floatToByteSignedNormalized( float f )
{
	return static_cast< sbyte >( f * 127 );
}

// static
float MathUtils::signedByteToFloatNormalized( sbyte sb )
{
	return( sb / 127.f );
}

// static
float MathUtils::rescaleFloatToFloat( float value,
									 float inputMin, float inputMax,
									 float outputMin, float outputMax )
{
	float fraction = ( value - inputMin ) / ( inputMax - inputMin );
	return( outputMin + fraction * ( outputMax - outputMin ) );
}

// static
int MathUtils::rescaleFloatToInt( float value,
							 float fMin, float fMax,
							 int iMin, int iMax )
{
	float fraction = ( value - fMin ) / ( fMax - fMin );
	return( iMin + ( int )( fraction * ( iMax - iMin ) + 0.5f ) );
}

// static
float MathUtils::rescaleIntToFloat( int value,
							 int iMin, int iMax,
							 float fMin, float fMax )
{
	float fraction = ( ( float )( value - iMin ) ) / ( iMax - iMin );
	return( fMin + fraction * ( fMax - fMin ) );
}

// static
int MathUtils::rescaleIntToInt( int value,
							   int inMin, int inMax,
							   int outMin, int outMax )
{
	float fraction = ( ( float )( value - inMin ) ) / ( inMax - inMin );
	return( ( int )( ( outMin + fraction * ( outMax - outMin ) ) + 0.5f ) );
}

// static
float MathUtils::cubicInterpolate( float p0, float p1, float p2, float p3, float t )
{
	// geometric construction:
	//            t
	//   (t+1)/2     t/2
	// t+1        t	        t-1

	// bottom level
	float p0p1 = lerp( p0, p1, t + 1 );
	float p1p2 = lerp( p1, p2, t );
	float p2p3 = lerp( p2, p3, t - 1 );

	// middle level
	float p0p1_p1p2 = lerp( p0p1, p1p2, 0.5f * ( t + 1 ) );
	float p1p2_p2p3 = lerp( p1p2, p2p3, 0.5f * t );

	// top level
	return lerp( p0p1_p1p2, p1p2_p2p3, t );
}

// static
float MathUtils::distanceSquared( float x0, float y0, float x1, float y1 )
{
	float dx = x1 - x0;
	float dy = y1 - y0;

	return( dx * dx + dy * dy );
}

// static
float MathUtils::gaussianWeight( float r, float sigma )
{
	return exp( -( r * r ) / ( 2.f * sigma * sigma ) );
}
