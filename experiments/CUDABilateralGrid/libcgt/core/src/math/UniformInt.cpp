#include "math/UniformInt.h"

UniformInt::UniformInt( uint seed ) :

	m_engine( static_cast< unsigned long >( seed ) )

{

}

UniformInt::UniformInt( int hi, uint seed ) :
	
	m_engine( static_cast< unsigned long >( seed ) ),
	m_distribution( 0, hi - 1 )

{

}

UniformInt::UniformInt( int lo, int hi, uint seed ) :

	m_engine( static_cast< unsigned long >( seed ) ),
	m_distribution( lo, hi - 1 )

{

}

int UniformInt::nextInt()
{
	return m_distribution( m_engine );
}
