#include "math/UniformFloat.h"

UniformFloat::UniformFloat( uint seed ) :

	m_engine( static_cast< unsigned long >( seed ) )

{

}

float UniformFloat::nextFloat()
{
	return m_distribution( m_engine );
}

float UniformFloat::nextRange( float lo, float hi )
{
	return lo + ( hi - lo ) * nextFloat();
}
