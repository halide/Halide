#include "geometry/Spline2f.h"

#include <cfloat>
#include <cmath>

#include "math/Arithmetic.h"
#include "math/MathUtils.h"
#include "vecmath/Vector3f.h"

Spline2f::Spline2f( int nPointsToEvaluateFactor ) :

m_bCacheIsDirty( true ),
m_nPointsToEvaluateFactor( nPointsToEvaluateFactor )

{

}

bool Spline2f::isValid()
{
	return( m_xSpline.isValid() && m_ySpline.isValid() );
}

int Spline2f::numPointsToEvaluateFactor()
{
	return m_nPointsToEvaluateFactor;
}

int Spline2f::numPointsToEvaluate()
{
	return numControlPoints() * m_nPointsToEvaluateFactor;
}

float Spline2f::delta()
{
	return 1.f / ( numPointsToEvaluate() - 1 );
}

int Spline2f::numControlPoints()
{
	return m_xSpline.numControlPoints();
}

// virtual
void Spline2f::appendControlPoint( const Vector2f& p )
{
	m_xSpline.appendControlPoint( p.x );
	m_ySpline.appendControlPoint( p.y );

	m_bCacheIsDirty = true;
}

// virtual
int Spline2f::insertControlPoint( const Vector2f& p )
{
	float t;
	closestPointOnSpline( p, &t );
	
	int controlPointIndex;

	if( t >= ( 1.f - delta() ) )
	{
		controlPointIndex = numControlPoints();
	}
	else if( t <= delta() )
	{
		controlPointIndex = 0;		
	}
	else
	{
		controlPointIndex = Arithmetic::roundToInt( t * numControlPoints() );
	}

	controlPointIndex = MathUtils::clampToRangeInt( controlPointIndex, 0, numControlPoints() );

	printf( "closest t = %f, controlPointIndex = %d\n", t, controlPointIndex );

	m_xSpline.insertControlPoint( controlPointIndex, p.x );
	m_ySpline.insertControlPoint( controlPointIndex, p.y );
	
	m_bCacheIsDirty = true;

	return controlPointIndex;
}

Vector2f Spline2f::getControlPoint( int i )
{
	return Vector2f( m_xSpline.getControlPoint( i ), m_ySpline.getControlPoint( i ) );
}

// virtual
void Spline2f::setControlPoint( int i, const Vector2f& p )
{
	m_xSpline.setControlPoint( i, p.x );
	m_ySpline.setControlPoint( i, p.y );

	m_bCacheIsDirty = true;
}

int Spline2f::closestControlPoint( const Vector2f& p, float* distanceSquared )
{
	int minIndex = -1;
	float minDistanceSquared = FLT_MAX;

	for( int i = 0; i < numControlPoints(); ++i )
	{
		Vector2f controlPoint = getControlPoint( i );
		float currentDistanceSquared = ( p - controlPoint ).absSquared();
		if( currentDistanceSquared < minDistanceSquared )
		{
			minDistanceSquared = currentDistanceSquared;
			minIndex = i;
		}
	}

	if( distanceSquared != NULL )
	{
		*distanceSquared = minDistanceSquared;
	}
	return minIndex;
}

Vector2f Spline2f::evaluateAt( float t )
{
	float x = m_xSpline.evaluateAt( t );
	float y = m_ySpline.evaluateAt( t );

	return Vector2f( x, y );
}

Vector2f Spline2f::derivativeAt( float t )
{
	float dx = m_xSpline.derivativeAt( t );
	float dy = m_ySpline.derivativeAt( t );

	return Vector2f( dx, dy );
}

Vector2f Spline2f::normalAt( float t )
{
	return derivativeAt( t ).normal();
}

float Spline2f::computeHalfSpace( const Vector2f& p, float* closestT, float* closestDistance )
{
	float t;
	Vector2f closestPoint = closestPointOnSpline( p, &t, closestDistance );

	Vector2f tangent = derivativeAt( t );
	Vector2f pointToClosestPoint = p - closestPoint;

	if( closestT != NULL )
	{
		*closestT = t;
	}

	return Vector2f::cross( tangent, pointToClosestPoint ).z;
}

Vector2f Spline2f::closestPointOnSpline( const Vector2f& p, float* closestT, float* closestDistance )
{
	if( isCacheDirty() )
	{
		updateCache();
	}

	int nPointsToEvaluate = numPointsToEvaluate();	

	Vector2f minPoint( 0, 0 );
	float minDistance = FLT_MAX;
	float minT = -1;
	float delta = 1.f / ( nPointsToEvaluate - 1 );

	for( int i = 0; i < nPointsToEvaluate; ++i )
	{
		Vector2f splinePoint = m_cache[ i ];
		float currentDistanceSquared = ( splinePoint - p ).absSquared();
		if( currentDistanceSquared < minDistance )
		{
			minDistance = currentDistanceSquared;
			minPoint = splinePoint;
			minT = i * delta;
		}
	}

	if( closestT != NULL )
	{
		*closestT = minT;
	}
	if( closestDistance != NULL )
	{
		*closestDistance = sqrt( minDistance );
	}
	return minPoint;
}

Reference< Spline2f > Spline2f::offsetPath( float distance )
{
	Reference< Spline2f > offsetSpline = new Spline2f;

	int nControlPoints = numControlPoints();
	float delta = 1.f / ( nControlPoints - 1 );

	for( int i = 0; i < nControlPoints; ++i )
	{
		float t = i * delta;
		
		Vector2f controlPoint = getControlPoint( i );
		Vector2f normalAtControlPoint = normalAt( t ).normalized();

		offsetSpline->appendControlPoint( controlPoint + distance * normalAtControlPoint );
	}

	return offsetSpline;
}

bool Spline2f::isCacheDirty()
{
	return
	(
		m_bCacheIsDirty ||
		( m_cache.size() != numPointsToEvaluate() )
	);
}

void Spline2f::updateCache()
{
	int nPointsToEvaluate = numPointsToEvaluate();

	m_cache.resize( nPointsToEvaluate );
	float delta = 1.f / ( nPointsToEvaluate - 1 );

	for( int i = 0; i < nPointsToEvaluate; ++i )
	{
		// compute t between 0 and 1
		float t = i * delta;
		Vector2f splinePoint = evaluateAt( t );
		m_cache[ i ] = splinePoint;
	}

	m_bCacheIsDirty = false;
}
