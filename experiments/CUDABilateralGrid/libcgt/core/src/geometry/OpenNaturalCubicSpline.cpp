#include "geometry/OpenNaturalCubicSpline.h"

#include <cmath>
#include <math/MathUtils.h>

OpenNaturalCubicSpline::OpenNaturalCubicSpline()
{

}

int OpenNaturalCubicSpline::numControlPoints()
{
	return m_vControlPoints.size();
}

bool OpenNaturalCubicSpline::isValid()
{
	return( numControlPoints() >= 4 );
}

float OpenNaturalCubicSpline::getControlPoint( int i )
{
	return m_vControlPoints[ i ];
}

void OpenNaturalCubicSpline::setControlPoints( QVector< float > controlPoints )
{
	m_vControlPoints = controlPoints;

	if( isValid() )
	{
		computeCoefficients();
	}
}

void OpenNaturalCubicSpline::appendControlPoint( float controlPoint )
{
	m_vControlPoints.append( controlPoint );
	
	if( isValid() )
	{
		computeCoefficients();
	}
}

void OpenNaturalCubicSpline::setControlPoint( int i, float p )
{
	m_vControlPoints[ i ] = p;

	if( isValid() )
	{
		computeCoefficients();
	}
}

void OpenNaturalCubicSpline::insertControlPoint( int i, float p )
{
	m_vControlPoints.insert( i, p );

	if( isValid() )
	{
		computeCoefficients();
	}
}

float OpenNaturalCubicSpline::evaluateAt( float t )
{
	if( !isValid() )
	{
		return 0;
	}

	// spacing between control points
	float delta = 1.0f / static_cast< float >( m_vControlPoints.size() - 1 );
	// clamp t
	float ct = MathUtils::clampToRangeFloat( t, 0.0f, 1.0f );

	// find the 4 nearest control points
	int n;
	if( ct == 1.0f )
	{
		n = m_vCoefficients.size() - 1;
	}
	else
	{
		n = static_cast< int >( ct / delta );
	}

	float u = ( ct - n * delta ) / delta;
	float u2 = u * u;
	float u3 = u2 * u;

	return( m_vCoefficients[n][0] + u * m_vCoefficients[n][1] + u2 * m_vCoefficients[n][2] + u3 * m_vCoefficients[n][3] );
}

float OpenNaturalCubicSpline::derivativeAt( float t )
{
	if( !isValid() )
	{
		return 0;
	}

	// spacing between control points
	float delta = 1.0f / static_cast< float >( m_vControlPoints.size() - 1 );
	// clamp t
	float ct = MathUtils::clampToRangeFloat( t, 0.0f, 1.0f );

	// find the 3 nearest control points
	int n;
	if( ct == 1.0f )
	{
		n = m_vCoefficients.size() - 1;
	}
	else
	{
		n = static_cast< int >( ct / delta );
	}

	float u = ( ct - n * delta ) / delta;
	float u2 = u * u;

	return( ( m_vCoefficients[n][1] + 2 * u * m_vCoefficients[n][2] + 3 * u2 * m_vCoefficients[n][3] ) / delta );
}

float OpenNaturalCubicSpline::inverse( float x, float tGuess, float epsilon, int maxIterations )
{
	float result = tGuess;
	float xResult = evaluateAt( result );
	float error = x - xResult;
	float absError = fabs( error );

	int n = 0;
	while( ( absError > epsilon ) && ( n < maxIterations ) )
	{
		float dxdt = derivativeAt( result );
		result += error / dxdt;
		xResult = evaluateAt( result );
		error = x - xResult;
		absError = fabs( error );

		++n;
	}

#if _DEBUG
	if( n == maxIterations )
	{
		printf( "max iterations reached! " );
	}
	printf( "error = %f\n", absError );
#endif

	return result;
}

//////////////////////////////////////////////////////////////////////////
// Private
//////////////////////////////////////////////////////////////////////////

void OpenNaturalCubicSpline::computeCoefficients()
{
	int nPoints = m_vControlPoints.size();

	QVector< float > gamma( nPoints );
	QVector< float > delta( nPoints );
	QVector< float > D( nPoints );

	gamma[0] = 0.5f;
	for( int i = 1; i < nPoints - 1; ++i )
	{
		gamma[i] = 1.0f / ( 4.0f - gamma[ i - 1 ] );
	}
	gamma[ nPoints - 1 ] = 1.0f / ( 2.0f - gamma[ nPoints - 2 ] );


	delta[0] = 3.0f * ( m_vControlPoints[1] - m_vControlPoints[0] ) * gamma[0];
	for( int i = 1; i < nPoints - 1; ++i )
	{
		delta[i] = ( 3.0f * ( m_vControlPoints[i+1] - m_vControlPoints[i-1] ) - delta[i-1] ) * gamma[i];
	}
	delta[ nPoints - 1 ] = ( 3.0f * ( m_vControlPoints[ nPoints - 1 ] - m_vControlPoints[ nPoints - 2 ] ) - delta[ nPoints - 2 ] ) * gamma[ nPoints - 1 ];

	D[ nPoints - 1 ] = delta[ nPoints - 1 ];
	for( int i = nPoints - 2; i >= 0; --i )
	{
		D[i] = delta[i] - gamma[i] * D[ i+1 ];
	}

	m_vCoefficients.resize( nPoints - 1 );

	for( int i = 0; i < nPoints - 1; ++i )
	{
		m_vCoefficients[i][0] = m_vControlPoints[i];
		m_vCoefficients[i][1] = D[i];
		m_vCoefficients[i][2] = 3.0f * ( m_vControlPoints[ i + 1 ] - m_vControlPoints[i] ) - 2.0f * D[i] - D[ i + 1 ];
		m_vCoefficients[i][3] = 2.0f * ( m_vControlPoints[i] - m_vControlPoints[ i + 1 ] ) + D[i] + D[ i + 1 ];
	}
}
