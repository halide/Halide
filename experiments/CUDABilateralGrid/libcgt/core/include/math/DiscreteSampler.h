#pragma once

#include <vector>
#include "math/Random.h"

// TODO: replace this with alias tables method

// Sampling from a discrete distribution
class DiscreteSampler
{
public:

	// histogram does not have to sum to 1
	// the sampling is drawn from the normalized PMF
	// p[b] = histogram[b] / sum(histogram)
	DiscreteSampler( const std::vector< float >& histogram );
	
	// returns the index of the bin in [0, pmf.size() )
	int sample( float u );

private:

	float m_sum;
	std::vector< float > m_cdf;
};