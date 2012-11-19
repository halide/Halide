#ifndef SPLINE_2F_H
#define SPLINE_2F_H

#include "common/Reference.h"
#include "vecmath/Vector2f.h"

#include "OpenNaturalCubicSpline.h"

class Spline2f
{
public:

	// Constructs a new spline
	// nPointsToEvaluateFactor tells how many points to evaluate
	// nPointsToEvaluate = nPointsToEvaluateFactor * numControlPoints
	Spline2f( int nPointsToEvaluateFactor = 100 );

	// returns true if both x and y spline are valid
	bool isValid();

	int numPointsToEvaluateFactor();
	int numPointsToEvaluate();
	float delta();

	int numControlPoints();
	virtual void appendControlPoint( const Vector2f& p );

	// inserts a control point into the spline at position p
	// 1. finds the point on the spline closest to p
	// 2. finds the t of the first control point less than p
	// 3. inserts it right after that control point
	// returns the index of the new control point
	virtual int insertControlPoint( const Vector2f& p );

	Vector2f getControlPoint( int i );
	virtual void setControlPoint( int i, const Vector2f& p );

	int closestControlPoint( const Vector2f& p, float* distanceSquared = NULL );

	Vector2f evaluateAt( float t );
	Vector2f derivativeAt( float t );
	Vector2f normalAt( float t );

	// numerically computes which half space a point p is in with respect to the spline
	// returns c where
	// c > 0 : positive half-space
	// c < 0 : negative half-space
	// c = 0 : exactly on the spline
	// closestT gives the closest point on the spline from p
	// when closestT is between 0 and 1, p is within the parameter space of the line
	// otherwise, p is tested against one of the endpoints
	float computeHalfSpace( const Vector2f& p, float* closestT = NULL, float* closestDistance = NULL );

	// numerically finds the closest point on the spline
	// by evaluating the spline at nPointsToEvaluate
	Vector2f closestPointOnSpline( const Vector2f& p, float* closestT = NULL, float* closestDistance = NULL );

	Reference< Spline2f > offsetPath( float distance );

private:

	bool isCacheDirty();
	void updateCache();

	OpenNaturalCubicSpline m_xSpline;
	OpenNaturalCubicSpline m_ySpline;

	bool m_bCacheIsDirty;
	int m_nPointsToEvaluateFactor;
	QVector< Vector2f > m_cache;

};

#endif // SPLINE_2F_H
