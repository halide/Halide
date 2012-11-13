#ifndef OPEN_NATURAL_CUBIC_SPLINE
#define OPEN_NATURAL_CUBIC_SPLINE

#include <QVector>
#include <vecmath/Vector4f.h>

class OpenNaturalCubicSpline
{
public:

	OpenNaturalCubicSpline();

	bool isValid();

	void setControlPoints( QVector< float > controlPoints );

	int numControlPoints();	
	float getControlPoint( int i );
	void setControlPoint( int i, float p );
	void insertControlPoint( int i, float p );

	void appendControlPoint( float controlPoint );

	// output = x( t )
	// t between 0 and 1
	float evaluateAt( float t );

	// output = dx/dt ( t )
	// t between 0 and 1
	float derivativeAt( float t );

	// get the inverse spline
	// given a value of x, find t such that x(t) = x
	float inverse( float x, float tGuess, float epsilon = 0.001f, int maxIterations = 50 );

private:

	void computeCoefficients();

	QVector< float > m_vControlPoints;
	QVector< Vector4f > m_vCoefficients;

};

#endif
