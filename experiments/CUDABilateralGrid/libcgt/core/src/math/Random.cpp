#include "math/Random.h"

Random::Random()
{

}

Random::Random( int seed ) :

	m_mtRand( seed )

{

}

double Random::nextDouble()
{
	return m_mtRand.rand();
}

float Random::nextFloat()
{
	return static_cast< float >( nextDouble() );
}
	
uint Random::nextInt()
{
	return m_mtRand.randInt();
}

double Random::nextDoubleRange( double lo, double hi )
{
	double range = hi - lo;
	return( lo + range * nextDouble() );
}

float Random::nextFloatRange( float lo, float hi )
{
	float range = hi - lo;
	return( lo + range * nextFloat() );
}

int Random::nextIntInclusive( int n )
{
	return m_mtRand.randInt( n );
}

int Random::nextIntExclusive( int n )
{
	return nextIntInclusive( n - 1 );
}
