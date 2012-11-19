#include "math/DiscreteSampler.h"

#include <algorithm>
#include <numeric>

DiscreteSampler::DiscreteSampler( const std::vector< float >& pmf ) :

	m_cdf( pmf.size() )

{
	// compute partial sum over the PMF
	std::partial_sum( pmf.begin(), pmf.end(), m_cdf.begin() );

	m_sum = m_cdf.back();
}

int DiscreteSampler::sample( float u )
{
	float v = u * m_sum;
	auto itr = std::lower_bound( m_cdf.begin(), m_cdf.end(), v );
	return static_cast< int >( itr - m_cdf.begin() );
}